/*
 * client_main.c
 * Cliente TCP por consola: se conecta al servidor, muestra la lista de
 * canciones y luego acepta comandos (ver client_commands.c):
 *
 *   LIST            -> vuelve a pedir la lista de canciones
 *   PLAY <nombre>   -> reproduce la cancion mientras la recibe (streaming)
 *   GET <nombre>    -> descarga la cancion en la carpeta "received/"
 *   SALIR           -> cierra el cliente
 *
 * Uso: ./bin/client <IP del servidor>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "protocol.h"
#include "client_commands.h"
#include "ring_buffer.h"

static ring_t *ring = NULL;     /* Buffer circular (memoria compartida) */
static pid_t main_pid;          /* Para saber si el que recibe la senial es el padre */

/* Ctrl+C: hay que borrar la memoria compartida y los semaforos antes de salir,
 * si no quedan creados en el sistema (se verian con "ipcs"). El proceso hijo
 * tambien recibe la senial, pero solo el padre borra. */
static void on_sigint(int sig)
{
    (void)sig;
    if (getpid() == main_pid)
        ring_destroy(ring);
    _exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, r;
    char line[LINE_SIZE];
    struct sockaddr_in server_addr;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <IP del servidor>\n", argv[0]);
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "IP invalida: %s\n", argv[1]);
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Conectado a %s:%d\n\n", argv[1], PORT);

    /* Crea el buffer circular una sola vez; se reutiliza en cada cancion */
    if ((ring = ring_create()) == NULL) {
        fprintf(stderr, "No se pudo crear el buffer circular\n");
        close(sockfd);
        exit(1);
    }
    main_pid = getpid();
    signal(SIGINT, on_sigint);
    /* El receptor (hijo) que termina no queda zombie, igual que en el servidor */
    signal(SIGCHLD, SIG_IGN);

    /* Al conectarse, muestra la lista de canciones */
    if (request_list(sockfd) == -1) {
        printf("Se cerro la conexion con el servidor\n");
        ring_destroy(ring);
        close(sockfd);
        return 1;
    }

    printf("\nComandos: LIST | PLAY <nombre> | GET <nombre> | SALIR\n");

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)       /* Ctrl+D */
            break;
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            continue;
        if (strcmp(line, "SALIR") == 0)
            break;

        if (strcmp(line, "LIST") == 0)
            r = request_list(sockfd);
        else if (strncmp(line, "PLAY ", 5) == 0)
            r = request_song(sockfd, line + 5, SONG_PLAY, ring);
        else if (strncmp(line, "GET ", 4) == 0)
            r = request_song(sockfd, line + 4, SONG_SAVE, ring);
        else {
            printf("Comando desconocido. Usa: LIST | PLAY <nombre> | GET <nombre> | SALIR\n");
            r = 0;
        }

        if (r == -1) {
            printf("Se cerro la conexion con el servidor\n");
            break;
        }
    }

    ring_destroy(ring);
    close(sockfd);
    return 0;
}
