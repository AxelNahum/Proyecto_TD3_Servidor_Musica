/*
 * receiver.c
 * Proceso receptor (productor): es el UNICO que lee del socket.
 *
 *   socket --> DATA/START/SEEKED/STOPPED/END --> buffer circular --> (reproductor)
 *          \-> SONGS/PAUSED/RESUMED/ERR      --> pantalla
 *
 * Tambien es el que obedece al servidor:
 *   - START / SEEKED / STOPPED ("marcas"): pide vaciar el buffer, porque el
 *     audio que habia ya no corresponde.
 *   - PAUSED / RESUMED: pausa o reanuda al reproductor.
 * Lo hace cuando LLEGA la respuesta, asi funciona igual si el comando lo dio
 * este usuario o (en una sala) otro integrante.
 *
 * Como los mensajes pueden llegar en cualquier momento, se imprimen y se
 * vuelve a mostrar el prompt "> " para que el usuario siga escribiendo.
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

static int paused = 0;      /* 1 si el reproductor esta frenado en pausa */

static void pause_player(ring_t *ring)
{
    if (!paused) {
        ring_pause(ring);
        paused = 1;
    }
}

static void resume_player(ring_t *ring)
{
    if (paused) {
        ring_resume(ring);
        paused = 0;
    }
}

/* Llego una marca: el audio que habia en el buffer ya no corresponde.
 * Se pide vaciar ANTES de poner la marca: el reproductor tira todo lo que
 * saque hasta encontrarla. Si estaba en pausa se reanuda, porque una cancion
 * nueva o un salto arrancan sonando (y asi el reproductor puede vaciar). */
static void put_mark(ring_t *ring, block_type_t type)
{
    ring_request_flush(ring);
    resume_player(ring);
    ring_put(ring, type, NULL, 0);
}

/* Lee los n nombres que siguen a "SONGS <n>" y los muestra */
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
    long long size;
    size_t len;
    int n, sec;

    while (read_line(fd, line, sizeof(line)) > 0) {

        if (sscanf(line, "DATA %zu", &len) == 1) {
            /* Un pedazo de la cancion: se lee y va al buffer */
            if (len > BLOCK_SIZE || recv_all(fd, block, len) == -1)
                break;
            if (ring_put(ring, BLOCK_DATA, block, len) == -1)
                break;

        } else if (sscanf(line, "START %lld %[^\n]", &size, name) == 2) {
            SHOW("Reproduciendo %s (%lld bytes)", name, size);
            put_mark(ring, BLOCK_START);

        } else if (sscanf(line, "SEEKED %d", &sec) == 1) {
            SHOW("Saltando a %d:%02d", sec / 60, sec % 60);
            put_mark(ring, BLOCK_SEEK);

        } else if (strcmp(line, "STOPPED") == 0) {
            SHOW("Cancion detenida");
            put_mark(ring, BLOCK_STOP);

        } else if (strcmp(line, "END") == 0) {
            ring_put(ring, BLOCK_END, NULL, 0);

        } else if (strcmp(line, "PAUSED") == 0) {
            pause_player(ring);
            SHOW("En pausa");

        } else if (strcmp(line, "RESUMED") == 0) {
            resume_player(ring);
            SHOW("Reanudado");

        } else if (sscanf(line, "SONGS %d", &n) == 1) {
            if (show_song_list(fd, n) == -1)
                break;

        } else if (strncmp(line, "ERR ", 4) == 0) {
            SHOW("Error del servidor: %s", line + 4);

        } else {
            SHOW("Mensaje desconocido del servidor: %s", line);
        }
    }

    printf("\nSe cerro la conexion con el servidor\n");
    fflush(stdout);
    resume_player(ring);                    /* Por si el reproductor estaba en pausa */
    ring_put(ring, BLOCK_QUIT, NULL, 0);    /* Que el reproductor termine */
    kill(getppid(), SIGTERM);               /* Que la consola termine y limpie */
}
