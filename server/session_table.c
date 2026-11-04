/*
 * session_table.c
 * Tabla de sesiones y salas en memoria compartida, protegida con un
 * semaforo mutex.
 *
 * Cada fila es un cliente conectado. El padre agrega la fila al aceptar la
 * conexion (y antes cuenta cuantas hay, para el limite); el hijo que atiende
 * a ese cliente la actualiza en cada comando y la borra al desconectarse.
 *
 * Cada sala tiene su propia cancion y su propio "reloj". Los hijos de los
 * integrantes la modifican con room_play/pause/resume/stop, y cada uno revisa
 * seguido el campo 'version' para enterarse de los cambios que hicieron los
 * demas.
 *
 * Como varios procesos pueden escribir a la vez, todo acceso pasa por el mutex.
 *
 * La posicion de reproduccion (de un cliente o de una sala) se calcula por
 * tiempo:
 *
 *   posicion = pos_base + (ahora - since)    si esta reproduciendo
 *   posicion = pos_base                      si esta en pausa
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "ipc.h"
#include "session_table.h"

typedef enum { STATE_NO_SONG, STATE_PLAYING, STATE_PAUSED } state_t;

/* Una cancion sonando y su reloj (lo tienen los clientes y las salas) */
typedef struct {
    char    song[256];      /* Cancion puesta */
    state_t state;
    double  duration;       /* Duracion de la cancion en segundos (0 = no se sabe) */
    double  pos_base;       /* Posicion (s) en el momento 'since' */
    double  since;          /* Momento (s, reloj del sistema) desde el que corre pos_base */
} playback_t;

typedef struct {
    int        used;        /* 1 si la fila esta ocupada */
    pid_t      pid;         /* Proceso hijo que atiende a este cliente */
    char       client[64];  /* "IP:puerto" */
    int        room;        /* Sala en la que esta (-1 = ninguna) */
    playback_t play;        /* Lo que escucha cuando esta solo */
} session_t;

typedef struct {
    int        used;
    char       name[ROOM_NAME_SIZE];
    int        members;
    playback_t play;        /* Lo que escucha la sala */
    int        version;
    int        seek_id;
} room_t;

typedef struct {
    session_t rows[MAX_SESSIONS];
    room_t    rooms[MAX_ROOMS];
} table_shared_t;

static int             shm_id = -1;
static int             sem_id = -1;
static table_shared_t *table  = NULL;
static int             limit  = 0;       /* Maximo de clientes a la vez */

/* Segundos desde un punto fijo (es el mismo reloj para todos los procesos) */
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void lock(void)   { semaphore_down(sem_id, 0); }
static void unlock(void) { semaphore_up(sem_id, 0); }

/* ---- Reloj de reproduccion (llamar con el mutex tomado) ---- */

static double position_of(const playback_t *p)
{
    double pos;

    if (p->state == STATE_NO_SONG)
        return 0;
    pos = p->pos_base;
    if (p->state == STATE_PLAYING)
        pos += now() - p->since;
    if (p->duration > 0 && pos > p->duration)
        pos = p->duration;
    return pos;
}

static void play_start(playback_t *p, const char *song, double duration, double from)
{
    snprintf(p->song, sizeof(p->song), "%s", song);
    p->state    = STATE_PLAYING;
    p->duration = duration;
    p->pos_base = from;
    p->since    = now();
}

static void play_pause(playback_t *p)
{
    if (p->state == STATE_PLAYING) {
        p->pos_base = position_of(p);   /* Se congela la posicion */
        p->state    = STATE_PAUSED;
    }
}

static void play_resume(playback_t *p)
{
    if (p->state == STATE_PAUSED) {
        p->since = now();               /* Sigue corriendo desde ahora */
        p->state = STATE_PLAYING;
    }
}

static void play_stop(playback_t *p)
{
    p->state   = STATE_NO_SONG;
    p->song[0] = '\0';
}

/* ---- Crear y borrar ---- */

int table_create(int max_clients)
{
    limit = max_clients > MAX_SESSIONS ? MAX_SESSIONS : max_clients;

    if ((shm_id = shared_mem_create(sizeof(table_shared_t))) == -1)
        return -1;
    if ((table = shared_mem_attach(shm_id)) == NULL ||
        (sem_id = semaphore_create(1)) == -1 ||
        semaphore_set(sem_id, 0, 1) == -1) {
        table_destroy();
        return -1;
    }
    memset(table, 0, sizeof(*table));
    return 0;
}

void table_destroy(void)
{
    if (sem_id != -1)
        semaphore_remove(sem_id);
    if (shm_id != -1)
        shared_mem_remove(shm_id, table);
    sem_id = shm_id = -1;
    table  = NULL;
}

/* Saca a un cliente de su sala; si era el ultimo, borra la sala (con el mutex tomado) */
static void leave_room(int row)
{
    int r = table->rows[row].room;

    if (r >= 0) {
        if (--table->rooms[r].members <= 0)
            table->rooms[r].used = 0;
        table->rows[row].room = -1;
    }
}

/* Libera las filas de hijos que ya no existen (por si alguno termino mal).
 * kill(pid, 0) no manda ninguna senial: solo dice si el proceso existe. */
static void remove_dead(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (table->rows[i].used && table->rows[i].pid > 0 &&
            kill(table->rows[i].pid, 0) == -1 && errno == ESRCH) {
            leave_room(i);
            table->rows[i].used = 0;
        }
}

/* ---- Funciones del padre ---- */

int table_add(const char *client)
{
    int count = 0, row = -1;

    lock();
    remove_dead();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (table->rows[i].used)
            count++;
        else if (row == -1)
            row = i;
    }

    if (count >= limit || row == -1) {
        unlock();
        return -1;                      /* Servidor lleno */
    }

    memset(&table->rows[row], 0, sizeof(session_t));
    table->rows[row].used = 1;
    table->rows[row].room = -1;
    table->rows[row].play.state = STATE_NO_SONG;
    snprintf(table->rows[row].client, sizeof(table->rows[row].client), "%s", client);
    unlock();
    return row;
}

void table_set_pid(int row, pid_t pid)
{
    lock();
    if (table->rows[row].used)
        table->rows[row].pid = pid;
    unlock();
}

int table_count(void)
{
    int count = 0;
    lock();
    remove_dead();
    for (int i = 0; i < MAX_SESSIONS; i++)
        count += table->rows[i].used;
    unlock();
    return count;
}

int table_max(void)
{
    return limit;
}

/* ---- Funciones del hijo (escuchando solo) ---- */

void table_remove(int row)
{
    lock();
    leave_room(row);
    table->rows[row].used = 0;
    unlock();
}

void table_play(int row, const char *song, double duration_sec, double from_sec)
{
    lock();
    play_start(&table->rows[row].play, song, duration_sec, from_sec);
    unlock();
}

void table_pause(int row)
{
    lock();
    play_pause(&table->rows[row].play);
    unlock();
}

void table_resume(int row)
{
    lock();
    play_resume(&table->rows[row].play);
    unlock();
}

void table_stop(int row)
{
    lock();
    play_stop(&table->rows[row].play);
    unlock();
}

double table_get_position(int row)
{
    double pos;
    lock();
    pos = position_of(&table->rows[row].play);
    unlock();
    return pos;
}

/* ---- Salas ---- */

int table_join(int row, const char *name, const char *song, double duration,
               double position, int paused)
{
    int r = -1, free_slot = -1;

    lock();
    leave_room(row);                    /* Si estaba en otra sala, sale */

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (table->rooms[i].used && strcmp(table->rooms[i].name, name) == 0)
            r = i;                      /* Ya existe */
        else if (!table->rooms[i].used && free_slot == -1)
            free_slot = i;
    }

    if (r == -1) {
        /* No existe: se crea, con lo que estaba escuchando este cliente */
        if (free_slot == -1) {
            unlock();
            return -1;                  /* No hay lugar para mas salas */
        }
        r = free_slot;
        memset(&table->rooms[r], 0, sizeof(room_t));
        table->rooms[r].used    = 1;
        table->rooms[r].version = 1;
        table->rooms[r].seek_id = 1;
        snprintf(table->rooms[r].name, sizeof(table->rooms[r].name), "%s", name);
        if (song[0] != '\0') {
            play_start(&table->rooms[r].play, song, duration, position);
            if (paused)
                play_pause(&table->rooms[r].play);
        } else {
            play_stop(&table->rooms[r].play);
        }
    }

    table->rooms[r].members++;
    table->rows[row].room = r;
    unlock();
    return r;
}

void table_leave(int row)
{
    lock();
    leave_room(row);
    unlock();
}

int table_room_info(int room, room_info_t *info)
{
    room_t *rm;

    lock();
    rm = &table->rooms[room];
    if (!rm->used) {
        unlock();
        return -1;
    }
    snprintf(info->name, sizeof(info->name), "%s", rm->name);
    snprintf(info->song, sizeof(info->song), "%s", rm->play.song);
    info->members  = rm->members;
    info->paused   = rm->play.state == STATE_PAUSED;
    info->position = position_of(&rm->play);
    info->duration = rm->play.duration;
    info->version  = rm->version;
    info->seek_id  = rm->seek_id;
    unlock();
    return 0;
}

void room_play(int room, const char *song, double duration, double from_sec)
{
    lock();
    play_start(&table->rooms[room].play, song, duration, from_sec);
    table->rooms[room].seek_id++;       /* Es un salto: cancion nueva o SEEK */
    table->rooms[room].version++;
    unlock();
}

void room_pause(int room)
{
    lock();
    play_pause(&table->rooms[room].play);
    table->rooms[room].version++;
    unlock();
}

void room_resume(int room)
{
    lock();
    play_resume(&table->rooms[room].play);
    table->rooms[room].version++;
    unlock();
}

void room_stop(int room)
{
    lock();
    play_stop(&table->rooms[room].play);
    table->rooms[room].version++;
    unlock();
}

int table_list_rooms(room_info_t *list, int max)
{
    int n = 0;

    lock();
    for (int i = 0; i < MAX_ROOMS && n < max; i++) {
        room_t *rm = &table->rooms[i];
        if (!rm->used)
            continue;
        snprintf(list[n].name, sizeof(list[n].name), "%s", rm->name);
        snprintf(list[n].song, sizeof(list[n].song), "%s", rm->play.song);
        list[n].members  = rm->members;
        list[n].paused   = rm->play.state == STATE_PAUSED;
        list[n].position = position_of(&rm->play);
        n++;
    }
    unlock();
    return n;
}

/* ---- Estadisticas ---- */

void table_print(void)
{
    int count = 0, rooms = 0;

    lock();
    remove_dead();

    printf("\n+----------------------+--------------------------+----------+---------------+------------+\n");
    printf("| %-20s | %-24s | %-8s | %-13s | %-10s |\n", "Cliente", "Cancion", "Posicion", "Estado", "Sala");
    printf("+----------------------+--------------------------+----------+---------------+------------+\n");

    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &table->rows[i];
        if (!s->used)
            continue;
        count++;

        /* Si esta en una sala, lo que escucha es lo de la sala */
        const playback_t *p = s->room >= 0 ? &table->rooms[s->room].play : &s->play;
        const char *room    = s->room >= 0 ? table->rooms[s->room].name : "--";

        if (p->state == STATE_NO_SONG) {
            printf("| %-20s | %-24s | %-8s | %-13s | %-10.10s |\n",
                   s->client, "--", "--", "Sin cancion", room);
            continue;
        }

        double pos = position_of(p);
        const char *state = p->state == STATE_PAUSED ? "En pausa" : "Reproduciendo";
        if (p->duration > 0 && pos >= p->duration)
            state = "Terminada";

        printf("| %-20s | %-24.24s | %5d:%02d | %-13s | %-10.10s |\n",
               s->client, p->song, (int)pos / 60, (int)pos % 60, state, room);
    }

    for (int i = 0; i < MAX_ROOMS; i++)
        rooms += table->rooms[i].used;

    printf("+----------------------+--------------------------+----------+---------------+------------+\n");
    printf("Clientes conectados: %d de %d. Salas activas: %d\n\n", count, limit, rooms);
    unlock();
    fflush(stdout);
}

/*
 * Lo mismo que table_print(), pero en lineas faciles de leer para la
 * interfaz grafica (modo --gui):
 *
 *   STATS <clientes> <maximo> <salas>
 *   CLIENT <ip:puerto> <estado> <posicion> <duracion> <sala o -> <cancion o ->
 *   ...
 *   END
 *
 * <estado> es NONE, PLAYING, PAUSED o FINISHED. Posicion y duracion en
 * segundos. La cancion va al final porque puede tener espacios.
 */
void table_print_gui(void)
{
    int count = 0, rooms = 0;

    lock();
    remove_dead();

    for (int i = 0; i < MAX_SESSIONS; i++)
        count += table->rows[i].used;
    for (int i = 0; i < MAX_ROOMS; i++)
        rooms += table->rooms[i].used;

    printf("STATS %d %d %d\n", count, limit, rooms);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &table->rows[i];
        if (!s->used)
            continue;

        const playback_t *p = s->room >= 0 ? &table->rooms[s->room].play : &s->play;
        const char *room    = s->room >= 0 ? table->rooms[s->room].name : "-";
        double pos          = position_of(p);
        const char *state   = "PLAYING";

        if (p->state == STATE_NO_SONG)
            state = "NONE";
        else if (p->state == STATE_PAUSED)
            state = "PAUSED";
        else if (p->duration > 0 && pos >= p->duration)
            state = "FINISHED";

        printf("CLIENT %s %s %d %d %s %s\n", s->client, state, (int)pos, (int)p->duration,
               room, p->state == STATE_NO_SONG ? "-" : p->song);
    }

    printf("END\n");
    unlock();
    fflush(stdout);
}

/* Se llama desde el manejador de Ctrl+C / SIGTERM: NO toma el mutex, porque
 * si la senial llego justo cuando el padre lo tenia tomado, se trabaria para
 * siempre. Solo lee los pid, asi que no hace falta. */
void table_kill_children(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (table->rows[i].used && table->rows[i].pid > 0)
            kill(table->rows[i].pid, SIGTERM);
}
