/* kos_sfxmgr_xbox.c -- KallistiOS's sound effect manager, over the Xbox's
 * AICA (Platform/xbox/aica/).
 *
 * KallistiOS v2.2.1 kernel/arch/dreamcast/sound/snd_sfxmgr.c
 * ((C)2000-2004 Megan Potter, (C)2023-2024 Ruslan Rostovtsev, (C)2023 Andy
 * Barajas, (C)2024 Stefanos Kornilios Mitsis Poiitidis; KOS licence,
 * Platform/xbox/LICENSE.KOS), the WAV readers and the channel commands as
 * they are: an effect's samples go into sound RAM (snd_mem_malloc,
 * spu_memload_sq), and playing it sends the firmware an AICA_CMD_CHAN
 * start, which the firmware (aica/aica_fw_xbox.c, KOS's stock driver on the
 * x86) plays on the chip's channels as it does on the Dreamcast.
 *
 * Doom 64 loads its 90-odd effects at boot from WAV images in memory
 * (src/s_sound.c snd_sfx_load_buf) -- Yamaha ADPCM, mono, 22050 Hz -- and
 * plays them by handle, panned and attenuated per its listener, with one
 * channel held for the plasma rifle's loop (src/p_pspr.c). The file-based
 * loaders are kept for completeness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <kos/dbglog.h>
#include <kos/fs.h>
#include <arch/irq.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>

#include "aica_cmd_iface.h"

typedef struct snd_effect {
    uint32_t locl, locr;
    uint32_t len;
    uint32_t rate;
    uint32_t used;
    uint32_t fmt;
    uint16_t stereo;
    struct snd_effect *next;    /* KOS: LIST_ENTRY */
} snd_effect_t;

static snd_effect_t *snd_effects;

/* The next channel we'll use to play sound effects. */
static int sfx_nextchan = 0;

/* Our channel-in-use mask. */
static uint64_t sfx_inuse = 0;

static void effect_link(snd_effect_t *t)
{
    t->next = snd_effects;
    snd_effects = t;
}

static void effect_unlink(snd_effect_t *t)
{
    snd_effect_t **p;

    for (p = &snd_effects; *p != NULL; p = &(*p)->next)
    {
        if (*p == t)
        {
            *p = t->next;
            return;
        }
    }
}

void snd_sfx_unload_all(void)
{
    snd_effect_t *t = snd_effects, *n;

    while (t)
    {
        n = t->next;
        snd_mem_free(t->locl);
        if (t->stereo)
            snd_mem_free(t->locr);
        free(t);
        t = n;
    }
    snd_effects = NULL;
}

void snd_sfx_unload(sfxhnd_t idx)
{
    snd_effect_t *t = (snd_effect_t *)(uintptr_t)idx;

    if (idx == SFXHND_INVALID)
    {
        dbglog(DBG_WARNING, "snd_sfx: can't unload an invalid SFXHND\n");
        return;
    }
    snd_mem_free(t->locl);
    if (t->stereo)
        snd_mem_free(t->locr);
    effect_unlink(t);
    free(t);
}

typedef struct {
    uint8_t riff[4];
    int32_t totalsize;
    uint8_t riff_format[4];
} wavmagic_t;

typedef struct {
    uint8_t id[4];
    uint32_t size;
} chunkhdr_t;

typedef struct {
    int16_t format;
    int16_t channels;
    int32_t sample_rate;
    int32_t byte_per_sec;
    int16_t blocksize;
    int16_t sample_size;
} fmthdr_t;

typedef struct {
    wavmagic_t magic;
    chunkhdr_t chunk;
    fmthdr_t fmt;
} wavhdr_t;

#define WAVE_FMT_PCM                   0x0001
#define WAVE_FMT_YAMAHA_ADPCM_ITU_G723 0x0014
#define WAVE_FMT_YAMAHA_ADPCM          0x0020

static int read_wav_header(file_t fd, wavhdr_t *wavhdr)
{
    if (fs_read(fd, &(wavhdr->magic), sizeof(wavhdr->magic)) != sizeof(wavhdr->magic))
    {
        dbglog(DBG_WARNING, "snd_sfx: can't read wav header\n");
        return -1;
    }
    if (strncmp((const char *)wavhdr->magic.riff, "RIFF", 4))
    {
        dbglog(DBG_WARNING, "snd_sfx: sfx file is not RIFF\n");
        return -1;
    }
    if (strncmp((const char *)wavhdr->magic.riff_format, "WAVE", 4))
    {
        dbglog(DBG_WARNING, "snd_sfx: sfx file is not RIFF WAVE\n");
        return -1;
    }
    do
    {
        if (fs_read(fd, &(wavhdr->chunk), sizeof(wavhdr->chunk)) != sizeof(wavhdr->chunk))
        {
            dbglog(DBG_WARNING, "snd_sfx: can't read chunk header\n");
            return -1;
        }
        if (strncmp((const char *)wavhdr->chunk.id, "fmt ", 4) == 0)
        {
            if (fs_read(fd, &(wavhdr->fmt), sizeof(wavhdr->fmt)) != sizeof(wavhdr->fmt))
            {
                dbglog(DBG_WARNING, "snd_sfx: can't read fmt header\n");
                return -1;
            }
            fs_seek(fd, wavhdr->chunk.size - sizeof(wavhdr->fmt), SEEK_CUR);
        }
        else if (strncmp((const char *)wavhdr->chunk.id, "data", 4) == 0)
        {
            break;
        }
        else
        {
            fs_seek(fd, wavhdr->chunk.size, SEEK_CUR);
        }
    } while (1);
    return 0;
}

static int read_wav_header_buf(char *buf, wavhdr_t *wavhdr, size_t *bufidx)
{
    size_t tmp_bufidx = *bufidx;
    int guard = 0;

    memcpy(&(wavhdr->magic), buf, sizeof(wavhdr->magic));
    tmp_bufidx += sizeof(wavhdr->magic);
    if (strncmp((const char *)wavhdr->magic.riff, "RIFF", 4))
    {
        dbglog(DBG_WARNING, "snd_sfx: sfx buffer is not RIFF\n");
        return -1;
    }
    if (strncmp((const char *)wavhdr->magic.riff_format, "WAVE", 4))
    {
        dbglog(DBG_WARNING, "snd_sfx: sfx buffer is not RIFF WAVE\n");
        return -1;
    }
    do
    {
        memcpy(&(wavhdr->chunk), buf + tmp_bufidx, sizeof(wavhdr->chunk));
        tmp_bufidx += sizeof(wavhdr->chunk);
        if (strncmp((const char *)wavhdr->chunk.id, "fmt ", 4) == 0)
        {
            memcpy(&(wavhdr->fmt), buf + tmp_bufidx, sizeof(wavhdr->fmt));
            tmp_bufidx += sizeof(wavhdr->fmt);
            tmp_bufidx += wavhdr->chunk.size - sizeof(wavhdr->fmt);
        }
        else if (strncmp((const char *)wavhdr->chunk.id, "data", 4) == 0)
        {
            break;
        }
        else
        {
            tmp_bufidx += wavhdr->chunk.size;
        }
        /* a buffer with no data chunk would walk off into memory */
        if (++guard > 64)
        {
            dbglog(DBG_WARNING, "snd_sfx: sfx buffer has no data chunk\n");
            return -1;
        }
    } while (1);
    *bufidx = tmp_bufidx;
    return 0;
}

static uint8_t *read_wav_data(file_t fd, wavhdr_t *wavhdr)
{
    uint8_t *wav_data = (uint8_t *)memalign(32, wavhdr->chunk.size);

    if (wav_data == NULL)
        return NULL;
    if ((size_t)fs_read(fd, wav_data, wavhdr->chunk.size) != wavhdr->chunk.size)
    {
        dbglog(DBG_WARNING, "snd_sfx: file has not been fully read.\n");
        free(wav_data);
        return NULL;
    }
    return wav_data;
}

static uint8_t *read_wav_data_buf(char *buf, wavhdr_t *wavhdr, size_t *bufidx)
{
    uint8_t *wav_data = (uint8_t *)memalign(32, wavhdr->chunk.size);

    if (wav_data == NULL)
        return NULL;
    memcpy(wav_data, buf + *bufidx, wavhdr->chunk.size);
    *bufidx += wavhdr->chunk.size;
    return wav_data;
}

static snd_effect_t *create_snd_effect(wavhdr_t *wavhdr, uint8_t *wav_data)
{
    snd_effect_t *effect;
    uint32_t len, rate;
    uint16_t channels, bitsize, fmt;

    effect = (snd_effect_t *)calloc(1, sizeof(snd_effect_t));
    if (effect == NULL)
        return NULL;

    fmt = (uint16_t)wavhdr->fmt.format;
    channels = (uint16_t)wavhdr->fmt.channels;
    rate = (uint32_t)wavhdr->fmt.sample_rate;
    bitsize = (uint16_t)wavhdr->fmt.sample_size;
    len = wavhdr->chunk.size;
    if (channels == 0)
        goto err_occurred;

    effect->rate = rate;
    effect->stereo = channels > 1;
    effect->locl = snd_mem_malloc(len / channels);
    if (!effect->locl)
        goto err_occurred;
    if (channels > 1)
    {
        effect->locr = snd_mem_malloc(len / channels);
        if (!effect->locr)
            goto err_occurred;
    }

    if (fmt == WAVE_FMT_YAMAHA_ADPCM_ITU_G723 || fmt == WAVE_FMT_YAMAHA_ADPCM)
    {
        effect->fmt = AICA_SM_ADPCM;
        effect->len = (len * 2) / channels;
    }
    else if (fmt == WAVE_FMT_PCM && bitsize == 8)
    {
        effect->fmt = AICA_SM_8BIT;
        effect->len = len / channels;
    }
    else if (fmt == WAVE_FMT_PCM && bitsize == 16)
    {
        effect->fmt = AICA_SM_16BIT;
        effect->len = (len / 2) / channels;
    }
    else
    {
        goto err_occurred;
    }

    if (channels == 1)
    {
        spu_memload_sq(effect->locl, wav_data, len);
    }
    else if (channels == 2 && fmt == WAVE_FMT_YAMAHA_ADPCM_ITU_G723)
    {
        /* not interleaved: the left half, then the right */
        spu_memload_sq(effect->locl, wav_data, len / 2);
        spu_memload_sq(effect->locr, wav_data + len / 2, len / 2);
    }
    else
    {
        uint32_t *left_buf = (uint32_t *)memalign(32, len / 2), *right_buf;

        if (left_buf == NULL)
            goto err_occurred;
        right_buf = (uint32_t *)memalign(32, len / 2);
        if (right_buf == NULL)
        {
            free(left_buf);
            goto err_occurred;
        }
        if (fmt == WAVE_FMT_PCM && bitsize == 16)
            snd_pcm16_split((uint32_t *)wav_data, left_buf, right_buf, len);
        else if (fmt == WAVE_FMT_PCM)
            snd_pcm8_split((uint32_t *)wav_data, left_buf, right_buf, len);
        else
            snd_adpcm_split((uint32_t *)wav_data, left_buf, right_buf, len);
        spu_memload_sq(effect->locl, left_buf, len / 2);
        spu_memload_sq(effect->locr, right_buf, len / 2);
        free(left_buf);
        free(right_buf);
    }
    return effect;

err_occurred:
    if (effect->locl)
        snd_mem_free(effect->locl);
    if (effect->locr)
        snd_mem_free(effect->locr);
    free(effect);
    return NULL;
}

sfxhnd_t snd_sfx_load(const char *fn)
{
    file_t fd;
    wavhdr_t wavhdr;
    snd_effect_t *effect;
    uint8_t *wav_data;

    fd = fs_open(fn, O_RDONLY);
    if (fd <= FILEHND_INVALID)
    {
        dbglog(DBG_ERROR, "snd_sfx_load: can't open %s\n", fn);
        return SFXHND_INVALID;
    }
    if (read_wav_header(fd, &wavhdr) < 0)
    {
        fs_close(fd);
        dbglog(DBG_ERROR, "snd_sfx_load: can't read wav header %s\n", fn);
        return SFXHND_INVALID;
    }
    wav_data = read_wav_data(fd, &wavhdr);
    fs_close(fd);
    if (!wav_data)
        return SFXHND_INVALID;
    effect = create_snd_effect(&wavhdr, wav_data);
    free(wav_data);
    if (!effect)
        return SFXHND_INVALID;
    effect_link(effect);
    return (sfxhnd_t)(uintptr_t)effect;
}

sfxhnd_t snd_sfx_load_raw_buf(char *buf, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels)
{
    snd_effect_t *effect;
    size_t chan_len;

    if (!buf || channels == 0)
        return SFXHND_INVALID;
    chan_len = len / channels;
    effect = (snd_effect_t *)calloc(1, sizeof(snd_effect_t));
    if (effect == NULL)
        return SFXHND_INVALID;
    effect->rate = rate;
    effect->stereo = channels > 1;
    switch (bitsize)
    {
        case 4:
            effect->fmt = AICA_SM_ADPCM;
            effect->len = (uint32_t)((len * 2) / channels);
            break;
        case 8:
            effect->fmt = AICA_SM_8BIT;
            effect->len = (uint32_t)(len / channels);
            break;
        case 16:
            effect->fmt = AICA_SM_16BIT;
            effect->len = (uint32_t)((len / 2) / channels);
            break;
        default:
            free(effect);
            return SFXHND_INVALID;
    }
    effect->locl = snd_mem_malloc(chan_len);
    if (!effect->locl)
    {
        free(effect);
        return SFXHND_INVALID;
    }
    spu_memload_sq(effect->locl, buf, chan_len);
    if (channels > 1)
    {
        effect->locr = snd_mem_malloc(chan_len);
        if (!effect->locr)
        {
            snd_mem_free(effect->locl);
            free(effect);
            return SFXHND_INVALID;
        }
        spu_memload_sq(effect->locr, buf + chan_len, chan_len);
    }
    effect_link(effect);
    return (sfxhnd_t)(uintptr_t)effect;
}

sfxhnd_t snd_sfx_load_fd(file_t fd, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels)
{
    char *buf = (char *)memalign(32, len ? len : 1);
    sfxhnd_t h = SFXHND_INVALID;

    if (buf == NULL)
        return SFXHND_INVALID;
    if ((size_t)fs_read(fd, buf, len) == len)
        h = snd_sfx_load_raw_buf(buf, len, rate, bitsize, channels);
    free(buf);
    return h;
}

sfxhnd_t snd_sfx_load_ex(const char *fn, uint32_t rate, uint16_t bitsize, uint16_t channels)
{
    sfxhnd_t effect;
    file_t fd = fs_open(fn, O_RDONLY);

    if (fd <= FILEHND_INVALID)
    {
        dbglog(DBG_ERROR, "snd_sfx_load_ex: can't open sfx %s\n", fn);
        return SFXHND_INVALID;
    }
    effect = snd_sfx_load_fd(fd, (size_t)fs_total(fd), rate, bitsize, channels);
    fs_close(fd);
    return effect;
}

sfxhnd_t snd_sfx_load_buf(char *buf)
{
    wavhdr_t wavhdr;
    snd_effect_t *effect;
    uint8_t *wav_data;
    size_t bufidx = 0;

    if (!buf)
    {
        dbglog(DBG_ERROR, "snd_sfx_load_buf: can't read wav data from NULL\n");
        return SFXHND_INVALID;
    }
    if (read_wav_header_buf(buf, &wavhdr, &bufidx) < 0)
    {
        dbglog(DBG_ERROR, "snd_sfx_load_buf: error reading wav header from buffer %p\n", (void *)buf);
        return SFXHND_INVALID;
    }
    wav_data = read_wav_data_buf(buf, &wavhdr, &bufidx);
    if (!wav_data)
        return SFXHND_INVALID;
    effect = create_snd_effect(&wavhdr, wav_data);
    free(wav_data);
    if (!effect)
        return SFXHND_INVALID;
    effect_link(effect);
    return (sfxhnd_t)(uintptr_t)effect;
}

int snd_sfx_play_chn(int chn, sfxhnd_t idx, int vol, int pan)
{
    sfx_play_data_t data;

    memset(&data, 0, sizeof(data));
    data.chn = chn;
    data.idx = idx;
    data.vol = vol;
    data.pan = pan;
    return snd_sfx_play_ex(&data);
}

static int find_free_channel(void)
{
    int chn, moved, old;

    old = irq_disable();
    chn = sfx_nextchan;
    moved = 0;
    while (sfx_inuse & (1ULL << chn))
    {
        chn = (chn + 1) % 64;
        if (sfx_nextchan == chn)
            break;
        moved++;
    }
    irq_restore(old);
    if (moved && chn == sfx_nextchan)
        return -1;
    sfx_nextchan = (chn + 2) % 64;  /* in case of stereo */
    return chn;
}

int snd_sfx_play(sfxhnd_t idx, int vol, int pan)
{
    sfx_play_data_t data;

    memset(&data, 0, sizeof(data));
    data.chn = -1;
    data.idx = idx;
    data.vol = vol;
    data.pan = pan;
    return snd_sfx_play_ex(&data);
}

int snd_sfx_play_ex(sfx_play_data_t *data)
{
    uint32_t size;
    snd_effect_t *t;
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);

    if (data->idx == SFXHND_INVALID)
        return -1;
    if (data->chn < 0)
    {
        data->chn = find_free_channel();
        if (data->chn < 0)
            return -1;
    }
    t = (snd_effect_t *)(uintptr_t)data->idx;
    size = t->len;
    if (size >= 65535)
        size = 65534;

    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = data->chn;
    chan->cmd = AICA_CH_CMD_START;
    chan->base = t->locl;
    chan->type = t->fmt;
    chan->length = size;
    chan->loop = data->loop;
    chan->loopstart = data->loopstart;
    chan->loopend = data->loopend ? data->loopend : size;
    chan->freq = data->freq > 0 ? (uint32_t)data->freq : t->rate;
    chan->vol = data->vol;

    if (!t->stereo)
    {
        chan->pan = data->pan;
        snd_sh4_to_aica(tmp, cmd->size);
    }
    else
    {
        chan->pan = 0;
        snd_sh4_to_aica_stop();
        snd_sh4_to_aica(tmp, cmd->size);
        cmd->cmd_id = data->chn + 1;
        chan->base = t->locr;
        chan->pan = 255;
        snd_sh4_to_aica(tmp, cmd->size);
        snd_sh4_to_aica_start();
    }
    return data->chn;
}

void snd_sfx_stop(int chn)
{
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);

    if (chn < 0)
        return;
    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = chn;
    chan->cmd = AICA_CH_CMD_STOP;
    chan->base = 0;
    chan->type = 0;
    chan->length = 0;
    chan->loop = 0;
    chan->loopstart = 0;
    chan->loopend = 0;
    chan->freq = 44100;
    chan->vol = 0;
    chan->pan = 0;
    snd_sh4_to_aica(tmp, cmd->size);
}

void snd_sfx_stop_all(void)
{
    int i;

    for (i = 0; i < 64; i++)
    {
        if (sfx_inuse & (1ULL << i))
            continue;
        snd_sfx_stop(i);
    }
}

int snd_sfx_chn_alloc(void)
{
    int old, chn;

    old = irq_disable();
    for (chn = 0; chn < 64; chn++)
        if (!(sfx_inuse & (1ULL << chn)))
            break;
    if (chn >= 64)
        chn = -1;
    else
        sfx_inuse |= 1ULL << chn;
    irq_restore(old);
    return chn;
}

void snd_sfx_chn_free(int chn)
{
    int old;

    if (chn < 0 || chn >= 64)
        return;
    old = irq_disable();
    sfx_inuse &= ~(1ULL << chn);
    irq_restore(old);
}
