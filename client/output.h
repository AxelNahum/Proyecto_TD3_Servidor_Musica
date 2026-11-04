/*
 * output.h
 * Lo que el cliente muestra. Hay dos modos:
 *   - consola (normal): mensajes para personas y el prompt "> "
 *   - --gui: eventos de una linea que lee la interfaz grafica
 *            (gui/client_gui.py), por ejemplo "PLAYING 30 0 tema.mp3"
 *
 * Cada lugar que avisa algo llama a las dos funciones; cada una solo
 * imprime en su modo.
 */
#ifndef OUTPUT_H
#define OUTPUT_H

extern int gui_mode;        /* 1 si se arranco con --gui (se hereda en los fork) */

/* Modo consola: imprime el mensaje en una linea nueva y vuelve a mostrar "> " */
void show_message(const char *fmt, ...);

/* Modo --gui: imprime el evento tal cual, en una linea */
void show_event(const char *fmt, ...);

#endif
