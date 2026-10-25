/*
 * recorder.c — запись в WAV
 *
 * Аудиопоток кладёт готовый выход в кольцевой буфер (synth_rec_*),
 * а здесь, в главном потоке, он переводится в 24-битный PCM и пишется
 * в файл. Звук в файле — ровно то, что слышно (после громкости и эффектов).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "recorder.h"
#include "synth.h"
#include "platform.h"
#include "trautonium.h"

static FILE    *f;
static uint32_t frames;
static char     path[1200];
static char     dir[1024];

const char *rec_dir(void)
{
    if (!dir[0]) snprintf(dir, sizeof dir, "%s", plat_music_dir());
    return dir;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v & 255; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 255; }

static void write_header(uint32_t nframes)
{
    uint8_t h[44];
    uint32_t data = nframes * 2 * 3;
    memcpy(h, "RIFF", 4);  put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); put32(h + 16, 16);
    put16(h + 20, 1);              /* PCM */
    put16(h + 22, 2);              /* стерео */
    put32(h + 24, SR);
    put32(h + 28, SR * 2 * 3);     /* байт в секунду */
    put16(h + 32, 2 * 3);          /* байт на кадр */
    put16(h + 34, 24);
    memcpy(h + 36, "data", 4); put32(h + 40, data);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    fseek(f, 0, SEEK_END);
}

int rec_start(void)
{
    if (f) return 1;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char stamp[64];
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &tmv);
    snprintf(path, sizeof path, "%s/Траутониум %s.wav", rec_dir(), stamp);

    f = fopen(path, "wb");
    if (!f) {
        /* ~/Music недоступна — пишем в папку настроек */
        snprintf(dir, sizeof dir, "%s/Recordings", plat_support_dir());
        mkdir(dir, 0755);
        snprintf(path, sizeof path, "%s/Траутониум %s.wav", dir, stamp);
        f = fopen(path, "wb");
        if (!f) return 0;
    }
    frames = 0;
    write_header(0);
    audio_lock();
    synth_rec_enable(1);
    audio_unlock();
    return 1;
}

void rec_poll(void)
{
    if (!f) return;
    static float   buf[16384];
    static uint8_t out[16384 / 2 * 6];
    int n;
    while ((n = synth_rec_read(buf, (int)(sizeof buf / sizeof buf[0]))) > 0) {
        for (int i = 0; i < n; i++) {
            float v = clampf(buf[i], -1.0f, 1.0f);
            int32_t s = (int32_t)lrintf(v * 8388607.0f);
            out[3 * i]     = s & 255;
            out[3 * i + 1] = (s >> 8) & 255;
            out[3 * i + 2] = (s >> 16) & 255;
        }
        fwrite(out, 3, (size_t)n, f);
        frames += (uint32_t)(n / 2);
    }
}

void rec_stop(void)
{
    if (!f) return;
    audio_lock();
    synth_rec_enable(0);
    audio_unlock();
    rec_poll();
    write_header(frames);
    fclose(f);
    f = NULL;
}

int rec_active(void) { return f != NULL; }

double rec_seconds(void) { return (double)frames / SR; }

const char *rec_last_path(void) { return path; }
