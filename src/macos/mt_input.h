/*
 * mt_input.h — чтение тачпада MacBook напрямую (мультитач + сила нажатия)
 *
 * Использует приватный фреймворк Apple MultitouchSupport. Он загружается
 * во время работы через dlopen, поэтому программа собирается и запускается
 * даже там, где фреймворка нет: mt_start() тогда просто вернёт 0.
 */
#ifndef MT_INPUT_H
#define MT_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#define MT_MAX_TOUCHES 16

/* флаги, которые возвращает mt_start() */
#define MT_OK       1   /* касания читаются */
#define MT_FORCE    2   /* тачпад сообщает силу нажатия (Force Touch) */
#define MT_HAPTIC   4   /* открыт приватный хаптик-актуатор */

typedef struct {
    int   id;          /* номер касания, постоянный, пока палец на тачпаде */
    float x, y;        /* 0..1, x слева направо, y СВЕРХУ вниз (как на экране) */
    float pressure;    /* сила нажатия, примерно в граммах (Force Touch) */
    float area;        /* суммарная ёмкость касания (≈ площадь пальца) */
} MTPoint;

typedef struct {
    int     count;
    MTPoint t[MT_MAX_TOUCHES];
    double  time;      /* когда пришёл кадр (секунды, mt_now()) */
} MTFrame;

int    mt_start(void);                     /* 0 — недоступно, иначе флаги MT_* */
void   mt_stop(void);
int    mt_read(MTFrame *out, int try_only);/* 1 — скопирован свежий кадр */
double mt_now(void);                       /* текущее время в тех же секундах */
void   mt_haptic(int strength);            /* щелчок тачпада: 1 — слабый, 3 — сильнее */

#ifdef __cplusplus
}
#endif

#endif
