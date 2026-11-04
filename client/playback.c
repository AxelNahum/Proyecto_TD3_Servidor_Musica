/*
 * playback.c
 * Proceso reproductor (consumidor):
 *
 *   buffer circular --> START: abre el reproductor de MP3 (cancion nueva)
 *                       SEEK:  lo vuelve a abrir (la cancion sigue en otra parte)
 *                       STOP:  lo cierra (se saco la cancion)
 *                       DATA:  espera si esta en pausa y reproduce el bloque
 *                       END:   termina de sonar y cierra el reproductor
 *                       QUIT:  sale
 *
 * Como se tira el audio viejo (al hacer PLAY de otra, SEEK o QUIT):
 *   - La consola suma 1 a "pedidos" (ring_request_flush) y manda el comando.
 *   - El servidor responde con una marca (START, SEEKED o STOPPED).
 *   - Este proceso cuenta las marcas que va sacando del buffer. Mientras haya
 *     menos marcas que pedidos, todo lo que saca es audio viejo: lo tira.
 *   Asi se descarta lo que habia en el buffer y tambien lo que venia en
 *   camino por la red.
 */
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "player.h"
#include "playback.h"

void playback_run(ring_t *ring)
{
    char block[BLOCK_SIZE];
    block_type_t type;
    player_t *player = NULL;
    int marks = 0;              /* Marcas (START/SEEK/STOP) que ya saque del buffer */
    long long position = 0;     /* Byte de la cancion que se esta reproduciendo */
    long long offset;
    int len;

    while ((len = ring_get(ring, &type, block, sizeof(block))) >= 0) {

        if (type == BLOCK_START || type == BLOCK_SEEK || type == BLOCK_STOP) {
            /* Una marca: lo que estaba sonando se corta al instante */
            marks++;
            player_abort(player);
            player = NULL;

            /* Si hay pedidos mas nuevos, esta marca ya es vieja: se ignora */
            if (marks < ring_flush_requests(ring))
                continue;

            if (type == BLOCK_START) {
                position = 0;
                ring_set_bitrate(ring, 0);      /* Se sabe cuando se decodifique */
                player = player_open();
            } else if (type == BLOCK_SEEK) {
                memcpy(&offset, block, sizeof(offset));
                position = offset;
                player = player_open();
            } else {
                ring_set_bitrate(ring, 0);      /* STOP: no hay cancion */
            }
            ring_set_position(ring, position);

        } else if (type == BLOCK_DATA) {
            /* Si la consola pidio pausa, el proceso se queda esperando aca */
            if (ring_wait_playing(ring) == -1)
                break;

            /* Audio viejo (llego antes de la marca del ultimo pedido): se tira */
            if (marks < ring_flush_requests(ring)) {
                player_abort(player);           /* Corta lo que estaba sonando */
                player = NULL;
                continue;
            }

            /* Si falla el audio se sigue vaciando el buffer igual, para que el
             * receptor no se quede bloqueado esperando lugar */
            if (player && player_feed(player, block, len) == -1) {
                printf("\nError al reproducir, se descarta el resto de la cancion\n> ");
                fflush(stdout);
                player_close(player);
                player = NULL;
            }

            /* Anota por donde va y el bitrate, para que la consola pueda hacer SEEK */
            position += len;
            ring_set_position(ring, position);
            if (player && ring_get_bitrate(ring) == 0)
                ring_set_bitrate(ring, player_bitrate(player));

        } else if (type == BLOCK_END) {
            if (marks < ring_flush_requests(ring))
                continue;                       /* Fin de una cancion vieja */
            player_close(player);               /* Espera a que termine de sonar */
            player = NULL;
            printf("\nFin de la cancion\n> ");
            fflush(stdout);

        } else if (type == BLOCK_QUIT) {
            break;
        }
    }

    player_abort(player);
}
