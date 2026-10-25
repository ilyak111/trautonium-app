/*
 * platform.h — то, что зависит от macOS: системные шрифты, папки,
 * блокировка системных жестов. Реализация — platform_mac.m.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- текст системным шрифтом (SF Pro) ---- */
enum { FONT_REGULAR = 0, FONT_MEDIUM = 1, FONT_BOLD = 2 };

typedef struct {
    int      w, h;           /* размер в пикселях */
    float    lw, lh;         /* размер в логических единицах (пт) */
    uint8_t *alpha;          /* w*h, покрытие 0..255 (текст белый) */
} TextBitmap;

/* 1 — успешно; bitmap освобождать через plat_text_free */
int  plat_text_raster(const char *utf8, float size_pt, int weight, float scale, TextBitmap *out);
void plat_text_free(TextBitmap *b);

/* ~/Library/Application Support/Trautonium */
const char *plat_support_dir(void);

/* ~/Music/Trautonium — сюда пишутся записи */
const char *plat_music_dir(void);

/* ---- системные жесты (Mission Control, рабочие столы) ----
 * Пока идёт игра, жесты macOS тремя-четырьмя пальцами можно заглушить.
 * Для этого нужно разрешение «Универсальный доступ». Программа сама
 * замечает, когда его выдали, — переключать настройку не нужно. */
enum {
    GESTURES_OFF      = 0,    /* блокировка не нужна */
    GESTURES_READY    = 1,    /* разрешение есть, перехват готов */
    GESTURES_BLOCKING = 2,    /* прямо сейчас жесты глушатся */
    GESTURES_WAITING  = -1,   /* ждём разрешения */
    GESTURES_FAILED   = -2    /* macOS не дала перехватить события */
};

void plat_gestures_want(int on);          /* хотим ли блокировать вообще */
void plat_gestures_request(void);         /* попросить разрешение (окно macOS + Настройки) */
void plat_gestures_poll(void);            /* раз в кадр: подхватить выданное разрешение */
void plat_gestures_block(int on);         /* глушить ли жесты прямо сейчас (идёт игра) */
int  plat_gestures_status(void);

void plat_open_url(const char *url);      /* ссылка или путь к папке */
void plat_reveal_file(const char *path);  /* показать файл в Finder */

#ifdef __cplusplus
}
#endif

#endif
