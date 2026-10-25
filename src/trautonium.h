/*
 * trautonium.h — общие типы и состояние программы
 *
 *   Patch — всё, что относится к звучанию и хранится в пресете;
 *   Prefs — настройки игры и железа (калибровка, хаптика…), в пресет не входят.
 *
 * Оба меняются только в главном потоке и только под блокировкой аудио
 * (audio_lock / audio_unlock), аудиопоток их только читает.
 */
#ifndef TRAUTONIUM_H
#define TRAUTONIUM_H

#include <stdint.h>

#define APP_NAME      "Траутониум"
#define APP_VERSION   "2.0"

#define SR            48000
#define NUM_MANUALS   2
#define NUM_SUBS      4
#define NUM_FORMANTS  4
#define NUM_VOWELS    5
#define NUM_REGISTERS 3
#define DIV_MIN       1
#define DIV_MAX       24
#define MAX_IN        24          /* одновременных касаний */
#define PAD_EDGE      0.03f       /* у самого края тачпада играть неудобно */

typedef enum { SNAP_OFF, SNAP_SOFT, SNAP_HARD, SNAP_COUNT } SnapMode;
typedef enum { PS_FORCE, PS_AREA, PS_FIXED, PS_COUNT } PressSource;
typedef enum { CURVE_SOFT, CURVE_LINEAR, CURVE_HARD, CURVE_COUNT } PressCurve;

enum { CAL_FORCE = 1, CAL_AREA = 2 };
enum { WAVE_SAW = 0, WAVE_NEON = 1, WAVE_COUNT };

typedef struct {
    int   low_note;            /* MIDI-нота у левого края */
    int   span;                /* октав на ширину тачпада */
    int   mono;                /* 1 — одноголосный мануал, как у Залы */
    int   glide_ms;            /* портаменто при перехвате ноты */
    int   snap;                /* SnapMode */
    int   wave;                /* 0 — пила, 1 — «неон» (заряд конденсатора, как в генераторе Залы) */
    float main_lvl;            /* уровень основного тона */
    int   div[NUM_SUBS];       /* делители субгармоник */
    float sub_lvl[NUM_SUBS];   /* уровни субгармоник */
    float noise;               /* шум «дыхания», 0..1 */
    float drift;               /* медленный дрейф высоты, 0..1 (до ±10 центов) */
    int   vowel;               /* -1 — без формант */
    int   reg;                 /* 0 бас, 1 тенор, 2 сопрано */
    float cutoff;              /* срез фильтра яркости, Гц */
    float res;                 /* резонанс 0..0.95 */
    float press_bright;        /* насколько нажим открывает фильтр */
    int   attack_ms;           /* огибающая: атака */
    int   decay_ms;            /*            спад до сустейна */
    float sustain;             /*            уровень, пока палец лежит (0 — ударный звук) */
    int   release_ms;          /*            затухание после отпускания */
    float volume;
} ManualParams;

typedef struct {
    ManualParams man[NUM_MANUALS];
    int   split;               /* 1 — два мануала, 0 — весь тачпад один мануал */
    int   scale;               /* индекс в scales[] */
    int   tonic;               /* 0 = C … 11 = B */
    float reverb;
    float echo_mix;            /* эхо: громкость повторов */
    int   echo_ms;             /*       время между повторами */
    float echo_fb;             /*       сколько повторов (обратная связь) */
} Patch;

typedef struct {
    int   press_src;           /* PressSource */
    int   curve;               /* PressCurve */
    float force_floor_g;       /* лёгкое касание (калибровка) */
    float force_max_g;         /* полное нажатие (калибровка) */
    float area_floor;
    float area_max;
    int   calibrated;          /* CAL_FORCE | CAL_AREA */
    int   gate_g;              /* порог касания, 0 — выкл */
    float min_level;           /* громкость при самом лёгком касании */
    int   snap_ms;             /* скорость мягкого притяжения */
    int   haptics;
    int   block_gestures;
    float master;
    char  audio_out[128];      /* имя аудиоустройства, пусто — системное */
} Prefs;

extern Patch P;
extern Prefs PF;

/* ---- лады ---- */
typedef struct {
    const char *id;            /* для файлов пресетов */
    const char *name;          /* для интерфейса */
    uint16_t    mask;          /* бит k — ступень на k полутонов выше тоники */
} Scale;

extern const Scale scales[];
extern const int   num_scales;

int   scale_has(int midi);                 /* нота входит в текущий лад */
float scale_nearest(float note);           /* ближайшая нота лада */
int   is_tonic(int midi);

/* ---- справочные таблицы ---- */
typedef struct { float freq, bw, amp_db; } Formant;
extern const Formant vowel_presets[NUM_REGISTERS][NUM_VOWELS][NUM_FORMANTS];
extern const char   *vowel_names[NUM_VOWELS];
extern const char   *register_names[NUM_REGISTERS];

void default_patch(Patch *p);
void default_prefs(Prefs *pf);

/* ---- общие утилиты ---- */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

void  note_name(int midi, char *buf, int n);   /* 69 → "A4" */
float x_to_semis(const ManualParams *m, float x);

void audio_lock(void);
void audio_unlock(void);

#endif
