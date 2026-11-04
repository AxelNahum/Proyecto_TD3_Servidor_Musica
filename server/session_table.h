/*
 * session_table.h
 * Tabla con todos los clientes conectados y las salas, en memoria compartida.
 * La crea el proceso padre antes de los fork(), asi la ven todos los hijos.
 */
#ifndef SESSION_TABLE_H
#define SESSION_TABLE_H

#include <sys/types.h>

#define MAX_SESSIONS   32   /* Tope de la tabla (el limite real se configura) */
#define MAX_ROOMS      16   /* Salas a la vez como maximo */
#define ROOM_NAME_SIZE 32

/* Foto del estado de una sala (lo que lee cada hijo para sincronizarse) */
typedef struct {
    char   name[ROOM_NAME_SIZE];
    int    members;
    char   song[256];       /* "" = la sala no tiene cancion */
    int    paused;
    double position;        /* Segundo por el que va la cancion de la sala */
    double duration;
    int    version;         /* Cambia con CUALQUIER modificacion de la sala */
    int    seek_id;         /* Cambia cuando hay un salto (cancion nueva o SEEK) */
} room_info_t;

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

/* ---- Las usa el hijo que atiende al cliente de esa fila (escuchando solo) ---- */

void   table_remove(int row);       /* Tambien lo saca de su sala */
void   table_play(int row, const char *song, double duration_sec, double from_sec);
void   table_pause(int row);
void   table_resume(int row);
void   table_stop(int row);
double table_get_position(int row); /* Segundo por el que va la reproduccion */

/* ---- Salas ---- */

/* Entra a la sala 'name' (si no existe, la crea). Si la crea, la sala arranca
 * con la cancion que estaba escuchando el cliente (song = "" si ninguna).
 * Si ya estaba en otra sala, sale de esa. Devuelve el numero de sala o -1 si
 * no hay lugar para mas salas. */
int  table_join(int row, const char *name, const char *song, double duration,
                double position, int paused);

/* Sale de su sala. Si era el ultimo integrante, la sala se borra. */
void table_leave(int row);

/* Copia el estado actual de la sala. Devuelve -1 si la sala no existe. */
int  table_room_info(int room, room_info_t *info);

/* Cambian el estado de la sala (y su 'version'); los hijos de todos los
 * integrantes lo detectan y se lo mandan a sus clientes. */
void room_play(int room, const char *song, double duration, double from_sec);
void room_pause(int room);
void room_resume(int room);
void room_stop(int room);

/* Copia hasta 'max' salas existentes en 'list'. Devuelve cuantas hay. */
int  table_list_rooms(room_info_t *list, int max);

#endif
