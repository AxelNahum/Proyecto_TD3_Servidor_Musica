/*
 * ring_buffer.c
 * Buffer circular productor-consumidor con 3 semaforos:
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "ipc.h"
#include "ring_buffer.h"

#define RING_SLOTS 64       /* Lugares del buffer: 64 x 4 KB = 256 KB */

enum { SEM_FREE, SEM_FULL, SEM_MUTEX, SEM_COUNT };

/* Un lugar del buffer: un bloque recibido del socket */
typedef struct {
    size_t len;
    char   data[BLOCK_SIZE];
} slot_t;

/* Lo que vive en la memoria compartida (lo ven el padre y el hijo) */
typedef struct {
    slot_t slots[RING_SLOTS];
    int    in;              /* Proximo lugar donde escribe el productor */
    int    out;             /* Proximo lugar de donde lee el consumidor */
    int    failed;          /* 1 si el productor termino por un error */
} ring_shared_t;

/* Lo que tiene cada proceso: los ids y el puntero a la memoria compartida.
 * Despues del fork el hijo tiene una copia de esto que apunta al mismo segmento. */
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
    if (r->shared == NULL || r->sem_id == -1 || ring_reset(r) == -1) {
        ring_destroy(r);
        return NULL;
    }
    return r;
}

int ring_reset(ring_t *r)
{
    r->shared->in     = 0;
    r->shared->out    = 0;
    r->shared->failed = 0;

    if (semaphore_set(r->sem_id, SEM_FREE,  RING_SLOTS) == -1 ||
        semaphore_set(r->sem_id, SEM_FULL,  0)          == -1 ||
        semaphore_set(r->sem_id, SEM_MUTEX, 1)          == -1)
        return -1;
    return 0;
}

int ring_put(ring_t *r, const void *data, size_t len)
{
    slot_t *slot;

    if (len > BLOCK_SIZE)
        len = BLOCK_SIZE;

    if (semaphore_down(r->sem_id, SEM_FREE) == -1)      /* Espera un lugar libre */
        return -1;
    if (semaphore_down(r->sem_id, SEM_MUTEX) == -1)
        return -1;

    slot = &r->shared->slots[r->shared->in];
    slot->len = len;
    if (len > 0)
        memcpy(slot->data, data, len);
    r->shared->in = (r->shared->in + 1) % RING_SLOTS;  /* Avanza en circulo */

    if (semaphore_up(r->sem_id, SEM_MUTEX) == -1)
        return -1;
    return semaphore_up(r->sem_id, SEM_FULL);           /* Avisa que hay un bloque */
}

int ring_get(ring_t *r, void *buf, size_t size)
{
    slot_t *slot;
    size_t len;

    if (semaphore_down(r->sem_id, SEM_FULL) == -1)      /* Espera un bloque */
        return -1;
    if (semaphore_down(r->sem_id, SEM_MUTEX) == -1)
        return -1;

    slot = &r->shared->slots[r->shared->out];
    len = slot->len < size ? slot->len : size;
    memcpy(buf, slot->data, len);
    r->shared->out = (r->shared->out + 1) % RING_SLOTS;

    if (semaphore_up(r->sem_id, SEM_MUTEX) == -1)
        return -1;
    if (semaphore_up(r->sem_id, SEM_FREE) == -1)        /* Avisa que hay un lugar */
        return -1;
    return (int)len;
}

int ring_end(ring_t *r, int failed)
{
    /* Se anota antes de poner el bloque de fin: cuando el consumidor lo saque,
     * el flag ya va a estar escrito */
    r->shared->failed = failed;
    return ring_put(r, NULL, 0);
}

int ring_failed(ring_t *r)
{
    return r->shared->failed;
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
