/*
 * presets.c — заводские и свои пресеты, автосохранение настроек
 *
 * Формат — простой текст, его удобно читать и править руками:
 *
 *   [patch]
 *   layout = 2
 *   scale = minor
 *   [manual1]
 *   low_note = A3
 *   div1 = 2
 *   ...
 *
 * Неизвестные ключи пропускаются, отсутствующие берутся по умолчанию,
 * поэтому старые файлы читаются и новыми версиями программы.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "presets.h"
#include "platform.h"

/* ===================================================================== */
/*                         заводские пресеты                              */
/*      (только отличия от default_patch — «Микстуры»)                    */
/* ===================================================================== */
typedef struct { const char *name, *text; } Factory;

static const Factory factory[] = {
    {"Микстура", ""},

    {"Хор",
     "[patch]\nreverb = 0.55\n"
     "[manual1]\nmode = poly\nsnap = soft\nglide_ms = 30\nmain = 0.7\nattack_ms = 60\ndrift = 0.35\n"
     "div1 = 2\nlvl1 = 0.5\ndiv2 = 3\nlvl2 = 0.35\ndiv3 = 4\nlvl3 = 0.25\ndiv4 = 6\nlvl4 = 0.15\n"
     "vowel = A\nvoice = soprano\ncutoff = 2600\nres = 0.1\npress_bright = 0.5\nrelease_ms = 450\nvolume = 0.5\n"
     "[manual2]\nmode = poly\nsnap = soft\nglide_ms = 30\nmain = 0.7\nattack_ms = 60\ndrift = 0.35\n"
     "div1 = 2\nlvl1 = 0.5\ndiv2 = 3\nlvl2 = 0.35\ndiv3 = 4\nlvl3 = 0.25\ndiv4 = 6\nlvl4 = 0.15\n"
     "vowel = O\nvoice = bass\ncutoff = 2200\nres = 0.1\npress_bright = 0.5\nrelease_ms = 450\nvolume = 0.55\n"},

    {"Орган",
     "[patch]\nreverb = 0.45\n"
     "[manual1]\nmode = poly\nsnap = hard\nglide_ms = 0\nmain = 0.7\n"
     "div1 = 2\nlvl1 = 0.6\ndiv2 = 4\nlvl2 = 0.45\ndiv3 = 8\nlvl3 = 0.3\ndiv4 = 16\nlvl4 = 0.2\n"
     "vowel = off\ncutoff = 3500\nres = 0.05\npress_bright = 0.2\nrelease_ms = 80\nvolume = 0.45\n"
     "[manual2]\nmode = poly\nsnap = hard\nglide_ms = 0\nmain = 0.7\n"
     "div1 = 2\nlvl1 = 0.6\ndiv2 = 3\nlvl2 = 0.4\ndiv3 = 4\nlvl3 = 0.3\ndiv4 = 8\nlvl4 = 0.25\n"
     "vowel = off\ncutoff = 2500\nres = 0.05\npress_bright = 0.2\nrelease_ms = 80\nvolume = 0.45\n"},

    {"Бас",
     "[patch]\nreverb = 0.2\n"
     "[manual2]\nlow_note = E1\nspan = 2\nmode = mono\nglide_ms = 40\nmain = 0.8\n"
     "div1 = 2\nlvl1 = 0.7\ndiv2 = 3\nlvl2 = 0.3\ndiv3 = 4\nlvl3 = 0.3\ndiv4 = 8\nlvl4 = 0.2\n"
     "vowel = off\ncutoff = 600\nres = 0.35\npress_bright = 0.8\nrelease_ms = 90\nvolume = 0.7\n"},

    {"Птицы",
     "[patch]\nreverb = 0.5\necho = 0.25\necho_ms = 260\necho_fb = 0.35\n"
     "[manual1]\nlow_note = E5\nspan = 3\nmode = mono\nglide_ms = 0\nmain = 1\n"
     "div1 = 3\nlvl1 = 0.15\ndiv2 = 5\nlvl2 = 0.1\ndiv3 = 7\nlvl3 = 0\ndiv4 = 9\nlvl4 = 0\n"
     "vowel = off\ncutoff = 3000\nres = 0.85\npress_bright = 1\nrelease_ms = 40\nvolume = 0.45\n"
     "[manual2]\nlow_note = E4\nspan = 3\nmode = mono\nglide_ms = 0\nmain = 1\n"
     "div1 = 2\nlvl1 = 0.3\ndiv2 = 3\nlvl2 = 0.2\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = off\ncutoff = 2200\nres = 0.8\npress_bright = 1\nrelease_ms = 60\nvolume = 0.45\n"},

    {"Голос",
     "[patch]\nreverb = 0.35\n"
     "[manual1]\nlow_note = C4\nmode = mono\nsnap = soft\nglide_ms = 60\nmain = 0.9\n"
     "div1 = 2\nlvl1 = 0.25\ndiv2 = 3\nlvl2 = 0\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = A\nvoice = soprano\ncutoff = 2200\nres = 0.1\npress_bright = 0.7\nrelease_ms = 220\n"
     "[manual2]\nlow_note = G2\nmode = mono\nsnap = soft\nglide_ms = 60\nmain = 0.9\n"
     "div1 = 2\nlvl1 = 0.3\ndiv2 = 3\nlvl2 = 0.2\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = O\nvoice = bass\ncutoff = 1800\nres = 0.1\npress_bright = 0.7\nrelease_ms = 220\n"},

    {"Виолончель",
     "[patch]\nreverb = 0.3\n"
     "[manual1]\nlow_note = C3\nspan = 3\nmode = mono\nsnap = soft\nglide_ms = 25\nmain = 0.8\n"
     "div1 = 2\nlvl1 = 0.35\ndiv2 = 3\nlvl2 = 0.25\ndiv3 = 4\nlvl3 = 0.15\ndiv4 = 5\nlvl4 = 0.1\n"
     "vowel = A\nvoice = tenor\ncutoff = 1600\nres = 0.2\npress_bright = 0.9\nrelease_ms = 180\nvolume = 0.6\n"
     "[manual2]\nlow_note = C2\nspan = 3\nmode = mono\nsnap = soft\nglide_ms = 25\nmain = 0.8\n"
     "div1 = 2\nlvl1 = 0.35\ndiv2 = 3\nlvl2 = 0.25\ndiv3 = 4\nlvl3 = 0.15\ndiv4 = 5\nlvl4 = 0.1\n"
     "vowel = O\nvoice = bass\ncutoff = 1400\nres = 0.2\npress_bright = 0.9\nrelease_ms = 180\nvolume = 0.65\n"},

    {"Унтертоны",
     "[patch]\nreverb = 0.45\necho = 0.2\necho_ms = 480\necho_fb = 0.4\n"
     "[manual1]\nmode = poly\nmain = 0.6\nwave = neon\ndrift = 0.4\n"
     "div1 = 3\nlvl1 = 0.5\ndiv2 = 5\nlvl2 = 0.4\ndiv3 = 7\nlvl3 = 0.3\ndiv4 = 9\nlvl4 = 0.25\n"
     "vowel = off\ncutoff = 2000\nres = 0.3\nrelease_ms = 600\n"
     "[manual2]\nmode = poly\nmain = 0.6\nwave = neon\ndrift = 0.4\n"
     "div1 = 3\nlvl1 = 0.5\ndiv2 = 5\nlvl2 = 0.4\ndiv3 = 7\nlvl3 = 0.3\ndiv4 = 9\nlvl4 = 0.25\n"
     "vowel = off\ncutoff = 1600\nres = 0.3\nrelease_ms = 600\n"},

    {"Пентатоника",
     "[patch]\nscale = pent_minor\ntonic = A\nreverb = 0.35\n"
     "[manual1]\nsnap = soft\n"
     "[manual2]\nsnap = soft\nvowel = O\n"},

    {"Чистая пила",
     "[patch]\nreverb = 0.1\n"
     "[manual1]\nmain = 1\nlvl1 = 0\nlvl2 = 0\nlvl3 = 0\nlvl4 = 0\nvowel = off\ndrift = 0\n"
     "cutoff = 8000\nres = 0\npress_bright = 0.3\n"
     "[manual2]\nmain = 1\nlvl1 = 0\nlvl2 = 0\nlvl3 = 0\nlvl4 = 0\nvowel = off\ndrift = 0\n"
     "cutoff = 8000\nres = 0\npress_bright = 0.3\n"},

    {"Колокол",
     "[patch]\nreverb = 0.45\necho = 0.25\necho_ms = 420\necho_fb = 0.3\n"
     "[manual1]\nwave = neon\nmain = 0.6\n"
     "div1 = 3\nlvl1 = 0.5\ndiv2 = 5\nlvl2 = 0.4\ndiv3 = 7\nlvl3 = 0.3\ndiv4 = 9\nlvl4 = 0.2\n"
     "vowel = off\ncutoff = 3500\nres = 0.3\npress_bright = 0.5\n"
     "attack_ms = 2\ndecay_ms = 1200\nsustain = 0\nrelease_ms = 1500\n"
     "[manual2]\nwave = neon\nmain = 0.6\n"
     "div1 = 2\nlvl1 = 0.5\ndiv2 = 3\nlvl2 = 0.4\ndiv3 = 5\nlvl3 = 0.3\ndiv4 = 7\nlvl4 = 0.2\n"
     "vowel = off\ncutoff = 2500\nres = 0.3\npress_bright = 0.5\n"
     "attack_ms = 2\ndecay_ms = 1500\nsustain = 0\nrelease_ms = 1800\n"},

    {"Щипок",
     "[patch]\nreverb = 0.3\necho = 0.2\necho_ms = 300\necho_fb = 0.3\n"
     "[manual1]\nmain = 0.9\n"
     "div1 = 2\nlvl1 = 0.4\ndiv2 = 3\nlvl2 = 0.2\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = off\ncutoff = 900\nres = 0.35\npress_bright = 1\n"
     "attack_ms = 2\ndecay_ms = 280\nsustain = 0\nrelease_ms = 300\n"
     "[manual2]\nmain = 0.9\n"
     "div1 = 2\nlvl1 = 0.5\ndiv2 = 4\nlvl2 = 0.3\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = off\ncutoff = 700\nres = 0.35\npress_bright = 1\n"
     "attack_ms = 2\ndecay_ms = 350\nsustain = 0\nrelease_ms = 350\n"},

    {"Ветер",
     "[patch]\nreverb = 0.55\necho = 0.3\necho_ms = 520\necho_fb = 0.45\n"
     "[manual1]\nsnap = soft\nmain = 0.5\nnoise = 0.55\ndrift = 0.6\n"
     "div1 = 2\nlvl1 = 0.2\ndiv2 = 3\nlvl2 = 0\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = U\nvoice = soprano\ncutoff = 2400\nres = 0.2\npress_bright = 0.8\n"
     "attack_ms = 250\nrelease_ms = 900\n"
     "[manual2]\nsnap = soft\nmain = 0.5\nnoise = 0.45\ndrift = 0.6\n"
     "div1 = 2\nlvl1 = 0.3\ndiv2 = 3\nlvl2 = 0.1\ndiv3 = 4\nlvl3 = 0\ndiv4 = 5\nlvl4 = 0\n"
     "vowel = O\nvoice = bass\ncutoff = 1600\nres = 0.2\npress_bright = 0.8\n"
     "attack_ms = 250\nrelease_ms = 900\n"},

    {"Космос",
     "[patch]\nreverb = 0.5\necho = 0.45\necho_ms = 600\necho_fb = 0.55\n"
     "[manual1]\nwave = neon\nsnap = soft\nglide_ms = 80\ndrift = 0.5\n"
     "vowel = O\nvoice = tenor\nattack_ms = 120\nrelease_ms = 800\n"
     "[manual2]\nwave = neon\nsnap = soft\nglide_ms = 80\ndrift = 0.5\n"
     "vowel = A\nvoice = bass\nattack_ms = 120\nrelease_ms = 800\n"},
};
#define NUM_FACTORY ((int)(sizeof factory / sizeof factory[0]))

/* ===================================================================== */
/*                          разбор значений                               */
/* ===================================================================== */
static const char *note_names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* «A3», «C#4», «Bb2» или число MIDI */
static int parse_note(const char *v, int dflt)
{
    if (isdigit((unsigned char)v[0]) || v[0] == '-') return atoi(v);
    int n = -1;
    switch (toupper((unsigned char)v[0])) {
        case 'C': n = 0; break;  case 'D': n = 2; break;  case 'E': n = 4; break;
        case 'F': n = 5; break;  case 'G': n = 7; break;  case 'A': n = 9; break;
        case 'B': n = 11; break; default: return dflt;
    }
    const char *p = v + 1;
    if (*p == '#') { n++; p++; }
    else if (*p == 'b') { n--; p++; }
    if (!*p) return (n + 12) % 12;            /* без октавы — класс высоты (тоника) */
    int oct = atoi(p);
    return clampi((oct + 1) * 12 + n, 0, 127);
}

static int pick(const char *v, const char *const *opts, int n, int dflt)
{
    for (int i = 0; i < n; i++)
        if (strcasecmp(v, opts[i]) == 0) return i;
    return dflt;
}

static const char *const vowel_ids[] = {"A", "E", "I", "O", "U"};
static const char *const voice_ids[] = {"bass", "tenor", "soprano"};
static const char *const snap_ids[]  = {"off", "soft", "hard"};
static const char *const psrc_ids[]  = {"force", "area", "none"};
static const char *const curve_ids[] = {"soft", "linear", "hard"};
static const char *const wave_ids[]  = {"saw", "neon"};

static void apply_manual(ManualParams *M, const char *k, const char *v)
{
    float f = (float)atof(v);
    int   i = atoi(v);
    if      (!strcmp(k, "low_note"))     M->low_note = clampi(parse_note(v, M->low_note), 12, 108);
    else if (!strcmp(k, "span"))         M->span = clampi(i, 1, 5);
    else if (!strcmp(k, "mode"))         M->mono = strcasecmp(v, "poly") != 0;
    else if (!strcmp(k, "glide_ms"))     M->glide_ms = clampi(i, 0, 2000);
    else if (!strcmp(k, "snap"))         M->snap = pick(v, snap_ids, 3, M->snap);
    else if (!strcmp(k, "main"))         M->main_lvl = clampf(f, 0, 1);
    else if (!strcmp(k, "vowel"))        M->vowel = pick(v, vowel_ids, 5, -1);
    else if (!strcmp(k, "voice"))        M->reg = pick(v, voice_ids, 3, M->reg);
    else if (!strcmp(k, "cutoff"))       M->cutoff = clampf(f, 100, 16000);
    else if (!strcmp(k, "res"))          M->res = clampf(f, 0, 0.95f);
    else if (!strcmp(k, "press_bright")) M->press_bright = clampf(f, 0, 1);
    else if (!strcmp(k, "release_ms"))   M->release_ms = clampi(i, 5, 5000);
    else if (!strcmp(k, "volume"))       M->volume = clampf(f, 0, 1);
    else if (!strcmp(k, "wave"))         M->wave = pick(v, wave_ids, 2, M->wave);
    else if (!strcmp(k, "noise"))        M->noise = clampf(f, 0, 1);
    else if (!strcmp(k, "drift"))        M->drift = clampf(f, 0, 1);
    else if (!strcmp(k, "attack_ms"))    M->attack_ms = clampi(i, 1, 5000);
    else if (!strcmp(k, "decay_ms"))     M->decay_ms = clampi(i, 5, 10000);
    else if (!strcmp(k, "sustain"))      M->sustain = clampf(f, 0, 1);
    else if (!strncmp(k, "div", 3) && k[3] >= '1' && k[3] <= '4' && !k[4])
        M->div[k[3] - '1'] = clampi(i, DIV_MIN, DIV_MAX);
    else if (!strncmp(k, "lvl", 3) && k[3] >= '1' && k[3] <= '4' && !k[4])
        M->sub_lvl[k[3] - '1'] = clampf(f, 0, 1);
}

static void apply_patch(Patch *p, const char *k, const char *v)
{
    if (!strcmp(k, "layout")) p->split = atoi(v) != 1;
    else if (!strcmp(k, "reverb")) p->reverb = clampf((float)atof(v), 0, 1);
    else if (!strcmp(k, "echo"))    p->echo_mix = clampf((float)atof(v), 0, 1);
    else if (!strcmp(k, "echo_ms")) p->echo_ms = clampi(atoi(v), 40, 1300);
    else if (!strcmp(k, "echo_fb")) p->echo_fb = clampf((float)atof(v), 0, 0.9f);
    else if (!strcmp(k, "tonic")) p->tonic = clampi(parse_note(v, p->tonic) % 12, 0, 11);
    else if (!strcmp(k, "scale")) {
        for (int i = 0; i < num_scales; i++)
            if (!strcasecmp(v, scales[i].id)) p->scale = i;
    }
}

static void apply_prefs(Prefs *pf, const char *k, const char *v)
{
    float f = (float)atof(v);
    int   i = atoi(v);
    if      (!strcmp(k, "press_source"))   pf->press_src = pick(v, psrc_ids, 3, pf->press_src);
    else if (!strcmp(k, "curve"))          pf->curve = pick(v, curve_ids, 3, pf->curve);
    else if (!strcmp(k, "force_floor"))    pf->force_floor_g = clampf(f, 0, 2000);
    else if (!strcmp(k, "force_max"))      pf->force_max_g = clampf(f, 5, 3000);
    else if (!strcmp(k, "area_floor"))     pf->area_floor = clampf(f, 0, 100);
    else if (!strcmp(k, "area_max"))       pf->area_max = clampf(f, 0, 100);
    else if (!strcmp(k, "calibrated"))     pf->calibrated = i;
    else if (!strcmp(k, "gate"))           pf->gate_g = clampi(i, 0, 500);
    else if (!strcmp(k, "min_level"))      pf->min_level = clampf(f, 0, 0.8f);
    else if (!strcmp(k, "snap_ms"))        pf->snap_ms = clampi(i, 10, 2000);
    else if (!strcmp(k, "haptics"))        pf->haptics = i != 0;
    else if (!strcmp(k, "block_gestures")) pf->block_gestures = i != 0;
    else if (!strcmp(k, "master"))         pf->master = clampf(f, 0, 1);
    else if (!strcmp(k, "audio_out"))      snprintf(pf->audio_out, sizeof pf->audio_out, "%s", v);
}

typedef struct {
    Patch *patch;
    Prefs *prefs;
    char  *preset; int preset_n;
    int   *modified;
} Target;

static void parse_text(const char *text, Target *t)
{
    char section[32] = "";
    const char *p = text;
    while (*p) {
        char line[512];
        int n = 0;
        while (*p && *p != '\n' && n < (int)sizeof line - 1) line[n++] = *p++;
        line[n] = 0;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) { *e = 0; snprintf(section, sizeof section, "%s", s + 1); }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);

        if (!strcmp(section, "patch") && t->patch) apply_patch(t->patch, k, v);
        else if (!strcmp(section, "manual1") && t->patch) apply_manual(&t->patch->man[0], k, v);
        else if (!strcmp(section, "manual2") && t->patch) apply_manual(&t->patch->man[1], k, v);
        else if (!strcmp(section, "prefs")) {
            if (!strcmp(k, "preset") && t->preset) snprintf(t->preset, t->preset_n, "%s", v);
            else if (!strcmp(k, "modified") && t->modified) *t->modified = atoi(v) != 0;
            else if (t->prefs) apply_prefs(t->prefs, k, v);
        }
    }
}

void patch_from_text(Patch *p, const char *text)
{
    default_patch(p);
    Target t = {p, NULL, NULL, 0, NULL};
    parse_text(text, &t);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 1 << 20) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* ===================================================================== */
/*                               запись                                   */
/* ===================================================================== */
static void fmt_note(char *b, int n, int midi)
{
    snprintf(b, n, "%s%d", note_names[((midi % 12) + 12) % 12], midi / 12 - 1);
}

static void write_patch(FILE *f, const Patch *p)
{
    fprintf(f, "[patch]\n");
    fprintf(f, "layout = %d\n", p->split ? 2 : 1);
    fprintf(f, "scale = %s\n", scales[clampi(p->scale, 0, num_scales - 1)].id);
    fprintf(f, "tonic = %s\n", note_names[clampi(p->tonic, 0, 11)]);
    fprintf(f, "reverb = %.3g\n", p->reverb);
    fprintf(f, "echo = %.3g\n", p->echo_mix);
    fprintf(f, "echo_ms = %d\n", p->echo_ms);
    fprintf(f, "echo_fb = %.3g\n", p->echo_fb);
    for (int m = 0; m < NUM_MANUALS; m++) {
        const ManualParams *M = &p->man[m];
        char nb[16];
        fmt_note(nb, sizeof nb, M->low_note);
        fprintf(f, "\n[manual%d]\n", m + 1);
        fprintf(f, "low_note = %s\n", nb);
        fprintf(f, "span = %d\n", M->span);
        fprintf(f, "mode = %s\n", M->mono ? "mono" : "poly");
        fprintf(f, "glide_ms = %d\n", M->glide_ms);
        fprintf(f, "snap = %s\n", snap_ids[clampi(M->snap, 0, 2)]);
        fprintf(f, "wave = %s\n", wave_ids[clampi(M->wave, 0, 1)]);
        fprintf(f, "main = %.3g\n", M->main_lvl);
        for (int k = 0; k < NUM_SUBS; k++)
            fprintf(f, "div%d = %d\nlvl%d = %.3g\n", k + 1, M->div[k], k + 1, M->sub_lvl[k]);
        fprintf(f, "vowel = %s\n", M->vowel < 0 ? "off" : vowel_ids[clampi(M->vowel, 0, 4)]);
        fprintf(f, "voice = %s\n", voice_ids[clampi(M->reg, 0, 2)]);
        fprintf(f, "cutoff = %.0f\n", M->cutoff);
        fprintf(f, "res = %.3g\n", M->res);
        fprintf(f, "press_bright = %.3g\n", M->press_bright);
        fprintf(f, "noise = %.3g\n", M->noise);
        fprintf(f, "drift = %.3g\n", M->drift);
        fprintf(f, "attack_ms = %d\n", M->attack_ms);
        fprintf(f, "decay_ms = %d\n", M->decay_ms);
        fprintf(f, "sustain = %.3g\n", M->sustain);
        fprintf(f, "release_ms = %d\n", M->release_ms);
        fprintf(f, "volume = %.3g\n", M->volume);
    }
}

static int write_atomic(const char *path, void (*body)(FILE *, const void *), const void *arg)
{
    char tmp[1400];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    body(f, arg);
    int ok = fflush(f) == 0;
    ok &= fclose(f) == 0;
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

/* ===================================================================== */
/*                           список пресетов                              */
/* ===================================================================== */
static char dir_presets[1024];
static char file_settings[1024];

typedef struct { char name[PRESET_NAME_MAX]; } UserPreset;
static UserPreset *user;
static int         nuser, cap_user;

static void mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static const char *EXT = ".trautonium";

static int cmp_user(const void *a, const void *b)
{
    return strcmp(((const UserPreset *)a)->name, ((const UserPreset *)b)->name);
}

void presets_rescan(void)
{
    nuser = 0;
    DIR *d = opendir(dir_presets);
    if (!d) return;
    struct dirent *e;
    size_t el = strlen(EXT);
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n <= el || strcmp(e->d_name + n - el, EXT)) continue;
        if (n - el >= PRESET_NAME_MAX) continue;
        if (nuser == cap_user) {
            cap_user = cap_user ? cap_user * 2 : 32;
            user = realloc(user, sizeof(UserPreset) * cap_user);
        }
        memcpy(user[nuser].name, e->d_name, n - el);
        user[nuser].name[n - el] = 0;
        nuser++;
    }
    closedir(d);
    qsort(user, nuser, sizeof(UserPreset), cmp_user);
}

void presets_init(void)
{
    const char *base = plat_support_dir();
    snprintf(dir_presets, sizeof dir_presets, "%s/Presets", base);
    snprintf(file_settings, sizeof file_settings, "%s/settings.ini", base);
    mkdir_p(dir_presets);
    presets_rescan();
}

const char *presets_dir(void) { return dir_presets; }

int presets_count(void) { return NUM_FACTORY + nuser; }

const char *preset_name(int i)
{
    if (i < 0 || i >= presets_count()) return "";
    return i < NUM_FACTORY ? factory[i].name : user[i - NUM_FACTORY].name;
}

int preset_is_factory(int i) { return i >= 0 && i < NUM_FACTORY; }

int preset_find(const char *name)
{
    for (int i = 0; i < presets_count(); i++)
        if (!strcmp(preset_name(i), name)) return i;
    return -1;
}

static void user_path(char *out, int n, const char *name)
{
    snprintf(out, n, "%s/%s%s", dir_presets, name, EXT);
}

int preset_load(int i, Patch *out)
{
    if (i < 0 || i >= presets_count()) return 0;
    if (i < NUM_FACTORY) { patch_from_text(out, factory[i].text); return 1; }
    char path[1200];
    user_path(path, sizeof path, user[i - NUM_FACTORY].name);
    char *text = read_file(path);
    if (!text) return 0;
    patch_from_text(out, text);
    free(text);
    return 1;
}

static void body_preset(FILE *f, const void *arg)
{
    fprintf(f, "# Пресет траутониума\n\n");
    write_patch(f, (const Patch *)arg);
}

/* имя файла: убираем то, что macOS не любит в именах */
static void sanitize(char *dst, int n, const char *src)
{
    int j = 0;
    while (*src == ' ' || *src == '.') src++;
    for (; *src && j < n - 1; src++) {
        char c = *src;
        if (c == '/' || c == ':' || c == '\\' || (unsigned char)c < 32) c = '-';
        dst[j++] = c;
    }
    /* не обрываем многобайтовый символ UTF-8 посередине */
    if (((unsigned char)*src & 0xC0) == 0x80) {
        while (j > 0 && ((unsigned char)dst[j - 1] & 0xC0) == 0x80) j--;
        if (j > 0) j--;
    }
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = 0;
}

int preset_save(const char *name, const Patch *p)
{
    char clean[PRESET_NAME_MAX];
    sanitize(clean, sizeof clean, name);
    if (!clean[0]) return -1;
    int fi = preset_find(clean);
    if (preset_is_factory(fi)) return -1;      /* заводские не перезаписываем */

    char path[1200];
    user_path(path, sizeof path, clean);
    if (!write_atomic(path, body_preset, p)) return -1;
    presets_rescan();
    return preset_find(clean);
}

int preset_delete(int i)
{
    if (i < NUM_FACTORY || i >= presets_count()) return 0;
    char path[1200];
    user_path(path, sizeof path, user[i - NUM_FACTORY].name);
    int ok = remove(path) == 0;
    presets_rescan();
    return ok;
}

/* ===================================================================== */
/*                          настройки целиком                             */
/* ===================================================================== */
typedef struct {
    const Prefs *pf; const Patch *p; const char *preset; int modified;
} SettingsArg;

static void body_settings(FILE *f, const void *arg)
{
    const SettingsArg *a = arg;
    const Prefs *pf = a->pf;
    fprintf(f, "# Настройки траутониума (сохраняются автоматически)\n\n[prefs]\n");
    fprintf(f, "preset = %s\n", a->preset);
    fprintf(f, "modified = %d\n", a->modified);
    fprintf(f, "press_source = %s\n", psrc_ids[clampi(pf->press_src, 0, 2)]);
    fprintf(f, "curve = %s\n", curve_ids[clampi(pf->curve, 0, 2)]);
    fprintf(f, "force_floor = %.1f\n", pf->force_floor_g);
    fprintf(f, "force_max = %.1f\n", pf->force_max_g);
    fprintf(f, "area_floor = %.4g\n", pf->area_floor);
    fprintf(f, "area_max = %.4g\n", pf->area_max);
    fprintf(f, "calibrated = %d\n", pf->calibrated);
    fprintf(f, "gate = %d\n", pf->gate_g);
    fprintf(f, "min_level = %.3g\n", pf->min_level);
    fprintf(f, "snap_ms = %d\n", pf->snap_ms);
    fprintf(f, "haptics = %d\n", pf->haptics);
    fprintf(f, "block_gestures = %d\n", pf->block_gestures);
    fprintf(f, "master = %.3g\n", pf->master);
    fprintf(f, "audio_out = %s\n\n", pf->audio_out);
    write_patch(f, a->p);
}

int settings_save(const Prefs *pf, const Patch *p, const char *preset, int modified)
{
    SettingsArg a = {pf, p, preset, modified};
    return write_atomic(file_settings, body_settings, &a);
}

int settings_load(Prefs *pf, Patch *p, char *preset, int preset_n, int *modified)
{
    default_prefs(pf);
    default_patch(p);
    if (preset_n > 0) snprintf(preset, preset_n, "%s", factory[0].name);
    if (modified) *modified = 0;

    char *text = read_file(file_settings);
    if (!text) return 0;
    Target t = {p, pf, preset, preset_n, modified};
    parse_text(text, &t);
    free(text);
    return 1;
}
