/*
 * servidor.c
 * Servidor TCP concurrente: por cada cliente que se conecta crea un
 * proceso hijo (fork) que le envia una palabra y cierra la conexion.
 *
 * Uso: ./servidor [palabra]     (por defecto envia "Hola")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PUERTO  3490    /* Puerto donde escucha el servidor */
#define BACKLOG 10      /* Conexiones en espera como maximo */

int main(int argc, char *argv[])
{
    int sockfd, newfd;
    struct sockaddr_in mi_dir, cli_dir;
    socklen_t tam;
    int opt = 1;
    const char *palabra = (argc > 1) ? argv[1] : "Hola";

    /* Los hijos que terminan no quedan zombies */
    signal(SIGCHLD, SIG_IGN);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* Permite reusar el puerto apenas se cierra el servidor */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&mi_dir, 0, sizeof(mi_dir));
    mi_dir.sin_family      = AF_INET;
    mi_dir.sin_port        = htons(PUERTO);
    mi_dir.sin_addr.s_addr = INADDR_ANY;   /* Escucha en todas las interfaces */

    if (bind(sockfd, (struct sockaddr *)&mi_dir, sizeof(mi_dir)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Servidor escuchando en el puerto %d. Palabra: \"%s\"\n", PUERTO, palabra);

    while (1) {
        tam = sizeof(cli_dir);
        if ((newfd = accept(sockfd, (struct sockaddr *)&cli_dir, &tam)) == -1) {
            perror("accept");
            continue;
        }

        printf("Conexion desde %s\n", inet_ntoa(cli_dir.sin_addr));

        if (fork() == 0) {
            /* Proceso hijo: atiende al cliente */
            close(sockfd);
            if (send(newfd, palabra, strlen(palabra), 0) == -1)
                perror("send");
            close(newfd);
            exit(0);
        }

        /* Proceso padre: no usa este descriptor */
        close(newfd);
    }

    return 0;
}
