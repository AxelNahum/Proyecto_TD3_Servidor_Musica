/*
 * player.c
 * Reproductor de MP3 en streaming.
 *
 *   bytes MP3 --mpg123_feed--> [decodificador] --mpg123_read--> PCM --out123_play--> placa de sonido
 *
 * libmpg123 en modo "feed" acepta el MP3 de a pedazos: cuando tiene datos
 * suficientes para un frame devuelve audio crudo (PCM). El formato del audio
 * (frecuencia de muestreo, canales) se conoce recien al decodificar el primer
 * frame (MPG123_NEW_FORMAT), y en ese momento se arranca la salida de audio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <mpg123.h>
#include <out123.h>
#include "player.h"

#define PCM_SIZE 16384      /* Tamanio del buffer de audio decodificado */

struct player {
    mpg123_handle *mh;      /* Decodificador MP3 */
    out123_handle *ao;      /* Salida de audio */
    int started;            /* 1 cuando ya se configuro el formato de salida */
};

player_t *player_open(void)
{
    static int initialized = 0;
    player_t *p;
    int err;

    if (!initialized) {
        mpg123_init();      /* Necesario en versiones viejas de libmpg123 */
        initialized = 1;
    }

    if ((p = calloc(1, sizeof(*p))) == NULL) {
        perror("calloc");
        return NULL;
    }

    if ((p->mh = mpg123_new(NULL, &err)) == NULL) {
        fprintf(stderr, "mpg123_new: %s\n", mpg123_plain_strerror(err));
        goto error;
    }
    if (mpg123_open_feed(p->mh) != MPG123_OK) {
        fprintf(stderr, "mpg123_open_feed: %s\n", mpg123_strerror(p->mh));
        goto error;
    }

    if ((p->ao = out123_new()) == NULL) {
        fprintf(stderr, "out123_new: no se pudo crear la salida de audio\n");
        goto error;
    }
    /* NULL, NULL: driver y dispositivo por defecto (PulseAudio, ALSA, ...) */
    if (out123_open(p->ao, NULL, NULL) != OUT123_OK) {
        fprintf(stderr, "out123_open: %s\n", out123_strerror(p->ao));
        goto error;
    }

    return p;

error:
    if (p->ao)
        out123_del(p->ao);
    if (p->mh)
        mpg123_delete(p->mh);
    free(p);
    return NULL;
}

/* Configura la salida de audio con el formato que informa el decodificador */
static int start_output(player_t *p)
{
    long rate;
    int channels, encoding;

    if (mpg123_getformat(p->mh, &rate, &channels, &encoding) != MPG123_OK) {
        fprintf(stderr, "mpg123_getformat: %s\n", mpg123_strerror(p->mh));
        return -1;
    }

    if (p->started)
        out123_stop(p->ao);     /* Cambio de formato en medio de la cancion */

    if (out123_start(p->ao, rate, channels, encoding) != OUT123_OK) {
        fprintf(stderr, "out123_start: %s\n", out123_strerror(p->ao));
        return -1;
    }
    p->started = 1;

    /* Informa que salida de audio se esta usando (util si no se escucha nada) */
    char *driver = NULL, *device = NULL;
    out123_driver_info(p->ao, &driver, &device);
    printf("Audio: %ld Hz, %d canal(es), salida \"%s\" (dispositivo \"%s\")\n",
           rate, channels, driver ? driver : "?", device ? device : "por defecto");
    return 0;
}

int player_feed(player_t *p, const void *data, size_t len)
{
    unsigned char pcm[PCM_SIZE];
    size_t done;
    int r;

    if (mpg123_feed(p->mh, data, len) != MPG123_OK) {
        fprintf(stderr, "mpg123_feed: %s\n", mpg123_strerror(p->mh));
        return -1;
    }

    /* Saca todo el audio que se pueda decodificar con lo recibido hasta ahora */
    while (1) {
        r = mpg123_read(p->mh, pcm, sizeof(pcm), &done);

        if (r == MPG123_NEW_FORMAT) {
            if (start_output(p) == -1)
                return -1;
        } else if (r != MPG123_OK && r != MPG123_NEED_MORE) {
            fprintf(stderr, "mpg123_read: %s\n", mpg123_strerror(p->mh));
            return -1;
        }

        /* out123_play se bloquea al ritmo de la placa de sonido */
        if (done > 0 && p->started && out123_play(p->ao, pcm, done) < done) {
            fprintf(stderr, "out123_play: %s\n", out123_strerror(p->ao));
            return -1;
        }

        if (r == MPG123_NEED_MORE)
            return 0;                       /* Hay que esperar mas bytes */
    }
}

/* Libera todo (lo usan player_close y player_abort) */
static void player_free(player_t *p)
{
    out123_close(p->ao);
    out123_del(p->ao);
    mpg123_delete(p->mh);
    free(p);
}

void player_close(player_t *p)
{
    if (p == NULL)
        return;
    if (p->started)
        out123_drain(p->ao);    /* Deja terminar de sonar lo que queda */
    player_free(p);
}

void player_abort(player_t *p)
{
    if (p == NULL)
        return;
    if (p->started)
        out123_drop(p->ao);     /* Tira lo que estaba por sonar: corta al instante */
    player_free(p);
}
