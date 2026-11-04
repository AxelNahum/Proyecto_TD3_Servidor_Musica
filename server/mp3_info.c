/*
 * mp3_info.c
 * Lee el bitrate de un MP3 mirando el encabezado del primer frame.
 *
 * Un MP3 es una sucesion de "frames". Cada frame empieza con 4 bytes de
 * encabezado; los primeros 11 bits estan en 1 (sincronismo) y despues vienen
 * la version, la capa y un indice que dice el bitrate:
 *
 *   byte 1        byte 2        byte 3
 *   1111 1111     111V VLLP     BBBB ....      V = version, L = capa, B = bitrate
 *
 * Antes del primer frame puede haber un tag ID3v2 (titulo, artista, ...) que
 * hay que saltear: empieza con "ID3" y su largo esta en los bytes 6 a 9.
 *
 * Se asume bitrate constante (CBR), como el de los tonos de prueba.
 */
#include <stdio.h>
#include "mp3_info.h"

#define SEARCH_LIMIT 65536  /* Cuantos bytes buscar el primer frame como maximo */

/* Bitrates en kbps segun el indice, para MP3 (capa III) */
static const int bitrates_mpeg1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
static const int bitrates_mpeg2[16] = { 0,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, 0 };

int mp3_bitrate_kbps(const char *path)
{
    unsigned char h[10];
    long skip = 0;
    int c, prev = 0;
    FILE *f;

    if ((f = fopen(path, "rb")) == NULL)
        return 0;

    /* Si tiene tag ID3v2, se saltea (el largo esta en 4 bytes de 7 bits) */
    if (fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3')
        skip = 10 + ((h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 |
                     (h[8] & 0x7F) << 7  | (h[9] & 0x7F));
    fseek(f, skip, SEEK_SET);

    /* Busca el sincronismo: un byte 0xFF seguido de un byte con los 3 bits altos en 1 */
    for (long i = 0; i < SEARCH_LIMIT && (c = fgetc(f)) != EOF; i++) {
        if (prev == 0xFF && (c & 0xE0) == 0xE0) {
            int version = (c >> 3) & 0x03;      /* 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5 */
            int layer   = (c >> 1) & 0x03;      /* 1 = capa III (MP3) */
            int b2      = fgetc(f);
            int index   = (b2 >> 4) & 0x0F;

            if (layer == 1 && version != 1 && b2 != EOF && index != 0 && index != 15) {
                fclose(f);
                return version == 3 ? bitrates_mpeg1[index] : bitrates_mpeg2[index];
            }
            c = b2;     /* No era un encabezado valido: se sigue buscando desde ahi */
        }
        prev = c;
    }

    fclose(f);
    return 0;
}
