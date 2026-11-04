/*
 * receiver.c
 * Proceso receptor (productor): es el UNICO que lee del socket.
 *
 *   socket --> DATA/START/SEEKED/STOPPED/END          --> buffer circular --> (reproductor)
 *          \-> SONGS/ROOMS/JOINED/LEFT/PAUSED/RESUMED/ERR --> pantalla
 *
 * Tambien es el que obedece al servidor:
 *   - START / SEEKED / STOPPED ("marcas"): pide vaciar el buffer, porque el
 *     audio que habia ya no corresponde.
 *   - PAUSED / RESUMED: pausa o reanuda al reproductor.
 * Lo hace cuando LLEGA la respuesta, asi funciona igual si el comando lo dio
 * este usuario o (en una sala) otro integrante.
 *
 * Lo que muestra depende del modo (ver output.h): mensajes para la consola,
 * o eventos de una linea para la interfaz grafica (--gui):
 *
 *   SONGS <n>           y luego n lineas  SONG <nombre>
 *   ROOMS <n>           y luego n lineas  ROOM <sala> <integrantes> <cancion>
 *   PLAYING <duracion> <desde> <nombre>
 *   SEEKED <seg> | PAUSED | RESUMED | STOPPED
 *   JOINED <sala> <integrantes> | LEFT
 *   ERROR <mensaje> | DISCONNECTED
 *
 * Los nombres de canciones van siempre al final de la linea, porque pueden
 * tener espacios.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "protocol.h"
#include "output.h"
#include "receiver.h"

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

    if (gui_mode)
        show_event("SONGS %d", n);
    else
        printf("\nCanciones disponibles (%d):\n", n);

    for (int i = 0; i < n; i++) {
        if (read_line(fd, line, sizeof(line)) <= 0)
            return -1;
        if (gui_mode)
            show_event("SONG %s", line);
        else
            printf("  %d. %s\n", i + 1, line);
    }

    if (!gui_mode) {
        printf("> ");
        fflush(stdout);
    }
    return 0;
}

/* Lee las n lineas que siguen a "ROOMS <n>" (<sala> <integrantes> <cancion>) y las muestra */
static int show_room_list(int fd, int n)
{
    char line[LINE_SIZE], name[LINE_SIZE], song[LINE_SIZE];
    int members;

    show_event("ROOMS %d", n);
    if (n == 0)
        show_message("No hay ninguna sala. Crea una con JOIN <nombre>");
    else if (!gui_mode)
        printf("\nSalas (%d):\n", n);

    for (int i = 0; i < n; i++) {
        if (read_line(fd, line, sizeof(line)) <= 0)
            return -1;
        if (sscanf(line, "%s %d %[^\n]", name, &members, song) != 3)
            continue;
        if (gui_mode)
            show_event("ROOM %s %d %s", name, members, song);
        else
            printf("  %-12s %d integrante(s)  - %s\n", name, members, song);
    }

    if (n > 0 && !gui_mode) {
        printf("> ");
        fflush(stdout);
    }
    return 0;
}

void receiver_run(int fd, ring_t *ring)
{
    char line[LINE_SIZE];
    char block[BLOCK_SIZE];
    char name[LINE_SIZE];
    long long size;
    size_t len;
    int n, sec, duration;

    while (read_line(fd, line, sizeof(line)) > 0) {

        if (sscanf(line, "DATA %zu", &len) == 1) {
            /* Un pedazo de la cancion: se lee y va al buffer */
            if (len > BLOCK_SIZE || recv_all(fd, block, len) == -1)
                break;
            if (ring_put(ring, BLOCK_DATA, block, len) == -1)
                break;

        } else if (sscanf(line, "START %lld %d %d %[^\n]", &size, &duration, &sec, name) == 4) {
            show_message("Reproduciendo %s (%d:%02d)", name, duration / 60, duration % 60);
            show_event("PLAYING %d %d %s", duration, sec, name);
            put_mark(ring, BLOCK_START);

        } else if (sscanf(line, "SEEKED %d", &sec) == 1) {
            show_message("Saltando a %d:%02d", sec / 60, sec % 60);
            show_event("SEEKED %d", sec);
            put_mark(ring, BLOCK_SEEK);

        } else if (strcmp(line, "STOPPED") == 0) {
            show_message("Cancion detenida");
            show_event("STOPPED");
            put_mark(ring, BLOCK_STOP);

        } else if (strcmp(line, "END") == 0) {
            ring_put(ring, BLOCK_END, NULL, 0);

        } else if (strcmp(line, "PAUSED") == 0) {
            pause_player(ring);
            show_message("En pausa");
            show_event("PAUSED");

        } else if (strcmp(line, "RESUMED") == 0) {
            resume_player(ring);
            show_message("Reanudado");
            show_event("RESUMED");

        } else if (sscanf(line, "SONGS %d", &n) == 1) {
            if (show_song_list(fd, n) == -1)
                break;

        } else if (sscanf(line, "ROOMS %d", &n) == 1) {
            if (show_room_list(fd, n) == -1)
                break;

        } else if (sscanf(line, "JOINED %s %d", name, &n) == 2) {
            show_message("Entraste a la sala %s (%d integrante(s))", name, n);
            show_event("JOINED %s %d", name, n);

        } else if (strcmp(line, "LEFT") == 0) {
            show_message("Saliste de la sala: seguis escuchando solo");
            show_event("LEFT");

        } else if (strncmp(line, "ERR ", 4) == 0) {
            show_message("Error del servidor: %s", line + 4);
            show_event("ERROR %s", line + 4);

        } else {
            show_message("Mensaje desconocido del servidor: %s", line);
            show_event("ERROR mensaje desconocido del servidor: %s", line);
        }
    }

    show_message("Se cerro la conexion con el servidor");
    show_event("DISCONNECTED");
    resume_player(ring);                    /* Por si el reproductor estaba en pausa */
    ring_put(ring, BLOCK_QUIT, NULL, 0);    /* Que el reproductor termine */
    kill(getppid(), SIGTERM);               /* Que la consola termine y limpie */
}
