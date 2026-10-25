/*
 * synth.c — звуковой движок траутониума
 *
 * Голос: пила (PolyBLEP) + 4 субгармоники f/N (настоящее деление частоты:
 * счётчик периодов основного тона) → фильтр яркости, открывающийся нажимом.
 * Мануал: сумма голосов → банк из 4 формант (гласная) → громкость.
 * Выход: мягкое ограничение и небольшой Freeverb.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "synth.h"
#include "mt_input.h"

/* ===================================================================== */
/*                          общие данные                                 */
/* ===================================================================== */
Patch P;
Prefs PF;

const char *vowel_names[NUM_VOWELS] = {"A", "E", "I", "O", "U"};
const char *register_names[NUM_REGISTERS] = {"Бас", "Тенор", "Сопрано"};

/* таблицы формант — из trautonium_line_control.c */
const Formant vowel_presets[NUM_REGISTERS][NUM_VOWELS][NUM_FORMANTS] = {
    { /* Bass */
        {{ 600, 60,   0}, {1040, 70,  -7}, {2250, 110,  -9}, {2450, 120,  -9}},
        {{ 400, 40,   0}, {1620, 80, -12}, {2400, 100,  -9}, {2800, 120, -12}},
        {{ 250, 60,   0}, {1750, 90, -30}, {2600, 100, -16}, {3050, 120, -22}},
        {{ 400, 40,   0}, { 750, 80, -11}, {2400, 100, -21}, {2600, 120, -20}},
        {{ 350, 40,   0}, { 600, 80, -20}, {2400, 100, -32}, {2675, 120, -28}},
    },
    { /* Tenor */
        {{ 650, 80,   0}, {1080, 90,  -6}, {2650, 120,  -7}, {2900, 130,  -8}},
        {{ 400, 70,   0}, {1700, 80, -14}, {2600, 100, -12}, {3200, 120, -14}},
        {{ 290, 40,   0}, {1870, 90, -15}, {2800, 100, -18}, {3250, 120, -20}},
        {{ 400, 40,   0}, { 800, 80, -10}, {2600, 100, -12}, {2800, 120, -12}},
        {{ 350, 40,   0}, { 600, 80, -20}, {2700, 100, -17}, {2900, 120, -14}},
    },
    { /* Soprano */
        {{ 800, 80,   0}, {1150, 90,  -6}, {2900, 120, -32}, {3900, 130, -20}},
        {{ 350, 60,   0}, {2000, 100, -20}, {2800, 120, -15}, {3600, 150, -40}},
        {{ 270, 60,   0}, {2140, 90, -12}, {2950, 100, -26}, {3900, 120, -26}},
        {{ 450, 70,   0}, { 800, 80, -11}, {2830, 100, -22}, {3800, 130, -50}},
        {{ 325, 50,   0}, { 700, 60, -16}, {2700, 170, -35}, {3800, 180, -40}},
    },
};


const Scale scales[] = {
    {"chromatic",  "Хроматика",          0x0FFF},
    {"major",      "Мажор",              (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11)},
    {"minor",      "Минор",              (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10)},
    {"harmonic",   "Гармонический минор",(1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11)},
    {"dorian",     "Дорийский",          (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10)},
    {"pent_major", "Пентатоника мажор",  (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9)},
    {"pent_minor", "Пентатоника минор",  (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10)},
    {"blues",      "Блюз",               (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10)},
    {"whole",      "Целотонный",         (1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<8)|(1<<10)},
};
const int num_scales = (int)(sizeof scales / sizeof scales[0]);

static int pc(int midi) { return ((midi % 12) + 12) % 12; }

int scale_has(int midi)
{
    int s = clampi(P.scale, 0, num_scales - 1);
    return (scales[s].mask >> pc(midi - P.tonic)) & 1;
}

int is_tonic(int midi) { return pc(midi - P.tonic) == 0; }

float scale_nearest(float note)
{
    int base = (int)lroundf(note);
    float best = (float)base;
    float bd = 1e9f;
    for (int d = -7; d <= 7; d++) {
        int n = base + d;
        if (!scale_has(n)) continue;
        float dist = fabsf((float)n - note);
        if (dist < bd) { bd = dist; best = (float)n; }
    }
    return best;
}

void note_name(int midi, char *buf, int n)
{
    static const char *nm[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    midi = clampi(midi, 0, 127);
    snprintf(buf, n, "%s%d", nm[pc(midi)], midi / 12 - 1);
}

float x_to_semis(const ManualParams *m, float x)
{
    float u = clampf((x - PAD_EDGE) / (1.0f - 2.0f * PAD_EDGE), 0.0f, 1.0f);
    return u * (float)(m->span * 12);
}

void default_patch(Patch *p)
{
    static const int   div0[NUM_SUBS] = {2, 3, 4, 5};   /* «микстура» Залы */
    static const int   div1[NUM_SUBS] = {2, 4, 6, 8};   /* как в оригинале */
    static const float lv0[NUM_SUBS]  = {0.55f, 0.40f, 0.30f, 0.20f};
    static const float lv1[NUM_SUBS]  = {0.50f, 0.50f, 0.25f, 0.25f};

    memset(p, 0, sizeof *p);
    for (int m = 0; m < NUM_MANUALS; m++) {
        ManualParams *M = &p->man[m];
        M->low_note     = m == 0 ? 57 : 45;      /* I: A3..A5, II: A2..A4 */
        M->span         = 2;
        M->mono         = 0;         /* поли: каждый палец — свой голос */
        M->glide_ms     = 20;
        M->snap         = SNAP_OFF;
        M->main_lvl     = 0.8f;
        for (int k = 0; k < NUM_SUBS; k++) {
            M->div[k]     = m == 0 ? div0[k] : div1[k];
            M->sub_lvl[k] = m == 0 ? lv0[k] : lv1[k];
        }
        M->vowel        = m == 0 ? -1 : 0;
        M->reg          = 1;
        M->cutoff       = 1800.0f;
        M->res          = 0.15f;
        M->press_bright = 0.6f;
        M->wave         = WAVE_SAW;
        M->noise        = 0.0f;
        M->drift        = 0.2f;      /* чуть живой высоты, как у лампового генератора */
        M->attack_ms    = 5;
        M->decay_ms     = 300;
        M->sustain      = 1.0f;
        M->release_ms   = 120;
        M->volume       = 0.55f;
    }
    p->split    = 1;
    p->scale    = 0;
    p->tonic    = 9;        /* A */
    p->reverb   = 0.25f;
    p->echo_mix = 0.0f;
    p->echo_ms  = 350;
    p->echo_fb  = 0.35f;
}

void default_prefs(Prefs *pf)
{
    memset(pf, 0, sizeof *pf);
    pf->press_src      = PS_FORCE;
    pf->curve          = CURVE_LINEAR;
    pf->force_floor_g  = 0.0f;
    pf->force_max_g    = 300.0f;
    pf->area_floor     = 0.0f;
    pf->area_max       = 0.0f;   /* 0 — подстраиваться автоматически */
    pf->calibrated     = 0;
    pf->gate_g         = 0;
    pf->min_level      = 0.12f;
    pf->snap_ms        = 120;
    pf->haptics        = 1;
    pf->block_gestures = 0;
    pf->master         = 0.8f;
}

/* ===================================================================== */
/*                              фильтры                                  */
/* ===================================================================== */
#define PI_F 3.14159265358979f

/* фильтр с переменной структурой (TPT SVF) — устойчив на любых частотах */
typedef struct { float a1, a2, a3, k; } SvfCoef;
typedef struct { float ic1, ic2; } Svf;

static SvfCoef svf_coef(float fc, float k)
{
    SvfCoef c;
    fc = clampf(fc, 20.0f, 0.45f * SR);
    float g = tanf(PI_F * fc / SR);
    c.k  = k;
    c.a1 = 1.0f / (1.0f + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

static inline void svf_tick(Svf *s, const SvfCoef *c, float x, float *lp, float *bp)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
}

#define FORMANT_MAKEUP_DB 10.0f

typedef struct {
    SvfCoef c[NUM_FORMANTS];
    float   gain[NUM_FORMANTS];
    Svf     st[NUM_FORMANTS];
    int     vowel, reg;            /* для чего посчитаны коэффициенты */
} FormantBank;

static FormantBank fbank[NUM_MANUALS];

static void formants_update(FormantBank *b, const ManualParams *M)
{
    if (b->vowel == M->vowel && b->reg == M->reg) return;
    b->vowel = M->vowel;
    b->reg   = M->reg;
    if (M->vowel < 0) return;
    int reg = clampi(M->reg, 0, NUM_REGISTERS - 1);
    int vow = clampi(M->vowel, 0, NUM_VOWELS - 1);
    for (int i = 0; i < NUM_FORMANTS; i++) {
        const Formant *f = &vowel_presets[reg][vow][i];
        b->c[i]    = svf_coef(f->freq, f->bw / f->freq);    /* k = 1/Q */
        b->gain[i] = powf(10.0f, (f->amp_db + FORMANT_MAKEUP_DB) / 20.0f);
    }
}

/* ===================================================================== */
/*                     реверберация (Freeverb)                            */
/* ===================================================================== */
#define NCOMB 8
#define NAP   4
#define RV_MAXLEN 1800

typedef struct { float buf[RV_MAXLEN]; int len, pos; float store; } Comb;
typedef struct { float buf[RV_MAXLEN]; int len, pos; } Allpass;

static Comb    combL[NCOMB], combR[NCOMB];
static Allpass apL[NAP], apR[NAP];

static void reverb_init(void)
{
    static const int cl[NCOMB] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int al[NAP]   = {556, 441, 341, 225};
    const float s = SR / 44100.0f;
    for (int i = 0; i < NCOMB; i++) {
        memset(&combL[i], 0, sizeof combL[i]);
        memset(&combR[i], 0, sizeof combR[i]);
        combL[i].len = (int)(cl[i] * s);
        combR[i].len = (int)((cl[i] + 23) * s);
    }
    for (int i = 0; i < NAP; i++) {
        memset(&apL[i], 0, sizeof apL[i]);
        memset(&apR[i], 0, sizeof apR[i]);
        apL[i].len = (int)(al[i] * s);
        apR[i].len = (int)((al[i] + 23) * s);
    }
}

static inline float comb_tick(Comb *c, float in)
{
    const float fb = 0.84f, damp = 0.3f;
    float out = c->buf[c->pos];
    c->store = out * (1.0f - damp) + c->store * damp;
    c->buf[c->pos] = in + c->store * fb;
    if (++c->pos >= c->len) c->pos = 0;
    return out;
}

static inline float ap_tick(Allpass *a, float in)
{
    float b = a->buf[a->pos];
    float out = -in + b;
    a->buf[a->pos] = in + b * 0.5f;
    if (++a->pos >= a->len) a->pos = 0;
    return out;
}

/* ===================================================================== */
/*                  эхо: стерео, с лёгким пинг-понгом                      */
/* Время меняется плавно, как у ленточного эха; в обратной связи — фильтр, */
/* каждый повтор чуть темнее предыдущего.                                 */
/* ===================================================================== */
#define ECHO_LEN 65536                       /* 1,36 с при 48 кГц */
static float echoL[ECHO_LEN], echoR[ECHO_LEN];
static int   echo_pos;
static float echo_cur = 350.0f * SR / 1000.0f;
static float echo_lpL, echo_lpR;

static inline float echo_read(const float *buf, float delay)
{
    float rp = (float)echo_pos - delay;
    while (rp < 0) rp += ECHO_LEN;
    int   i0 = (int)rp;
    float fr = rp - (float)i0;
    float a = buf[i0 & (ECHO_LEN - 1)], b = buf[(i0 + 1) & (ECHO_LEN - 1)];
    return a + (b - a) * fr;
}

static void echo_init(void)
{
    memset(echoL, 0, sizeof echoL);
    memset(echoR, 0, sizeof echoR);
    echo_pos = 0;
    echo_lpL = echo_lpR = 0;
    echo_cur = clampi(P.echo_ms, 40, 1300) * SR / 1000.0f;
}

/* ===================================================================== */
/*                     форма волны «неон»                                 */
/* Генератор Траутониума — неоновая лампа и конденсатор: напряжение растёт */
/* по экспоненте заряда и резко сбрасывается. Получается пила с более     */
/* мягким верхом. Табличка + линейная интерполяция.                        */
/* ===================================================================== */
#define NEON_N 512
static float neon_tab[NEON_N + 1];

static void neon_init(void)
{
    const float k = 2.6f;
    double mean = 0;
    for (int i = 0; i <= NEON_N; i++) {
        float t = (float)i / NEON_N;
        neon_tab[i] = 2.0f * (1.0f - expf(-k * t)) / (1.0f - expf(-k)) - 1.0f;
        if (i < NEON_N) mean += neon_tab[i];
    }
    /* убираем постоянную составляющую, иначе субгармоники копят её в фильтре */
    mean /= NEON_N;
    for (int i = 0; i <= NEON_N; i++) neon_tab[i] -= (float)mean;
}

static inline float wave_shape(int wave, float t)
{
    if (wave != WAVE_NEON) return 2.0f * t - 1.0f;
    float f = t * NEON_N;
    int   i = (int)f;
    if (i >= NEON_N) i = NEON_N - 1;
    float fr = f - (float)i;
    return neon_tab[i] + (neon_tab[i + 1] - neon_tab[i]) * fr;
}

/* ===================================================================== */
/*                                голоса                                 */
/* ===================================================================== */
#define MAX_VOICES   16
#define OWNER_MONO   4000       /* + номер мануала */
#define OWNER_KEY    5000       /* + номер клавиши */

enum { ENV_ATTACK, ENV_DECAY };

typedef struct {
    int      active, gate, claimed;
    int      owner, manual, src;   /* src — касание, которое сейчас ведёт голос */
    float    lhz, target_lhz;      /* log2(частоты) */
    float    lvl, target_lvl;      /* громкость от нажима */
    float    env;                  /* огибающая 0..1 */
    int      stage;
    float    pn;
    float    phase;
    int      cnt[NUM_SUBS];
    float    drift, drift_tgt, drift_t;   /* медленный дрейф высоты */
    uint32_t rng;
    Svf      tone;
} Voice;

typedef struct {
    int      id, manual, gate, from_trackpad, fresh;
    unsigned order;
    float    x, y, pn, pressure_g, area;
    float    raw;              /* нота под пальцем */
    float    avg, avg_prev;    /* сглаженная нота (для мягкого притяжения) */
    float    speed;            /* скорость скольжения, полутонов/с */
    float    off;              /* текущая поправка притяжения */
    float    pitch;            /* звучащая нота */
} Track;

typedef struct {
    int   id;
    float x, y, pn;
} FbTouch;

static Voice    voices[MAX_VOICES];
static Track    tracks[MAX_IN];
static int      ntracks;
static unsigned order_ctr;

static int      mt_flags;
static int      input_active;
static MTFrame  last_frame;
static float    area_auto = 0.3f;

static FbTouch  fb[MAX_IN];
static int      fb_count;
static int      key_down[NUM_NOTE_KEYS], key_manual[NUM_NOTE_KEYS], key_semis[NUM_NOTE_KEYS];

static float    wave[WAVE_BUF];
static int      wave_pos;

static inline float polyblep(float t, float dt)
{
    if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

static Voice *find_voice(int owner)
{
    for (int i = 0; i < MAX_VOICES; i++)
        if (voices[i].active && voices[i].owner == owner) return &voices[i];
    return NULL;
}

static Voice *alloc_voice(void)
{
    for (int i = 0; i < MAX_VOICES; i++)
        if (!voices[i].active) return &voices[i];
    /* все заняты — забираем самый тихий затухающий, иначе самый тихий */
    Voice *best = NULL;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!voices[i].gate && (!best || voices[i].env < best->env)) best = &voices[i];
    if (best) return best;
    best = &voices[0];
    for (int i = 1; i < MAX_VOICES; i++)
        if (voices[i].lvl * voices[i].env < best->lvl * best->env) best = &voices[i];
    return best;
}

static float note_lhz(float note) { return log2f(440.0f) + (note - 69.0f) / 12.0f; }

static float pn_to_level(float pn)
{
    return PF.min_level + (1.0f - PF.min_level) * clampf(pn, 0.0f, 1.0f);
}

static inline float rnd(uint32_t *s)            /* xorshift32 → [-1, 1) */
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* голос owner на мануале m должен звучать нотой note с нажимом pn;
   src — касание, которое его ведёт (смена src = новая атака) */
static void claim_voice(int owner, int m, float note, float pn, int src)
{
    float tl = note_lhz(note);
    Voice *v = find_voice(owner);
    if (!v) {
        v = alloc_voice();
        memset(v, 0, sizeof *v);
        v->active = 1;
        v->owner  = owner;
        v->lhz    = tl;
        v->pn     = pn;
        v->lvl    = pn_to_level(pn);
        v->stage  = ENV_ATTACK;
        v->rng    = (uint32_t)owner * 2654435761u + 0x9E3779B9u;
        v->src    = src;
    } else if (!v->gate) {
        v->lhz   = tl;          /* новое касание после отпускания — высота сразу */
        v->stage = ENV_ATTACK;  /* атака с текущего уровня, без щелчка */
    } else if (v->src != src) {
        v->stage = ENV_ATTACK;  /* перехват ноты другим пальцем — новый «удар» */
    }
    v->src        = src;
    v->manual     = m;
    v->gate       = 1;
    v->claimed    = 1;
    v->target_lhz = tl;
    v->pn        += (pn - v->pn) * 0.5f;
    v->target_lvl = pn_to_level(pn);
}

static void render_voice(Voice *v, const ManualParams *M, float *bus, int n)
{
    int   div[NUM_SUBS];
    float lv[NUM_SUBS];
    float lvl0 = M->main_lvl, sum = lvl0;
    const float bdt = (float)n / SR;

    for (int k = 0; k < NUM_SUBS; k++) {
        div[k] = clampi(M->div[k], DIV_MIN, DIV_MAX);
        lv[k]  = M->sub_lvl[k];
        sum   += lv[k];
        if (v->cnt[k] >= div[k]) v->cnt[k] %= div[k];
    }
    float norm = 1.0f / (0.5f + 0.5f * (sum > 0.01f ? sum : 0.01f));

    /* дрейф: новая случайная цель каждые 0,15–0,5 с, плавное движение к ней */
    v->drift_t -= bdt;
    if (v->drift_t <= 0.0f) {
        v->drift_tgt = rnd(&v->rng);
        v->drift_t   = 0.15f + 0.35f * (0.5f + 0.5f * rnd(&v->rng));
    }
    v->drift += (v->drift_tgt - v->drift) * (1.0f - expf(-bdt / 0.35f));
    float drift_oct = M->drift * v->drift * (10.0f / 1200.0f);

    float tau_p = (M->glide_ms > 6 ? M->glide_ms : 6) * 0.001f;
    float cp = 1.0f - expf(-1.0f / (tau_p * SR));

    /* громкость от нажима: быстро вверх, чуть медленнее вниз */
    float tl = v->target_lvl;
    float cl = 1.0f - expf(-1.0f / ((tl > v->lvl ? 0.004f : 0.012f) * SR));

    /* огибающая */
    float a_inc = 1.0f / (clampi(M->attack_ms, 1, 5000) * 0.001f * SR);
    float c_dec = 1.0f - expf(-1.0f / (clampi(M->decay_ms, 5, 10000) * 0.001f * SR));
    float c_rel = 1.0f - expf(-1.0f / (clampi(M->release_ms, 5, 10000) * 0.001f * SR));
    float sus   = clampf(M->sustain, 0.0f, 1.0f);

    /* нажим открывает фильтр яркости (до трёх октав) */
    float fc = M->cutoff * exp2f(M->press_bright * v->pn * 3.0f);
    float k  = 2.0f * (1.0f - M->res) * 0.7071f;
    if (k < 0.06f) k = 0.06f;
    SvfCoef tc = svf_coef(fc, k);

    float noise = M->noise * 0.7f;
    int   wave  = M->wave;

    for (int i = 0; i < n; i++) {
        v->lhz += (v->target_lhz - v->lhz) * cp;
        float dt = exp2f(v->lhz + drift_oct) / SR;
        if (dt > 0.45f) dt = 0.45f;

        v->phase += dt;
        if (v->phase >= 1.0f) {
            v->phase -= 1.0f;
            for (int s = 0; s < NUM_SUBS; s++)
                if (++v->cnt[s] >= div[s]) v->cnt[s] = 0;
        }
        float t = v->phase;
        float x = lvl0 * (wave_shape(wave, t) - polyblep(t, dt));

        /* субгармоника f/N: счётчик периодов основного тона + его фаза */
        for (int s = 0; s < NUM_SUBS; s++) {
            if (lv[s] <= 0.0f) continue;
            float N  = (float)div[s];
            float ts = ((float)v->cnt[s] + t) / N;
            x += lv[s] * (wave_shape(wave, ts) - polyblep(ts, dt / N));
        }
        x *= norm;
        if (noise > 0.0f) x += noise * rnd(&v->rng);

        float lp, bp;
        svf_tick(&v->tone, &tc, x, &lp, &bp);

        if (v->gate) {
            if (v->stage == ENV_ATTACK) {
                v->env += a_inc;
                if (v->env >= 1.0f) { v->env = 1.0f; v->stage = ENV_DECAY; }
            } else {
                v->env += (sus - v->env) * c_dec;
            }
        } else {
            v->env -= v->env * c_rel;
        }
        v->lvl += (tl - v->lvl) * cl;
        bus[i] += lp * v->lvl * v->env;
    }

    if (!v->gate && v->env < 1e-4f) v->active = 0;
}

/* ===================================================================== */
/*                            нажим                                      */
/* ===================================================================== */
float synth_press_norm(float pressure_g, float area)
{
    float u;
    switch (PF.press_src) {
        case PS_FORCE: {
            float lo = PF.force_floor_g, hi = PF.force_max_g;
            if (hi < lo + 5.0f) hi = lo + 5.0f;
            u = (pressure_g - lo) / (hi - lo);
        } break;
        case PS_AREA: {
            float lo = PF.area_floor;
            float hi = PF.area_max > 0.0f ? PF.area_max : area_auto;
            if (hi < lo + 0.01f) hi = lo + 0.01f;
            u = (area - lo) / (hi - lo);
        } break;
        default:
            return 0.7f;
    }
    u = clampf(u, 0.0f, 1.0f);
    switch (PF.curve) {
        case CURVE_SOFT: return powf(u, 0.55f);
        case CURVE_HARD: return powf(u, 1.4f);
        default:         return powf(u, 0.8f);
    }
}

/* ===================================================================== */
/*                  касания → ноты (раз в блок)                           */
/* ===================================================================== */
typedef struct {
    int   id, from_trackpad;
    float x, y, pressure_g, area, pn_override;
} InTouch;

static int manual_for_y(float y, int sel)
{
    if (!P.split) return sel;
    return y < 0.5f ? 0 : 1;
}

static int single_manual;   /* какой мануал на весь тачпад при раскладке «1 мануал» */

/* мягкое притяжение: пока палец стоит, высота «доезжает» до ноты лада;
   пока скользит — точно следует за пальцем */
static void track_pitch(Track *tr, const ManualParams *M)
{
    const float dt = (float)SYNTH_BLOCK / SR;

    if (M->snap == SNAP_HARD) { tr->pitch = scale_nearest(tr->raw); tr->off = 0; return; }
    if (M->snap == SNAP_OFF)  { tr->pitch = tr->raw; tr->off = 0; return; }

    if (tr->fresh) {
        tr->avg = tr->avg_prev = tr->raw;
        tr->speed = 0.0f;
        tr->off = scale_nearest(tr->raw) - tr->raw;     /* касание сразу в ноту */
    } else {
        tr->avg_prev = tr->avg;
        tr->avg += (tr->raw - tr->avg) * (1.0f - expf(-dt / 0.10f));
        float sp = fabsf(tr->avg - tr->avg_prev) / dt;
        tr->speed += (sp - tr->speed) * (1.0f - expf(-dt / 0.03f));

        float tau, target;
        if (tr->speed > 3.5f) { target = 0.0f; tau = 0.03f; }           /* скользит */
        else {
            target = scale_nearest(tr->avg) - tr->avg;
            tau = (PF.snap_ms > 5 ? PF.snap_ms : 5) * 0.001f;
        }
        tr->off += (target - tr->off) * (1.0f - expf(-dt / tau));
    }
    tr->pitch = tr->raw + tr->off;
}

static void control_update(void)
{
    InTouch in[MAX_IN];
    int nin = 0;

    if ((mt_flags & MT_OK) && input_active) {
        MTFrame f;
        if (mt_read(&f, 1)) last_frame = f;
        /* кадров давно не было (палец убран, тачпад «уснул») — касаний нет */
        if (mt_now() - last_frame.time < 1.0) {
            for (int i = 0; i < last_frame.count && nin < MAX_IN; i++) {
                const MTPoint *p = &last_frame.t[i];
                InTouch *t = &in[nin++];
                t->id = p->id; t->x = p->x; t->y = p->y;
                t->pressure_g = p->pressure; t->area = p->area;
                t->pn_override = -1.0f;
                t->from_trackpad = 1;
                if (p->area > area_auto) area_auto = p->area;
            }
        }
    }
    for (int i = 0; i < fb_count && nin < MAX_IN; i++) {
        InTouch *t = &in[nin++];
        t->id = fb[i].id; t->x = fb[i].x; t->y = fb[i].y;
        t->pressure_g = 0; t->area = 0;
        t->pn_override = fb[i].pn;
        t->from_trackpad = 0;
    }

    /* сопоставляем касания с дорожками: мануал выбирается в момент касания */
    Track nt[MAX_IN];
    int nn = 0;
    for (int i = 0; i < nin; i++) {
        Track *old = NULL;
        for (int j = 0; j < ntracks; j++)
            if (tracks[j].id == in[i].id) { old = &tracks[j]; break; }

        Track tr;
        if (old) { tr = *old; tr.fresh = 0; }
        else {
            memset(&tr, 0, sizeof tr);
            tr.id = in[i].id;
            tr.manual = manual_for_y(in[i].y, single_manual);
            tr.order = ++order_ctr;
            tr.fresh = 1;
        }
        tr.from_trackpad = in[i].from_trackpad;
        tr.x = in[i].x;
        tr.y = in[i].y;
        tr.pressure_g = in[i].pressure_g;
        tr.area = in[i].area;
        tr.pn = in[i].pn_override >= 0.0f ? in[i].pn_override
                                         : synth_press_norm(in[i].pressure_g, in[i].area);

        /* порог касания с гистерезисом: «проволока прижата к шине» */
        if (in[i].pn_override >= 0.0f || PF.press_src != PS_FORCE || PF.gate_g <= 0)
            tr.gate = 1;
        else if (!tr.gate && in[i].pressure_g >= PF.gate_g)
            tr.gate = 1;
        else if (tr.gate && in[i].pressure_g < 0.6f * PF.gate_g)
            tr.gate = 0;

        const ManualParams *M = &P.man[tr.manual];
        tr.raw = (float)M->low_note + x_to_semis(M, tr.x);
        track_pitch(&tr, M);
        nt[nn++] = tr;
    }
    memcpy(tracks, nt, sizeof(Track) * nn);
    ntracks = nn;

    for (int i = 0; i < MAX_VOICES; i++) voices[i].claimed = 0;

    for (int m = 0; m < NUM_MANUALS; m++) {
        if (P.man[m].mono) {
            /* как на проволоке: звучит последний прижатый палец */
            Track *best = NULL;
            for (int i = 0; i < ntracks; i++)
                if (tracks[i].manual == m && tracks[i].gate &&
                    (!best || tracks[i].order > best->order))
                    best = &tracks[i];
            if (best) claim_voice(OWNER_MONO + m, m, best->pitch, best->pn, best->id);
        } else {
            for (int i = 0; i < ntracks; i++)
                if (tracks[i].manual == m && tracks[i].gate)
                    claim_voice(tracks[i].id, m, tracks[i].pitch, tracks[i].pn, tracks[i].id);
        }
    }

    for (int i = 0; i < NUM_NOTE_KEYS; i++)
        if (key_down[i]) {
            int m = key_manual[i];
            claim_voice(OWNER_KEY + i, m, (float)(P.man[m].low_note + key_semis[i]), 0.7f, OWNER_KEY + i);
        }

    for (int i = 0; i < MAX_VOICES; i++)
        if (voices[i].active && voices[i].gate && !voices[i].claimed)
            voices[i].gate = 0;
}

/* ===================================================================== */
/*         запись: кольцевой буфер «аудиопоток → главный поток»           */
/* ===================================================================== */
#define REC_RING (1 << 18)                    /* 2,7 с стерео */
static float        rec_ring[REC_RING];
static atomic_uint  rec_w, rec_r;
static atomic_int   rec_on;
static atomic_uint  rec_drop;

static void rec_push(const float *out, int frames)
{
    if (!atomic_load_explicit(&rec_on, memory_order_relaxed)) return;
    unsigned w = atomic_load_explicit(&rec_w, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&rec_r, memory_order_acquire);
    unsigned need = (unsigned)frames * 2;
    if (REC_RING - (w - r) < need) { atomic_fetch_add(&rec_drop, (unsigned)frames); return; }
    for (unsigned i = 0; i < need; i++) rec_ring[(w + i) & (REC_RING - 1)] = out[i];
    atomic_store_explicit(&rec_w, w + need, memory_order_release);
}

void synth_rec_enable(int on)
{
    if (on) {
        atomic_store(&rec_r, atomic_load(&rec_w));
        atomic_store(&rec_drop, 0);
    }
    atomic_store(&rec_on, on);
}

int synth_rec_read(float *dst, int max_floats)
{
    unsigned r = atomic_load_explicit(&rec_r, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&rec_w, memory_order_acquire);
    unsigned n = w - r;
    if (n > (unsigned)max_floats) n = (unsigned)max_floats & ~1u;
    for (unsigned i = 0; i < n; i++) dst[i] = rec_ring[(r + i) & (REC_RING - 1)];
    atomic_store_explicit(&rec_r, r + n, memory_order_release);
    return (int)n;
}

unsigned synth_rec_dropped(void) { return atomic_load(&rec_drop); }

static void render_block(float *out, int n)
{
    float bus[NUM_MANUALS][SYNTH_BLOCK];
    memset(bus, 0, sizeof bus);

    for (int i = 0; i < MAX_VOICES; i++)
        if (voices[i].active)
            render_voice(&voices[i], &P.man[voices[i].manual], bus[voices[i].manual], n);

    for (int m = 0; m < NUM_MANUALS; m++) {
        const ManualParams *M = &P.man[m];
        FormantBank *B = &fbank[m];
        float *b = bus[m];
        formants_update(B, M);
        if (M->vowel >= 0) {
            for (int i = 0; i < n; i++) {
                float y = 0.0f, lp, bp;
                for (int f = 0; f < NUM_FORMANTS; f++) {
                    svf_tick(&B->st[f], &B->c[f], b[i], &lp, &bp);
                    y += B->gain[f] * B->c[f].k * bp;     /* k·bp — пик = 1 */
                }
                b[i] = y;
            }
        }
        for (int i = 0; i < n; i++) b[i] *= M->volume;
    }

    /* мануалы чуть разведены по панораме: I левее, II правее */
    const float pan[NUM_MANUALS][2] = {{1.10f, 0.90f}, {0.90f, 1.10f}};

    float wet  = 3.0f * P.reverb;
    float emix = 1.1f * P.echo_mix;
    float efb  = clampf(P.echo_fb, 0.0f, 0.9f);
    float etgt = clampi(P.echo_ms, 40, 1300) * SR / 1000.0f;
    const float elp = 0.35f;                 /* тон повторов */

    for (int i = 0; i < n; i++) {
        float dl = bus[0][i] * pan[0][0] + bus[1][i] * pan[1][0];
        float dr = bus[0][i] * pan[0][1] + bus[1][i] * pan[1][1];
        float dry = 0.5f * (dl + dr);

        /* эхо */
        echo_cur += (etgt - echo_cur) * 0.0002f;
        float el = echo_read(echoL, echo_cur), er = echo_read(echoR, echo_cur);
        echo_lpL += (er - echo_lpL) * elp;
        echo_lpR += (el - echo_lpR) * elp;
        echoL[echo_pos] = dry * 0.7f + efb * echo_lpL;
        echoR[echo_pos] = dry * 0.3f + efb * echo_lpR;
        echo_pos = (echo_pos + 1) & (ECHO_LEN - 1);
        dl += el * emix;
        dr += er * emix;

        /* реверберация */
        float rin = (dl + dr) * 0.0075f;
        float wl = 0.0f, wr = 0.0f;
        for (int c = 0; c < NCOMB; c++) {
            wl += comb_tick(&combL[c], rin);
            wr += comb_tick(&combR[c], rin);
        }
        for (int a = 0; a < NAP; a++) {
            wl = ap_tick(&apL[a], wl);
            wr = ap_tick(&apR[a], wr);
        }
        out[2 * i]     = tanhf((dl + wl * wet) * PF.master);   /* мягкое ограничение */
        out[2 * i + 1] = tanhf((dr + wr * wet) * PF.master);

        wave[wave_pos] = dry;
        wave_pos = (wave_pos + 1) % WAVE_BUF;
    }
    rec_push(out, n);
}

void synth_render(float *out, int frames)
{
    int pos = 0;
    while (pos < frames) {
        int n = frames - pos;
        if (n > SYNTH_BLOCK) n = SYNTH_BLOCK;
        control_update();
        render_block(out + 2 * pos, n);
        pos += n;
    }
}

void synth_audio_callback(void *userdata, uint8_t *stream, int len)
{
    (void)userdata;
    synth_render((float *)stream, len / (int)(2 * sizeof(float)));
}

/* ===================================================================== */
/*                      интерфейс для главного потока                     */
/* ===================================================================== */
void synth_init(int trackpad_flags)
{
    memset(voices, 0, sizeof voices);
    memset(fbank, 0, sizeof fbank);
    for (int m = 0; m < NUM_MANUALS; m++) fbank[m].vowel = fbank[m].reg = -99;
    ntracks = 0;
    fb_count = 0;
    memset(key_down, 0, sizeof key_down);
    mt_flags = trackpad_flags;
    reverb_init();
    echo_init();
    neon_init();
}

void synth_set_input_active(int on) { input_active = on; }

void synth_fb_upsert(int id, float x, float y, float pn)
{
    for (int i = 0; i < fb_count; i++)
        if (fb[i].id == id) { fb[i].x = x; fb[i].y = y; fb[i].pn = pn; return; }
    if (fb_count >= MAX_IN) return;
    fb[fb_count].id = id;
    fb[fb_count].x = x;
    fb[fb_count].y = y;
    fb[fb_count].pn = pn;
    fb_count++;
}

void synth_fb_remove(int id)
{
    for (int i = 0; i < fb_count; i++)
        if (fb[i].id == id) { fb[i] = fb[--fb_count]; return; }
}

void synth_fb_clear(void) { fb_count = 0; }

void synth_key(int idx, int down, int manual, int semis)
{
    if (idx < 0 || idx >= NUM_NOTE_KEYS) return;
    key_down[idx] = down;
    if (down) { key_manual[idx] = manual; key_semis[idx] = semis; }
}

void synth_keys_clear(void) { memset(key_down, 0, sizeof key_down); }

/* выбранный мануал нужен и для раскладки «1 мануал» */
void synth_set_selected_manual(int m) { single_manual = clampi(m, 0, NUM_MANUALS - 1); }

int synth_snapshot(SynthTouch *out, int max)
{
    int n = 0;
    for (int i = 0; i < ntracks && n < max; i++) {
        const Track *t = &tracks[i];
        SynthTouch *s = &out[n++];
        s->id = t->id;
        s->manual = t->manual;
        s->gate = t->gate;
        s->from_trackpad = t->from_trackpad;
        s->x = t->x; s->y = t->y;
        s->pn = t->pn;
        s->pressure_g = t->pressure_g;
        s->area = t->area;
        s->raw_note = t->raw;
        s->pitch_note = t->pitch;
    }
    return n;
}

void synth_wave(float *out, int n)
{
    if (n > WAVE_BUF) n = WAVE_BUF;
    int start = (wave_pos - n + WAVE_BUF) % WAVE_BUF;
    for (int i = 0; i < n; i++) out[i] = wave[(start + i) % WAVE_BUF];
}
