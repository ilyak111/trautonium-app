/*
 * presets.h — пресеты (заводские и свои) и автосохранение настроек.
 *
 * Всё хранится текстом в ~/Library/Application Support/Trautonium:
 *   settings.ini          — настройки, калибровка и текущее звучание;
 *   Presets/<имя>.trautonium — свои пресеты.
 */
#ifndef PRESETS_H
#define PRESETS_H

#include "trautonium.h"

#define PRESET_NAME_MAX 64

void presets_init(void);                 /* создать папки, прочитать список */
void presets_rescan(void);
int  presets_count(void);
const char *preset_name(int i);
int  preset_is_factory(int i);
int  preset_find(const char *name);      /* -1 — нет такого */
int  preset_load(int i, Patch *out);     /* 1 — успешно */
int  preset_save(const char *name, const Patch *p);   /* индекс или -1 */
int  preset_delete(int i);               /* только свои */
const char *presets_dir(void);

/* текущее состояние целиком: 1 — файл был, 0 — первый запуск */
int  settings_load(Prefs *pf, Patch *p, char *preset, int preset_n, int *modified);
int  settings_save(const Prefs *pf, const Patch *p, const char *preset, int modified);

/* для тестов: разбор пресета из текста */
void patch_from_text(Patch *p, const char *text);

#endif
