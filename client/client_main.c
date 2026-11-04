/*
 * client_main.c
 * Cliente TCP por consola. Al conectarse se divide en tres procesos:
 *
 *   PADRE  (consola):     lee lo que escribe el usuario y manda los comandos
 *   HIJO 1 (receptor):    lee todo lo que llega del socket y obedece las
 *                         respuestas (pausar, vaciar el buffer) (receiver.c)
 *   HIJO 2 (reproductor): saca los bloques del buffer y los reproduce (playback.c)
 *
 *   El receptor y el reproductor se pasan el audio por un buffer circular en
 *   memoria compartida (ring_buffer.c).
 *
 * La consola solo manda comandos: no pausa ni vacia nada por su cuenta. Eso
 * lo hace el receptor cuando llega la respuesta del servidor.
 *
 * Comandos:
 *   LIST            -> pide la lista de canciones
 *   PLAY <nombre>   -> reproduce la cancion (si sonaba otra, la reemplaza)
 *   PAUSE           -> pausa la cancion
 *   RESUME          -> la reanuda desde donde quedo
 *   SEEK <seg>      -> va a ese segundo; SEEK +10 / SEEK -10 adelanta / atrasa
 *   QUIT            -> saca la cancion (deja de sonar)
 *   ROOMS           -> lista las salas
 *   JOIN <sala>     -> entra a una sala (si no existe, la crea)
 *   LEAVE           -> sale de la sala y sigue escuchando solo
 *   SALIR           -> cierra el cliente
 *
 * En una sala, PLAY / PAUSE / RESUME / SEEK / QUIT afectan a todos los
 * integrantes.
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
#include "output.h"
#include "ring_buffer.h"
#include "receiver.h"
#include "playback.h"

#define HELP "Comandos: LIST | PLAY <nombre> | PAUSE | RESUME | SEEK <seg> | QUIT |\n" \
             "          ROOMS | JOIN <sala> | LEAVE | SALIR"

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
    char line[LINE_SIZE];
    struct sockaddr_in server_addr;

    /* Uso: ./bin/client <IP> [--gui] */
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--gui") != 0)) {
        fprintf(stderr, "Uso: %s <IP del servidor> [--gui]\n", argv[0]);
        exit(1);
    }
    gui_mode = (argc == 3);     /* Se hereda en los fork: los tres procesos lo ven */

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

    if (gui_mode)
        show_event("CONNECTED %s %d", argv[1], PORT);
    else
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

    /* ---- Padre: consola ----
     * En modo --gui los comandos los escribe la interfaz grafica (por un
     * pipe) en vez del usuario; el resto funciona igual. */
    if (!gui_mode)
        printf("%s\n", HELP);
    send_line(sockfd, "LIST");      /* Al conectarse, pide la lista */

    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL)       /* Ctrl+D, o se cerro la interfaz */
            break;
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') {
            show_message("");
            continue;
        }
        if (strcmp(line, "SALIR") == 0)
            break;

        /* Los comandos van al servidor tal cual, salvo PLAY, que en el
         * protocolo se llama GET */
        if (strncmp(line, "PLAY ", 5) == 0) {
            send_line(sockfd, "GET %s", line + 5);
        } else if (strcmp(line, "LIST") == 0 || strcmp(line, "PAUSE") == 0 ||
                   strcmp(line, "RESUME") == 0 || strcmp(line, "QUIT") == 0 ||
                   strncmp(line, "SEEK ", 5) == 0 || strcmp(line, "ROOMS") == 0 ||
                   strncmp(line, "JOIN ", 5) == 0 || strcmp(line, "LEAVE") == 0) {
            send_line(sockfd, "%s", line);
        } else {
            show_message("Comando desconocido. %s", HELP);
            show_event("ERROR comando desconocido: %s", line);
        }
    }

    cleanup();
    close(sockfd);
    return 0;
}
