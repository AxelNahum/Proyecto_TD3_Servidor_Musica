/*
 * server_commands.c
 * Atencion de un cliente en el proceso hijo.
 * Comandos: LIST, GET, PAUSE, RESUME, SEEK, QUIT, JOIN, LEAVE, ROOMS
 * (ver protocol.h).
 *
 * El hijo tiene que hacer dos cosas a la vez: mandar la cancion de a
 * bloques y escuchar los comandos del cliente. Para eso usa select(), que
 * espera hasta que:
 *
 *   - el socket tiene datos para leer   -> llego un comando
 *   - el socket puede aceptar mas datos -> se manda el proximo bloque
 *   - pasaron 100 ms                    -> se revisa si se puede mandar mas
 *                                          y si cambio algo en la sala
 *
 * Ritmo de envio: la cancion se manda como mucho AHEAD_SEC segundos por
 * delante de la posicion de reproduccion (que se calcula por tiempo en la
 * tabla de sesiones). Asi, si cambia algo, hay poco audio viejo en camino.
 *
 * SALAS: cuando el cliente esta en una sala, sus comandos no le contestan
 * directamente: modifican la sala (en la memoria compartida). Despues TODOS
 * los hijos de la sala (este incluido) notan el cambio en sync_room() y le
 * mandan a su cliente lo que corresponda (START, SEEKED, PAUSED, ...).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "protocol.h"
#include "server_commands.h"
#include "session_table.h"
#include "mp3_info.h"

#define MAX_SONGS   256     /* Canciones que se listan como maximo */
#define NAME_SIZE   256
#define AHEAD_SEC   5.0     /* Segundos que se manda por delante de lo que suena */
#define CHECK_USEC  100000  /* Cada cuanto se revisa (100 ms) */

static const char *songs_dir;
static const char *client_str;      /* "IP:puerto" del cliente, para los mensajes */
static int         table_row;       /* Fila de este cliente en la tabla de sesiones */

/* Lo que se le esta mandando a este cliente.
 * 'song' y 'has_song' no son lo mismo: el servidor manda la cancion un poco
 * antes de que suene, asi que puede terminar de mandarla (song = NULL)
 * mientras el cliente todavia la escucha (has_song = 1). */
static FILE *song     = NULL;       /* Archivo que se esta mandando (NULL = ya se mando todo) */
static int   has_song = 0;          /* 1 si el cliente tiene una cancion puesta */
static int   paused   = 0;          /* 1 si esta en pausa */
static char  song_path[LINE_SIZE];  /* Ruta de la cancion (para volver a abrirla en SEEK) */
static char  song_name[NAME_SIZE];  /* Nombre de la cancion */
static long long song_size = 0;     /* Tamanio en bytes */
static int   song_kbps = 0;         /* Bitrate (0 = no se pudo leer) */

/* Sala en la que esta el cliente, y la ultima version de la sala que ya se
 * le mando (para darse cuenta cuando otro integrante cambia algo) */
static int room         = -1;
static int room_version = -1;
static int room_seek_id = -1;

/* Bytes por segundo de la cancion (128 kbps = 16000 bytes por segundo) */
static double bytes_per_second(void)
{
    return song_kbps * 1000.0 / 8;
}

/* Duracion de la cancion en segundos (0 si no se sabe el bitrate) */
static double song_duration(void)
{
    return song_kbps > 0 ? song_size / bytes_per_second() : 0;
}

/* Abre la cancion 'name' de la carpeta. Devuelve 0 si se pudo, -1 si no.
 * Si no se puede, lo que sonaba sigue igual. */
static int open_song(const char *name)
{
    char path[LINE_SIZE];
    struct stat st;
    FILE *f;

    /* No se permite salir de la carpeta (por ejemplo "GET ../../etc/passwd") */
    if (name[0] == '\0' || name[0] == '.' || strchr(name, '/') != NULL)
        return -1;

    snprintf(path, sizeof(path), "%s/%s", songs_dir, name);
    if (stat(path, &st) == -1 || !S_ISREG(st.st_mode) || (f = fopen(path, "rb")) == NULL)
        return -1;

    if (song != NULL)
        fclose(song);
    song      = f;
    has_song  = 1;
    paused    = 0;
    song_size = st.st_size;
    song_kbps = mp3_bitrate_kbps(path);
    snprintf(song_path, sizeof(song_path), "%s", path);
    snprintf(song_name, sizeof(song_name), "%s", name);
    return 0;
}

/* Mueve el archivo al segundo 'sec' (lo vuelve a abrir si ya se mando todo) */
static int seek_song(double sec)
{
    if (song == NULL && (song = fopen(song_path, "rb")) == NULL)
        return -1;
    fseek(song, (long)(sec * bytes_per_second()), SEEK_SET);
    return 0;
}

/* Deja de mandar la cancion */
static void close_song(void)
{
    if (song != NULL)
        fclose(song);
    song     = NULL;
    has_song = 0;
    paused   = 0;
}

/* Segundo por el que va la reproduccion (de la sala, o propia si esta solo) */
static double playback_position(void)
{
    room_info_t info;

    if (room >= 0 && table_room_info(room, &info) == 0)
        return info.position;
    return table_get_position(table_row);
}

/* ------------------------------------------------------------------------ */
/* Sincronizacion con la sala                                               */
/* ------------------------------------------------------------------------ */

/*
 * Si la sala cambio desde la ultima vez, compara la sala con lo que tiene
 * este cliente y le manda lo que haga falta para quedar igual:
 *   - la sala no tiene cancion          -> STOPPED
 *   - la sala tiene otra cancion        -> START (desde donde va la sala)
 *   - hubo un salto (SEEK)              -> SEEKED
 *   - cambio la pausa                   -> PAUSED / RESUMED
 */
static int sync_room(int fd)
{
    room_info_t info;

    if (room < 0 || table_room_info(room, &info) == -1)
        return 0;
    if (info.version == room_version)
        return 0;                               /* No cambio nada */
    room_version = info.version;

    /* La sala no tiene cancion */
    if (info.song[0] == '\0') {
        if (!has_song)
            return 0;
        close_song();
        printf("[%s] Sala %s: sin cancion\n", client_str, info.name);
        return send_line(fd, "STOPPED");
    }

    /* La sala tiene otra cancion (o este cliente no tenia ninguna) */
    if (!has_song || strcmp(info.song, song_name) != 0) {
        if (open_song(info.song) == -1)
            return send_line(fd, "ERR no se pudo abrir la cancion de la sala");
        room_seek_id = info.seek_id;
        if (info.position > 0)
            seek_song(info.position);           /* Entra con la cancion empezada */
        printf("[%s] Sala %s: %s desde %d:%02d\n", client_str, info.name, info.song,
               (int)info.position / 60, (int)info.position % 60);
        if (send_line(fd, "START %lld %s", song_size, song_name) == -1)
            return -1;
        if (info.paused) {
            paused = 1;
            return send_line(fd, "PAUSED");
        }
        return 0;
    }

    /* Misma cancion, pero hubo un salto */
    if (info.seek_id != room_seek_id) {
        room_seek_id = info.seek_id;
        if (seek_song(info.position) == -1)
            return send_line(fd, "ERR no se pudo volver a abrir la cancion");
        paused = 0;
        if (send_line(fd, "SEEKED %d", (int)info.position) == -1)
            return -1;
    }

    /* Pausa / reanudacion */
    if (info.paused && !paused) {
        paused = 1;
        return send_line(fd, "PAUSED");
    }
    if (!info.paused && paused) {
        paused = 0;
        return send_line(fd, "RESUMED");
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Comandos                                                                 */
/* ------------------------------------------------------------------------ */

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

/* GET: pone una cancion. Solo: responde START. En sala: cambia la sala. */
static int cmd_get(int fd, const char *name)
{
    if (room >= 0) {
        /* Se abre aca para comprobar que existe y saber la duracion; despues
         * sync_room() ve que la sala cambio y manda START */
        if (open_song(name) == -1)
            return send_line(fd, "ERR no existe la cancion %s", name);
        has_song = 0;       /* Para que sync_room() la "vea" como nueva y mande START */
        room_play(room, name, song_duration(), 0);
        printf("[%s] GET %s (sala)\n", client_str, name);
        return sync_room(fd);
    }

    if (open_song(name) == -1) {
        printf("[%s] GET %s -> no existe\n", client_str, name);
        return send_line(fd, "ERR no existe la cancion %s", name);
    }
    table_play(table_row, song_name, song_duration(), 0);
    printf("[%s] GET %s -> %lld bytes, %d kbps\n", client_str, name, song_size, song_kbps);
    return send_line(fd, "START %lld %s", song_size, song_name);
}

static int cmd_pause(int fd)
{
    if (!has_song)
        return send_line(fd, "ERR no hay ninguna cancion sonando");
    printf("[%s] PAUSE\n", client_str);

    if (room >= 0) {
        room_pause(room);
        return sync_room(fd);
    }
    paused = 1;
    table_pause(table_row);
    return send_line(fd, "PAUSED");
}

static int cmd_resume(int fd)
{
    if (!has_song)
        return send_line(fd, "ERR no hay ninguna cancion sonando");
    printf("[%s] RESUME\n", client_str);

    if (room >= 0) {
        room_resume(room);
        return sync_room(fd);
    }
    paused = 0;
    table_resume(table_row);
    return send_line(fd, "RESUMED");
}

/* SEEK <seg>: va a ese segundo. SEEK +<seg> / SEEK -<seg>: adelanta / atrasa. */
static int cmd_seek(int fd, const char *arg)
{
    double seconds = atof(arg);     /* atof("+10") = 10, atof("-10") = -10 */
    double target;

    if (!has_song)
        return send_line(fd, "ERR no hay ninguna cancion sonando");
    if (song_kbps <= 0)
        return send_line(fd, "ERR no se puede hacer SEEK en esta cancion (bitrate desconocido)");

    if (arg[0] == '+' || arg[0] == '-')
        target = playback_position() + seconds;     /* Relativo */
    else
        target = seconds;                           /* Absoluto */
    if (target < 0)
        target = 0;
    if (target > song_duration())
        target = song_duration();       /* Al final: va a mandar END enseguida */

    printf("[%s] SEEK -> %d:%02d\n", client_str, (int)target / 60, (int)target % 60);

    if (room >= 0) {
        room_play(room, song_name, song_duration(), target);    /* Salto para todos */
        return sync_room(fd);
    }
    if (seek_song(target) == -1)
        return send_line(fd, "ERR no se pudo volver a abrir la cancion");
    paused = 0;
    table_play(table_row, song_name, song_duration(), target);
    return send_line(fd, "SEEKED %d", (int)target);
}

/* QUIT: saca la cancion (deja de sonar) */
static int cmd_quit(int fd)
{
    printf("[%s] QUIT\n", client_str);
    if (room >= 0) {
        room_stop(room);
        return sync_room(fd);
    }
    close_song();
    table_stop(table_row);
    return send_line(fd, "STOPPED");
}

/* JOIN <sala>: entra a la sala (si no existe, la crea con lo que esta sonando) */
static int cmd_join(int fd, const char *name)
{
    room_info_t info;
    double pos = has_song ? playback_position() : 0;
    int len = strlen(name);

    /* Nombre: entre 1 y 31 caracteres, solo letras, numeros, '-' y '_' */
    if (len == 0 || len >= ROOM_NAME_SIZE)
        return send_line(fd, "ERR nombre de sala invalido");
    for (int i = 0; i < len; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '-' && name[i] != '_')
            return send_line(fd, "ERR nombre de sala invalido (solo letras, numeros, - y _)");

    room = table_join(table_row, name, has_song ? song_name : "", song_duration(), pos, paused);
    if (room == -1)
        return send_line(fd, "ERR no hay lugar para mas salas");

    table_room_info(room, &info);
    printf("[%s] JOIN %s (%d integrantes)\n", client_str, name, info.members);

    /* Si la acaba de crear con su cancion, ya esta sincronizado. Si no, se
     * fuerza la sincronizacion para que pase a escuchar lo de la sala. */
    if (info.members == 1 && has_song && strcmp(info.song, song_name) == 0) {
        room_version = info.version;
        room_seek_id = info.seek_id;
    } else {
        room_version = -1;
        room_seek_id = -1;
    }

    if (send_line(fd, "JOINED %s %d", name, info.members) == -1)
        return -1;
    return sync_room(fd);
}

/* LEAVE: sale de la sala y sigue escuchando lo mismo, pero solo */
static int cmd_leave(int fd)
{
    room_info_t info;

    if (room < 0)
        return send_line(fd, "ERR no estas en ninguna sala");
    memset(&info, 0, sizeof(info));

    if (table_room_info(room, &info) == 0 && has_song) {
        /* Sigue desde donde iba la sala, ahora con su propio reloj */
        table_play(table_row, song_name, song_duration(), info.position);
        if (paused)
            table_pause(table_row);
    } else {
        table_stop(table_row);
    }

    printf("[%s] LEAVE %s\n", client_str, info.name);
    table_leave(table_row);
    room = -1;
    return send_line(fd, "LEFT");
}

/* ROOMS: "ROOMS <n>" y luego una linea por sala: <nombre> <integrantes> <cancion> */
static int cmd_rooms(int fd)
{
    room_info_t list[MAX_ROOMS];
    int n = table_list_rooms(list, MAX_ROOMS);

    if (send_line(fd, "ROOMS %d", n) == -1)
        return -1;
    for (int i = 0; i < n; i++)
        if (send_line(fd, "%s %d %s", list[i].name, list[i].members,
                      list[i].song[0] ? list[i].song : "--") == -1)
            return -1;
    return 0;
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
    if (strncmp(line, "JOIN ", 5) == 0)
        return cmd_join(fd, line + 5);
    if (strcmp(line, "LEAVE") == 0)
        return cmd_leave(fd);
    if (strcmp(line, "ROOMS") == 0)
        return cmd_rooms(fd);
    if (line[0] == '\0')
        return 0;
    return send_line(fd, "ERR comando desconocido: %s", line);
}

/* ------------------------------------------------------------------------ */
/* Envio de la cancion                                                      */
/* ------------------------------------------------------------------------ */

/* 1 si hay que mandar otro bloque ahora: hay cancion, no esta en pausa y
 * no se mando mas de AHEAD_SEC segundos por delante de lo que suena */
static int can_send_more(void)
{
    if (song == NULL || paused)
        return 0;
    if (song_kbps <= 0)
        return 1;                       /* Sin bitrate no se puede medir: se manda */

    double sent_sec = ftell(song) / bytes_per_second();
    return sent_sec < playback_position() + AHEAD_SEC;
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
     * queda en 1 para que PAUSE / RESUME / SEEK sigan funcionando */
    fclose(song);
    song = NULL;
    printf("[%s] Cancion enviada completa\n", client_str);
    return send_line(fd, "END");
}

void handle_client(int fd, const char *dir, const char *client, int row)
{
    char line[LINE_SIZE];
    fd_set readfds, writefds;
    struct timeval timeout, *wait;
    int want_write;

    songs_dir  = dir;
    client_str = client;
    table_row  = row;

    while (1) {
        want_write = can_send_more();

        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(fd, &readfds);                   /* Siempre: ver si llega un comando */
        if (want_write)
            FD_SET(fd, &writefds);              /* Solo si hay que mandar audio */

        /* Se espera con un timeout corto si hay que volver a revisar algo:
         * si esta en una sala (otro puede cambiarla) o si hay cancion pero ya
         * se mando suficiente. Si no, se espera sin limite a un comando. */
        wait = NULL;
        if (room >= 0 || (song != NULL && !paused && !want_write)) {
            timeout.tv_sec  = 0;
            timeout.tv_usec = CHECK_USEC;
            wait = &timeout;
        }

        if (select(fd + 1, &readfds, &writefds, NULL, wait) == -1) {
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

        /* Si otro integrante de la sala cambio algo, se le avisa a este cliente */
        if (sync_room(fd) == -1)
            break;

        if (FD_ISSET(fd, &writefds) && can_send_more()) {
            if (send_next_block(fd) == -1)
                break;
        }
    }

    if (song != NULL)
        fclose(song);
    table_remove(table_row);        /* Se borra su fila (y sale de su sala) */
    printf("[%s] Desconectado (quedan %d clientes)\n", client_str, table_count());
}
