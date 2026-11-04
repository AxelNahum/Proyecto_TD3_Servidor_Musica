/*
 * server_commands.c
 * Implementacion de los comandos del protocolo del lado del servidor:
 *
 *   LIST            -> lista de canciones de la carpeta
 *   GET <nombre>    -> tamanio del archivo y luego el archivo en bloques
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "protocol.h"
#include "server_commands.h"

#define MAX_SONGS  256      /* Canciones que se listan como maximo */
#define NAME_SIZE  256

static const char *songs_dir;
static const char *client_str;      /* "IP:puerto" del cliente, para los mensajes */

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Responde a LIST: "OK <n>" y luego un nombre por linea */
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

    if (send_line(fd, "OK %d", n) == -1)
        return -1;
    for (int i = 0; i < n; i++)
        if (send_line(fd, "%s", names[i]) == -1)
            return -1;

    printf("[%s] LIST -> %d canciones\n", client_str, n);
    return 0;
}

/* Responde a GET: "OK <tamanio>" y luego el archivo en bloques */
static int cmd_get(int fd, const char *name)
{
    char path[LINE_SIZE];
    char block[BLOCK_SIZE];
    struct stat st;
    size_t nread;
    FILE *f = NULL;

    /* No se permite salir de la carpeta (por ejemplo "GET ../../etc/passwd") */
    if (name[0] == '\0' || name[0] == '.' || strchr(name, '/') != NULL)
        return send_line(fd, "ERR nombre invalido");

    snprintf(path, sizeof(path), "%s/%s", songs_dir, name);
    if (stat(path, &st) == -1 || !S_ISREG(st.st_mode) ||
        (f = fopen(path, "rb")) == NULL) {
        printf("[%s] GET %s -> no existe\n", client_str, name);
        return send_line(fd, "ERR no existe la cancion %s", name);
    }

    printf("[%s] GET %s -> enviando %lld bytes\n",
           client_str, name, (long long)st.st_size);

    if (send_line(fd, "OK %lld", (long long)st.st_size) == -1) {
        fclose(f);
        return -1;
    }

    while ((nread = fread(block, 1, sizeof(block), f)) > 0) {
        if (send_all(fd, block, nread) == -1) {
            perror("send");
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    printf("[%s] GET %s -> enviada\n", client_str, name);
    return 0;
}

void handle_client(int fd, const char *dir, const char *client)
{
    char line[LINE_SIZE];
    int r;

    songs_dir  = dir;
    client_str = client;

    while ((r = read_line(fd, line, sizeof(line))) > 0) {
        if (strcmp(line, "LIST") == 0)
            r = cmd_list(fd);
        else if (strncmp(line, "GET ", 4) == 0)
            r = cmd_get(fd, line + 4);
        else if (line[0] == '\0')
            continue;
        else
            r = send_line(fd, "ERR comando desconocido: %s", line);

        if (r == -1)
            break;
    }

    printf("[%s] Desconectado\n", client_str);
}
