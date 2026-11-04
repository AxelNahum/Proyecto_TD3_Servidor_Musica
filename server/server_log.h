/*
 * server_log.h
 * Mensajes del servidor. En modo normal se imprimen tal cual en la terminal;
 * en modo --gui se imprimen como "LOG <mensaje>" para que la interfaz
 * grafica los pueda reconocer.
 */
#ifndef SERVER_LOG_H
#define SERVER_LOG_H

/* Activa o desactiva el modo --gui */
void log_set_gui_mode(int on);
int  log_gui_mode(void);

/* Imprime un mensaje (con formato printf, sin '\n' al final) */
void server_log(const char *fmt, ...);

#endif
