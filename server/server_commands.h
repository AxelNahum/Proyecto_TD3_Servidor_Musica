/*
 * server_commands.h
 * Atencion de un cliente en el proceso hijo: lectura e interpretacion de
 * los comandos del protocolo.
 */
#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

/* Atiende los comandos del cliente conectado en 'fd' hasta que se desconecta.
 * 'songs_dir' es la carpeta de canciones, 'client' su "IP:puerto" (para los
 * mensajes) y 'row' su fila en la tabla de sesiones. */
void handle_client(int fd, const char *songs_dir, const char *client, int row);

#endif
