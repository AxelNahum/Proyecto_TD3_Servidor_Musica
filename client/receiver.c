/*
 * receiver.c
 * Proceso receptor (productor): es el UNICO que lee del socket.
 *
 *   socket --> DATA/START/SEEKED/STOPPED/END --> buffer circular --> (reproductor)
 *          \-> SONGS/PAUSED/RESUMED/ERR      --> pantalla
 *
 * Como los mensajes pueden llegar en cualquier momento (por ejemplo la
 * respuesta a un PAUSE en medio de la cancion), se imprimen y se vuelve a
 * mostrar el prompt "> " para que el usuario siga escribiendo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "protocol.h"
#include "receiver.h"

/* Imprime un mensaje y vuelve a mostrar el prompt de la consola */
#define SHOW(...) do { printf("\n"); printf(__VA_ARGS__); printf("\n> "); fflush(stdout); } while (0)

/* Lee "SONGS <n>" y los n nombres que siguen, y los muestra */
static int show_song_list(int fd, int n)
{
    char line[LINE_SIZE];

    printf("\nCanciones disponibles (%d):\n", n);
    for (int i = 0; i < n; i++) {
        if (read_line(fd, line, sizeof(line)) <= 0)
            return -1;
        printf("  %d. %s\n", i + 1, line);
    }
    printf("> ");
    fflush(stdout);
    return 0;
}

void receiver_run(int fd, ring_t *ring)
{
    char line[LINE_SIZE];
    char block[BLOCK_SIZE];
    char name[LINE_SIZE];
    long long size, offset;
    size_t len;
    int n;

    while (read_line(fd, line, sizeof(line)) > 0) {

        if (sscanf(line, "DATA %zu", &len) == 1) {
            /* Un pedazo de la cancion: se lee y va al buffer */
            if (len > BLOCK_SIZE || recv_all(fd, block, len) == -1)
                break;
            if (ring_put(ring, BLOCK_DATA, block, len) == -1)
                break;

        } else if (sscanf(line, "START %lld %[^\n]", &size, name) == 2) {
            SHOW("Reproduciendo %s (%lld bytes)", name, size);
            ring_put(ring, BLOCK_START, NULL, 0);

        } else if (sscanf(line, "SEEKED %lld", &offset) == 1) {
            int kbps = ring_get_bitrate(ring);
            if (kbps > 0) {
                long long sec = offset / (kbps * 1000 / 8);
                SHOW("Saltando a %lld:%02lld", sec / 60, sec % 60);
            }
            ring_put(ring, BLOCK_SEEK, &offset, sizeof(offset));

        } else if (strcmp(line, "STOPPED") == 0) {
            SHOW("Cancion detenida");
            ring_put(ring, BLOCK_STOP, NULL, 0);

        } else if (strcmp(line, "END") == 0) {
            ring_put(ring, BLOCK_END, NULL, 0);

        } else if (sscanf(line, "SONGS %d", &n) == 1) {
            if (show_song_list(fd, n) == -1)
                break;

        } else if (strcmp(line, "PAUSED") == 0) {
            SHOW("En pausa");

        } else if (strcmp(line, "RESUMED") == 0) {
            SHOW("Reanudado");

        } else if (strncmp(line, "ERR ", 4) == 0) {
            SHOW("Error del servidor: %s", line + 4);

        } else {
            SHOW("Mensaje desconocido del servidor: %s", line);
        }
    }

    printf("\nSe cerro la conexion con el servidor\n");
    fflush(stdout);
    ring_put(ring, BLOCK_QUIT, NULL, 0);    /* Que el reproductor termine */
    kill(getppid(), SIGTERM);               /* Que la consola termine y limpie */
}
