/*
 * ring_buffer.h
 * Buffer circular en memoria compartida entre el receptor (productor) y el
 * reproductor (consumidor), mas un semaforo para pausar la reproduccion.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>

/* Tipos de bloque que pasan por el buffer */
typedef enum {
    BLOCK_START,    /* Marca: empieza una cancion nueva */
    BLOCK_SEEK,     /* Marca: la cancion sigue desde otra posicion */
    BLOCK_STOP,     /* Marca: se saco la cancion */
    BLOCK_DATA,     /* Un pedazo de la cancion (MP3) */
    BLOCK_END,      /* Termino la cancion */
    BLOCK_QUIT      /* Se cerro la conexion: el reproductor tiene que terminar */
} block_type_t;

typedef struct ring ring_t;

/* Crea la memoria compartida y los semaforos. Se llama UNA vez, antes de los
 * fork. Devuelve NULL si hubo error. */
ring_t *ring_create(void);

/* PRODUCTOR: guarda un bloque. Se bloquea si el buffer esta lleno. */
int ring_put(ring_t *r, block_type_t type, const void *data, size_t len);

/* CONSUMIDOR: saca el bloque mas viejo. Se bloquea si el buffer esta vacio.
 * Deja el tipo en '*type' y los datos en 'buf' (de 'size' bytes).
 * Devuelve el largo de los datos o -1 si hubo error. */
int ring_get(ring_t *r, block_type_t *type, void *buf, size_t size);

/* Pausa: el reproductor se queda esperando en ring_wait_playing() hasta
 * que se llame a ring_resume(). Las usa el receptor (al llegar PAUSED/RESUMED). */
int ring_pause(ring_t *r);
int ring_resume(ring_t *r);

/* REPRODUCTOR: si esta en pausa espera (sin gastar procesador); si no, sigue. */
int ring_wait_playing(ring_t *r);

/* Vaciado del buffer (cancion nueva, SEEK, QUIT):
 * el receptor llama a ring_request_flush() al recibir una marca, ANTES de
 * ponerla en el buffer. El reproductor compara los pedidos con las marcas
 * que ya saco: mientras haya menos marcas que pedidos, el audio que saca es
 * viejo. */
void ring_request_flush(ring_t *r);
int  ring_flush_requests(ring_t *r);

/* Borra la memoria compartida y los semaforos del sistema. */
void ring_destroy(ring_t *r);

#endif
