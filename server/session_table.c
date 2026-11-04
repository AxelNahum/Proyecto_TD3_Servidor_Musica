/*
 * session_table.c
 * Tabla de sesiones en memoria compartida, protegida con un semaforo mutex.
 *
 * Cada fila es un cliente conectado. El padre agrega la fila al aceptar la
 * conexion (y antes cuenta cuantas hay, para el limite); el hijo que atiende
 * a ese cliente la actualiza en cada comando y la borra al desconectarse.
 * Como varios hijos pueden escribir a la vez, todo acceso pasa por el mutex.
 *
 * La posicion de reproduccion se calcula por tiempo, porque el servidor manda
 * la cancion mas rapido de lo que suena:
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

typedef struct {
    int     used;           /* 1 si la fila esta ocupada */
    pid_t   pid;            /* Proceso hijo que atiende a este cliente */
    char    client[64];     /* "IP:puerto" */
    char    song[256];      /* Cancion puesta */
    state_t state;
    double  duration;       /* Duracion de la cancion en segundos (0 = no se sabe) */
    double  pos_base;       /* Posicion (s) en el momento 'since' */
    double  since;          /* Momento (s, reloj del sistema) desde el que corre pos_base */
} session_t;

typedef struct {
    session_t rows[MAX_SESSIONS];
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

/* Posicion actual de una fila (llamar con el mutex tomado) */
static double current_position(const session_t *s)
{
    double pos = s->pos_base;
    if (s->state == STATE_PLAYING)
        pos += now() - s->since;
    if (s->duration > 0 && pos > s->duration)
        pos = s->duration;
    return pos;
}

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

/* Libera las filas de hijos que ya no existen (por si alguno termino mal).
 * kill(pid, 0) no manda ninguna senial: solo dice si el proceso existe. */
static void remove_dead(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (table->rows[i].used && table->rows[i].pid > 0 &&
            kill(table->rows[i].pid, 0) == -1 && errno == ESRCH)
            table->rows[i].used = 0;
}

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
    table->rows[row].used  = 1;
    table->rows[row].state = STATE_NO_SONG;
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

void table_remove(int row)
{
    lock();
    table->rows[row].used = 0;
    unlock();
}

void table_play(int row, const char *song, double duration_sec, double from_sec)
{
    lock();
    session_t *s = &table->rows[row];
    snprintf(s->song, sizeof(s->song), "%s", song);
    s->state    = STATE_PLAYING;
    s->duration = duration_sec;
    s->pos_base = from_sec;
    s->since    = now();
    unlock();
}

void table_pause(int row)
{
    lock();
    session_t *s = &table->rows[row];
    if (s->state == STATE_PLAYING) {
        s->pos_base = current_position(s);  /* Se congela la posicion */
        s->state    = STATE_PAUSED;
    }
    unlock();
}

void table_resume(int row)
{
    lock();
    session_t *s = &table->rows[row];
    if (s->state == STATE_PAUSED) {
        s->since = now();                   /* Sigue corriendo desde ahora */
        s->state = STATE_PLAYING;
    }
    unlock();
}

void table_stop(int row)
{
    lock();
    table->rows[row].state   = STATE_NO_SONG;
    table->rows[row].song[0] = '\0';
    unlock();
}

void table_print(void)
{
    int count = 0;

    lock();
    remove_dead();

    printf("\n+----------------------+------------------------------+----------+---------------+\n");
    printf("| %-20s | %-28s | %-8s | %-13s |\n", "Cliente", "Cancion", "Posicion", "Estado");
    printf("+----------------------+------------------------------+----------+---------------+\n");

    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &table->rows[i];
        if (!s->used)
            continue;
        count++;

        if (s->state == STATE_NO_SONG) {
            printf("| %-20s | %-28s | %-8s | %-13s |\n", s->client, "--", "--", "Sin cancion");
            continue;
        }

        double pos = current_position(s);
        const char *state = s->state == STATE_PAUSED ? "En pausa" : "Reproduciendo";
        if (s->duration > 0 && pos >= s->duration)
            state = "Terminada";

        printf("| %-20s | %-28.28s | %5d:%02d | %-13s |\n",
               s->client, s->song, (int)pos / 60, (int)pos % 60, state);
    }

    printf("+----------------------+------------------------------+----------+---------------+\n");
    printf("Clientes conectados: %d de %d\n\n", count, limit);
    unlock();
    fflush(stdout);
}
