/*
 * ring_buffer.h
 * Buffer circular en memoria compartida entre el receptor (proceso hijo,
 * productor) y el reproductor (proceso padre, consumidor).
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>

typedef struct ring ring_t;

/* Crea la memoria compartida y los semaforos. Se llama UNA vez, antes del fork.
 * Devuelve NULL si hubo error. */
ring_t *ring_create(void);

/* Deja el buffer vacio (para empezar una cancion nueva). */
int ring_reset(ring_t *r);

/* PRODUCTOR: guarda un bloque. Se bloquea si el buffer esta lleno.
 * Un bloque de largo 0 indica "fin de la cancion". */
int ring_put(ring_t *r, const void *data, size_t len);

/* PRODUCTOR: avisa que termino, poniendo el bloque de fin.
 * 'failed' = 1 si termino por un error (por ejemplo, se corto la conexion). */
int ring_end(ring_t *r, int failed);

/* CONSUMIDOR: despues de sacar el bloque de fin, dice si el productor fallo. */
int ring_failed(ring_t *r);

/* CONSUMIDOR: saca el bloque mas viejo y lo copia en 'buf' (de 'size' bytes).
 * Se bloquea si el buffer esta vacio.
 * Devuelve el largo del bloque (0 = fin de la cancion) o -1 si hubo error. */
int ring_get(ring_t *r, void *buf, size_t size);

/* Borra la memoria compartida y los semaforos del sistema. */
void ring_destroy(ring_t *r);

#endif
