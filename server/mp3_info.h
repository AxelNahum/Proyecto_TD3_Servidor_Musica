/*
 * mp3_info.h
 * Lee el bitrate de un archivo MP3 (para pasar de bytes a segundos).
 */
#ifndef MP3_INFO_H
#define MP3_INFO_H

/* Devuelve el bitrate en kbps (por ejemplo 128), o 0 si no se pudo leer. */
int mp3_bitrate_kbps(const char *path);

#endif
