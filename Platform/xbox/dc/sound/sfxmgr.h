/* dc/sound/sfxmgr.h -- Xbox shim: KallistiOS v2.2.1's sound effect manager
 * (KOS dc/sound/sfxmgr.h, KOS licence, Platform/xbox/LICENSE.KOS).
 *
 * Doom 64 loads every sound effect into sound RAM at boot with
 * snd_sfx_load_buf (src/s_sound.c) -- Yamaha ADPCM WAVs, mono, 22050 Hz --
 * and plays them on the AICA's channels, one channel held for the plasma
 * gun's loop (src/p_pspr.c). The implementation is KOS's own
 * (Platform/xbox/kos_sfxmgr_xbox.c) over the AICA layer (aica/), which runs
 * KOS's stock firmware. */
#ifndef XBOX_DC_SOUND_SFXMGR_H
#define XBOX_DC_SOUND_SFXMGR_H

#include <kos/cdefs.h>
#include <kos/fs.h>
#include <stdint.h>

__BEGIN_DECLS

typedef uint32_t sfxhnd_t;
#define SFXHND_INVALID 0

typedef struct sfx_play_data {
    int chn;
    sfxhnd_t idx;
    int vol;
    int pan;
    int loop;
    int freq;
    unsigned int loopstart;
    unsigned int loopend;
} sfx_play_data_t;

sfxhnd_t snd_sfx_load(const char *fn);
sfxhnd_t snd_sfx_load_ex(const char *fn, uint32_t rate, uint16_t bitsize, uint16_t channels);
sfxhnd_t snd_sfx_load_fd(file_t fd, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels);
sfxhnd_t snd_sfx_load_buf(char *buf);
sfxhnd_t snd_sfx_load_raw_buf(char *buf, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels);
void snd_sfx_unload(sfxhnd_t idx);
void snd_sfx_unload_all(void);
int snd_sfx_play(sfxhnd_t idx, int vol, int pan);
int snd_sfx_play_chn(int chn, sfxhnd_t idx, int vol, int pan);
int snd_sfx_play_ex(sfx_play_data_t *data);
void snd_sfx_stop(int chn);
void snd_sfx_stop_all(void);
int snd_sfx_chn_alloc(void);
void snd_sfx_chn_free(int chn);

__END_DECLS

#endif /* XBOX_DC_SOUND_SFXMGR_H */
