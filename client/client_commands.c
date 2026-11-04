/*
 * client_commands.c
 * Implementacion de los pedidos del cliente al servidor (LIST, GET).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
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

/* Muestra el avance como porcentaje */
static void show_progress(long long done, long long size)
{
    printf("\r  %3lld%%  (%lld / %lld bytes)", size ? done * 100 / size : 100, done, size);
    fflush(stdout);
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

/*
 * Recibe 'size' bytes del socket en bloques.
 * Si 'f' no es NULL los guarda en el archivo; si 'ring' no es NULL los pone
 * en el buffer circular. Devuelve 0 si llego todo, -1 si se corto la conexion.
 */
static int receive_blocks(int fd, long long size, FILE *f, ring_t *ring)
{
    char block[BLOCK_SIZE];
    long long received = 0;

    while (received < size) {
        size_t want = sizeof(block);
        if (size - received < (long long)want)
            want = size - received;

        ssize_t n = recv(fd, block, want, 0);
        if (n == -1 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;

        if (f)
            fwrite(block, 1, n, f);
        if (ring && ring_put(ring, block, n) == -1)
            return -1;

        received += n;
        if (f)
            show_progress(received, size);
    }
    return 0;
}

/* GET: guarda la cancion en received/<nombre> */
static int save_song(int fd, const char *name, long long size)
{
    char path[LINE_SIZE];
    FILE *f;
    int r;

    /* Si no se puede crear el archivo, igual hay que recibir los datos para
     * no desincronizar el protocolo: se descartan */
    mkdir(RECEIVED_DIR, 0755);
    snprintf(path, sizeof(path), "%s/%s", RECEIVED_DIR, name);
    if ((f = fopen(path, "wb")) == NULL) {
        perror(path);
        f = fopen("/dev/null", "wb");
    }

    printf("Recibiendo %s (%lld bytes)...\n", name, size);
    r = receive_blocks(fd, size, f, NULL);
    printf("\n");
    if (f)
        fclose(f);

    if (r == -1)
        printf("Se corto la conexion durante la descarga\n");
    else
        printf("Guardada en %s\n", path);
    return r;
}

/*
 * PLAY: reproduce la cancion mientras la recibe, con dos procesos:
 *
 *   HIJO  (receptor, productor):    socket --> ring_put()
 *   PADRE (reproductor, consumidor): ring_get() --> player_feed() --> parlantes
 *
 * El receptor pone un bloque de largo 0 al final para avisar que termino.
 */
static int play_song(int fd, const char *name, long long size, ring_t *ring)
{
    char block[BLOCK_SIZE];
    long long played = 0;
    player_t *player;
    pid_t pid;
    int len;

    if (ring_reset(ring) == -1)
        return -1;

    fflush(stdout);     /* Para que el hijo no herede texto sin imprimir */

    pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ---- Proceso hijo: receptor ---- */
        int r = receive_blocks(fd, size, NULL, ring);
        ring_end(ring, r == -1);    /* Fin de la cancion (y si hubo error) */
        _exit(0);                   /* _exit: no vacia los buffers del padre */
    }

    /* ---- Proceso padre: reproductor ---- */
    if ((player = player_open()) == NULL)
        printf("No se pudo abrir el audio, se descarta la cancion\n");
    printf("Reproduciendo %s (Ctrl+C para cortar)...\n", name);

    /* Aunque falle el audio hay que seguir vaciando el buffer: si no, el
     * receptor se queda bloqueado esperando un lugar libre */
    while ((len = ring_get(ring, block, sizeof(block))) > 0) {
        if (player && player_feed(player, block, len) == -1) {
            printf("\nError al reproducir, se descarta el resto\n");
            player_close(player);
            player = NULL;
        }
        played += len;
        show_progress(played, size);
    }
    printf("\n");

    player_close(player);       /* Espera a que termine de sonar */

    /* Si fallo el buffer, el receptor puede estar bloqueado: se lo termina.
     * No hace falta waitpid(): SIGCHLD esta ignorada (ver client_main.c), asi
     * que el sistema limpia al hijo cuando termina y no queda zombie. */
    if (len == -1)
        kill(pid, SIGTERM);

    /* Se fija si llego toda la cancion */
    if (len == -1 || ring_failed(ring)) {
        printf("Se corto la conexion durante la recepcion\n");
        return -1;
    }

    printf("Fin de la cancion\n");
    return 0;
}

int request_song(int fd, const char *name, song_mode_t mode, ring_t *ring)
{
    char line[LINE_SIZE];
    long long size;

    if (send_line(fd, "GET %s", name) == -1 || read_line(fd, line, sizeof(line)) <= 0)
        return -1;

    if (sscanf(line, "OK %lld", &size) != 1) {
        show_error(line);
        return 0;
    }

    if (mode == SONG_SAVE)
        return save_song(fd, name, size);
    return play_song(fd, name, size, ring);
}
