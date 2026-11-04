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
 * Como se tira el audio viejo (cancion nueva, SEEK o QUIT):
 *   - Cuando llega una marca (START, SEEKED o STOPPED), el receptor suma 1 a
 *     "pedidos" (ring_request_flush) y despues pone la marca en el buffer.
 *   - Este proceso cuenta las marcas que va sacando del buffer. Mientras haya
 *     menos marcas que pedidos, todo lo que saca es audio viejo: lo tira.
 */
#include <stdio.h>
#include "protocol.h"
#include "output.h"
#include "player.h"
#include "playback.h"

void playback_run(ring_t *ring)
{
    char block[BLOCK_SIZE];
    block_type_t type;
    player_t *player = NULL;
    int marks = 0;              /* Marcas (START/SEEK/STOP) que ya saque del buffer */
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

            if (type != BLOCK_STOP)
                player = player_open();         /* START o SEEK: vuelve a sonar */

        } else if (type == BLOCK_DATA) {
            /* Si esta en pausa, el proceso se queda esperando aca */
            if (ring_wait_playing(ring) == -1)
                break;

            /* Audio viejo (llego antes de la ultima marca): se tira */
            if (marks < ring_flush_requests(ring)) {
                player_abort(player);           /* Corta lo que estaba sonando */
                player = NULL;
                continue;
            }

            /* Si falla el audio se sigue vaciando el buffer igual, para que el
             * receptor no se quede bloqueado esperando lugar */
            if (player && player_feed(player, block, len) == -1) {
                show_message("Error al reproducir, se descarta el resto de la cancion");
                show_event("ERROR no se pudo reproducir el audio");
                player_close(player);
                player = NULL;
            }

        } else if (type == BLOCK_END) {
            if (marks < ring_flush_requests(ring))
                continue;                       /* Fin de una cancion vieja */
            player_close(player);               /* Espera a que termine de sonar */
            player = NULL;
            show_message("Fin de la cancion");
            show_event("FINISHED");

        } else if (type == BLOCK_QUIT) {
            break;
        }
    }

    player_abort(player);
}
