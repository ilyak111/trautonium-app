/*
 * synth.h — звуковой движок: голоса, фильтры, реверберация,
 * превращение касаний в ноты (мануалы, моно/поли, притяжение к ладу).
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>
#include "trautonium.h"

#define SYNTH_BLOCK  256        /* управление пересчитывается каждые 5.3 мс */
#define WAVE_BUF     2048
#define NUM_NOTE_KEYS 18        /* A Z S X C F V G B N J M K , L . / '  — от G# до C# */

/* касание, как его видит синтезатор (для отображения и хаптики) */
typedef struct {
    int   id, manual, gate, from_trackpad;
    float x, y;                 /* 0..1, y сверху вниз */
    float pn;                   /* нажим 0..1 после калибровки и кривой */
    float pressure_g, area;     /* сырые значения датчика */
    float raw_note;             /* нота под пальцем (MIDI, дробная) */
    float pitch_note;           /* что звучит (с притяжением) */
} SynthTouch;

void synth_init(int trackpad_flags);
void synth_audio_callback(void *userdata, uint8_t *stream, int len);

/* всё ниже — только под audio_lock() */
void synth_set_input_active(int on);        /* читать ли тачпад */
void synth_fb_upsert(int id, float x, float y, float pn);   /* мышь / SDL-касания */
void synth_fb_remove(int id);
void synth_fb_clear(void);
void synth_key(int idx, int down, int manual, int semis);  /* нота с клавиатуры: semis — полутонов от левого края */
void synth_keys_clear(void);
void synth_set_selected_manual(int m);       /* для раскладки «1 мануал» */
int  synth_snapshot(SynthTouch *out, int max);   /* касания последнего блока */
void synth_wave(float *out, int n);              /* последние n сэмплов для осциллографа */

/* нажим 0..1 из сырых значений (общая формула для синтезатора и калибровки) */
float synth_press_norm(float pressure_g, float area);

/* запись: аудиопоток складывает выход в кольцевой буфер, главный — забирает */
void     synth_rec_enable(int on);
int      synth_rec_read(float *dst, int max_floats);   /* стерео, чередование L R */
unsigned synth_rec_dropped(void);                      /* сколько кадров не влезло */

/* офлайн-рендер для тестов: n кадров стерео */
void synth_render(float *out, int frames);

#endif
