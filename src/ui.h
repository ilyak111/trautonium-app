/*
 * ui.h — рисование: текст системным шрифтом (с кэшем), прямоугольники,
 * круги, линии. Все координаты — логические единицы окна (высота ≈ 790),
 * масштаб до пикселей Retina считается здесь.
 */
#ifndef UI_H
#define UI_H

#include <SDL.h>

enum { TS_SMALL, TS_NORMAL, TS_LARGE, TS_TITLE, TS_HUGE, TS_COUNT };
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

void  ui_init(SDL_Renderer *r);
void  ui_set_scale(float scale);          /* пикселей на логическую единицу */
float ui_scale(void);
void  ui_shutdown(void);
void  ui_frame_done(void);

float ui_text(float x, float y, const char *s, int size, int weight, SDL_Color c, int align);
float ui_text_w(const char *s, int size, int weight);
float ui_text_h(int size);
/* перенос по словам; возвращает занятую высоту */
float ui_text_wrap(float x, float y, float w, const char *s, int size, int weight, SDL_Color c);

void ui_fill(float x, float y, float w, float h, SDL_Color c);
void ui_round(float x, float y, float w, float h, float r, SDL_Color c);
void ui_frame(float x, float y, float w, float h, SDL_Color c);
void ui_line(float x1, float y1, float x2, float y2, SDL_Color c);
void ui_circle(float cx, float cy, float r, SDL_Color c);
void ui_ring(float cx, float cy, float r, float width, SDL_Color c);

static inline SDL_Color rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a) { SDL_Color c = {r, g, b, a}; return c; }
static inline SDL_Color with_a(SDL_Color c, Uint8 a) { c.a = a; return c; }

#endif
