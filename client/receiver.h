/*
 * receiver.h
 * Proceso receptor (productor): lee todo lo que manda el servidor.
 */
#ifndef RECEIVER_H
#define RECEIVER_H

#include "ring_buffer.h"

/* Lee mensajes del socket hasta que se cierra la conexion:
 *   - DATA / START / END van al buffer circular (para el reproductor)
 *   - las demas respuestas (lista, pausa, errores) se muestran en pantalla
 * Al terminar le avisa al proceso padre con SIGTERM. */
void receiver_run(int fd, ring_t *ring);

#endif
