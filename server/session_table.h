/*
 * session_table.h
 * Tabla con todos los clientes conectados, en memoria compartida.
 * La crea el proceso padre antes de los fork(), asi la ven todos los hijos.
 */
#ifndef SESSION_TABLE_H
#define SESSION_TABLE_H

#include <sys/types.h>

#define MAX_SESSIONS 32     /* Tope de la tabla (el limite real se configura) */

/* Crea la tabla y su semaforo. 'max_clients' es el limite de conexiones.
 * Devuelve 0 si salio bien, -1 si hubo error. */
int table_create(int max_clients);

/* Borra la tabla y el semaforo del sistema (lo hace el padre al terminar). */
void table_destroy(void);

/* ---- Las usa el padre ---- */

/* Agrega un cliente. Devuelve el numero de fila, o -1 si se llego al limite. */
int  table_add(const char *client);
void table_set_pid(int row, pid_t pid);
int  table_count(void);
int  table_max(void);
void table_print(void);

/* ---- Las usa el hijo que atiende al cliente de esa fila ---- */

void table_remove(int row);
void table_play(int row, const char *song, double duration_sec, double from_sec);
void table_pause(int row);
void table_resume(int row);
void table_stop(int row);

/* Segundo de la cancion por el que va la reproduccion de ese cliente */
double table_get_position(int row);

#endif
