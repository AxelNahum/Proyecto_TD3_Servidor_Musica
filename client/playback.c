/*
 * playback.c
 * Proceso reproductor (consumidor):
 *
 *   buffer circular --> START: abre el reproductor de MP3
 *                       DATA:  espera si esta en pausa y reproduce el bloque
 *                       END:   termina de sonar y cierra el reproductor
 *                       QUIT:  sale
 */
#include <stdio.h>
#include "protocol.h"
#include "player.h"
#include "playback.h"

void playback_run(ring_t *ring)
{
    char block[BLOCK_SIZE];
    block_type_t type;
    player_t *player = NULL;
    int len;

    while ((len = ring_get(ring, &type, block, sizeof(block))) >= 0) {

        if (type == BLOCK_START) {
            player_close(player);       /* Si sonaba otra, termina de sonar y se cierra */
            player = player_open();

        } else if (type == BLOCK_DATA) {
            /* Si la consola pidio pausa, el proceso se queda esperando aca */
            if (ring_wait_playing(ring) == -1)
                break;
            /* Si falla el audio se sigue vaciando el buffer igual, para que el
             * receptor no se quede bloqueado esperando lugar */
            if (player && player_feed(player, block, len) == -1) {
                printf("\nError al reproducir, se descarta el resto de la cancion\n> ");
                fflush(stdout);
                player_close(player);
                player = NULL;
            }

        } else if (type == BLOCK_END) {
            player_close(player);       /* Espera a que termine de sonar */
            player = NULL;
            printf("\nFin de la cancion\n> ");
            fflush(stdout);

        } else if (type == BLOCK_QUIT) {
            break;
        }
    }

    player_close(player);
}
