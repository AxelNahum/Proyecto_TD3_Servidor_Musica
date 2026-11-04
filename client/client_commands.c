/*
 * client_commands.c
 * Implementacion de los pedidos del cliente al servidor (LIST, GET).
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "protocol.h"
#include "client_commands.h"
#include "player.h"

/* Muestra un error del servidor ("ERR ...") o una respuesta inesperada */
static void show_error(const char *resp)
{
    if (strncmp(resp, "ERR ", 4) == 0)
        printf("Error del servidor: %s\n", resp + 4);
    else
        printf("Respuesta inesperada: %s\n", resp);
}

int request_list(int fd)
{
    char line[LINE_SIZE];
    int n;

    if (send_line(fd, "LIST") == -1 || read_line(fd, line, sizeof(line)) <= 0)
        return -1;

    if (sscanf(line, "OK %d", &n) != 1) {
        show_error(line);
        return 0;
    }

    printf("Canciones disponibles (%d):\n", n);
    for (int i = 0; i < n; i++) {
        if (read_line(fd, line, sizeof(line)) <= 0)
            return -1;
        printf("  %d. %s\n", i + 1, line);
    }
    return 0;
}

int request_song(int fd, const char *name, song_mode_t mode)
{
    char line[LINE_SIZE];
    char path[LINE_SIZE];
    char block[BLOCK_SIZE];
    long long size, received = 0;
    FILE *f = NULL;
    player_t *player = NULL;

    if (send_line(fd, "GET %s", name) == -1 || read_line(fd, line, sizeof(line)) <= 0)
        return -1;

    if (sscanf(line, "OK %lld", &size) != 1) {
        show_error(line);
        return 0;
    }

    /* Si no se puede guardar o reproducir, igual hay que recibir los datos
     * para no desincronizar el protocolo: se descartan */
    if (mode == SONG_SAVE) {
        mkdir(RECEIVED_DIR, 0755);
        snprintf(path, sizeof(path), "%s/%s", RECEIVED_DIR, name);
        if ((f = fopen(path, "wb")) == NULL)
            perror(path);
        printf("Recibiendo %s (%lld bytes)...\n", name, size);
    } else {
        if ((player = player_open()) == NULL)
            printf("No se pudo abrir el audio, se descarta la cancion\n");
        printf("Reproduciendo %s (Ctrl+C para cortar)...\n", name);
    }

    while (received < size) {
        size_t want = sizeof(block);
        if (size - received < (long long)want)
            want = size - received;

        ssize_t n = recv(fd, block, want, 0);
        if (n == -1 && errno == EINTR)
            continue;
        if (n <= 0) {
            printf("\nSe corto la conexion durante la recepcion\n");
            if (f)
                fclose(f);
            player_close(player);
            return -1;
        }

        if (f)
            fwrite(block, 1, n, f);
        if (player && player_feed(player, block, n) == -1) {
            printf("\nError al reproducir, se descarta el resto\n");
            player_close(player);
            player = NULL;
        }

        received += n;
        printf("\r  %3lld%%  (%lld / %lld bytes)", received * 100 / size, received, size);
        fflush(stdout);
    }
    printf("\n");

    if (f) {
        fclose(f);
        printf("Guardada en %s\n", path);
    }
    if (player) {
        player_close(player);   /* Espera a que termine de sonar */
        printf("Fin de la cancion\n");
    }
    return 0;
}
