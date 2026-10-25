/*
 * ui.c — примитивы рисования и кэш надписей
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "platform.h"

static SDL_Renderer *R;
static float         S = 1.0f;

static const float sizes_pt[TS_COUNT] = {11.5f, 13.0f, 15.5f, 21.0f, 30.0f};

/* ---- кэш надписей: белый текст, цвет задаётся при рисовании ---- */
#define TEXT_CACHE 640
typedef struct {
    char         s[160];
    int          size, weight;
    SDL_Texture *tex;
    float        lw, lh;
    unsigned     used;
} TextEntry;

static TextEntry cache[TEXT_CACHE];
static unsigned  frame_no = 1;
static float     line_h[TS_COUNT];

static void cache_clear(void)
{
    for (int i = 0; i < TEXT_CACHE; i++) {
        if (cache[i].tex) SDL_DestroyTexture(cache[i].tex);
        cache[i].tex = NULL;
    }
    memset(line_h, 0, sizeof line_h);
}

void ui_init(SDL_Renderer *r)
{
    R = r;
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
}

void ui_set_scale(float scale)
{
    if (scale <= 0.0f) scale = 1.0f;
    if (fabsf(scale - S) > 1e-4f) {
        cache_clear();
        S = scale;
    }
    SDL_RenderSetScale(R, S, S);
}

float ui_scale(void) { return S; }

void ui_shutdown(void) { cache_clear(); }

void ui_frame_done(void) { frame_no++; }

static TextEntry *text_get(const char *s, int size, int weight)
{
    if (!s || !s[0]) return NULL;
    size = size < 0 ? 0 : (size >= TS_COUNT ? TS_COUNT - 1 : size);

    TextEntry *slot = NULL;
    for (int i = 0; i < TEXT_CACHE; i++) {
        TextEntry *e = &cache[i];
        if (e->tex) {
            if (e->size == size && e->weight == weight && strncmp(e->s, s, sizeof e->s) == 0) {
                e->used = frame_no;
                return e;
            }
            if (!slot || (slot->tex && e->used < slot->used)) slot = e;
        } else if (!slot || slot->tex) {
            slot = e;
        }
    }

    TextBitmap bm;
    if (!plat_text_raster(s, sizes_pt[size], weight, S, &bm)) return NULL;

    SDL_Texture *tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, bm.w, bm.h);
    if (!tex) { plat_text_free(&bm); return NULL; }
    Uint32 *px = SDL_malloc((size_t)bm.w * bm.h * 4);
    if (!px) { SDL_DestroyTexture(tex); plat_text_free(&bm); return NULL; }
    for (int i = 0; i < bm.w * bm.h; i++) px[i] = ((Uint32)bm.alpha[i] << 24) | 0x00FFFFFFu;
    SDL_UpdateTexture(tex, NULL, px, bm.w * 4);
    SDL_free(px);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    if (slot->tex) SDL_DestroyTexture(slot->tex);
    snprintf(slot->s, sizeof slot->s, "%s", s);
    slot->size   = size;
    slot->weight = weight;
    slot->tex    = tex;
    slot->lw     = bm.lw;
    slot->lh     = bm.lh;
    slot->used   = frame_no;
    plat_text_free(&bm);
    return slot;
}

float ui_text(float x, float y, const char *s, int size, int weight, SDL_Color c, int align)
{
    TextEntry *e = text_get(s, size, weight);
    if (!e) return 0;
    if (align == AL_CENTER) x -= e->lw / 2;
    if (align == AL_RIGHT)  x -= e->lw;
    /* привязка к пиксельной сетке — текст остаётся чётким */
    x = roundf(x * S) / S;
    y = roundf(y * S) / S;
    SDL_FRect d = {x, y, e->lw, e->lh};
    SDL_SetTextureColorMod(e->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(e->tex, c.a);
    SDL_RenderCopyF(R, e->tex, NULL, &d);
    return e->lw;
}

float ui_text_w(const char *s, int size, int weight)
{
    TextEntry *e = text_get(s, size, weight);
    return e ? e->lw : 0;
}

float ui_text_h(int size)
{
    size = size < 0 ? 0 : (size >= TS_COUNT ? TS_COUNT - 1 : size);
    if (line_h[size] <= 0) {
        TextEntry *e = text_get("Ag", size, 0);
        line_h[size] = e ? e->lh : sizes_pt[size] * 1.3f;
    }
    return line_h[size];
}

float ui_text_wrap(float x, float y, float w, const char *s, int size, int weight, SDL_Color c)
{
    char line[800] = "";
    float lh = ui_text_h(size) * 1.05f, cy = y;
    const char *p = s;
    while (*p) {
        /* следующее слово (с пробелом) */
        const char *q = p;
        while (*q && *q != ' ' && *q != '\n') q++;
        char word[256];
        int wl = (int)(q - p);
        if (wl > (int)sizeof word - 1) wl = (int)sizeof word - 1;
        memcpy(word, p, wl);
        word[wl] = 0;

        char test[768];
        snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && ui_text_w(test, size, weight) > w) {
            ui_text(x, cy, line, size, weight, c, AL_LEFT);
            cy += lh;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", test);
        }
        if (*q == '\n') {
            ui_text(x, cy, line, size, weight, c, AL_LEFT);
            cy += lh;
            line[0] = 0;
        }
        p = *q ? q + 1 : q;
    }
    if (line[0]) { ui_text(x, cy, line, size, weight, c, AL_LEFT); cy += lh; }
    return cy - y;
}

static void set(SDL_Color c) { SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a); }

void ui_fill(float x, float y, float w, float h, SDL_Color c)
{
    SDL_FRect r = {x, y, w, h};
    set(c);
    SDL_RenderFillRectF(R, &r);
}

void ui_frame(float x, float y, float w, float h, SDL_Color c)
{
    SDL_FRect r = {x, y, w, h};
    set(c);
    SDL_RenderDrawRectF(R, &r);
}

void ui_line(float x1, float y1, float x2, float y2, SDL_Color c)
{
    set(c);
    SDL_RenderDrawLineF(R, x1, y1, x2, y2);
}

/* закрашенная фигура построчно с шагом в один физический пиксель */
void ui_round(float x, float y, float w, float h, float r, SDL_Color c)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 0.5f) { ui_fill(x, y, w, h, c); return; }
    set(c);
    float step = 1.0f / S;
    for (float yy = 0; yy < h; yy += step) {
        float inset = 0;
        float dy = 0;
        if (yy < r) dy = r - yy - step * 0.5f;
        else if (yy > h - r) dy = yy + step * 0.5f - (h - r);
        if (dy > 0) inset = r - sqrtf(fmaxf(0.0f, r * r - dy * dy));
        float hh = fminf(step, h - yy);
        SDL_FRect row = {x + inset, y + yy, w - 2 * inset, hh};
        SDL_RenderFillRectF(R, &row);
    }
}

void ui_circle(float cx, float cy, float r, SDL_Color c)
{
    set(c);
    float step = 1.0f / S;
    for (float dy = -r; dy <= r; dy += step) {
        float dx = sqrtf(fmaxf(0.0f, r * r - dy * dy));
        SDL_FRect row = {cx - dx, cy + dy, 2 * dx, step};
        SDL_RenderFillRectF(R, &row);
    }
}

void ui_ring(float cx, float cy, float r, float width, SDL_Color c)
{
    set(c);
    int n = (int)(r * S * 4) + 24;
    for (float k = 0; k < width; k += 1.0f / S) {
        float rr = r - k;
        float px = cx + rr, py = cy;
        for (int i = 1; i <= n; i++) {
            float a = 2.0f * (float)M_PI * i / n;
            float nx = cx + rr * cosf(a), ny = cy + rr * sinf(a);
            SDL_RenderDrawLineF(R, px, py, nx, ny);
            px = nx; py = ny;
        }
    }
}
