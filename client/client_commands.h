/*
 * client_commands.h
 * Pedidos del cliente al servidor segun el protocolo.
 * Todas devuelven 0 si salio bien (o el servidor respondio con un error)
 * y -1 si se corto la conexion.
 */
#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#define RECEIVED_DIR "received"

/* Envia LIST y muestra las canciones disponibles */
int request_list(int fd);

/* Envia GET y guarda la cancion en "received/<nombre>" */
int request_song(int fd, const char *name);

#endif
