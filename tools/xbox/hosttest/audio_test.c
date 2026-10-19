/* audio_test.c -- the Xbox port's sound path, run on the PC.
 *
 * Builds the AICA layer (Platform/xbox/aica/: KOS's firmware on the x86 and
 * the mixer, without DirectSound), KOS's sound stream and sound effect
 * manager as the port carries them, and the game's own music player
 * (src/sndwav.c), and drives them as the game does: the music player's
 * thread polls its stream, effects are loaded from their WAVs and played.
 * The mix is rendered in real time into a WAV file for listening or for
 * tools/xbox/hosttest/audio_check.py, which holds the music in it to a
 * direct decode of the same file.
 *
 *   audio_test <data folder> <out.wav> <seconds> [music name] [sfx 0|1]
 *              [stall at s] [stall length s] [loop 0|1]
 *
 * A stall freezes every thread but this one -- the music player's among
 * them -- for its length while the mix goes on: the stream's feeder held
 * off, as a level loading from the disc can hold it off on the Xbox.
 */
#include "xtl.h"

#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "sndwav.h"
#include "aica/aica_xbox.h"

doom64_settings_t menu_settings;

void aica_host_render(int16_t *out, int passes);

/* ----------------------------------------- what the Xbox build gets elsewhere */

int dbglog_level = DBG_DEBUG;

void kos_dbglog_printf(int lvl, const char *fmt, ...)
{
    va_list ap;

    (void)lvl;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void dbglog_set_level(int level)
{
    dbglog_level = level;
}

int dbgio_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return n;
}

static CRITICAL_SECTION sIrq;

irq_mask_t irq_disable(void)
{
    EnterCriticalSection(&sIrq);
    return 0;
}

void irq_restore(irq_mask_t v)
{
    (void)v;
    LeaveCriticalSection(&sIrq);
}

uint64_t timer_ms_gettime64(void)
{
    return GetTickCount64();
}

void xbox_fpu_setup(void)
{
}

const char *xbox_vmu_save_dir(void)
{
    return NULL;
}

int xbox_vmu_is_meta(const char *name)
{
    (void)name;
    return 0;
}

void __I_Error(const char *funcname, char *error, ...)
{
    fprintf(stderr, "I_Error %s: %s\n", funcname, error);
    exit(2);
}

ssize_t fs_load(const char *src, void **out_ptr)
{
    file_t f = fs_open(src, O_RDONLY);
    ssize_t n;

    *out_ptr = NULL;
    if (f == FILEHND_INVALID)
        return -1;
    n = fs_total(f);
    *out_ptr = malloc((size_t)n + 1);
    fs_read(f, *out_ptr, (size_t)n);
    fs_close(f);
    return n;
}

/* ------------------------------------------------------------------ test */

static LONG WINAPI crashed(EXCEPTION_POINTERS *e)
{
    fprintf(stderr, "audio_test: CRASH %08lx at %p (image %p)\n", e->ExceptionRecord->ExceptionCode,
            e->ExceptionRecord->ExceptionAddress, (void *)GetModuleHandleA(NULL));
    {
        /* the stack words that point into the image: likely return addresses */
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        const uint32_t *sp = (const uint32_t *)(uintptr_t)e->ContextRecord->Esp;
        int i;

        for (i = 0; i < 256; i++)
            if (sp[i] >= base && sp[i] < base + 0x200000)
                fprintf(stderr, " %08x", sp[i] - (uint32_t)base + 0x400000);
        fputc('\n', stderr);
    }
    fflush(stderr);
    ExitProcess(3);
    return 0;
}

/* every thread of this process but the caller's, suspended (on) or resumed */
static void hold_others(int on)
{
    THREADENTRY32 te;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();

    if (snap == INVALID_HANDLE_VALUE)
        return;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te))
    {
        do
        {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != me)
            {
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);

                if (h != NULL)
                {
                    if (on)
                        SuspendThread(h);
                    else
                        ResumeThread(h);
                    CloseHandle(h);
                }
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    const char *names[] = { "sfx_pistol", "sfx_shotgun", "sfx_doorup", "sfx_impsit1", "sfx_explode" };
    sfxhnd_t sfx[5];
    const char *mus = argc > 4 ? argv[4] : "musamb01";
    int use_sfx = argc > 5 ? atoi(argv[5]) : 1;
    int seconds = argc > 3 ? atoi(argv[3]) : 10;
    double stall_at = argc > 6 ? atof(argv[6]) : -1.0, stall_len = argc > 7 ? atof(argv[7]) : 1.5;
    int total = seconds * 44100 / AICA_PASS, done = 0, i, next_sfx = 0, stalled = 0;
    char path[512];
    int16_t pcm[AICA_PASS * 2];
    FILE *out;
    LARGE_INTEGER f, t0, t;

    if (argc < 3)
    {
        fprintf(stderr, "usage: audio_test <data folder> <out.wav> <seconds> [music] [sfx]\n");
        return 1;
    }
    SetUnhandledExceptionFilter(crashed);
    InitializeCriticalSection(&sIrq);
    menu_settings.MusVolume = 100;
    menu_settings.SfxVolume = 100;

    if (!wav_init())
    {
        fprintf(stderr, "wav_init failed\n");
        return 1;
    }
    for (i = 0; i < 5; i++)
    {
        void *buf;

        snprintf(path, sizeof(path), "/cd/sfx/%s.wav", names[i]);
        if (fs_load(path, &buf) < 0)
        {
            fprintf(stderr, "no %s\n", path);
            return 1;
        }
        sfx[i] = snd_sfx_load_buf((char *)buf);
        free(buf);
    }
    if (strcmp(mus, "none") != 0)
    {
        snprintf(path, sizeof(path), "/cd/mus/%s.adpcm", mus);
        if (wav_create(path, argc > 8 ? atoi(argv[8]) : 1) == SND_STREAM_INVALID)
        {
            fprintf(stderr, "wav_create %s failed\n", path);
            return 1;
        }
        wav_play();
        wav_volume(255);
    }

    out = fopen(argv[2], "wb");
    fwrite("RIFF", 1, 4, out); put32(out, 36 + total * AICA_PASS * 4);
    fwrite("WAVEfmt ", 1, 8, out); put32(out, 16); put16(out, 1); put16(out, 2);
    put32(out, 44100); put32(out, 44100 * 4); put16(out, 4); put16(out, 16);
    fwrite("data", 1, 4, out); put32(out, total * AICA_PASS * 4);

    /* in real time: the music player's thread polls on the wall clock */
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    while (done < total)
    {
        int due;

        QueryPerformanceCounter(&t);
        due = (int)((double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart * 44100.0 / AICA_PASS);
        if (due > total)
            due = total;
        if (done >= due)
        {
            Sleep(1);
            continue;
        }
        if (stall_at >= 0.0)
        {
            double now = (double)done * AICA_PASS / 44100.0;

            if (stalled == 0 && now >= stall_at)
            {
                hold_others(1);
                stalled = 1;
                fprintf(stderr, "audio_test: feeder held at %.2f s\n", now);
            }
            else if (stalled == 1 && now >= stall_at + stall_len)
            {
                hold_others(0);
                stalled = 2;
                fprintf(stderr, "audio_test: feeder released at %.2f s\n", now);
            }
        }
        while (done < due)
        {
            aica_host_render(pcm, 1);
            fwrite(pcm, 4, AICA_PASS, out);
            done++;
            if (use_sfx && done >= next_sfx)
            {
                int k = (done / 441) % 5;

                snd_sfx_play(sfx[k], 124, (int)((done / 220) * 53) & 255);
                next_sfx = done + 441 / 2;      /* twice a second */
            }
        }
    }
    fclose(out);
    fprintf(stderr, "audio_test: %d s of %s%s -> %s\n", seconds, mus, use_sfx ? " + sfx" : "", argv[2]);
    return 0;
}
