#!/usr/bin/env python3
"""
client_gui.py
Interfaz grafica del cliente (PySide6): el reproductor.

La ventana no hace nada de red ni de audio: lanza el cliente en C
(bin/client <IP> --gui), le escribe los comandos (PLAY, PAUSE, SEEK, JOIN...)
por su entrada y lee los eventos que imprime:

    CONNECTED <ip> <puerto>
    SONGS <n>      y luego n lineas  SONG <nombre>
    ROOMS <n>      y luego n lineas  ROOM <sala> <integrantes> <cancion>
    PLAYING <duracion> <desde> <nombre>
    SEEKED <seg> | PAUSED | RESUMED | STOPPED | FINISHED
    JOINED <sala> <integrantes> | LEFT
    ERROR <mensaje> | DISCONNECTED

Los nombres de canciones van al final de la linea porque pueden tener espacios.

La posicion de la barra se calcula aca con un reloj (igual que el servidor):
se sabe desde que segundo arranco y cuanto tiempo paso, sin contar las pausas.

QProcess avisa con una senial cada vez que el cliente imprime algo, asi que
la ventana nunca se traba esperando (no hacen falta threads).

Uso: python3 client/gui/client_gui.py   (o el acceso directo que crea "make install-gui-client")
"""
import html
import re
import sys
import time
from pathlib import Path

from PySide6.QtCore import QPointF, QProcess, QRectF, QSettings, QSize, Qt, QTimer
from PySide6.QtGui import QColor, QGuiApplication, QIcon, QPainter, QPixmap, QPolygonF
from PySide6.QtWidgets import (
    QApplication, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QListWidget,
    QListWidgetItem, QMainWindow, QMessageBox, QPlainTextEdit, QPushButton,
    QSlider, QStyle, QStyleOptionSlider, QVBoxLayout, QWidget,
)

import theme     # Esta en la misma carpeta

# Carpeta raiz del proyecto (este archivo esta en client/gui/) y ejecutable del cliente
PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BIN = PROJECT_DIR / "bin" / "client"

SEEK_STEP = 10              # Segundos que adelantan / atrasan los botones
ROOMS_REFRESH_MS = 3000     # Cada cuanto se actualiza la lista de salas
ROOM_NAME = re.compile(r"^[A-Za-z0-9_-]{1,31}$")   # Lo que acepta el servidor


def draw_icon(kind, color, size=18, disabled_color=None):
    """Icono de reproductor (play, pause, rewind, forward, stop). Si se pasa
    'disabled_color', el boton deshabilitado lo muestra en ese color."""
    icon = QIcon(draw_pixmap(kind, color, size))
    if disabled_color:
        icon.addPixmap(draw_pixmap(kind, disabled_color, size), QIcon.Disabled)
    return icon


def draw_pixmap(kind, color, size):
    """Dibuja el icono con triangulos y rectangulos en vez de usar caracteres
    como ⏪ o ⏩, porque Ubuntu los muestra como emojis naranjas."""
    pixmap = QPixmap(size, size)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.Antialiasing)
    painter.setPen(Qt.NoPen)
    painter.setBrush(QColor(color))
    s = size

    def triangle(tip_x, base_x):
        """Triangulo con la punta en tip_x y la base vertical en base_x."""
        painter.drawPolygon(QPolygonF([QPointF(base_x, s * 0.15),
                                       QPointF(tip_x, s * 0.5),
                                       QPointF(base_x, s * 0.85)]))

    if kind == "play":
        triangle(s * 0.88, s * 0.22)
    elif kind == "pause":
        painter.drawRoundedRect(QRectF(s * 0.20, s * 0.15, s * 0.22, s * 0.70), 1.5, 1.5)
        painter.drawRoundedRect(QRectF(s * 0.58, s * 0.15, s * 0.22, s * 0.70), 1.5, 1.5)
    elif kind == "forward":                     # ▶▶  adelantar
        triangle(s * 0.52, s * 0.05)
        triangle(s * 0.98, s * 0.51)
    elif kind == "rewind":                      # ◀◀  atrasar
        triangle(s * 0.02, s * 0.49)
        triangle(s * 0.48, s * 0.95)
    elif kind == "stop":
        painter.drawRoundedRect(QRectF(s * 0.2, s * 0.2, s * 0.6, s * 0.6), 2, 2)

    painter.end()
    return pixmap


class SeekSlider(QSlider):
    """Barra de posicion: un clic en cualquier lugar salta ahi.
    (Un QSlider comun, con un clic en la linea, solo avanza un "paso").

    Truco: al hacer clic fuera del circulo, primero se mueve el circulo hasta
    donde se hizo clic y despues se deja que Qt maneje el clic normal. Para
    Qt es como si se hubiera agarrado el circulo, asi que se puede seguir
    arrastrando y al soltar avisa con sliderReleased (que manda el SEEK)."""

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton and self.maximum() > self.minimum():
            option = QStyleOptionSlider()
            self.initStyleOption(option)
            style = self.style()
            handle = style.subControlRect(QStyle.CC_Slider, option, QStyle.SC_SliderHandle, self)
            click = event.position().toPoint()

            if not handle.contains(click):
                groove = style.subControlRect(QStyle.CC_Slider, option, QStyle.SC_SliderGroove, self)
                span = max(groove.width() - handle.width(), 1)
                x = click.x() - groove.x() - handle.width() // 2
                self.setValue(QStyle.sliderValueFromPosition(
                    self.minimum(), self.maximum(), min(max(x, 0), span), span))

        super().mousePressEvent(event)


def format_time(seconds):
    """Pasa segundos a m:ss (por ejemplo 75 -> 1:15)."""
    seconds = max(0, int(seconds))
    return f"{seconds // 60}:{seconds % 60:02d}"


class ClientWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Reproductor de Música")
        self.resize(1000, 720)
        self.setMinimumSize(820, 600)

        # Preferencias que se guardan entre usos (~/.config/TD3/ReproductorMusica.conf)
        self.settings = QSettings("TD3", "ReproductorMusica")
        self.dark = self.settings.value("dark_mode", True, type=bool)

        self.process = None         # El cliente en C, cuando esta conectado
        self.pending = ""           # Pedazo de linea que todavia no termino de llegar

        # Lo que esta sonando y su reloj
        self.song = None            # Nombre de la cancion (None = nada)
        self.duration = 0           # Duracion en segundos (0 = no se sabe)
        self.base_pos = 0.0         # Posicion (s) en el momento base_time
        self.base_time = 0.0        # time.monotonic() desde el que corre base_pos
        self.paused = False
        self.dragging = False       # El usuario esta arrastrando la barra
        self.room = None            # Sala en la que esta (None = escuchando solo)

        # Reloj de la ventana: actualiza la barra de posicion 4 veces por segundo
        self.clock = QTimer(self)
        self.clock.timeout.connect(self.update_position)

        # Pide la lista de salas cada tanto (para ver las que crean otros)
        self.rooms_timer = QTimer(self)
        self.rooms_timer.timeout.connect(lambda: self.send("ROOMS"))

        self.build_ui()
        self.apply_theme()
        self.set_connected(False)
        self.center_on_screen()
        self.clock.start(250)

    # ------------------------------------------------------------------
    # Armado de la ventana
    # ------------------------------------------------------------------

    def build_ui(self):
        layout = QVBoxLayout()
        layout.setContentsMargins(16, 12, 16, 16)
        layout.setSpacing(12)
        layout.addLayout(self.build_header())
        layout.addWidget(self.build_connection())

        middle = QHBoxLayout()
        middle.setSpacing(12)
        middle.addWidget(self.build_songs(), stretch=2)

        right = QVBoxLayout()
        right.setSpacing(12)
        right.addWidget(self.build_player())
        right.addWidget(self.build_rooms(), stretch=1)
        middle.addLayout(right, stretch=3)

        layout.addLayout(middle, stretch=3)
        layout.addWidget(self.build_log(), stretch=1)

        central = QWidget()
        central.setLayout(layout)
        self.setCentralWidget(central)

    def build_header(self):
        title = QLabel("♪  Reproductor de Música")
        title.setObjectName("title")

        self.pill = QLabel()
        self.pill.setObjectName("pill")

        self.theme_button = QPushButton()
        self.theme_button.clicked.connect(self.toggle_theme)

        header = QHBoxLayout()
        header.addWidget(title)
        header.addSpacing(12)
        header.addWidget(self.pill)
        header.addStretch()
        header.addWidget(self.theme_button)
        return header

    def build_connection(self):
        box = QGroupBox("Conexión")
        row = QHBoxLayout(box)
        row.setSpacing(10)

        self.ip_edit = QLineEdit(self.settings.value("server_ip", ""))
        self.ip_edit.setPlaceholderText("IP del servidor, por ejemplo 192.168.1.17")
        self.ip_edit.returnPressed.connect(self.connect_to_server)

        self.connect_button = QPushButton("Conectar")
        self.connect_button.setObjectName("primary")
        self.connect_button.setMinimumWidth(130)
        self.connect_button.clicked.connect(self.connect_to_server)
        self.disconnect_button = QPushButton("Desconectar")
        self.disconnect_button.setMinimumWidth(130)
        self.disconnect_button.clicked.connect(self.disconnect_from_server)

        row.addWidget(QLabel("Servidor:"))
        row.addWidget(self.ip_edit, stretch=1)
        row.addWidget(self.connect_button)
        row.addWidget(self.disconnect_button)
        return box

    def build_songs(self):
        box = QGroupBox("Canciones")
        layout = QVBoxLayout(box)

        self.songs_list = QListWidget()
        self.songs_list.itemDoubleClicked.connect(lambda item: self.play(item.text()))

        hint = QLabel("Doble clic para reproducir")
        hint.setObjectName("cardHint")
        self.refresh_songs_button = QPushButton("Actualizar")
        self.refresh_songs_button.setObjectName("small")
        self.refresh_songs_button.clicked.connect(lambda: self.send("LIST"))

        bottom = QHBoxLayout()
        bottom.addWidget(hint)
        bottom.addStretch()
        bottom.addWidget(self.refresh_songs_button)

        layout.addWidget(self.songs_list)
        layout.addLayout(bottom)
        return box

    def build_player(self):
        box = QGroupBox("Reproduciendo")
        layout = QVBoxLayout(box)
        layout.setSpacing(8)

        self.song_label = QLabel()
        self.song_label.setObjectName("songTitle")
        self.song_label.setWordWrap(True)
        self.room_label = QLabel()
        self.room_label.setObjectName("cardHint")

        # Boton para sacar la cancion: arriba a la derecha, separado de los
        # controles de reproduccion
        self.stop_button = QPushButton(" Sacar")
        self.stop_button.setObjectName("small")
        self.stop_button.setToolTip("Sacar la canción (deja de sonar)")
        self.stop_button.clicked.connect(lambda: self.send("QUIT"))

        title_row = QHBoxLayout()
        title_row.addWidget(self.song_label, stretch=1)
        title_row.addWidget(self.stop_button, alignment=Qt.AlignTop)

        # Barra de posicion con el tiempo a cada lado
        self.position_label = QLabel("0:00")
        self.position_label.setObjectName("time")
        self.duration_label = QLabel("0:00")
        self.duration_label.setObjectName("time")
        self.slider = SeekSlider(Qt.Horizontal)
        self.slider.sliderPressed.connect(self.slider_pressed)
        self.slider.sliderReleased.connect(self.slider_released)
        self.slider.sliderMoved.connect(lambda v: self.position_label.setText(format_time(v)))

        bar = QHBoxLayout()
        bar.setSpacing(10)
        bar.addWidget(self.position_label)
        bar.addWidget(self.slider, stretch=1)
        bar.addWidget(self.duration_label)

        # Botones: atrasar, play/pausa, adelantar (el play queda en el medio).
        # Solo iconos (el detalle esta en el cartel al pasar el mouse). Los iconos
        # se dibujan en apply_theme(), porque el color depende del modo.
        self.back_button = QPushButton()
        self.back_button.setObjectName("transport")
        self.back_button.setToolTip(f"Atrasar {SEEK_STEP} segundos")
        self.back_button.clicked.connect(lambda: self.send(f"SEEK -{SEEK_STEP}"))

        self.play_button = QPushButton()
        self.play_button.setObjectName("playButton")
        self.play_button.setToolTip("Reproducir / Pausa")
        self.play_button.setIconSize(QSize(22, 22))
        self.play_button.clicked.connect(self.play_pause)

        self.forward_button = QPushButton()
        self.forward_button.setObjectName("transport")
        self.forward_button.setToolTip(f"Adelantar {SEEK_STEP} segundos")
        self.forward_button.clicked.connect(lambda: self.send(f"SEEK +{SEEK_STEP}"))

        buttons = QHBoxLayout()
        buttons.setSpacing(16)
        buttons.addStretch()
        for button in (self.back_button, self.play_button, self.forward_button):
            buttons.addWidget(button)
        buttons.addStretch()

        layout.addLayout(title_row)
        layout.addWidget(self.room_label)
        layout.addSpacing(4)
        layout.addLayout(bar)
        layout.addLayout(buttons)
        return box

    def build_rooms(self):
        box = QGroupBox("Salas")
        layout = QVBoxLayout(box)

        self.room_edit = QLineEdit()
        self.room_edit.setPlaceholderText("Nombre de la sala (letras, números, - y _)")
        self.room_edit.returnPressed.connect(self.join_room)
        self.join_button = QPushButton("Unirse")
        self.join_button.setObjectName("primary")
        self.join_button.clicked.connect(self.join_room)
        self.leave_button = QPushButton("Salir")
        self.leave_button.clicked.connect(lambda: self.send("LEAVE"))

        row = QHBoxLayout()
        row.setSpacing(10)
        row.addWidget(self.room_edit, stretch=1)
        row.addWidget(self.join_button)
        row.addWidget(self.leave_button)

        self.rooms_list = QListWidget()
        self.rooms_list.itemDoubleClicked.connect(
            lambda item: self.send(f"JOIN {item.data(Qt.UserRole)}"))

        hint = QLabel("Doble clic en una sala para entrar")
        hint.setObjectName("cardHint")

        layout.addLayout(row)
        layout.addWidget(self.rooms_list)
        layout.addWidget(hint)
        return box

    def build_log(self):
        box = QGroupBox("Actividad")
        self.log_view = QPlainTextEdit()
        self.log_view.setObjectName("log")
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(500)     # Para que no crezca sin limite
        QVBoxLayout(box).addWidget(self.log_view)
        return box

    # ------------------------------------------------------------------
    # Detalles de la ventana
    # ------------------------------------------------------------------

    def center_on_screen(self):
        """Ubica la ventana en el centro de la pantalla."""
        frame = self.frameGeometry()
        frame.moveCenter(QGuiApplication.primaryScreen().availableGeometry().center())
        self.move(frame.topLeft())

    def apply_theme(self):
        self.setStyleSheet(theme.stylesheet(self.dark))
        self.theme_button.setText("Modo claro 🔆" if self.dark else "Modo oscuro 🌙")

        # Iconos de los botones, con el color del texto de este modo
        colors = theme.DARK if self.dark else theme.LIGHT
        text, dim = colors["text"], colors["disabled"]
        self.back_button.setIcon(draw_icon("rewind", text, disabled_color=dim))
        self.forward_button.setIcon(draw_icon("forward", text, disabled_color=dim))
        self.stop_button.setIcon(draw_icon("stop", text, 12, disabled_color=dim))
        self.update_play_button()

    def toggle_theme(self):
        self.dark = not self.dark
        self.settings.setValue("dark_mode", self.dark)
        self.apply_theme()

    def set_pill(self, state, text):
        """Cambia el indicador de estado: state = stopped, waiting u online."""
        self.pill.setText(f"●  {text}")
        self.pill.setProperty("state", state)
        self.pill.style().unpolish(self.pill)      # Para que Qt vuelva a aplicar el estilo
        self.pill.style().polish(self.pill)

    def add_log(self, text, error=False):
        line = html.escape(f"{time.strftime('%H:%M:%S')}  {text}")
        if error:
            line = f'<span style="color:{theme.state_color("ERROR", self.dark)}">{line}</span>'
        self.log_view.appendHtml(line)

    def set_connected(self, connected):
        """Habilita / deshabilita los controles segun si hay conexion."""
        self.connect_button.setEnabled(not connected)
        self.ip_edit.setEnabled(not connected)
        self.disconnect_button.setEnabled(connected)
        for widget in (self.songs_list, self.refresh_songs_button, self.room_edit,
                       self.join_button, self.rooms_list):
            widget.setEnabled(connected)

        if not connected:
            self.set_pill("stopped", "Desconectado")
            self.songs_list.clear()
            self.rooms_list.clear()
            self.room = None
            self.rooms_timer.stop()
            self.set_song(None)
        self.update_room_label()

    def set_song(self, name, duration=0, start=0):
        """Cambia la cancion que suena (None = nada) y reinicia el reloj."""
        self.song = name
        self.duration = duration
        self.base_pos = start
        self.base_time = time.monotonic()
        self.paused = False

        has_song = name is not None
        self.song_label.setText(name if has_song else "Nada sonando")
        self.slider.setRange(0, max(duration, 1))
        self.slider.setValue(int(start))
        self.duration_label.setText(format_time(duration))
        self.position_label.setText(format_time(start))
        for widget in (self.slider, self.back_button, self.forward_button, self.stop_button):
            widget.setEnabled(has_song)
        self.update_play_button()

    def update_play_button(self):
        # Con algo sonando: pausa / reanudar. Sin nada: reproduce la cancion elegida
        playing = self.song is not None and not self.paused
        self.play_button.setIcon(draw_icon("pause" if playing else "play", "white", 22))
        self.play_button.setEnabled(self.process is not None)

    def update_room_label(self):
        if self.room:
            self.room_label.setText(f"En la sala  «{self.room}»  — lo que hagas lo escuchan todos")
        else:
            self.room_label.setText("Escuchando solo")
        self.leave_button.setEnabled(self.room is not None)

    def current_position(self):
        """Segundo por el que va la cancion, segun el reloj de la ventana."""
        pos = self.base_pos
        if not self.paused:
            pos += time.monotonic() - self.base_time
        if self.duration > 0:
            pos = min(pos, self.duration)
        return pos

    def update_position(self):
        """Lo llama el reloj 4 veces por segundo: mueve la barra."""
        if self.song is None or self.dragging:
            return
        pos = self.current_position()
        self.slider.setValue(int(pos))
        self.position_label.setText(format_time(pos))

    # ------------------------------------------------------------------
    # Acciones del usuario
    # ------------------------------------------------------------------

    def send(self, command):
        """Le manda un comando al cliente en C (como si se escribiera en la consola)."""
        if self.process is not None:
            self.process.write((command + "\n").encode())

    def play(self, name):
        self.send(f"PLAY {name}")       # El nombre puede tener espacios: va completo

    def play_pause(self):
        if self.song is None:
            item = self.songs_list.currentItem()
            if item is None:
                self.add_log("Elegí una canción de la lista")
                return
            self.play(item.text())
        elif self.paused:
            self.send("RESUME")
        else:
            self.send("PAUSE")

    def slider_pressed(self):
        self.dragging = True

    def slider_released(self):
        self.dragging = False
        self.send(f"SEEK {self.slider.value()}")

    def join_room(self):
        name = self.room_edit.text().strip()
        if not ROOM_NAME.match(name):
            QMessageBox.warning(self, "Nombre de sala inválido",
                                "El nombre puede tener hasta 31 letras, números, - y _ "
                                "(sin espacios ni tildes).")
            return
        self.send(f"JOIN {name}")

    # ------------------------------------------------------------------
    # Conectar y desconectar (lanzar y cerrar el cliente en C)
    # ------------------------------------------------------------------

    def connect_to_server(self):
        ip = self.ip_edit.text().strip()
        if not ip:
            self.ip_edit.setFocus()
            return
        if not CLIENT_BIN.exists():
            QMessageBox.warning(self, "Falta el cliente",
                                f"No se encontró {CLIENT_BIN}.\nCompilalo con: make client")
            return

        self.settings.setValue("server_ip", ip)     # Se recuerda para la proxima vez

        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(PROJECT_DIR))
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.readyReadStandardError.connect(self.read_errors)
        self.process.finished.connect(self.client_finished)
        self.process.start(str(CLIENT_BIN), [ip, "--gui"])

        self.connect_button.setEnabled(False)
        self.ip_edit.setEnabled(False)
        self.set_pill("waiting", "Conectando...")
        self.add_log(f"Conectando a {ip}...")

    def disconnect_from_server(self):
        if self.process is None:
            return
        # Como en la consola: SALIR. El cliente termina sus procesos y borra
        # la memoria compartida. Si no responde, se lo termina con SIGTERM.
        self.send("SALIR")
        if not self.process.waitForFinished(2000):
            self.process.terminate()
            if not self.process.waitForFinished(2000):
                self.process.kill()

    def client_finished(self, exit_code, exit_status):
        self.add_log("Desconectado")
        self.process = None
        self.set_connected(False)

    def closeEvent(self, event):
        """Al cerrar la ventana, se desconecta."""
        self.disconnect_from_server()
        event.accept()

    # ------------------------------------------------------------------
    # Lectura de los eventos del cliente en C
    # ------------------------------------------------------------------

    def read_output(self):
        # Puede llegar mas de una linea, o una linea cortada: se juntan los
        # pedazos y solo se procesan las lineas completas
        self.pending += self.process.readAllStandardOutput().data().decode(errors="replace")
        *lines, self.pending = self.pending.split("\n")
        for line in lines:
            self.handle_event(line.rstrip("\r"))

    def read_errors(self):
        text = self.process.readAllStandardError().data().decode(errors="replace")
        for line in text.splitlines():
            if line.strip():
                self.add_log(f"ERROR: {line}", error=True)

    def handle_event(self, line):
        # Se separa el nombre del evento del resto (el resto puede tener espacios)
        event, _, rest = line.partition(" ")

        if event == "CONNECTED":
            ip, port = rest.split()
            self.set_connected(True)
            self.set_pill("online", f"Conectado a {ip}")
            self.add_log(f"Conectado a {ip}:{port}")
            self.update_play_button()
            self.send("ROOMS")
            self.rooms_timer.start(ROOMS_REFRESH_MS)

        elif event == "SONGS":
            self.songs_list.clear()

        elif event == "SONG":
            self.songs_list.addItem(rest)

        elif event == "ROOMS":
            self.rooms_list.clear()

        elif event == "ROOM":
            name, members, song = rest.split(" ", 2)
            text = f"{name}   ·   {members} integrante(s)   ·   {song}"
            if name == self.room:
                text = "✔  " + text
            item = QListWidgetItem(text)
            item.setData(Qt.UserRole, name)
            self.rooms_list.addItem(item)

        elif event == "PLAYING":
            duration, start, name = rest.split(" ", 2)
            self.set_song(name, int(duration), int(start))
            self.add_log(f"Reproduciendo {name}")

        elif event == "SEEKED":
            self.base_pos = int(rest)
            self.base_time = time.monotonic()
            self.paused = False
            self.update_play_button()
            self.add_log(f"Saltando a {format_time(int(rest))}")

        elif event == "PAUSED":
            self.base_pos = self.current_position()     # Se congela el reloj
            self.paused = True
            self.update_play_button()
            self.add_log("En pausa")

        elif event == "RESUMED":
            self.base_time = time.monotonic()           # Sigue corriendo desde ahora
            self.paused = False
            self.update_play_button()
            self.add_log("Reanudado")

        elif event == "STOPPED":
            self.set_song(None)
            self.add_log("Canción detenida")

        elif event == "FINISHED":
            self.set_song(None)
            self.add_log("Fin de la canción")

        elif event == "JOINED":
            name, members = rest.split()
            self.room = name
            self.update_room_label()
            self.add_log(f"Entraste a la sala {name} ({members} integrante(s))")
            self.send("ROOMS")

        elif event == "LEFT":
            self.add_log(f"Saliste de la sala {self.room}: seguís escuchando solo")
            self.room = None
            self.update_room_label()
            self.send("ROOMS")

        elif event == "ERROR":
            self.add_log(rest, error=True)

        elif event == "DISCONNECTED":
            self.add_log("El servidor cerró la conexión", error=True)


def main():
    app = QApplication(sys.argv)
    app.setWindowIcon(QIcon(str(Path(__file__).resolve().parent / "icon.svg")))   # Icono de la barra de tareas
    window = ClientWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
