/*
 * app.c — траутониум для тачпада MacBook: окно, управление,
 * пресеты, калибровка нажима, справка, режим сцены.
 *
 * Развитие trautonium_line_control.c (tanukiichii/tutorials).
 *
 *   • тачпад — это два мануала (верхняя и нижняя половина);
 *   • положение пальца по горизонтали — непрерывная высота тона;
 *   • сила нажатия — громкость и яркость;
 *   • «Моно»: второй палец перехватывает ноту, как на проволоке;
 *     «Поли»: каждый палец — свой голос;
 *   • на нотах тачпад слегка щёлкает (как «язычки» траутониума);
 *   • мягкое притяжение к нотам лада: скользишь свободно, а остановился —
 *     высота сама «доезжает» до чистой ноты.
 *
 * Модули: synth.c (звук), presets.c (пресеты и настройки),
 * ui.c (рисование), mt_input.m (тачпад), platform_mac.m (macOS).
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trautonium.h"
#include "synth.h"
#include "presets.h"
#include "platform.h"
#include "ui.h"
#include "mt_input.h"
#include "recorder.h"

/* ===================================================================== */
/*                               цвета                                   */
/* ===================================================================== */
#define C_BG      rgba( 22,  21,  26, 255)
#define C_SURF    rgba( 34,  33,  40, 255)
#define C_SURF2   rgba( 46,  44,  54, 255)
#define C_LINE    rgba( 70,  67,  80, 255)
#define C_TEXT    rgba(236, 234, 240, 255)
#define C_DIM     rgba(150, 146, 160, 255)
#define C_FAINT   rgba(105, 101, 115, 255)
#define C_ACC     rgba(245, 180, 202, 255)
#define C_DARK    rgba( 26,  24,  30, 255)
#define C_WARN    rgba(250, 200, 120, 255)
#define C_REC     rgba(255, 95, 95, 255)

static const SDL_Color C_MAN[NUM_MANUALS] = {{245, 170, 195, 255}, {120, 205, 215, 255}};

/* ===================================================================== */
/*                             состояние                                 */
/* ===================================================================== */
#define LOGICAL_H 790.0f
#define MIN_LW    980.0f

static SDL_Window       *win;
static SDL_Renderer     *ren;
static SDL_AudioDeviceID adev;
static float LW = 1000, LH = 790;          /* логический размер окна */

static int mt_flags;
static int focused = 1;
static int play_mode;
static int scene_mode;
static int sel_tab;                         /* 0, 1 — мануалы; 2 — общие */
static int sel_manual;                      /* последний выбранный мануал */

enum { OV_NONE, OV_HELP, OV_PRESETS, OV_NAME, OV_CALIB, OV_WELCOME, OV_CONFIRM };
static int overlay;

/* пресеты */
static char  cur_preset[PRESET_NAME_MAX];
static int   modified;
static Patch undo_patch;
static char  undo_name[PRESET_NAME_MAX];
static int   undo_modified, has_undo;
static float preset_scroll;

/* ввод имени */
static char name_buf[PRESET_NAME_MAX];

/* автосохранение */
static int    settings_dirty;
static Uint32 last_change_ms;

/* сообщение внизу экрана */
static char   toast_msg[320];
static Uint32 toast_until;

/* наведение мышью — для описания параметра */
static int hover_id = -1;

static int mouse_down_pad;

void audio_lock(void)   { if (adev) SDL_LockAudioDevice(adev); }
void audio_unlock(void) { if (adev) SDL_UnlockAudioDevice(adev); }

static void toast(const char *msg)
{
    snprintf(toast_msg, sizeof toast_msg, "%s", msg);
    toast_until = SDL_GetTicks() + 2600;
}

static void touch_settings(void)
{
    settings_dirty = 1;
    last_change_ms = SDL_GetTicks();
}

static void patch_changed(void)
{
    modified = 1;
    touch_settings();
}

/* ===================================================================== */
/*                            параметры                                  */
/* ===================================================================== */
enum {
    /* мануал */
    P_LOW, P_SPAN, P_MODE, P_GLIDE, P_SNAP,
    P_MAIN, P_D1, P_L1, P_D2, P_L2, P_D3, P_L3, P_D4, P_L4,
    P_VOWEL, P_REG, P_CUTOFF, P_RES, P_PBRIGHT, P_RELEASE, P_VOL,
    P_WAVE, P_NOISE, P_DRIFT, P_ATTACK, P_DECAY, P_SUSTAIN,
    NUM_MPARAMS,
    /* общие: звучание (в пресете) */
    G_LAYOUT = 100, G_SCALE, G_TONIC, G_REVERB, G_ECHO, G_ECHOMS, G_ECHOFB,
    /* общие: игра и выход (настройки) */
    G_PSRC, G_CURVE, G_FLOOR, G_SENS, G_GATE, G_MINLVL, G_SNAPMS,
    G_HAPTIC, G_GESTURES, G_MASTER, G_AUDIO,
    /* кнопки */
    B_CALIB = 200, B_FOLDER, B_REC, B_RECDIR, B_GESTPERM,
    /* заголовки */
    H_PITCH = 300, H_TIMBRE, H_FILTER, H_SOUND, H_PLAY, H_TOOLS, H_ENV, H_OUT,
    /* строки «субгармоника: делитель + уровень» */
    R_SUB1 = 400, R_SUB2, R_SUB3, R_SUB4
};

typedef struct { int id; const char *label; const char *desc; } ParamInfo;

static const ParamInfo pinfo[] = {
    {P_LOW,     "Нота у левого края", "Какая нота звучит у левого края тачпада. [ и ] сдвигают на октаву."},
    {P_SPAN,    "Диапазон",           "Сколько октав умещается на ширине тачпада. Меньше — шире полутон под пальцем и легче попадать в ноты. Клавиши − и =."},
    {P_MODE,    "Режим",              "Моно — как у Залы: один голос на мануал, второй палец перехватывает ноту, отпустил — снова звучит первый. Поли — каждый палец свой голос, можно брать аккорды. Клавиша P."},
    {P_GLIDE,   "Портаменто",         "Скорость скольжения высоты, когда второй палец перехватывает ноту в режиме Моно. Клавиша E."},
    {P_SNAP,    "Притяжение к нотам", "Выкл — высота точно следует за пальцем. Мягкое — скользишь свободно, а когда палец останавливается, высота сама доезжает до ноты лада (вибрато сохраняется). Жёсткое — только ноты лада, лесенкой. Клавиша W."},
    {P_MAIN,    "Основной тон f",     "Громкость основного тона генератора (пила)."},
    {P_D1,      "Субгармоника 1",     "Делитель частоты: f/N — тон в N раз ниже основного. Субгармоники — сердце звука Mixtur-Trautonium."},
    {P_L1,      "      уровень",      "Громкость этой субгармоники. 0 — выключена."},
    {P_D2,      "Субгармоника 2",     "Делитель частоты: f/N — тон в N раз ниже основного."},
    {P_L2,      "      уровень",      "Громкость этой субгармоники. 0 — выключена."},
    {P_D3,      "Субгармоника 3",     "Делитель частоты: f/N — тон в N раз ниже основного."},
    {P_L3,      "      уровень",      "Громкость этой субгармоники. 0 — выключена."},
    {P_D4,      "Субгармоника 4",     "Делитель частоты: f/N — тон в N раз ниже основного."},
    {P_L4,      "      уровень",      "Громкость этой субгармоники. 0 — выключена."},
    {P_VOWEL,   "Гласная",            "Формантный фильтр: окрашивает звук в гласную A E I O U. Клавиши 1–5, 0 — выключить."},
    {P_REG,     "Голос формант",      "Таблицы формант для баса, тенора или сопрано. Клавиша 6."},
    {P_CUTOFF,  "Яркость",            "Срез фильтра яркости. Стрелки ↑ ↓."},
    {P_RES,     "Резонанс",           "Подъём на частоте среза — звук становится «гнусавее» и выразительнее. Стрелки ← →."},
    {P_PBRIGHT, "Нажим → яркость",    "Насколько сильное нажатие открывает фильтр (до трёх октав). Звук становится не только громче, но и ярче."},
    {P_RELEASE, "Затухание",          "Сколько звучит нота после того, как палец отпущен."},
    {P_WAVE,    "Форма волны",        "Пила — яркая и ровная. Неон — как генератор на неоновой лампе в траутониуме Залы: пила с экспоненциальным подъёмом, мягче и теплее."},
    {P_NOISE,   "Шум",                "Шум «дыхания» внутри голоса. Он идёт через тот же фильтр и тоже следует за нажимом — хорошо звучит с гласными."},
    {P_DRIFT,   "Дрейф",              "Медленное случайное «плавание» высоты, до ±10 центов, как у лампового генератора. Делает звук живее, в аккордах даёт лёгкие биения. 0 — идеально ровно."},
    {P_ATTACK,  "Атака",              "За сколько звук нарастает после касания. Короткая — чёткое начало, длинная — звук «выплывает»."},
    {P_DECAY,   "Спад",               "За сколько после атаки громкость опускается до уровня сустейна."},
    {P_SUSTAIN, "Сустейн",            "Уровень, на котором звук держится, пока палец на тачпаде. 0 — ударный режим: звук гаснет сам, даже если палец не убран, как колокол или щипок. Новый палец — новый удар."},
    {P_VOL,     "Громкость мануала",  "Громкость этого мануала в общем звуке."},

    {G_LAYOUT,  "Раскладка",          "2 мануала — верхняя половина тачпада мануал I, нижняя — мануал II. 1 мануал — весь тачпад отдан выбранному мануалу. Клавиша Y."},
    {G_SCALE,   "Лад",                "Ноты лада подсвечиваются на экране, тачпад щёлкает только на них, к ним же работает притяжение. Клавиша T."},
    {G_TONIC,   "Тоника",             "От какой ноты строится лад. Shift+T."},
    {G_REVERB,  "Реверберация",       "Объём зала. Клавиша R."},
    {G_ECHO,    "Эхо",                "Громкость повторов. Эхо стерео: повторы чуть расходятся между левым и правым каналом и с каждым разом темнеют, как у ленточного эха."},
    {G_ECHOMS,  "Время эха",          "Пауза между повторами. При изменении на ходу высота повторов плавно «плывёт», как у ленты."},
    {G_ECHOFB,  "Повторы эха",        "Сколько раз отражается звук: чем больше, тем дольше хвост."},
    {G_AUDIO,   "Аудиовыход",         "Куда идёт звук. «Системный» — туда же, куда и всё остальное (и переключается вместе с системой)."},
    {B_REC,     "● Записать в WAV",   "Записать игру в файл WAV (24 бит, 48 кГц) — ровно то, что слышно. Файлы попадают в папку Музыка → Trautonium. ⌘R."},
    {B_RECDIR,  "Папка записей",      "Открыть в Finder папку с записями."},
    {B_GESTPERM,"Выдать разрешение для жестов…", "Откроет окно macOS. Включите «Траутониум» в Настройках → Конфиденциальность и безопасность → Универсальный доступ — программа сама заметит разрешение."},
    {G_PSRC,    "Источник нажима",    "Сила — датчик Force Touch (граммы). Площадь — насколько плотно лежит палец (запасной вариант). Нет — громкость постоянная. Клавиша D."},
    {G_CURVE,   "Кривая нажима",      "Мягкая — громко уже при небольшом нажиме. Жёсткая — чтобы стало громко, нужно давить сильно."},
    {G_FLOOR,   "Лёгкое касание",     "Сила, которая считается самым лёгким касанием. Проще всего настроить через калибровку."},
    {G_SENS,    "Полное нажатие",     "Сила, которая даёт полную громкость. Проще всего настроить через калибровку. Клавиши 7 / 8."},
    {G_GATE,    "Порог касания",      "Звук появляется только при нажиме сильнее порога — как будто проволоку нужно прижать к шине. Палец можно держать на тачпаде молча и «вступать» нажимом."},
    {G_MINLVL,  "Громкость касания",  "Громкость при самом лёгком прикосновении."},
    {G_SNAPMS,  "Скорость притяжения","За сколько высота доезжает до ноты при мягком притяжении."},
    {G_HAPTIC,  "Хаптика",            "Лёгкий щелчок тачпада на каждой ноте (или ступени лада) — играть можно не глядя. Клавиша H."},
    {G_GESTURES,"Блокировать жесты",  "Пока идёт игра, жесты macOS тремя-четырьмя пальцами (Mission Control, рабочие столы) не срабатывают. Нужно разрешение «Универсальный доступ»: при включении macOS попросит его, а программа сама заметит, когда оно появится. После пересборки приложения разрешение придётся выдать заново."},
    {G_MASTER,  "Общая громкость",    "Громкость всего инструмента."},
    {B_CALIB,   "Калибровка нажима…", "Подстроить нажим под руку: лёгкое касание и сильное нажатие. 20 секунд. ⌘K."},
    {B_FOLDER,  "Папка пресетов",     "Открыть в Finder папку, где лежат свои пресеты. Это обычные текстовые файлы — их можно копировать и отправлять друзьям."},

    {H_PITCH,   "ВЫСОТА",  NULL},
    {H_TIMBRE,  "ТЕМБР",   NULL},
    {H_FILTER,  "ФИЛЬТР",  NULL},
    {H_SOUND,   "ЗВУЧАНИЕ (в пресете)", NULL},
    {H_PLAY,    "ИГРА",    NULL},
    {H_TOOLS,   "",        NULL},
    {H_ENV,     "ОГИБАЮЩАЯ И ГРОМКОСТЬ", NULL},
    {H_OUT,     "ВЫХОД",   NULL},
};

static const ParamInfo *info(int id)
{
    for (int i = 0; i < (int)(sizeof pinfo / sizeof pinfo[0]); i++)
        if (pinfo[i].id == id) return &pinfo[i];
    return NULL;
}

static const int rows_manual[] = {
    H_PITCH, P_LOW, P_SPAN, P_MODE, P_GLIDE, P_SNAP,
    H_TIMBRE, P_WAVE, P_MAIN, R_SUB1, R_SUB2, R_SUB3, R_SUB4, P_NOISE, P_DRIFT, P_VOWEL, P_REG,
    H_FILTER, P_CUTOFF, P_RES, P_PBRIGHT,
    H_ENV, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, P_VOL,
};
static const int rows_general[] = {
    H_SOUND, G_LAYOUT, G_SCALE, G_TONIC, G_REVERB, G_ECHO, G_ECHOMS, G_ECHOFB,
    H_PLAY, G_PSRC, G_CURVE, G_FLOOR, G_SENS, G_GATE, G_MINLVL, G_SNAPMS, G_HAPTIC, G_GESTURES,
    H_OUT, G_MASTER, G_AUDIO,
    H_TOOLS, B_REC, B_RECDIR, B_CALIB, B_FOLDER, B_GESTPERM,
};

static int is_header(int id) { return id >= 300 && id < 400; }
static int is_subrow(int id) { return id >= R_SUB1 && id <= R_SUB4; }
static int is_button(int id) { return id >= 200 && id < 300; }
static int in_patch(int id)  { return id < NUM_MPARAMS || (id >= G_LAYOUT && id <= G_ECHOFB); }

static const char *snap_names[]  = {"Выкл", "Мягкое", "Жёсткое"};
static const char *psrc_names[]  = {"Сила", "Площадь", "Нет"};
static const char *curve_names[] = {"Мягкая", "Линейная", "Жёсткая"};
static const char *wave_names[]  = {"Пила", "Неон"};
static const char *tonic_names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static void gestures_apply(void);
static void start_calibration(void);
static void toggle_recording(void);
static void step_audio_out(int dir);
static void audio_out_label(char *b, int n);

static void param_value(int id, int m, char *b, int n)
{
    const ManualParams *M = &P.man[m];
    char nm[16];
    switch (id) {
        case P_LOW:     note_name(M->low_note, nm, sizeof nm); snprintf(b, n, "%s", nm); break;
        case P_SPAN:    snprintf(b, n, "%d окт.", M->span); break;
        case P_MODE:    snprintf(b, n, "%s", M->mono ? "Моно" : "Поли"); break;
        case P_GLIDE:   snprintf(b, n, "%d мс", M->glide_ms); break;
        case P_SNAP:    snprintf(b, n, "%s", snap_names[clampi(M->snap, 0, 2)]); break;
        case P_MAIN:    snprintf(b, n, "%d%%", (int)lroundf(M->main_lvl * 100)); break;
        case P_D1: case P_D2: case P_D3: case P_D4:
            snprintf(b, n, "f/%d", M->div[(id - P_D1) / 2]); break;
        case P_L1: case P_L2: case P_L3: case P_L4:
            snprintf(b, n, "%d%%", (int)lroundf(M->sub_lvl[(id - P_L1) / 2] * 100)); break;
        case P_VOWEL:   snprintf(b, n, "%s", M->vowel < 0 ? "Выкл" : vowel_names[M->vowel]); break;
        case P_REG:     snprintf(b, n, "%s", register_names[clampi(M->reg, 0, 2)]); break;
        case P_CUTOFF:  snprintf(b, n, "%.0f Гц", M->cutoff); break;
        case P_RES:     snprintf(b, n, "%.2f", M->res); break;
        case P_PBRIGHT: snprintf(b, n, "%d%%", (int)lroundf(M->press_bright * 100)); break;
        case P_RELEASE: snprintf(b, n, "%d мс", M->release_ms); break;
        case P_VOL:     snprintf(b, n, "%d%%", (int)lroundf(M->volume * 100)); break;
        case P_WAVE:    snprintf(b, n, "%s", wave_names[clampi(M->wave, 0, 1)]); break;
        case P_NOISE:   snprintf(b, n, "%d%%", (int)lroundf(M->noise * 100)); break;
        case P_DRIFT:   snprintf(b, n, "%d%%", (int)lroundf(M->drift * 100)); break;
        case P_ATTACK:  snprintf(b, n, "%d мс", M->attack_ms); break;
        case P_DECAY:   snprintf(b, n, "%d мс", M->decay_ms); break;
        case P_SUSTAIN:
            if (M->sustain <= 0.001f) snprintf(b, n, "0 — удар");
            else snprintf(b, n, "%d%%", (int)lroundf(M->sustain * 100));
            break;

        case G_LAYOUT:  snprintf(b, n, "%s", P.split ? "2 мануала" : "1 мануал"); break;
        case G_SCALE:   snprintf(b, n, "%s", scales[clampi(P.scale, 0, num_scales - 1)].name); break;
        case G_TONIC:   snprintf(b, n, "%s", tonic_names[clampi(P.tonic, 0, 11)]); break;
        case G_REVERB:  snprintf(b, n, "%d%%", (int)lroundf(P.reverb * 100)); break;
        case G_ECHO:
            if (P.echo_mix <= 0.001f) snprintf(b, n, "Выкл");
            else snprintf(b, n, "%d%%", (int)lroundf(P.echo_mix * 100));
            break;
        case G_ECHOMS:  snprintf(b, n, "%d мс", P.echo_ms); break;
        case G_ECHOFB:  snprintf(b, n, "%d%%", (int)lroundf(P.echo_fb * 100)); break;
        case G_AUDIO:   audio_out_label(b, n); break;
        case G_PSRC:    snprintf(b, n, "%s", psrc_names[clampi(PF.press_src, 0, 2)]); break;
        case G_CURVE:   snprintf(b, n, "%s", curve_names[clampi(PF.curve, 0, 2)]); break;
        case G_FLOOR:
            if (PF.press_src == PS_AREA) snprintf(b, n, "%.2f", PF.area_floor);
            else snprintf(b, n, "%.0f г", PF.force_floor_g);
            break;
        case G_SENS:
            if (PF.press_src == PS_AREA) {
                if (PF.area_max > 0) snprintf(b, n, "%.2f", PF.area_max);
                else snprintf(b, n, "авто");
            } else snprintf(b, n, "%.0f г", PF.force_max_g);
            break;
        case G_GATE:
            if (PF.gate_g > 0) snprintf(b, n, "%d г", PF.gate_g);
            else snprintf(b, n, "Выкл");
            break;
        case G_MINLVL:  snprintf(b, n, "%d%%", (int)lroundf(PF.min_level * 100)); break;
        case G_SNAPMS:  snprintf(b, n, "%d мс", PF.snap_ms); break;
        case G_HAPTIC:  snprintf(b, n, "%s", PF.haptics ? "Вкл" : "Выкл"); break;
        case G_GESTURES: {
            int gs = plat_gestures_status();
            snprintf(b, n, "%s", !PF.block_gestures ? "Выкл" :
                                 gs == GESTURES_WAITING ? "Ждёт доступа" :
                                 gs == GESTURES_FAILED ? "Не удалось" : "Вкл");
        } break;
        case G_MASTER:  snprintf(b, n, "%d%%", (int)lroundf(PF.master * 100)); break;
        default:        b[0] = 0;
    }
}

static void step_list(int *v, const int *list, int n, int dir)
{
    int idx = 0;
    for (int i = 0; i < n; i++) if (list[i] <= *v) idx = i;
    idx = clampi(idx + dir, 0, n - 1);
    *v = list[idx];
}

static void step_listf(float *v, const float *list, int n, int dir)
{
    int idx = 0;
    for (int i = 0; i < n; i++) if (list[i] <= *v + 1e-3f) idx = i;
    idx = clampi(idx + dir, 0, n - 1);
    *v = list[idx];
}

static void step_f(float *v, float d, float lo, float hi)
{
    *v = clampf(roundf((*v + d) * 100.0f) / 100.0f, lo, hi);
}


static void param_step(int id, int m, int dir)
{
    static const int   glides[]   = {0, 10, 20, 40, 80, 150, 300, 600};
    static const int   releases[] = {10, 30, 60, 120, 250, 450, 700, 1000, 2000};
    static const int   gates[]    = {0, 5, 10, 20, 40, 80};
    static const int   snapms[]   = {30, 60, 120, 200, 350, 600};
    static const float sens[]     = {50, 100, 150, 200, 250, 300, 400, 500, 600, 800, 1000};
    static const float floors[]   = {0, 5, 10, 20, 30, 50, 75, 100};
    static const int   attacks[]  = {1, 5, 15, 30, 60, 120, 250, 500, 1000, 2000};
    static const int   decays[]   = {50, 100, 200, 300, 500, 800, 1200, 2000, 3500, 6000};
    static const int   echoms[]   = {60, 100, 150, 200, 250, 300, 350, 450, 600, 800, 1100};

    if (is_header(id)) return;
    if (id == B_CALIB)    { start_calibration(); return; }
    if (id == B_FOLDER)   { plat_open_url(presets_dir()); return; }
    if (id == B_REC)      { toggle_recording(); return; }
    if (id == B_RECDIR)   { plat_open_url(rec_dir()); return; }
    if (id == B_GESTPERM) { plat_gestures_request(); return; }
    if (id == G_AUDIO)    { step_audio_out(dir); return; }

    ManualParams *M = &P.man[m];
    audio_lock();
    switch (id) {
        case P_LOW:     M->low_note = clampi(M->low_note + dir, 24, 96); break;
        case P_SPAN:    M->span = clampi(M->span + dir, 1, 5); break;
        case P_MODE:    M->mono = !M->mono; break;
        case P_GLIDE:   step_list(&M->glide_ms, glides, 8, dir); break;
        case P_SNAP:    M->snap = (M->snap + dir + SNAP_COUNT) % SNAP_COUNT; break;
        case P_MAIN:    step_f(&M->main_lvl, 0.05f * dir, 0, 1); break;
        case P_D1: case P_D2: case P_D3: case P_D4: {
            int k = (id - P_D1) / 2;
            M->div[k] = clampi(M->div[k] + dir, DIV_MIN, DIV_MAX);
        } break;
        case P_L1: case P_L2: case P_L3: case P_L4:
            step_f(&M->sub_lvl[(id - P_L1) / 2], 0.05f * dir, 0, 1); break;
        case P_VOWEL:
            M->vowel += dir;
            if (M->vowel >= NUM_VOWELS) M->vowel = -1;
            if (M->vowel < -1) M->vowel = NUM_VOWELS - 1;
            break;
        case P_REG:     M->reg = (M->reg + dir + NUM_REGISTERS) % NUM_REGISTERS; break;
        case P_CUTOFF:  M->cutoff = clampf(M->cutoff * powf(1.15f, (float)dir), 150, 16000); break;
        case P_RES:     step_f(&M->res, 0.05f * dir, 0, 0.95f); break;
        case P_PBRIGHT: step_f(&M->press_bright, 0.1f * dir, 0, 1); break;
        case P_RELEASE: step_list(&M->release_ms, releases, 9, dir); break;
        case P_VOL:     step_f(&M->volume, 0.05f * dir, 0, 1); break;
        case P_WAVE:    M->wave = (M->wave + dir + WAVE_COUNT) % WAVE_COUNT; break;
        case P_NOISE:   step_f(&M->noise, 0.05f * dir, 0, 1); break;
        case P_DRIFT:   step_f(&M->drift, 0.1f * dir, 0, 1); break;
        case P_ATTACK:  step_list(&M->attack_ms, attacks, 10, dir); break;
        case P_DECAY:   step_list(&M->decay_ms, decays, 10, dir); break;
        case P_SUSTAIN: step_f(&M->sustain, 0.1f * dir, 0, 1); break;

        case G_LAYOUT:  P.split = !P.split; break;
        case G_SCALE:   P.scale = (P.scale + dir + num_scales) % num_scales; break;
        case G_TONIC:   P.tonic = (P.tonic + dir + 12) % 12; break;
        case G_REVERB:  step_f(&P.reverb, 0.05f * dir, 0, 1); break;
        case G_ECHO:    step_f(&P.echo_mix, 0.05f * dir, 0, 1); break;
        case G_ECHOMS:  step_list(&P.echo_ms, echoms, 11, dir); break;
        case G_ECHOFB:  step_f(&P.echo_fb, 0.05f * dir, 0, 0.9f); break;
        case G_PSRC:    PF.press_src = (PF.press_src + dir + PS_COUNT) % PS_COUNT; break;
        case G_CURVE:   PF.curve = (PF.curve + dir + CURVE_COUNT) % CURVE_COUNT; break;
        case G_FLOOR:
            if (PF.press_src == PS_AREA) step_f(&PF.area_floor, 0.02f * dir, 0, 5);
            else step_listf(&PF.force_floor_g, floors, 8, dir);
            break;
        case G_SENS:
            if (PF.press_src == PS_AREA) {
                if (PF.area_max <= 0) PF.area_max = 0.3f;
                step_f(&PF.area_max, 0.05f * dir, 0.05f, 5);
            } else step_listf(&PF.force_max_g, sens, 11, dir);
            break;
        case G_GATE:    step_list(&PF.gate_g, gates, 6, dir); break;
        case G_MINLVL:  step_f(&PF.min_level, 0.05f * dir, 0, 0.8f); break;
        case G_SNAPMS:  step_list(&PF.snap_ms, snapms, 6, dir); break;
        case G_HAPTIC:  PF.haptics = !PF.haptics; break;
        case G_GESTURES:PF.block_gestures = !PF.block_gestures; break;
        case G_MASTER:  step_f(&PF.master, 0.05f * dir, 0, 1); break;
    }
    audio_unlock();

    if (id == G_GESTURES) {
        plat_gestures_want(PF.block_gestures);
        if (PF.block_gestures && plat_gestures_status() == GESTURES_WAITING) {
            plat_gestures_request();
            toast("Включите «Траутониум» в Настройках → Универсальный доступ — дальше всё само");
        }
        gestures_apply();
    }
    if (in_patch(id)) patch_changed();
    else touch_settings();
}

/* ===================================================================== */
/*                               пресеты                                 */
/* ===================================================================== */
static void remember_undo(void)
{
    undo_patch = P;
    snprintf(undo_name, sizeof undo_name, "%s", cur_preset);
    undo_modified = modified;
    has_undo = 1;
}

static void load_preset(int i)
{
    Patch np;
    if (!preset_load(i, &np)) { toast("Не удалось прочитать пресет"); return; }
    remember_undo();
    audio_lock();
    P = np;
    audio_unlock();
    snprintf(cur_preset, sizeof cur_preset, "%s", preset_name(i));
    modified = 0;
    touch_settings();
}

static void step_preset(int dir)
{
    int n = presets_count();
    if (n == 0) return;
    int i = preset_find(cur_preset);
    i = i < 0 ? 0 : (i + dir + n) % n;
    load_preset(i);
    char msg[160];
    snprintf(msg, sizeof msg, "Пресет «%s»   ⌘Z — вернуть как было", cur_preset);
    toast(msg);
}

static void undo(void)
{
    if (!has_undo) { toast("Нечего возвращать"); return; }
    Patch cur = P;
    char  cn[PRESET_NAME_MAX];
    int   cm = modified;
    snprintf(cn, sizeof cn, "%s", cur_preset);

    audio_lock();
    P = undo_patch;
    audio_unlock();
    snprintf(cur_preset, sizeof cur_preset, "%s", undo_name);
    modified = undo_modified;

    undo_patch = cur;                      /* повторное ⌘Z — обратно */
    snprintf(undo_name, sizeof undo_name, "%s", cn);
    undo_modified = cm;
    touch_settings();
    toast("Возвращено предыдущее звучание");
}

/* копирование с обрезкой по границе символа UTF-8 */
static void utf8_copy(char *dst, int n, const char *src)
{
    int len = (int)strlen(src);
    if (len > n - 1) {
        len = n - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void begin_save_as(void)
{
    int i = preset_find(cur_preset);
    char tmp[PRESET_NAME_MAX + 32];
    if (i >= 0 && preset_is_factory(i)) snprintf(tmp, sizeof tmp, "%s — мой", cur_preset);
    else snprintf(tmp, sizeof tmp, "%s", cur_preset);
    utf8_copy(name_buf, sizeof name_buf, tmp);
    overlay = OV_NAME;
    SDL_StartTextInput();
}

static void save_preset(void)
{
    int i = preset_find(cur_preset);
    if (i < 0 || preset_is_factory(i)) { begin_save_as(); return; }
    if (preset_save(cur_preset, &P) < 0) { toast("Не удалось сохранить"); return; }
    modified = 0;
    touch_settings();
    toast("Пресет сохранён");
}

static void finish_save_as(void)
{
    SDL_StopTextInput();
    overlay = OV_NONE;
    int fi = preset_find(name_buf);
    if (fi >= 0 && preset_is_factory(fi)) { toast("Так называется заводской пресет — выберите другое имя"); return; }
    int i = preset_save(name_buf, &P);
    if (i < 0) { toast("Не удалось сохранить пресет"); return; }
    snprintf(cur_preset, sizeof cur_preset, "%s", preset_name(i));
    modified = 0;
    touch_settings();
    toast(fi >= 0 ? "Пресет перезаписан" : "Пресет сохранён");
}

static void ask_delete(void)
{
    int i = preset_find(cur_preset);
    if (i < 0 || preset_is_factory(i)) { toast("Заводские пресеты удалить нельзя"); return; }
    overlay = OV_CONFIRM;
}

static void do_delete(void)
{
    overlay = OV_NONE;
    int i = preset_find(cur_preset);
    if (i >= 0 && preset_delete(i)) {
        modified = 1;        /* звучание осталось, но файла больше нет */
        touch_settings();
        toast("Пресет удалён (звучание осталось — его можно сохранить заново)");
    }
}

/* ===================================================================== */
/*                    режим игры, сцена, жесты                           */
/* ===================================================================== */
static int input_active(void) { return play_mode && focused && overlay == OV_NONE; }

static void gestures_apply(void)
{
    plat_gestures_block(PF.block_gestures && input_active());
}

static void update_input(void)
{
    int on = input_active();
    audio_lock();
    synth_set_input_active(on);
    audio_unlock();
    SDL_SetRelativeMouseMode((on || overlay == OV_CALIB) ? SDL_TRUE : SDL_FALSE);
    gestures_apply();
}

static void set_play(int on)
{
    play_mode = on;
    audio_lock();
    synth_fb_clear();
    audio_unlock();
    mouse_down_pad = 0;
    update_input();
}

static void set_scene(int on)
{
    scene_mode = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    set_play(on);
}

static void open_overlay(int ov)
{
    overlay = ov;
    update_input();
}

static void close_overlay(void)
{
    if (overlay == OV_NAME) SDL_StopTextInput();
    overlay = OV_NONE;
    update_input();
}

/* ===================================================================== */
/*                         калибровка нажима                             */
/* ===================================================================== */
#define CAL_MAX_SAMPLES 2000
#define CAL_NEED_S      2.0f

#define CAL_LIFT_S      0.35f

/* LIFT — пауза между шагами: ждём, пока палец полностью уберут с тачпада */
enum { CS_INTRO, CS_LIGHT, CS_LIFT, CS_HARD, CS_CHECK };

static struct {
    int    step;
    int    src;                         /* PS_FORCE или PS_AREA */
    float  samples[CAL_MAX_SAMPLES];
    int    ns;
    float  contact_s;
    float  lift_s;                      /* сколько тачпад уже свободен */
    int    lift_next;                   /* какой шаг начнётся после паузы */
    int    too_light;                   /* на втором шаге жмут недостаточно сильно */
    float  light, hard;
    char   error[400];
    Prefs  backup;
    Uint32 last_ms;
} cal;

static void start_calibration(void)
{
    if (!(mt_flags & MT_OK)) {
        toast("Тачпад не читается напрямую — калибровать нечего");
        return;
    }
    memset(&cal, 0, sizeof cal);
    cal.backup = PF;
    cal.src = (mt_flags & MT_FORCE) && PF.press_src != PS_AREA ? PS_FORCE : PS_AREA;
    cal.step = CS_INTRO;
    cal.last_ms = SDL_GetTicks();
    if (play_mode && !scene_mode) set_play(0);
    open_overlay(OV_CALIB);
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static float percentile(float *v, int n, float q)
{
    if (n <= 0) return 0;
    qsort(v, n, sizeof(float), cmp_float);
    int i = clampi((int)(q * (n - 1) + 0.5f), 0, n - 1);
    return v[i];
}

static void cal_next_step(int step)
{
    cal.step = step;
    cal.ns = 0;
    cal.contact_s = 0;
    cal.lift_s = 0;
    cal.too_light = 0;
}

/* сначала убрать палец, потом начнётся шаг next */
static void cal_wait_lift(int next)
{
    cal_next_step(CS_LIFT);
    cal.lift_next = next;
}

/* порог «сильного» нажатия для второго шага */
static float cal_hard_threshold(void)
{
    return cal.src == PS_FORCE ? cal.light + 40.0f : cal.light * 1.3f + 0.02f;
}

/* вызывается каждый кадр, пока открыта калибровка */
static void cal_update(void)
{
    Uint32 now = SDL_GetTicks();
    float dt = (now - cal.last_ms) / 1000.0f;
    cal.last_ms = now;
    if (dt > 0.1f) dt = 0.1f;
    if (cal.step == CS_INTRO || cal.step == CS_CHECK) return;

    MTFrame f;
    int touching = mt_read(&f, 0) && f.count > 0 && mt_now() - f.time < 0.3;

    if (cal.step == CS_LIFT) {
        if (touching) cal.lift_s = 0;
        else if ((cal.lift_s += dt) >= CAL_LIFT_S) cal_next_step(cal.lift_next);
        return;
    }
    if (!touching) { cal.too_light = 0; return; }

    /* берём самое сильное касание */
    float v = 0;
    for (int i = 0; i < f.count; i++) {
        float x = cal.src == PS_FORCE ? f.t[i].pressure : f.t[i].area;
        if (x > v) v = x;
    }

    if (cal.step == CS_LIGHT) {
        cal.contact_s += dt;
        if (cal.ns < CAL_MAX_SAMPLES) cal.samples[cal.ns++] = v;
        if (cal.contact_s >= CAL_NEED_S) {
            cal.light = percentile(cal.samples, cal.ns, 0.5f);
            cal.error[0] = 0;
            cal_wait_lift(CS_HARD);
        }
        return;
    }

    /* CS_HARD: время идёт, только пока нажим заметно сильнее лёгкого */
    cal.too_light = v < cal_hard_threshold();
    if (cal.too_light) return;
    cal.contact_s += dt;
    if (cal.ns < CAL_MAX_SAMPLES) cal.samples[cal.ns++] = v;
    if (cal.contact_s < CAL_NEED_S) return;

    cal.hard = percentile(cal.samples, cal.ns, 0.75f);
    audio_lock();
    PF.press_src = cal.src;
    if (cal.src == PS_FORCE) {
        PF.force_floor_g = cal.light < 3.0f ? 0.0f : cal.light;
        PF.force_max_g   = cal.hard;
        PF.calibrated   |= CAL_FORCE;
    } else {
        PF.area_floor  = cal.light;
        PF.area_max    = cal.hard;
        PF.calibrated |= CAL_AREA;
    }
    audio_unlock();
    touch_settings();
    cal_next_step(CS_CHECK);
}

static void cal_finish(int keep)
{
    if (!keep) {
        audio_lock();
        PF = cal.backup;
        audio_unlock();
    }
    touch_settings();
    close_overlay();
    toast(keep ? "Калибровка сохранена" : "Калибровка отменена");
}

/* ===================================================================== */
/*                               раскладка                               */
/* ===================================================================== */
#define PANEL_W 316.0f
#define ROW_H   20.0f

typedef struct { float x, y, w, h; } Box;

static int in_box(Box b, float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

static Box pad_box(void)
{
    Box b;
    if (scene_mode) { b.x = 24; b.y = 64; b.w = LW - 48; b.h = LH - 64 - 52; }
    else            { b.x = 20; b.y = 72; b.w = LW - PANEL_W - 60; b.h = LH - 72 - 196; }
    return b;
}

static Box strip_box(int m)
{
    Box b = pad_box();
    if (P.split) { b.h /= 2; b.y += m * b.h; }
    return b;
}

static Box scope_box(void)
{
    Box p = pad_box();
    Box b = {p.x, p.y + p.h + 14, p.w, 116};
    return b;
}

static float panel_x(void) { return LW - PANEL_W - 20; }

/* элементы, по которым можно щёлкнуть */
enum { HIT_ROW, HIT_MINUS, HIT_PLUS, HIT_TAB, HIT_PREV, HIT_NEXT, HIT_NAME,
       HIT_SAVE, HIT_SAVEAS, HIT_DELETE, HIT_HELP, HIT_LIST_ITEM };
typedef struct { Box b; int kind, id; } Hit;
static Hit hits[96];
static int nhits;

static void add_hit(Box b, int kind, int id)
{
    if (nhits < (int)(sizeof hits / sizeof hits[0])) { hits[nhits].b = b; hits[nhits].kind = kind; hits[nhits].id = id; nhits++; }
}

/* ===================================================================== */
/*                         координаты мыши                               */
/* ===================================================================== */
static void mouse_logical(float *x, float *y)
{
    int mx, my, ww, wh, ow, oh;
    SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    float px_per_pt = ww > 0 ? (float)ow / ww : 1.0f;
    *x = mx * px_per_pt / ui_scale();
    *y = my * px_per_pt / ui_scale();
}

static void update_layout(void)
{
    int ow, oh;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    if (ow <= 0 || oh <= 0) return;
    float s = oh / LOGICAL_H;
    if (ow / s < MIN_LW) s = ow / MIN_LW;
    ui_set_scale(s);
    LW = ow / s;
    LH = oh / s;
}

/* ===================================================================== */
/*                               рисование                               */
/* ===================================================================== */
static SynthTouch snap_t[MAX_IN];
static int        snap_n;

/* щелчки тачпада: палец пересёк ноту (или ступень лада) */
typedef struct { int id; float last; int seen; } HapTrack;
static HapTrack hap[MAX_IN];
static int      nhap;
static Uint32   last_haptic_ms;

static void haptics_update(void)
{
    int strongest = 0;
    for (int j = 0; j < nhap; j++) hap[j].seen = 0;

    for (int i = 0; i < snap_n; i++) {
        const SynthTouch *t = &snap_t[i];
        if (!t->from_trackpad) continue;
        HapTrack *h = NULL;
        for (int j = 0; j < nhap; j++) if (hap[j].id == t->id) { h = &hap[j]; break; }
        if (!h) {
            if (nhap >= MAX_IN) continue;
            h = &hap[nhap++];
            h->id = t->id;
            h->last = t->raw_note;
        }
        h->seen = 1;
        float a = fminf(h->last, t->raw_note), b = fmaxf(h->last, t->raw_note);
        for (int k = (int)floorf(a) + 1; k <= (int)floorf(b) && b - a < 12; k++) {
            if (!scale_has(k)) continue;
            int s = is_tonic(k) ? 3 : 1;
            if (s > strongest) strongest = s;
        }
        h->last = t->raw_note;
    }
    int k = 0;
    for (int j = 0; j < nhap; j++) if (hap[j].seen) hap[k++] = hap[j];
    nhap = k;

    Uint32 now = SDL_GetTicks();
    if (strongest && PF.haptics && input_active() && (mt_flags & MT_OK) && now - last_haptic_ms > 15) {
        mt_haptic(strongest);
        last_haptic_ms = now;
    }
}

static void draw_strip(int m, Box r)
{
    const ManualParams *M = &P.man[m];
    SDL_Color mc = C_MAN[m];
    ui_fill(r.x, r.y, r.w, r.h, C_SURF);

    float wy = r.y + r.h / 2;
    ui_line(r.x + 4, wy, r.x + r.w - 4, wy, with_a(mc, 70));

    float x0 = r.x + PAD_EDGE * r.w, x1 = r.x + r.w - PAD_EDGE * r.w;
    int ns = M->span * 12;
    int chromatic = P.scale == 0;
    float semi_w = (x1 - x0) / ns;
    int label_all = !chromatic && semi_w * 1.6f > 18;

    for (int i = 0; i <= ns; i++) {
        float x = x0 + (x1 - x0) * i / ns;
        int midi = M->low_note + i;
        int pcl = ((midi % 12) + 12) % 12;
        int black = pcl == 1 || pcl == 3 || pcl == 6 || pcl == 8 || pcl == 10;
        int tonic = is_tonic(midi);
        int in_sc = scale_has(midi);
        int c_note = pcl == 0;

        float len;
        SDL_Color col;
        if (chromatic) {
            len = c_note ? 26 : (black ? 8 : 14);
            col = c_note ? mc : with_a(C_DIM, black ? 110 : 170);
        } else if (tonic) {
            len = 28; col = mc;
        } else if (in_sc) {
            len = 16; col = with_a(C_TEXT, 190);
        } else {
            len = 5;  col = with_a(C_FAINT, 120);
        }
        ui_line(x, r.y + r.h - 1, x, r.y + r.h - len, col);

        int full_line = chromatic ? c_note : tonic;
        if (full_line) ui_line(x, r.y + 1, x, r.y + r.h - len, with_a(mc, 40));
        else if (!chromatic && in_sc) ui_line(x, r.y + 1, x, r.y + r.h - len, with_a(C_TEXT, 14));

        if ((chromatic && c_note) || (!chromatic && (tonic || (in_sc && label_all)))) {
            char nm[16];
            note_name(midi, nm, sizeof nm);
            ui_text(x + 3, r.y + r.h - 22, nm, TS_SMALL, FONT_REGULAR,
                    (tonic || (chromatic && c_note)) ? with_a(mc, 230) : C_DIM, AL_LEFT);
        }
    }

    char hdr[200], lo[16], hi[16];
    note_name(M->low_note, lo, sizeof lo);
    note_name(M->low_note + ns, hi, sizeof hi);
    snprintf(hdr, sizeof hdr, "%s  ·  %s  ·  %s–%s  ·  %s%s%s",
             m == 0 ? "Мануал I" : "Мануал II", M->mono ? "моно" : "поли", lo, hi,
             M->vowel < 0 ? "без гласной" : "гласная ",
             M->vowel < 0 ? "" : vowel_names[M->vowel],
             M->snap == SNAP_SOFT ? "  ·  мягкое притяжение" : M->snap == SNAP_HARD ? "  ·  только ноты" : "");
    ui_text(r.x + 10, r.y + 7, hdr, TS_SMALL, FONT_MEDIUM, mc, AL_LEFT);
}

static void draw_fingers(Box pr)
{
    int big = scene_mode;
    for (int i = 0; i < snap_n; i++) {
        const SynthTouch *t = &snap_t[i];
        SDL_Color mc = C_MAN[t->manual];
        float cx = pr.x + t->x * pr.w;
        float cy = pr.y + t->y * pr.h;
        float rad = (big ? 10 : 7) + (big ? 28 : 18) * t->pn;
        if (t->gate) {
            ui_circle(cx, cy, rad, with_a(mc, 90));
            ui_circle(cx, cy, 4, mc);
        } else {
            ui_ring(cx, cy, rad, 1.5f, with_a(mc, 140));
        }

        float note = t->pitch_note;
        int nearest = (int)lroundf(note);
        int cents = (int)lroundf((note - nearest) * 100.0f);
        char nm[16], lbl[96], sub[48] = "";
        note_name(nearest, nm, sizeof nm);

        /* подпись: нота (+ отклонение) и нажим */
        int   main_size = big ? TS_HUGE : TS_NORMAL;
        int   main_wt   = big ? FONT_BOLD : FONT_MEDIUM;
        if (!big && abs(cents) >= 1) snprintf(lbl, sizeof lbl, "%s %+d¢", nm, cents);
        else snprintf(lbl, sizeof lbl, "%s", nm);
        if (!big && t->from_trackpad && PF.press_src != PS_FIXED) {
            if (PF.press_src == PS_AREA) snprintf(sub, sizeof sub, "S %.2f", t->area);
            else snprintf(sub, sizeof sub, "%.0f г", t->pressure_g);
        }
        float wmain = ui_text_w(lbl, main_size, main_wt);
        float wsub  = sub[0] ? ui_text_w(sub, TS_SMALL, FONT_REGULAR) + 6 : 0;
        float total = wmain + wsub;
        float h     = ui_text_h(main_size);

        /* справа от пальца, а если не помещается в поле — слева */
        float lx = cx + rad + 6;
        if (lx + total > pr.x + pr.w - 6) lx = cx - rad - 6 - total;
        if (lx < pr.x + 6) lx = pr.x + 6;
        float ly = clampf(cy - h / 2, pr.y + 4, pr.y + pr.h - h - 4);

        ui_text(lx, ly, lbl, main_size, main_wt, C_TEXT, AL_LEFT);
        if (sub[0])
            ui_text(lx + wmain + 6, ly + (h - ui_text_h(TS_SMALL)) / 2 + 1, sub, TS_SMALL, FONT_REGULAR, C_DIM, AL_LEFT);
    }
}

static void draw_pad(void)
{
    Box pr = pad_box();
    if (P.split) {
        draw_strip(0, strip_box(0));
        draw_strip(1, strip_box(1));
        ui_line(pr.x, pr.y + pr.h / 2, pr.x + pr.w, pr.y + pr.h / 2, C_BG);
    } else {
        draw_strip(sel_manual, pr);
    }
    draw_fingers(pr);

    SDL_Color fc = input_active() ? C_ACC : C_LINE;
    ui_frame(pr.x, pr.y, pr.w, pr.h, fc);
    if (input_active()) ui_frame(pr.x - 1, pr.y - 1, pr.w + 2, pr.h + 2, with_a(fc, 120));

    if (!input_active() && overlay == OV_NONE) {
        float cx = pr.x + pr.w / 2, cy = pr.y + pr.h / 2;
        ui_round(cx - 235, cy - 38, 470, 76, 12, rgba(14, 13, 18, 225));
        ui_text(cx, cy - 30, !focused ? "Окно не в фокусе — щёлкните по нему" : "Пробел — играть на тачпаде",
                TS_TITLE, FONT_MEDIUM, C_ACC, AL_CENTER);
        ui_text(cx, cy + 6, "или зажмите мышь на полосе: выше — тише, ниже — громче",
                TS_SMALL, FONT_REGULAR, C_DIM, AL_CENTER);
    }
}

static void draw_scope(void)
{
    Box b = scope_box();
    ui_fill(b.x, b.y, b.w, b.h, C_SURF);
    float w[WAVE_BUF];
    audio_lock();
    synth_wave(w, WAVE_BUF);
    audio_unlock();

    const int show = 1024;
    int start = WAVE_BUF - show;
    for (int i = WAVE_BUF - 2; i >= show; i--)
        if (w[i - 1] < 0.0f && w[i] >= 0.0f) { start = i - show + 1; break; }
    if (start < 0) start = 0;

    SDL_Color c = with_a(C_ACC, 230);
    float px = 0, py = 0;
    for (int i = 0; i < show; i++) {
        float v = clampf(w[start + i], -1, 1);
        float x = b.x + i * b.w / (show - 1);
        float y = b.y + b.h / 2 - v * (b.h / 2 - 2);
        if (i) ui_line(px, py, x, y, c);
        px = x; py = y;
    }
}

static void draw_rec_badge(float x, float y)
{
    if (!rec_active()) return;
    int sec = (int)rec_seconds();
    char t[48];
    snprintf(t, sizeof t, "ЗАПИСЬ %d:%02d", sec / 60, sec % 60);
    float w = ui_text_w(t, TS_NORMAL, FONT_BOLD) + 30;
    ui_round(x, y - 2, w, 24, 12, rgba(70, 20, 24, 255));
    if ((SDL_GetTicks() / 600) % 2) ui_circle(x + 12, y + 10, 4.5f, C_REC);
    ui_text(x + 22, y + 1, t, TS_NORMAL, FONT_BOLD, C_REC, AL_LEFT);
}

static void draw_header(void)
{
    if (scene_mode) {
        char t[160];
        snprintf(t, sizeof t, "%s%s", cur_preset, modified ? " •" : "");
        float w = ui_text(24, 18, t, TS_TITLE, FONT_BOLD, C_TEXT, AL_LEFT);
        draw_rec_badge(24 + w + 18, 22);
        ui_text(LW - 24, 22, "Esc или ⌘F — выйти из сцены   ⌘← ⌘→ — пресеты   ⌘/ — справка",
                TS_NORMAL, FONT_REGULAR, C_DIM, AL_RIGHT);
        return;
    }
    float x = 20;
    x += ui_text(x, 14, APP_NAME, TS_TITLE, FONT_BOLD, C_TEXT, AL_LEFT) + 16;
    if (input_active()) x += ui_text(x, 20, "● ИГРА — Esc для выхода", TS_NORMAL, FONT_MEDIUM, C_ACC, AL_LEFT);
    else x += ui_text(x, 20, "○ режим настройки", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
    draw_rec_badge(x + 18, 18);

    char st[220];
    if (mt_flags & MT_OK) {
        const char *cal = (PF.press_src == PS_FORCE && (PF.calibrated & CAL_FORCE)) ||
                          (PF.press_src == PS_AREA && (PF.calibrated & CAL_AREA)) ? "откалиброван" : "не откалиброван";
        snprintf(st, sizeof st, "Тачпад: мультитач,  сила нажатия %s,  нажим %s,  хаптика %s",
                 (mt_flags & MT_FORCE) ? "есть" : "нет", cal,
                 (mt_flags & MT_HAPTIC) ? "своя" : "системная");
    } else {
        snprintf(st, sizeof st, "MultitouchSupport недоступен: касания из стандартных событий, без силы нажатия");
    }
    if (PF.block_gestures) {
        int gs = plat_gestures_status();
        strncat(st, gs == GESTURES_BLOCKING ? ",  жесты macOS заглушены" :
                    gs == GESTURES_WAITING ? ",  жесты: ждём разрешения" :
                    gs == GESTURES_FAILED ? ",  жесты: не удалось" : "", sizeof st - strlen(st) - 1);
    }
    ui_text(20, 46, st, TS_SMALL, FONT_REGULAR, (mt_flags & MT_OK) ? C_FAINT : C_WARN, AL_LEFT);
}

static void button(Box b, const char *label, int kind, int id, int active)
{
    float mx, my;
    mouse_logical(&mx, &my);
    int hov = overlay == OV_NONE && !input_active() && in_box(b, mx, my);
    ui_round(b.x, b.y, b.w, b.h, 6, active ? C_ACC : (hov ? C_LINE : C_SURF2));
    ui_text(b.x + b.w / 2, b.y + (b.h - ui_text_h(TS_NORMAL)) / 2, label, TS_NORMAL,
            active ? FONT_MEDIUM : FONT_REGULAR, active ? C_DARK : C_TEXT, AL_CENTER);
    add_hit(b, kind, id);
}

static void draw_preset_bar(void)
{
    float x = panel_x(), y = 12;
    Box prev = {x, y, 30, 30}, next = {x + PANEL_W - 30, y, 30, 30};
    Box name = {x + 34, y, PANEL_W - 68, 30};
    button(prev, "‹", HIT_PREV, 0, 0);
    button(next, "›", HIT_NEXT, 0, 0);

    float mx, my;
    mouse_logical(&mx, &my);
    int hov = overlay == OV_NONE && !input_active() && in_box(name, mx, my);
    ui_round(name.x, name.y, name.w, name.h, 6, hov ? C_LINE : C_SURF2);
    char t[160];
    snprintf(t, sizeof t, "%s%s", cur_preset, modified ? " •" : "");
    ui_text(name.x + name.w / 2, name.y + 5, t, TS_LARGE, FONT_MEDIUM, C_TEXT, AL_CENTER);
    ui_text(name.x + name.w - 10, name.y + 8, "▾", TS_SMALL, FONT_REGULAR, C_DIM, AL_RIGHT);
    add_hit(name, HIT_NAME, 0);

    float b1 = 96, b3 = 84, b2 = PANEL_W - b1 - b3 - 8;
    button((Box){x, y + 36, b1, 24}, "Сохранить", HIT_SAVE, 0, 0);
    button((Box){x + b1 + 4, y + 36, b2, 24}, "Сохранить как…", HIT_SAVEAS, 0, 0);
    button((Box){x + b1 + b2 + 8, y + 36, b3, 24}, "Удалить", HIT_DELETE, 0, 0);
}

static void draw_row(int id, float y)
{
    float x = panel_x();
    const ParamInfo *pi = info(id);
    if (!pi) return;

    if (is_header(id)) {
        if (pi->label[0]) ui_text(x + 4, y + 6, pi->label, TS_SMALL, FONT_MEDIUM, C_FAINT, AL_LEFT);
        return;
    }
    Box row = {x, y, PANEL_W, ROW_H};
    float mx, my;
    mouse_logical(&mx, &my);
    int hov = overlay == OV_NONE && !input_active() && in_box(row, mx, my);
    if (hov) { ui_round(row.x, row.y, row.w, row.h, 4, C_SURF2); hover_id = id; }

    if (is_button(id)) {
        const char *label = pi->label;
        char tmp[96];
        if (id == B_REC && rec_active()) {
            int sec = (int)rec_seconds();
            snprintf(tmp, sizeof tmp, "■ Остановить запись   %d:%02d", sec / 60, sec % 60);
            label = tmp;
        }
        ui_text(x + 8, y + 2, label, TS_NORMAL, FONT_MEDIUM,
                (id == B_REC && rec_active()) ? C_REC : C_ACC, AL_LEFT);
        add_hit(row, HIT_ROW, id);
        return;
    }

    int m = sel_tab < 2 ? sel_tab : sel_manual;
    char val[64];
    param_value(id, m, val, sizeof val);
    ui_text(x + 8, y + 2, pi->label, TS_NORMAL, FONT_REGULAR, C_TEXT, AL_LEFT);
    ui_text(x + PANEL_W - 60, y + 2, val, TS_NORMAL, FONT_MEDIUM, C_ACC, AL_RIGHT);

    Box mi = {x + PANEL_W - 52, y + 1, 24, ROW_H - 2}, pl = {x + PANEL_W - 26, y + 1, 24, ROW_H - 2};
    ui_round(mi.x, mi.y, mi.w, mi.h, 4, hov && in_box(mi, mx, my) ? C_LINE : C_SURF2);
    ui_round(pl.x, pl.y, pl.w, pl.h, 4, hov && in_box(pl, mx, my) ? C_LINE : C_SURF2);
    ui_text(mi.x + 12, y + 1, "−", TS_NORMAL, FONT_REGULAR, C_TEXT, AL_CENTER);
    ui_text(pl.x + 12, y + 1, "+", TS_NORMAL, FONT_REGULAR, C_TEXT, AL_CENTER);
    /* сначала строка целиком, потом кнопки: при щелчке побеждает последнее подходящее */
    add_hit(row, HIT_ROW, id);
    add_hit(mi, HIT_MINUS, id);
    add_hit(pl, HIT_PLUS, id);
}

/* мини-регулятор «− значение +» */
static void stepper(float bx, float y, float vw, int id, const char *val, int hov_row, float mx, float my)
{
    Box mi = {bx, y + 1, 22, ROW_H - 2};
    Box pl = {bx + 22 + vw, y + 1, 22, ROW_H - 2};
    ui_round(mi.x, mi.y, mi.w, mi.h, 4, hov_row && in_box(mi, mx, my) ? C_LINE : C_SURF2);
    ui_round(pl.x, pl.y, pl.w, pl.h, 4, hov_row && in_box(pl, mx, my) ? C_LINE : C_SURF2);
    ui_text(mi.x + 11, y + 1, "−", TS_NORMAL, FONT_REGULAR, C_TEXT, AL_CENTER);
    ui_text(pl.x + 11, y + 1, "+", TS_NORMAL, FONT_REGULAR, C_TEXT, AL_CENTER);
    ui_text(bx + 22 + vw / 2, y + 2, val, TS_NORMAL, FONT_MEDIUM, C_ACC, AL_CENTER);
    add_hit((Box){bx, y, 44 + vw, ROW_H}, HIT_ROW, id);
    add_hit(mi, HIT_MINUS, id);
    add_hit(pl, HIT_PLUS, id);
}

/* субгармоника: делитель и уровень в одной строке */
static void draw_sub_row(int k, float y)
{
    float x = panel_x();
    int m = sel_tab < 2 ? sel_tab : sel_manual;
    Box row = {x, y, PANEL_W, ROW_H};
    float mx, my;
    mouse_logical(&mx, &my);
    int hov = overlay == OV_NONE && !input_active() && in_box(row, mx, my);
    int id_div = P_D1 + 2 * k, id_lvl = P_L1 + 2 * k;
    if (hov) {
        ui_round(row.x, row.y, row.w, row.h, 4, C_SURF2);
        hover_id = mx < x + 214 ? id_div : id_lvl;
    }
    char lbl[32], vd[16], vl[16];
    snprintf(lbl, sizeof lbl, "Субгармоника %d", k + 1);
    ui_text(x + 8, y + 2, lbl, TS_NORMAL, FONT_REGULAR, C_TEXT, AL_LEFT);
    param_value(id_div, m, vd, sizeof vd);
    param_value(id_lvl, m, vl, sizeof vl);
    stepper(x + 128, y, 40, id_div, vd, hov, mx, my);
    stepper(x + PANEL_W - 2 - 44 - 46, y, 46, id_lvl, vl, hov, mx, my);
}

static void draw_panel(void)
{
    float x = panel_x();
    draw_preset_bar();

    static const char *tabs[3] = {"Мануал I", "Мануал II", "Общие"};
    float tw = (PANEL_W - 8) / 3.0f, ty = 84;
    for (int i = 0; i < 3; i++) {
        Box b = {x + i * (tw + 4), ty, tw, 28};
        SDL_Color c = i < 2 ? C_MAN[i] : C_ACC;
        float mx, my;
        mouse_logical(&mx, &my);
        int hov = overlay == OV_NONE && !input_active() && in_box(b, mx, my);
        if (i == sel_tab) ui_round(b.x, b.y, b.w, b.h, 6, c);
        else ui_round(b.x, b.y, b.w, b.h, 6, hov ? C_LINE : C_SURF2);
        ui_text(b.x + b.w / 2, b.y + 6, tabs[i], TS_NORMAL, FONT_MEDIUM, i == sel_tab ? C_DARK : C_TEXT, AL_CENTER);
        add_hit(b, HIT_TAB, i);
    }

    const int *rows = sel_tab < 2 ? rows_manual : rows_general;
    int nrows = sel_tab < 2 ? (int)(sizeof rows_manual / sizeof(int)) : (int)(sizeof rows_general / sizeof(int));
    float y = ty + 36;
    for (int i = 0; i < nrows; i++) {
        int id = rows[i];
        if (id == B_GESTPERM && !(PF.block_gestures && plat_gestures_status() == GESTURES_WAITING)) continue;
        if (is_header(id)) {
            if (i > 0) y += 6;
            draw_row(id, y);
            y += 20;
        } else if (is_subrow(id)) {
            draw_sub_row(id - R_SUB1, y);
            y += ROW_H;
        } else {
            draw_row(id, y);
            y += ROW_H;
        }
    }

    /* описание параметра под курсором */
    float dy = LH - 104;
    ui_line(x, dy - 8, x + PANEL_W, dy - 8, C_SURF2);
    const ParamInfo *pi = hover_id >= 0 ? info(hover_id) : NULL;
    if (pi && pi->desc) ui_text_wrap(x + 4, dy, PANEL_W - 8, pi->desc, TS_SMALL, FONT_REGULAR, C_DIM);
    else ui_text_wrap(x + 4, dy, PANEL_W - 8,
                      "Наведите курсор на параметр — здесь появится описание. "
                      "Колесо мыши или два пальца — менять значение.",
                      TS_SMALL, FONT_REGULAR, C_FAINT);
}

static void draw_footer(void)
{
    Box s = scope_box();
    float y = s.y + s.h + 16;
    ui_text(20, y, "Пробел — играть   ⌘F — сцена   ⌘R — запись   ⌘/ — справка   ⌘S — сохранить пресет",
            TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
}

static void draw_toast(void)
{
    if (SDL_GetTicks() > toast_until || !toast_msg[0]) return;
    Box pr = pad_box();
    float w = ui_text_w(toast_msg, TS_NORMAL, FONT_MEDIUM) + 32;
    float x = pr.x + (pr.w - w) / 2, y = pr.y + pr.h - 50;
    ui_round(x, y, w, 32, 10, rgba(12, 11, 16, 235));
    ui_text(x + w / 2, y + 7, toast_msg, TS_NORMAL, FONT_MEDIUM, C_TEXT, AL_CENTER);
}

/* ---- оверлеи ---- */
static void dim_screen(void) { ui_fill(0, 0, LW, LH, rgba(8, 8, 12, 190)); }

static Box card(float w, float h)
{
    Box b = {(LW - w) / 2, (LH - h) / 2, w, h};
    ui_round(b.x, b.y, b.w, b.h, 14, rgba(32, 30, 38, 252));
    ui_round(b.x, b.y, b.w, 3, 2, C_ACC);
    return b;
}

static void draw_help(void)
{
    dim_screen();
    Box b = card(860, 585);
    float x = b.x + 32, y = b.y + 26;
    ui_text(x, y, "Как играть", TS_TITLE, FONT_BOLD, C_TEXT, AL_LEFT);
    ui_text(b.x + b.w - 32, y + 6, "Esc или ⌘/ — закрыть", TS_NORMAL, FONT_REGULAR, C_DIM, AL_RIGHT);
    y += 44;
    y += ui_text_wrap(x, y, b.w - 64,
        "Пробел включает игру: курсор пропадает, тачпад становится инструментом. Верхняя половина — "
        "мануал I, нижняя — мануал II. Положение пальца слева направо — высота, сила нажатия — громкость "
        "и яркость. Покачивайте пальцем — получится вибрато. На каждой ноте тачпад слегка щёлкает.",
        TS_NORMAL, FONT_REGULAR, C_DIM) + 14;

    static const char *col1[][2] = {
        {"Пробел", "играть / настраивать"},
        {"Esc", "выйти из игры или сцены"},
        {"⌘F", "сцена: на весь экран"},
        {"Tab", "мануал I / II / общие"},
        {"1 – 5", "гласная A E I O U"},
        {"0", "без гласной"},
        {"6", "голос формант"},
        {"[  ]", "октава вниз / вверх"},
        {"−  =", "диапазон"},
        {"↑ ↓", "яркость"},
        {"← →", "резонанс"},
        {"P", "моно / поли"},
        {"W", "притяжение к нотам"},
        {"⌘R", "запись в WAV"},
    };
    static const char *col2[][2] = {
        {"E", "портаменто"},
        {"T / Shift+T", "лад / тоника"},
        {"Y", "раскладка: 2 или 1 мануал"},
        {"R", "реверберация"},
        {"H", "хаптика"},
        {"D", "источник нажима"},
        {"7 / 8", "полное нажатие ± "},
        {"⌘← ⌘→", "предыдущий / следующий пресет"},
        {"⌘S / ⇧⌘S", "сохранить / сохранить как"},
        {"⌘Z", "вернуть предыдущее звучание"},
        {"⌘K", "калибровка нажима"},
        {"A Z S X … / '", "ноты с клавиатуры (G#–C#)"},
        {"⌘/", "эта справка"},
        {"⌘Q", "выход"},
    };
    float cy = y;
    for (int i = 0; i < 14; i++) {
        ui_text(x, cy, col1[i][0], TS_NORMAL, FONT_MEDIUM, C_ACC, AL_LEFT);
        ui_text(x + 110, cy, col1[i][1], TS_NORMAL, FONT_REGULAR, C_TEXT, AL_LEFT);
        ui_text(x + 410, cy, col2[i][0], TS_NORMAL, FONT_MEDIUM, C_ACC, AL_LEFT);
        ui_text(x + 530, cy, col2[i][1], TS_NORMAL, FONT_REGULAR, C_TEXT, AL_LEFT);
        cy += 25;
    }
    cy += 10;
    ui_text_wrap(x, cy, b.w - 64,
        "Совет: чтобы аккорды тремя-четырьмя пальцами не вызывали Mission Control, отключите эти жесты "
        "в Системных настройках → Трекпад → Другие жесты (или попробуйте «Блокировать жесты» во вкладке «Общие»).",
        TS_SMALL, FONT_REGULAR, C_DIM);
}

static void draw_presets_list(void)
{
    dim_screen();
    float x = panel_x(), y = 46, w = PANEL_W, h = LH - 80;
    ui_round(x - 6, y, w + 12, h, 12, rgba(32, 30, 38, 252));
    int n = presets_count();
    float ih = 28;
    float content = n * ih + 40;
    float maxs = content > h - 16 ? content - (h - 16) : 0;
    preset_scroll = clampf(preset_scroll, 0, maxs);

    float mx, my;
    mouse_logical(&mx, &my);
    float cy = y + 10 - preset_scroll;
    int shown_user_header = 0;
    ui_text(x + 6, cy, "ЗАВОДСКИЕ", TS_SMALL, FONT_MEDIUM, C_FAINT, AL_LEFT);
    cy += 22;
    for (int i = 0; i < n; i++) {
        if (!preset_is_factory(i) && !shown_user_header) {
            cy += 8;
            if (cy > y && cy < y + h - 20) ui_text(x + 6, cy, "МОИ", TS_SMALL, FONT_MEDIUM, C_FAINT, AL_LEFT);
            cy += 22;
            shown_user_header = 1;
        }
        Box it = {x, cy, w, ih - 2};
        if (cy > y && cy + ih < y + h) {
            int cur = !strcmp(preset_name(i), cur_preset);
            int hov = in_box(it, mx, my);
            if (cur || hov) ui_round(it.x, it.y, it.w, it.h, 6, cur ? with_a(C_ACC, 60) : C_SURF2);
            ui_text(it.x + 10, it.y + 5, preset_name(i), TS_NORMAL, cur ? FONT_MEDIUM : FONT_REGULAR,
                    C_TEXT, AL_LEFT);
            add_hit(it, HIT_LIST_ITEM, i);
        }
        cy += ih;
    }
    if (!shown_user_header) {
        cy += 8;
        ui_text(x + 6, cy, "Своих пресетов пока нет — ⇧⌘S сохранит текущее звучание.",
                TS_SMALL, FONT_REGULAR, C_FAINT, AL_LEFT);
    }
}

static void draw_name_entry(void)
{
    dim_screen();
    Box b = card(520, 170);
    ui_text(b.x + 28, b.y + 24, "Сохранить пресет как", TS_LARGE, FONT_BOLD, C_TEXT, AL_LEFT);
    Box f = {b.x + 28, b.y + 64, b.w - 56, 36};
    ui_round(f.x, f.y, f.w, f.h, 8, C_BG);
    float tw = ui_text(f.x + 12, f.y + 8, name_buf, TS_LARGE, FONT_REGULAR, C_TEXT, AL_LEFT);
    if ((SDL_GetTicks() / 500) % 2) ui_fill(f.x + 13 + tw, f.y + 8, 1.5f, 20, C_ACC);
    ui_text(b.x + 28, b.y + 120, "Enter — сохранить    Esc — отмена", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
}

static void draw_confirm(void)
{
    dim_screen();
    Box b = card(520, 150);
    char t[160];
    snprintf(t, sizeof t, "Удалить пресет «%s»?", cur_preset);
    ui_text(b.x + 28, b.y + 26, t, TS_LARGE, FONT_BOLD, C_TEXT, AL_LEFT);
    ui_text(b.x + 28, b.y + 64, "Файл будет удалён. Текущее звучание останется.", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
    ui_text(b.x + 28, b.y + 104, "Enter — удалить    Esc — отмена", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
}

static void draw_welcome(void)
{
    dim_screen();
    Box b = card(640, 300);
    float x = b.x + 32, y = b.y + 28;
    ui_text(x, y, "Добро пожаловать в Траутониум", TS_TITLE, FONT_BOLD, C_TEXT, AL_LEFT);
    y += 46;
    y += ui_text_wrap(x, y, b.w - 64,
        "Тачпад — это две «проволоки» инструмента Оскара Залы. Где палец — там высота тона, "
        "как сильно нажимаете — так громко и ярко звучит.", TS_NORMAL, FONT_REGULAR, C_DIM) + 12;
    y += ui_text_wrap(x, y, b.w - 64,
        "Сначала стоит откалибровать нажим под вашу руку: это 20 секунд — коснуться легко, потом нажать сильно.",
        TS_NORMAL, FONT_REGULAR, C_TEXT) + 26;
    ui_text(x, y, "Пробел — откалибровать", TS_LARGE, FONT_MEDIUM, C_ACC, AL_LEFT);
    ui_text(x, y + 30, "Esc — позже (⌘K в любой момент)", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
    ui_text(x, y + 56, "⌘/ — справка по управлению", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
}

static void draw_level_bar(float x, float y, float w, float h, float v, SDL_Color c)
{
    ui_round(x, y, w, h, h / 2, C_BG);
    if (v > 0) ui_round(x, y, fmaxf(h, w * clampf(v, 0, 1)), h, h / 2, c);
}

static void draw_calibration(void)
{
    dim_screen();
    Box b = card(640, 330);
    float x = b.x + 32, y = b.y + 26;
    const char *src = cal.src == PS_FORCE ? "сила, г" : "площадь касания";
    char t[200];

    ui_text(x, y, "Калибровка нажима", TS_TITLE, FONT_BOLD, C_TEXT, AL_LEFT);
    static const char *steps[] = {"", "Шаг 1 из 2", "Шаг 1 из 2 ✓", "Шаг 2 из 2", "Проверка"};
    ui_text(b.x + b.w - 32, y + 6, steps[cal.step], TS_NORMAL, FONT_REGULAR, C_DIM, AL_RIGHT);
    y += 50;

    MTFrame f;
    int have = mt_read(&f, 0) && f.count > 0 && mt_now() - f.time < 0.3;
    float live = 0;
    if (have) for (int i = 0; i < f.count; i++) {
        float v = cal.src == PS_FORCE ? f.t[i].pressure : f.t[i].area;
        if (v > live) live = v;
    }

    switch (cal.step) {
    case CS_INTRO:
        y += ui_text_wrap(x, y, b.w - 64,
            "Программа запомнит, как вы касаетесь тачпада легко и как нажимаете сильно, и растянет "
            "громкость между этими двумя точками. Курсор на время спрячется.",
            TS_NORMAL, FONT_REGULAR, C_DIM) + 20;
        snprintf(t, sizeof t, "Измеряем: %s", src);
        ui_text(x, y, t, TS_NORMAL, FONT_REGULAR, C_FAINT, AL_LEFT);
        ui_text(x, b.y + b.h - 56, "Пробел — начать    Esc — отмена", TS_LARGE, FONT_MEDIUM, C_ACC, AL_LEFT);
        break;

    case CS_LIFT:
        if (cal.lift_next == CS_HARD) {
            if (cal.src == PS_FORCE) snprintf(t, sizeof t, "Лёгкое касание запомнено: %.0f г", cal.light);
            else snprintf(t, sizeof t, "Лёгкое касание запомнено: %.3f", cal.light);
            ui_text(x, y, t, TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
            y += 34;
        }
        ui_text(x, y, "Уберите палец с тачпада", TS_TITLE, FONT_BOLD, C_ACC, AL_LEFT);
        y += 40;
        ui_text(x, y, cal.lift_next == CS_HARD ? "Следующий шаг начнётся, когда тачпад будет свободен."
                                               : "Калибровка начнётся заново, когда тачпад будет свободен.",
                TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
        y += 40;
        ui_text(x, b.y + b.h - 50, "Esc — отмена", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
        break;

    case CS_LIGHT:
    case CS_HARD:
        if (cal.error[0]) {
            y += ui_text_wrap(x, y, b.w - 64, cal.error, TS_NORMAL, FONT_MEDIUM, C_WARN) + 10;
        }
        ui_text(x, y, cal.step == CS_LIGHT ? "Положите один палец и касайтесь едва-едва"
                                           : "Теперь нажмите сильно — как в самом громком месте",
                TS_LARGE, FONT_MEDIUM, C_TEXT, AL_LEFT);
        y += 30;
        ui_text(x, y, cal.step == CS_LIGHT ? "Держите палец на тачпаде, пока полоса не заполнится."
                                           : "Давите уверенно, но без усилия «до щелчка». Полоса растёт, только пока нажатие сильное.",
                TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
        y += 44;
        draw_level_bar(x, y, b.w - 64, 12, cal.contact_s / CAL_NEED_S, C_ACC);
        y += 30;
        if (cal.src == PS_FORCE) snprintf(t, sizeof t, "Сейчас: %.0f г", have ? live : 0.0f);
        else snprintf(t, sizeof t, "Сейчас: %.3f", have ? live : 0.0f);
        ui_text(x, y, t, TS_NORMAL, FONT_REGULAR, have ? C_TEXT : C_FAINT, AL_LEFT);
        if (cal.step == CS_HARD) {
            if (cal.src == PS_FORCE) snprintf(t, sizeof t, "Лёгкое касание: %.0f г", cal.light);
            else snprintf(t, sizeof t, "Лёгкое касание: %.3f", cal.light);
            ui_text(x + 220, y, t, TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
            if (have && cal.too_light)
                ui_text(x + b.w - 64, y, "Сильнее!", TS_NORMAL, FONT_BOLD, C_WARN, AL_RIGHT);
        }
        ui_text(x, b.y + b.h - 50, "Esc — отмена", TS_NORMAL, FONT_REGULAR, C_DIM, AL_LEFT);
        break;

    case CS_CHECK: {
        if (cal.src == PS_FORCE) snprintf(t, sizeof t, "Готово: лёгкое касание %.0f г, полное нажатие %.0f г", cal.light, cal.hard);
        else snprintf(t, sizeof t, "Готово: лёгкое касание %.3f, полное нажатие %.3f", cal.light, cal.hard);
        ui_text(x, y, t, TS_LARGE, FONT_MEDIUM, C_TEXT, AL_LEFT);
        y += 32;
        y += ui_text_wrap(x, y, b.w - 64,
            "Понажимайте в разных местах тачпада: полоса показывает громкость. Одинаковое усилие должно давать похожий уровень.",
            TS_NORMAL, FONT_REGULAR, C_DIM) + 14;
        int n = have ? (f.count < 3 ? f.count : 3) : 0;
        for (int i = 0; i < 3; i++) {
            float v = 0, raw = 0;
            if (i < n) { raw = cal.src == PS_FORCE ? f.t[i].pressure : f.t[i].area;
                         v = synth_press_norm(f.t[i].pressure, f.t[i].area); }
            draw_level_bar(x, y, b.w - 200, 12, i < n ? v : 0, i < n ? C_ACC : C_SURF2);
            if (i < n) {
                if (cal.src == PS_FORCE) snprintf(t, sizeof t, "%.0f г · %d%%", raw, (int)lroundf(v * 100));
                else snprintf(t, sizeof t, "%.3f · %d%%", raw, (int)lroundf(v * 100));
                ui_text(x + b.w - 186, y - 3, t, TS_NORMAL, FONT_REGULAR, C_TEXT, AL_LEFT);
            }
            y += 24;
        }
        ui_text(x, b.y + b.h - 56, "Пробел — сохранить    R — заново    Esc — отменить",
                TS_LARGE, FONT_MEDIUM, C_ACC, AL_LEFT);
    } break;
    }
}

static void draw(void)
{
    nhits = 0;
    hover_id = -1;
    ui_fill(0, 0, LW, LH, C_BG);

    audio_lock();
    snap_n = synth_snapshot(snap_t, MAX_IN);
    audio_unlock();
    haptics_update();

    draw_header();
    draw_pad();
    if (!scene_mode) {
        draw_scope();
        draw_footer();
        draw_panel();
    }
    draw_toast();

    if (overlay != OV_NONE) nhits = 0;     /* под оверлеем ничего не нажимается */
    switch (overlay) {
        case OV_HELP:     draw_help(); break;
        case OV_PRESETS:  draw_presets_list(); break;
        case OV_NAME:     draw_name_entry(); break;
        case OV_CONFIRM:  draw_confirm(); break;
        case OV_WELCOME:  draw_welcome(); break;
        case OV_CALIB:    draw_calibration(); break;
    }
}

/* ===================================================================== */
/*                                ввод                                   */
/* ===================================================================== */
/* физические клавиши: работает при любой раскладке, в том числе русской */
/* нижний ряд — белые клавиши, средний — чёрные, как в исходной программе;
   число — полутонов от левого края мануала */
static const struct { SDL_Scancode sc; int semis; } note_keys[NUM_NOTE_KEYS] = {
    {SDL_SCANCODE_A, -1},                        /* G# перед A (на русской раскладке — «ф») */
    {SDL_SCANCODE_Z, 0},  {SDL_SCANCODE_S, 1},  {SDL_SCANCODE_X, 2},  {SDL_SCANCODE_C, 3},
    {SDL_SCANCODE_F, 4},  {SDL_SCANCODE_V, 5},  {SDL_SCANCODE_G, 6},  {SDL_SCANCODE_B, 7},
    {SDL_SCANCODE_N, 8},  {SDL_SCANCODE_J, 9},  {SDL_SCANCODE_M, 10}, {SDL_SCANCODE_K, 11},
    {SDL_SCANCODE_COMMA, 12}, {SDL_SCANCODE_L, 13}, {SDL_SCANCODE_PERIOD, 14},
    {SDL_SCANCODE_SLASH, 15},
    {SDL_SCANCODE_APOSTROPHE, 16},               /* C# над «/» (на русской раскладке — «э») */
};

static void select_tab(int t)
{
    sel_tab = clampi(t, 0, 2);
    if (sel_tab < 2) {
        sel_manual = sel_tab;
        audio_lock();
        synth_set_selected_manual(sel_manual);
        audio_unlock();
    }
}

static void octave(int dir)
{
    audio_lock();
    P.man[sel_manual].low_note = clampi(P.man[sel_manual].low_note + 12 * dir, 24, 96);
    audio_unlock();
    patch_changed();
}

static void set_vowel(int v)
{
    audio_lock();
    P.man[sel_manual].vowel = v;
    audio_unlock();
    patch_changed();
}

static void handle_overlay_key(SDL_Scancode sc, SDL_Keymod mod)
{
    int cmd = (mod & KMOD_GUI) != 0;
    switch (overlay) {
    case OV_HELP:
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_SLASH || sc == SDL_SCANCODE_F1 || sc == SDL_SCANCODE_RETURN)
            close_overlay();
        break;
    case OV_PRESETS:
        if (sc == SDL_SCANCODE_ESCAPE) close_overlay();
        break;
    case OV_CONFIRM:
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) { do_delete(); update_input(); }
        else if (sc == SDL_SCANCODE_ESCAPE) close_overlay();
        break;
    case OV_NAME:
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
            if (name_buf[0]) { finish_save_as(); update_input(); }
        } else if (sc == SDL_SCANCODE_ESCAPE) close_overlay();
        else if (sc == SDL_SCANCODE_BACKSPACE) {
            int n = (int)strlen(name_buf);
            if (cmd) n = 0;
            else {
                while (n > 0 && ((unsigned char)name_buf[n - 1] & 0xC0) == 0x80) n--;   /* UTF-8 */
                if (n > 0) n--;
            }
            name_buf[n] = 0;
        }
        break;
    case OV_WELCOME:
        if (sc == SDL_SCANCODE_SPACE) { overlay = OV_NONE; start_calibration(); }
        else if (sc == SDL_SCANCODE_ESCAPE) close_overlay();
        else if (sc == SDL_SCANCODE_SLASH || sc == SDL_SCANCODE_F1) open_overlay(OV_HELP);
        break;
    case OV_CALIB:
        if (sc == SDL_SCANCODE_ESCAPE) cal_finish(0);
        else if (cal.step == CS_INTRO && sc == SDL_SCANCODE_SPACE) { cal.error[0] = 0; cal_next_step(CS_LIGHT); }
        else if (cal.step == CS_CHECK && sc == SDL_SCANCODE_SPACE) cal_finish(1);
        else if (cal.step == CS_CHECK && sc == SDL_SCANCODE_R) { cal.error[0] = 0; cal_wait_lift(CS_LIGHT); }
        break;
    }
}

static void handle_key(SDL_Scancode sc, SDL_Keymod mod, int repeat)
{
    int cmd = (mod & KMOD_GUI) != 0;
    int shift = (mod & KMOD_SHIFT) != 0;

    if (overlay != OV_NONE) { if (!repeat) handle_overlay_key(sc, mod); return; }

    /* стрелки можно держать */
    if (!cmd) {
        if (sc == SDL_SCANCODE_UP)    { param_step(P_CUTOFF, sel_manual, +1); return; }
        if (sc == SDL_SCANCODE_DOWN)  { param_step(P_CUTOFF, sel_manual, -1); return; }
        if (sc == SDL_SCANCODE_RIGHT) { param_step(P_RES, sel_manual, +1); return; }
        if (sc == SDL_SCANCODE_LEFT)  { param_step(P_RES, sel_manual, -1); return; }
    }
    if (repeat) return;

    if (cmd) {
        switch (sc) {
            case SDL_SCANCODE_S:     if (shift) begin_save_as(); else save_preset(); break;
            case SDL_SCANCODE_Z:     undo(); break;
            case SDL_SCANCODE_K:     start_calibration(); break;
            case SDL_SCANCODE_R:     toggle_recording(); break;
            case SDL_SCANCODE_LEFT:  step_preset(-1); break;
            case SDL_SCANCODE_RIGHT: step_preset(+1); break;
            case SDL_SCANCODE_F:     set_scene(!scene_mode); break;
            case SDL_SCANCODE_SLASH: open_overlay(OV_HELP); break;   /* ⌘/ и ⌘? */
            default: break;
        }
        return;
    }

    if (sc == SDL_SCANCODE_SLASH && shift) { open_overlay(OV_HELP); return; }

    for (int i = 0; i < NUM_NOTE_KEYS; i++) {
        if (sc == note_keys[i].sc) {
            audio_lock();
            synth_key(i, 1, sel_manual, note_keys[i].semis);
            audio_unlock();
            return;
        }
    }

    switch (sc) {
        case SDL_SCANCODE_SPACE:  set_play(!play_mode); break;
        case SDL_SCANCODE_ESCAPE:
            if (scene_mode) set_scene(0);
            else if (play_mode) set_play(0);
            break;
        case SDL_SCANCODE_F1:     open_overlay(OV_HELP); break;
        case SDL_SCANCODE_TAB:    select_tab(shift ? (sel_tab + 2) % 3 : (sel_tab + 1) % 3); break;
        case SDL_SCANCODE_1: case SDL_SCANCODE_2: case SDL_SCANCODE_3:
        case SDL_SCANCODE_4: case SDL_SCANCODE_5:
            set_vowel((int)(sc - SDL_SCANCODE_1)); break;
        case SDL_SCANCODE_0:      set_vowel(-1); break;
        case SDL_SCANCODE_6:      param_step(P_REG, sel_manual, +1); break;
        case SDL_SCANCODE_LEFTBRACKET:  octave(-1); break;
        case SDL_SCANCODE_RIGHTBRACKET: octave(+1); break;
        case SDL_SCANCODE_MINUS:  param_step(P_SPAN, sel_manual, -1); break;
        case SDL_SCANCODE_EQUALS: param_step(P_SPAN, sel_manual, +1); break;
        case SDL_SCANCODE_P:      param_step(P_MODE, sel_manual, +1); break;
        case SDL_SCANCODE_W:      param_step(P_SNAP, sel_manual, +1); break;
        case SDL_SCANCODE_E: {
            ManualParams *M = &P.man[sel_manual];
            int before = M->glide_ms;
            param_step(P_GLIDE, sel_manual, +1);
            if (M->glide_ms == before) { audio_lock(); M->glide_ms = 0; audio_unlock(); }
        } break;
        case SDL_SCANCODE_T:      param_step(shift ? G_TONIC : G_SCALE, sel_manual, +1); break;
        case SDL_SCANCODE_Y:      param_step(G_LAYOUT, sel_manual, +1); break;
        case SDL_SCANCODE_H:      param_step(G_HAPTIC, sel_manual, +1); break;
        case SDL_SCANCODE_R: {
            static const float steps[] = {0.0f, 0.15f, 0.30f, 0.45f, 0.60f, 0.75f, 0.90f};
            float r = 0.0f;
            for (int i = 0; i < 7; i++) if (steps[i] > P.reverb + 0.01f) { r = steps[i]; break; }
            audio_lock(); P.reverb = r; audio_unlock();
            patch_changed();
        } break;
        case SDL_SCANCODE_D:      param_step(G_PSRC, sel_manual, +1); break;
        case SDL_SCANCODE_7:      param_step(G_SENS, sel_manual, +1); break;
        case SDL_SCANCODE_8:      param_step(G_SENS, sel_manual, -1); break;
        default: break;
    }
}

static void handle_key_up(SDL_Scancode sc)
{
    for (int i = 0; i < NUM_NOTE_KEYS; i++)
        if (sc == note_keys[i].sc) { audio_lock(); synth_key(i, 0, sel_manual, note_keys[i].semis); audio_unlock(); }
}

#define OWNER_MOUSE 3000

static void mouse_touch(float x, float y)
{
    Box pr = pad_box();
    float nx = clampf((x - pr.x) / pr.w, 0, 1);
    float ny = clampf((y - pr.y) / pr.h, 0, 0.999f);
    float local = P.split ? fmodf(ny * 2.0f, 1.0f) : ny;   /* ниже в полосе — громче */
    audio_lock();
    synth_fb_upsert(OWNER_MOUSE, nx, ny, clampf(local * 1.1f, 0.05f, 1.0f));
    audio_unlock();
}

static void handle_click(float x, float y)
{
    if (overlay == OV_PRESETS) {
        for (int i = 0; i < nhits; i++)
            if (hits[i].kind == HIT_LIST_ITEM && in_box(hits[i].b, x, y)) {
                load_preset(hits[i].id);
                close_overlay();
                return;
            }
        close_overlay();
        return;
    }
    if (overlay == OV_HELP) { close_overlay(); return; }
    if (overlay != OV_NONE) return;

    for (int i = nhits - 1; i >= 0; i--) {
        Hit *h = &hits[i];
        if (!in_box(h->b, x, y)) continue;
        switch (h->kind) {
            case HIT_MINUS:  param_step(h->id, sel_tab < 2 ? sel_tab : sel_manual, -1); return;
            case HIT_PLUS:
            case HIT_ROW:    param_step(h->id, sel_tab < 2 ? sel_tab : sel_manual, +1); return;
            case HIT_TAB:    select_tab(h->id); return;
            case HIT_PREV:   step_preset(-1); return;
            case HIT_NEXT:   step_preset(+1); return;
            case HIT_NAME:   preset_scroll = 0; open_overlay(OV_PRESETS); return;
            case HIT_SAVE:   save_preset(); return;
            case HIT_SAVEAS: begin_save_as(); return;
            case HIT_DELETE: ask_delete(); return;
            case HIT_HELP:   open_overlay(OV_HELP); return;
        }
    }
    Box pr = pad_box();
    if (in_box(pr, x, y)) {
        mouse_down_pad = 1;
        mouse_touch(x, y);
    }
}

static void handle_wheel(int dir, float fy)
{
    float x, y;
    mouse_logical(&x, &y);
    if (overlay == OV_PRESETS) { preset_scroll -= fy * 28; return; }
    if (overlay != OV_NONE) return;
    for (int i = 0; i < nhits; i++)
        if (hits[i].kind == HIT_ROW && in_box(hits[i].b, x, y) && !is_button(hits[i].id)) {
            param_step(hits[i].id, sel_tab < 2 ? sel_tab : sel_manual, dir);
            return;
        }
}

/* ===================================================================== */
/*                                main                                   */
/* ===================================================================== */
/* источник нажима, выбранный пользователем, если на этом запуске его пришлось
   заменить (нет датчика силы) — в настройки пишем его, а не замену */
static int user_press_src = -1;

/* ===================================================================== */
/*                         аудиовыход и запись                            */
/* ===================================================================== */
static int audio_fallback;     /* выбранное устройство пропало — играем через системное */

static void open_audio(void)
{
    if (adev) { SDL_CloseAudioDevice(adev); adev = 0; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = SR;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 256;
    want.callback = synth_audio_callback;
    audio_fallback = 0;
    if (PF.audio_out[0]) {
        adev = SDL_OpenAudioDevice(PF.audio_out, 0, &want, &have, 0);
        if (!adev) audio_fallback = 1;
    }
    if (!adev) adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) { fprintf(stderr, "Не удалось открыть аудиоустройство: %s\n", SDL_GetError()); return; }
    synth_set_input_active(input_active());
    SDL_PauseAudioDevice(adev, 0);
}

static void audio_out_label(char *b, int n)
{
    if (!PF.audio_out[0]) { snprintf(b, n, "Системный"); return; }
    /* длинные имена устройств обрезаем по границе символа */
    char tmp[40];
    int len = (int)strlen(PF.audio_out), cut = len;
    if (cut > 22) {
        cut = 22;
        while (cut > 0 && ((unsigned char)PF.audio_out[cut] & 0xC0) == 0x80) cut--;
    }
    memcpy(tmp, PF.audio_out, cut);
    tmp[cut] = 0;
    snprintf(b, n, "%s%s%s", audio_fallback ? "(нет) " : "", tmp, cut < len ? "…" : "");
}

static void step_audio_out(int dir)
{
    int n = SDL_GetNumAudioDevices(0);
    int cur = -1;                                   /* -1 — системный */
    for (int i = 0; i < n; i++) {
        const char *nm = SDL_GetAudioDeviceName(i, 0);
        if (nm && PF.audio_out[0] && !strcmp(nm, PF.audio_out)) cur = i;
    }
    cur += dir;
    if (cur < -1) cur = n - 1;
    if (cur >= n) cur = -1;
    const char *nm = cur >= 0 ? SDL_GetAudioDeviceName(cur, 0) : NULL;
    snprintf(PF.audio_out, sizeof PF.audio_out, "%s", nm ? nm : "");
    open_audio();
    touch_settings();
}

static void toggle_recording(void)
{
    if (rec_active()) {
        rec_stop();
        const char *p = rec_last_path();
        const char *name = strrchr(p, '/');
        char msg[300];
        snprintf(msg, sizeof msg, "Записано: %s — Музыка → Trautonium", name ? name + 1 : p);
        toast(msg);
    } else if (rec_start()) {
        toast("Запись пошла — ⌘R ещё раз, чтобы остановить");
    } else {
        toast("Не удалось создать файл записи");
    }
}

static void autosave(int force)
{
    if (!settings_dirty) return;
    if (!force && SDL_GetTicks() - last_change_ms < 1500) return;
    Prefs out = PF;
    if (user_press_src >= 0 && PF.press_src != user_press_src) {
        int forced_now = (!(mt_flags & MT_OK) && PF.press_src == PS_FIXED) ||
                         (!(mt_flags & MT_FORCE) && PF.press_src == PS_AREA);
        if (forced_now) out.press_src = user_press_src;
        else user_press_src = -1;             /* пользователь выбрал сам */
    }
    settings_save(&out, &P, cur_preset, modified);
    settings_dirty = 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_StopTextInput();

    presets_init();
    int had_settings = settings_load(&PF, &P, cur_preset, sizeof cur_preset, &modified);

    mt_flags = mt_start();
    if (!(mt_flags & MT_OK) && PF.press_src != PS_FIXED) {
        user_press_src = PF.press_src;
        PF.press_src = PS_FIXED;
    } else if ((mt_flags & MT_OK) && !(mt_flags & MT_FORCE) && PF.press_src == PS_FORCE) {
        user_press_src = PF.press_src;
        PF.press_src = PS_AREA;
    }

    win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1040, 790,
                           SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, 900, 640);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    ui_init(ren);
    update_layout();

    synth_init(mt_flags);
    synth_set_selected_manual(sel_manual);

    open_audio();

    plat_gestures_want(PF.block_gestures);
    if (!had_settings) { overlay = OV_WELCOME; settings_dirty = 1; }

    int run = 1;
    SDL_Event e;
    while (run) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: run = 0; break;

            case SDL_AUDIODEVICEREMOVED:
                if (!e.adevice.iscapture && e.adevice.which == adev) {
                    open_audio();
                    toast("Аудиоустройство отключено — звук идёт через системный выход");
                }
                break;
            case SDL_AUDIODEVICEADDED:
                /* вернулось выбранное устройство — переключаемся обратно */
                if (!e.adevice.iscapture && audio_fallback && PF.audio_out[0]) {
                    open_audio();
                    if (!audio_fallback) toast("Выбранный аудиовыход снова доступен");
                }
                break;

            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    focused = 0;
                    audio_lock();
                    synth_keys_clear();
                    synth_fb_clear();
                    audio_unlock();
                    mouse_down_pad = 0;
                    update_input();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    focused = 1;
                    update_input();
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                    update_layout();
                    break;
                }
                break;

            case SDL_KEYDOWN:
                handle_key(e.key.keysym.scancode, (SDL_Keymod)e.key.keysym.mod, e.key.repeat);
                break;
            case SDL_KEYUP:
                handle_key_up(e.key.keysym.scancode);
                break;
            case SDL_TEXTINPUT:
                if (overlay == OV_NAME && strlen(name_buf) + strlen(e.text.text) < sizeof name_buf - 1)
                    strcat(name_buf, e.text.text);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (!input_active() && overlay != OV_CALIB && e.button.button == SDL_BUTTON_LEFT) {
                    float x, y;
                    mouse_logical(&x, &y);
                    handle_click(x, y);
                }
                break;
            case SDL_MOUSEMOTION:
                if (!input_active() && mouse_down_pad) {
                    float x, y;
                    mouse_logical(&x, &y);
                    mouse_touch(x, y);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT && mouse_down_pad) {
                    mouse_down_pad = 0;
                    audio_lock(); synth_fb_remove(OWNER_MOUSE); audio_unlock();
                }
                break;
            case SDL_MOUSEWHEEL:
                if (!input_active()) {
                    float fy = e.wheel.preciseY;
                    if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) fy = -fy;
                    if (overlay == OV_PRESETS) handle_wheel(0, fy);
                    else if (e.wheel.y != 0) handle_wheel((e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y) > 0 ? 1 : -1, fy);
                }
                break;

            /* запасной вариант, если MultitouchSupport не загрузился */
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
                if (input_active() && !(mt_flags & MT_OK)) {
                    int id = 2000 + (int)(e.tfinger.fingerId & 0x1ff);
                    audio_lock();
                    synth_fb_upsert(id, e.tfinger.x, e.tfinger.y, 0.75f);
                    audio_unlock();
                }
                break;
            case SDL_FINGERUP:
                if (!(mt_flags & MT_OK)) {
                    int id = 2000 + (int)(e.tfinger.fingerId & 0x1ff);
                    audio_lock(); synth_fb_remove(id); audio_unlock();
                }
                break;
            }
        }

        if (overlay == OV_CALIB) cal_update();
        rec_poll();
        plat_gestures_poll();
        gestures_apply();
        update_layout();
        draw();
        SDL_RenderPresent(ren);
        ui_frame_done();
        autosave(0);
        SDL_Delay(1);    /* vsync держит ~60 кадров/с */
    }

    rec_stop();
    autosave(1);
    plat_gestures_block(0);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (adev) SDL_CloseAudioDevice(adev);
    mt_stop();
    ui_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
