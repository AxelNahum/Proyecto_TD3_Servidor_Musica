/*
 * player.h
 * Reproductor de MP3 por partes: se le van pasando los bytes a medida que
 * llegan por la red, los decodifica (libmpg123) y los manda a la placa de
 * sonido (libout123).
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stddef.h>

typedef struct player player_t;

/* Prepara el decodificador y la salida de audio.
 * Devuelve NULL si hubo error (ya informado por pantalla). */
player_t *player_open(void);

/* Decodifica los 'len' bytes de MP3 de 'data' y reproduce el audio que salga.
 * Se bloquea mientras la placa de sonido no acepta mas audio, por eso el
 * cliente lee del socket al ritmo de la reproduccion.
 * Devuelve 0 si salio bien, -1 si hubo error. */
int player_feed(player_t *p, const void *data, size_t len);

/* Bitrate de la cancion en kbps (0 si todavia no se decodifico nada). */
int player_bitrate(player_t *p);

/* Espera a que termine de sonar lo que queda y libera todo. */
void player_close(player_t *p);

/* Corta al instante (tira lo que estaba por sonar) y libera todo.
 * Se usa al cambiar de cancion, hacer SEEK o QUIT. */
void player_abort(player_t *p);

#endif
