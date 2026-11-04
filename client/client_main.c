/*
 * client_main.c
 * Cliente TCP por consola. Al conectarse se divide en tres procesos:
 *
 *   PADRE  (consola):     lee lo que escribe el usuario y manda los comandos
 *   HIJO 1 (receptor):    lee todo lo que llega del socket (receiver.c)
 *   HIJO 2 (reproductor): saca los bloques del buffer y los reproduce (playback.c)
 *
 *   El receptor y el reproductor se pasan el audio por un buffer circular en
 *   memoria compartida (ring_buffer.c).
 *
 * Comandos:
 *   LIST            -> pide la lista de canciones
 *   PLAY <nombre>   -> reproduce la cancion (si sonaba otra, la reemplaza)
 *   PAUSE           -> pausa la cancion
 *   RESUME          -> la reanuda desde donde quedo
 *   SEEK <seg>      -> va a ese segundo; SEEK +10 / SEEK -10 adelanta / atrasa
 *   QUIT            -> saca la cancion (deja de sonar)
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
#include <sys/wait.h>
#include "protocol.h"
#include "ring_buffer.h"
#include "receiver.h"
#include "playback.h"

static ring_t *ring = NULL;     /* Buffer circular (memoria compartida) */
static pid_t main_pid;          /* PID de la consola (el padre) */
static pid_t receiver_pid = 0;
static pid_t playback_pid = 0;

/* Termina los dos hijos y borra la memoria compartida y los semaforos */
static void cleanup(void)
{
    if (receiver_pid > 0)
        kill(receiver_pid, SIGTERM);
    if (playback_pid > 0)
        kill(playback_pid, SIGTERM);
    while (wait(NULL) > 0)      /* Espera a que terminen */
        ;
    ring_destroy(ring);
}

/*
 * SEEK: el usuario escribe segundos y el servidor necesita bytes.
 *   "SEEK 30"  -> ir a 0:30
 *   "SEEK +10" -> adelantar 10 segundos
 *   "SEEK -10" -> atrasar 10 segundos
 * Para convertir se usa el bitrate que anota el reproductor:
 *   bytes por segundo = kbps * 1000 / 8   (128 kbps -> 16000 bytes/s)
 */
static int seek(int sockfd, const char *arg)
{
    int kbps = ring_get_bitrate(ring);
    long long bytes_per_sec, target;
    int seconds = atoi(arg);

    if (kbps <= 0) {
        printf("No se puede hacer SEEK: no hay ninguna cancion sonando\n> ");
        return -1;
    }
    bytes_per_sec = (long long)kbps * 1000 / 8;

    if (arg[0] == '+' || arg[0] == '-')
        target = ring_get_position(ring) + seconds * bytes_per_sec;    /* Relativo */
    else
        target = seconds * bytes_per_sec;                               /* Absoluto */
    if (target < 0)
        target = 0;

    ring_request_flush(ring);       /* El audio de la posicion vieja se tira */
    send_line(sockfd, "SEEK %lld", target);
    return 0;
}

/* Ctrl+C, o SIGTERM del receptor cuando se corta la conexion.
 * Los hijos tambien reciben las seniales, pero solo el padre limpia. */
static void on_exit_signal(int sig)
{
    (void)sig;
    if (getpid() == main_pid)
        cleanup();
    _exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd;
    int paused = 0;
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

    printf("Conectado a %s:%d\n", argv[1], PORT);

    /* El buffer se crea antes de los fork para que los dos hijos lo compartan */
    if ((ring = ring_create()) == NULL) {
        fprintf(stderr, "No se pudo crear el buffer circular\n");
        close(sockfd);
        exit(1);
    }

    main_pid = getpid();
    signal(SIGINT, on_exit_signal);
    signal(SIGTERM, on_exit_signal);
    signal(SIGCHLD, SIG_IGN);       /* Los hijos que terminan no quedan zombies */
    signal(SIGPIPE, SIG_IGN);       /* Si el servidor cierra (por ejemplo, lleno), send() da
                                     * error en vez de matar a la consola sin limpiar */
    fflush(stdout);                 /* Para que los hijos no hereden texto sin imprimir */

    /* ---- Hijo 1: receptor ---- */
    if ((receiver_pid = fork()) == -1) {
        perror("fork");
        cleanup();
        exit(1);
    }
    if (receiver_pid == 0) {
        receiver_run(sockfd, ring);
        _exit(0);
    }

    /* ---- Hijo 2: reproductor ---- */
    if ((playback_pid = fork()) == -1) {
        perror("fork");
        cleanup();
        exit(1);
    }
    if (playback_pid == 0) {
        close(sockfd);              /* El reproductor no usa el socket */
        playback_run(ring);
        _exit(0);
    }

    /* ---- Padre: consola ---- */
    printf("Comandos: LIST | PLAY <nombre> | PAUSE | RESUME | SEEK <seg> | QUIT | SALIR\n");
    send_line(sockfd, "LIST");      /* Al conectarse, pide la lista */

    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL)       /* Ctrl+D */
            break;
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') {
            printf("> ");
            fflush(stdout);
            continue;
        }
        if (strcmp(line, "SALIR") == 0)
            break;

        if (strcmp(line, "LIST") == 0) {
            send_line(sockfd, "LIST");

        } else if (strncmp(line, "PLAY ", 5) == 0) {
            ring_request_flush(ring);       /* Lo que quede de la anterior se tira */
            send_line(sockfd, "GET %s", line + 5);
            if (paused) {                   /* Una cancion nueva arranca sonando */
                ring_resume(ring);
                paused = 0;
            }

        } else if (strncmp(line, "SEEK ", 5) == 0) {
            if (seek(sockfd, line + 5) == 0 && paused) {   /* Despues de saltar sigue sonando */
                ring_resume(ring);
                paused = 0;
            }

        } else if (strcmp(line, "QUIT") == 0) {
            ring_request_flush(ring);
            send_line(sockfd, "QUIT");
            if (paused) {                   /* Para que el reproductor vacie el buffer */
                ring_resume(ring);
                paused = 0;
            }

        } else if (strcmp(line, "PAUSE") == 0) {
            if (paused) {
                printf("Ya esta en pausa\n> ");
            } else {
                send_line(sockfd, "PAUSE"); /* El servidor deja de mandar */
                ring_pause(ring);           /* El reproductor se frena ya */
                paused = 1;
            }

        } else if (strcmp(line, "RESUME") == 0) {
            if (!paused) {
                printf("No esta en pausa\n> ");
            } else {
                send_line(sockfd, "RESUME");
                ring_resume(ring);
                paused = 0;
            }

        } else {
            printf("Comando desconocido. Usa: LIST | PLAY <nombre> | PAUSE | RESUME | SEEK <seg> | QUIT | SALIR\n> ");
        }
        fflush(stdout);
    }

    cleanup();
    close(sockfd);
    return 0;
}
