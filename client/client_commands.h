/*
 * client_commands.h
 * Pedidos del cliente al servidor segun el protocolo.
 * Todas devuelven 0 si salio bien (o el servidor respondio con un error)
 * y -1 si se corto la conexion.
 */
#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "ring_buffer.h"

#define RECEIVED_DIR "received"

/* Envia LIST y muestra las canciones disponibles */
int request_list(int fd);

/* Que hacer con la cancion que se recibe */
typedef enum {
    SONG_SAVE,      /* Guardarla en "received/<nombre>" */
    SONG_PLAY       /* Reproducirla mientras se recibe (streaming) */
} song_mode_t;

/* Envia GET y guarda o reproduce la cancion segun 'mode'.
 * Para reproducir usa el buffer circular 'ring': un proceso hijo recibe del
 * socket y llena el buffer, y este proceso lo vacia y reproduce. */
int request_song(int fd, const char *name, song_mode_t mode, ring_t *ring);

#endif
