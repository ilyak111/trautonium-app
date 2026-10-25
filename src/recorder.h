/*
 * recorder.h — запись игры в WAV (24 бит, 48 кГц, стерео).
 * Файлы складываются в ~/Music/Trautonium.
 */
#ifndef RECORDER_H
#define RECORDER_H

int         rec_start(void);        /* 1 — запись пошла */
void        rec_poll(void);         /* вызывать каждый кадр: переносит звук в файл */
void        rec_stop(void);         /* дописать заголовок и закрыть файл */
int         rec_active(void);
double      rec_seconds(void);
const char *rec_last_path(void);    /* путь к последнему файлу */
const char *rec_dir(void);

#endif
