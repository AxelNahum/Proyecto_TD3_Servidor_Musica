/*
 * playback.h
 * Proceso reproductor (consumidor): saca los bloques del buffer circular y
 * los hace sonar con el reproductor de MP3 (player.c).
 */
#ifndef PLAYBACK_H
#define PLAYBACK_H

#include "ring_buffer.h"

/* Reproduce lo que llega al buffer hasta recibir BLOCK_QUIT. */
void playback_run(ring_t *ring);

#endif
