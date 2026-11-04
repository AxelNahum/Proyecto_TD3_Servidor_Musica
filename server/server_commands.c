/*
 * server_commands.c
 * Atencion de un cliente en el proceso hijo.
 * Comandos: LIST, GET, PAUSE, RESUME, SEEK, QUIT (ver protocol.h).
 *
 * El hijo tiene que hacer dos cosas a la vez: mandar la cancion de a
 * bloques y escuchar los comandos del cliente (PAUSE, RESUME, ...). Para eso
 * usa select(), que espera hasta que pase alguna de estas dos cosas:
 *
 *   - el socket tiene datos para leer   -> llego un comando
 *   - el socket puede aceptar mas datos -> se manda el proximo bloque
 *
 * (lo segundo solo se pide si hay una cancion sonando y no esta en pausa)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "protocol.h"
#include "server_commands.h"
#include "session_table.h"
#include "mp3_info.h"

#define MAX_SONGS  256      /* Canciones que se listan como maximo */
#define NAME_SIZE  256

static const char *songs_dir;
static const char *client_str;      /* "IP:puerto" del cliente, para los mensajes */
static int         table_row;       /* Fila de este cliente en la tabla de sesiones */

/* Estado de la reproduccion de este cliente.
 * 'song' y 'has_song' no son lo mismo: el servidor manda la cancion mas rapido
 * de lo que suena (el cliente la va guardando en su buffer), asi que puede
 * terminar de mandarla (song = NULL) mientras el cliente todavia la escucha
 * (has_song = 1). */
static FILE *song     = NULL;       /* Archivo que se esta mandando (NULL = ya se mando todo) */
static int   has_song = 0;          /* 1 si el cliente tiene una cancion puesta */
static int   paused   = 0;          /* 1 si el cliente pidio pausa */
static char  song_path[LINE_SIZE];  /* Ruta de la cancion puesta (para volver a abrirla en SEEK) */
static long long song_size = 0;     /* Tamanio de la cancion puesta, en bytes */
static int   song_kbps = 0;         /* Bitrate de la cancion (0 = no se pudo leer) */
static char  song_name[NAME_SIZE];  /* Nombre de la cancion puesta (para la tabla) */

/* Pasa de bytes a segundos con el bitrate (128 kbps = 16000 bytes por segundo) */
static double bytes_to_seconds(long long bytes)
{
    return song_kbps > 0 ? bytes / (song_kbps * 1000.0 / 8) : 0;
}

/*
 * Los comandos que cortan la cancion (GET, SEEK, QUIT) responden SIEMPRE con
 * exactamente una "marca": START, SEEKED o STOPPED (tambien si hubo error).
 * El cliente las cuenta para saber que audio ya no corresponde y tirarlo.
 */

/* Saca la cancion puesta */
static void stop_song(void)
{
    if (song != NULL)
        fclose(song);
    song     = NULL;
    has_song = 0;
    paused   = 0;
    table_stop(table_row);
}

/* Responde un error a GET/SEEK y saca la cancion: manda "ERR ..." y la marca "STOPPED" */
static int error_and_stop(int fd, const char *msg)
{
    stop_song();
    if (send_line(fd, "ERR %s", msg) == -1)
        return -1;
    return send_line(fd, "STOPPED");
}

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* LIST: "SONGS <n>" y luego un nombre por linea */
static int cmd_list(int fd)
{
    static char names[MAX_SONGS][NAME_SIZE];
    char path[LINE_SIZE];
    struct dirent *ent;
    struct stat st;
    int n = 0;
    DIR *dir;

    if ((dir = opendir(songs_dir)) == NULL) {
        perror("opendir");
        return send_line(fd, "ERR no se pudo abrir la carpeta de canciones");
    }

    while ((ent = readdir(dir)) != NULL && n < MAX_SONGS) {
        if (ent->d_name[0] == '.')              /* Ocultos, "." y ".." */
            continue;
        snprintf(path, sizeof(path), "%s/%s", songs_dir, ent->d_name);
        if (stat(path, &st) == -1 || !S_ISREG(st.st_mode))
            continue;                           /* Solo archivos comunes */
        snprintf(names[n++], NAME_SIZE, "%s", ent->d_name);
    }
    closedir(dir);

    qsort(names, n, NAME_SIZE, compare_names);

    if (send_line(fd, "SONGS %d", n) == -1)
        return -1;
    for (int i = 0; i < n; i++)
        if (send_line(fd, "%s", names[i]) == -1)
            return -1;

    printf("[%s] LIST -> %d canciones\n", client_str, n);
    return 0;
}

/* GET: abre la cancion y avisa "START <tamanio> <nombre>".
 * Los bloques los va mandando el loop de handle_client(). */
static int cmd_get(int fd, const char *name)
{
    char path[LINE_SIZE];
    struct stat st;
    FILE *f;

    /* No se permite salir de la carpeta (por ejemplo "GET ../../etc/passwd") */
    if (name[0] == '\0' || name[0] == '.' || strchr(name, '/') != NULL)
        return error_and_stop(fd, "nombre invalido");

    snprintf(path, sizeof(path), "%s/%s", songs_dir, name);
    if (stat(path, &st) == -1 || !S_ISREG(st.st_mode) || (f = fopen(path, "rb")) == NULL) {
        printf("[%s] GET %s -> no existe\n", client_str, name);
        return error_and_stop(fd, "no existe esa cancion");
    }

    /* Si ya habia una cancion, se reemplaza por la nueva */
    stop_song();
    song      = f;
    has_song  = 1;
    song_size = st.st_size;
    song_kbps = mp3_bitrate_kbps(path);
    snprintf(song_path, sizeof(song_path), "%s", path);
    snprintf(song_name, sizeof(song_name), "%s", name);

    /* En la tabla: reproduciendo desde el segundo 0 */
    table_play(table_row, song_name, bytes_to_seconds(song_size), 0);

    printf("[%s] GET %s -> %lld bytes, %d kbps\n", client_str, name, song_size, song_kbps);
    return send_line(fd, "START %lld %s", (long long)st.st_size, name);
}

static int cmd_pause(int fd)
{
    if (!has_song)
        return send_line(fd, "ERR no hay ninguna cancion sonando");
    paused = 1;
    table_pause(table_row);
    printf("[%s] PAUSE\n", client_str);
    return send_line(fd, "PAUSED");
}

static int cmd_resume(int fd)
{
    if (!has_song)
        return send_line(fd, "ERR no hay ninguna cancion sonando");
    paused = 0;
    table_resume(table_row);
    printf("[%s] RESUME\n", client_str);
    return send_line(fd, "RESUMED");
}

/* SEEK <byte>: sigue mandando la cancion desde esa posicion (en bytes) */
static int cmd_seek(int fd, const char *arg)
{
    long long offset = atoll(arg);

    if (!has_song)
        return error_and_stop(fd, "no hay ninguna cancion sonando");

    /* Si ya se habia mandado toda, el archivo esta cerrado: se vuelve a abrir */
    if (song == NULL && (song = fopen(song_path, "rb")) == NULL)
        return error_and_stop(fd, "no se pudo volver a abrir la cancion");

    if (offset < 0)
        offset = 0;
    if (offset > song_size)
        offset = song_size;         /* Al final: va a mandar END enseguida */

    fseek(song, offset, SEEK_SET);
    paused = 0;

    /* En la tabla: reproduciendo desde el segundo que corresponde a ese byte */
    table_play(table_row, song_name, bytes_to_seconds(song_size), bytes_to_seconds(offset));

    printf("[%s] SEEK -> byte %lld de %lld\n", client_str, offset, song_size);
    return send_line(fd, "SEEKED %lld", offset);
}

/* QUIT: saca la cancion (deja de sonar) */
static int cmd_quit(int fd)
{
    stop_song();
    printf("[%s] QUIT\n", client_str);
    return send_line(fd, "STOPPED");
}

/* Ejecuta un comando recibido. Devuelve -1 si hay que cortar la conexion. */
static int run_command(int fd, const char *line)
{
    if (strcmp(line, "LIST") == 0)
        return cmd_list(fd);
    if (strncmp(line, "GET ", 4) == 0)
        return cmd_get(fd, line + 4);
    if (strcmp(line, "PAUSE") == 0)
        return cmd_pause(fd);
    if (strcmp(line, "RESUME") == 0)
        return cmd_resume(fd);
    if (strncmp(line, "SEEK ", 5) == 0)
        return cmd_seek(fd, line + 5);
    if (strcmp(line, "QUIT") == 0)
        return cmd_quit(fd);
    if (line[0] == '\0')
        return 0;
    return send_line(fd, "ERR comando desconocido: %s", line);
}

/* Manda el proximo bloque de la cancion como "DATA <n>" + n bytes.
 * Si se termino el archivo, manda "END". */
static int send_next_block(int fd)
{
    char block[BLOCK_SIZE];
    size_t n = fread(block, 1, sizeof(block), song);

    if (n > 0) {
        if (send_line(fd, "DATA %zu", n) == -1 || send_all(fd, block, n) == -1)
            return -1;
        return 0;
    }

    /* Se termino de mandar, pero el cliente la sigue escuchando: has_song
     * queda en 1 para que PAUSE / RESUME sigan funcionando */
    fclose(song);
    song = NULL;
    printf("[%s] Cancion enviada completa\n", client_str);
    return send_line(fd, "END");
}

void handle_client(int fd, const char *dir, const char *client, int row)
{
    char line[LINE_SIZE];
    fd_set readfds, writefds;

    songs_dir  = dir;
    client_str = client;
    table_row  = row;

    while (1) {
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(fd, &readfds);                   /* Siempre: ver si llega un comando */
        if (song != NULL && !paused)
            FD_SET(fd, &writefds);              /* Solo si hay que mandar audio */

        if (select(fd + 1, &readfds, &writefds, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (FD_ISSET(fd, &readfds)) {
            if (read_line(fd, line, sizeof(line)) <= 0)
                break;                          /* El cliente se desconecto */
            if (run_command(fd, line) == -1)
                break;
        }

        if (FD_ISSET(fd, &writefds) && song != NULL && !paused) {
            if (send_next_block(fd) == -1)
                break;
        }
    }

    if (song != NULL)
        fclose(song);
    table_remove(table_row);        /* El cliente ya no esta: se borra su fila */
    printf("[%s] Desconectado (quedan %d clientes)\n", client_str, table_count());
}
