/*
 * ring_buffer.c
 * Buffer circular productor-consumidor con semaforos:
 *
 *   SEM_FREE   cuenta los lugares libres   (arranca en RING_SLOTS)
 *   SEM_FULL   cuenta los bloques ocupados (arranca en 0)
 *   SEM_MUTEX  exclusion mutua sobre el buffer (arranca en 1)
 *
 *   Productor:  down(FREE)  down(MUTEX)  guardar  up(MUTEX)  up(FULL)
 *   Consumidor: down(FULL)  down(MUTEX)  sacar    up(MUTEX)  up(FREE)
 *
 * Asi el productor se bloquea cuando el buffer esta lleno y el consumidor
 * cuando esta vacio, sin consumir procesador mientras esperan.
 *
 * Ademas hay un cuarto semaforo para la pausa:
 *
 *   SEM_PLAY   1 = reproduciendo, 0 = en pausa
 *
 *   Reproductor, antes de cada bloque: down(PLAY) up(PLAY)
 *     -> si esta en 1 pasa de largo; si esta en 0 se queda esperando.
 *   PAUSE:  down(PLAY)  (lo deja en 0)
 *   RESUME: up(PLAY)    (lo vuelve a 1 y despierta al reproductor)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "ipc.h"
#include "ring_buffer.h"

#define RING_SLOTS 64       /* Lugares del buffer: 64 x 4 KB = 256 KB */

enum { SEM_FREE, SEM_FULL, SEM_MUTEX, SEM_PLAY, SEM_COUNT };

/* Un lugar del buffer */
typedef struct {
    block_type_t type;
    size_t       len;
    char         data[BLOCK_SIZE];
} slot_t;

/* Lo que vive en la memoria compartida (lo ven todos los procesos) */
typedef struct {
    slot_t    slots[RING_SLOTS];
    int       in;               /* Proximo lugar donde escribe el productor */
    int       out;              /* Proximo lugar de donde lee el consumidor */
    int       flush_requests;   /* Cuantas veces la consola pidio vaciar (PLAY/SEEK/QUIT) */
    int       bitrate_kbps;     /* Bitrate de la cancion que suena (0 = no se sabe) */
    long long position;         /* Byte de la cancion que se esta reproduciendo */
} ring_shared_t;

/* Lo que tiene cada proceso: los ids y el puntero a la memoria compartida.
 * Despues del fork cada hijo tiene una copia de esto que apunta al mismo segmento. */
struct ring {
    int            shm_id;
    int            sem_id;
    ring_shared_t *shared;
};

ring_t *ring_create(void)
{
    ring_t *r = malloc(sizeof(*r));
    if (r == NULL) {
        perror("malloc");
        return NULL;
    }

    r->shm_id = shared_mem_create(sizeof(ring_shared_t));
    if (r->shm_id == -1) {
        free(r);
        return NULL;
    }

    r->shared = shared_mem_attach(r->shm_id);
    r->sem_id = semaphore_create(SEM_COUNT);
    if (r->shared == NULL || r->sem_id == -1) {
        ring_destroy(r);
        return NULL;
    }

    r->shared->in             = 0;
    r->shared->out            = 0;
    r->shared->flush_requests = 0;
    r->shared->bitrate_kbps   = 0;
    r->shared->position       = 0;

    if (semaphore_set(r->sem_id, SEM_FREE,  RING_SLOTS) == -1 ||
        semaphore_set(r->sem_id, SEM_FULL,  0)          == -1 ||
        semaphore_set(r->sem_id, SEM_MUTEX, 1)          == -1 ||
        semaphore_set(r->sem_id, SEM_PLAY,  1)          == -1) {
        ring_destroy(r);
        return NULL;
    }
    return r;
}

int ring_put(ring_t *r, block_type_t type, const void *data, size_t len)
{
    slot_t *slot;

    if (len > BLOCK_SIZE)
        len = BLOCK_SIZE;

    if (semaphore_down(r->sem_id, SEM_FREE) == -1)      /* Espera un lugar libre */
        return -1;
    if (semaphore_down(r->sem_id, SEM_MUTEX) == -1)
        return -1;

    slot = &r->shared->slots[r->shared->in];
    slot->type = type;
    slot->len  = len;
    if (len > 0)
        memcpy(slot->data, data, len);
    r->shared->in = (r->shared->in + 1) % RING_SLOTS;  /* Avanza en circulo */

    if (semaphore_up(r->sem_id, SEM_MUTEX) == -1)
        return -1;
    return semaphore_up(r->sem_id, SEM_FULL);           /* Avisa que hay un bloque */
}

int ring_get(ring_t *r, block_type_t *type, void *buf, size_t size)
{
    slot_t *slot;
    size_t len;

    if (semaphore_down(r->sem_id, SEM_FULL) == -1)      /* Espera un bloque */
        return -1;
    if (semaphore_down(r->sem_id, SEM_MUTEX) == -1)
        return -1;

    slot = &r->shared->slots[r->shared->out];
    *type = slot->type;
    len = slot->len < size ? slot->len : size;
    memcpy(buf, slot->data, len);
    r->shared->out = (r->shared->out + 1) % RING_SLOTS;

    if (semaphore_up(r->sem_id, SEM_MUTEX) == -1)
        return -1;
    if (semaphore_up(r->sem_id, SEM_FREE) == -1)        /* Avisa que hay un lugar */
        return -1;
    return (int)len;
}

int ring_pause(ring_t *r)
{
    return semaphore_down(r->sem_id, SEM_PLAY);
}

int ring_resume(ring_t *r)
{
    return semaphore_up(r->sem_id, SEM_PLAY);
}

int ring_wait_playing(ring_t *r)
{
    if (semaphore_down(r->sem_id, SEM_PLAY) == -1)
        return -1;
    return semaphore_up(r->sem_id, SEM_PLAY);
}

/*
 * Los datos compartidos que no son el buffer (pedidos, bitrate, posicion) los
 * escribe un proceso y los lee otro: se protegen con el mismo MUTEX.
 */

void ring_request_flush(ring_t *r)
{
    semaphore_down(r->sem_id, SEM_MUTEX);
    r->shared->flush_requests++;
    semaphore_up(r->sem_id, SEM_MUTEX);
}

int ring_flush_requests(ring_t *r)
{
    int n;
    semaphore_down(r->sem_id, SEM_MUTEX);
    n = r->shared->flush_requests;
    semaphore_up(r->sem_id, SEM_MUTEX);
    return n;
}

void ring_set_bitrate(ring_t *r, int kbps)
{
    semaphore_down(r->sem_id, SEM_MUTEX);
    r->shared->bitrate_kbps = kbps;
    semaphore_up(r->sem_id, SEM_MUTEX);
}

int ring_get_bitrate(ring_t *r)
{
    int kbps;
    semaphore_down(r->sem_id, SEM_MUTEX);
    kbps = r->shared->bitrate_kbps;
    semaphore_up(r->sem_id, SEM_MUTEX);
    return kbps;
}

void ring_set_position(ring_t *r, long long byte)
{
    semaphore_down(r->sem_id, SEM_MUTEX);
    r->shared->position = byte;
    semaphore_up(r->sem_id, SEM_MUTEX);
}

long long ring_get_position(ring_t *r)
{
    long long byte;
    semaphore_down(r->sem_id, SEM_MUTEX);
    byte = r->shared->position;
    semaphore_up(r->sem_id, SEM_MUTEX);
    return byte;
}

void ring_destroy(ring_t *r)
{
    if (r == NULL)
        return;
    if (r->sem_id != -1)
        semaphore_remove(r->sem_id);
    shared_mem_remove(r->shm_id, r->shared);
    free(r);
}
