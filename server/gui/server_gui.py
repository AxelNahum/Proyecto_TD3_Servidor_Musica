#!/usr/bin/env python3
"""
server_gui.py
Interfaz grafica del servidor (PySide6).

La ventana no hace nada de red: lanza el servidor en C (bin/server) con la
opcion --gui y lee lo que imprime. El servidor manda:

    READY <puerto> <maximo>          arranco bien
    STATS <clientes> <maximo> <salas>
    CLIENT <ip:puerto> <estado> <posicion> <duracion> <sala> <cancion>
    ...
    END                              (la tabla completa, cada 1 segundo)
    LOG <mensaje>                    algo que hizo un cliente

QProcess avisa con una senial cada vez que el servidor imprime algo, asi que
la ventana nunca se traba esperando (no hacen falta threads).

Uso: python3 server/gui/server_gui.py   (o el acceso directo que crea "make install-gui-server")
"""
import html
import subprocess
import sys
import time
from pathlib import Path

from PySide6.QtCore import QProcess, QSettings, Qt
from PySide6.QtGui import QColor, QGuiApplication, QIcon
from PySide6.QtWidgets import (
    QAbstractSpinBox, QApplication, QFileDialog, QFrame, QGridLayout, QGroupBox,
    QHBoxLayout, QHeaderView, QLabel, QLineEdit, QMainWindow, QMessageBox,
    QPlainTextEdit, QPushButton, QSpinBox, QStackedWidget, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

import theme     # Esta en la misma carpeta

# Carpeta raiz del proyecto (este archivo esta en server/gui/) y ejecutable del servidor
PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
SERVER_BIN = PROJECT_DIR / "bin" / "server"

# Texto de cada estado que manda el servidor
STATE_TEXT = {
    "PLAYING":  "Reproduciendo",
    "PAUSED":   "En pausa",
    "FINISHED": "Terminada",
    "NONE":     "Sin canción",
}


def format_time(seconds):
    """Pasa segundos a m:ss (por ejemplo 75 -> 1:15)."""
    return f"{seconds // 60}:{seconds % 60:02d}"


def local_ips():
    """Devuelve las IP de esta maquina en la red (las mismas que 'hostname -I')."""
    try:
        out = subprocess.run(["hostname", "-I"], capture_output=True, text=True).stdout
        return [ip for ip in out.split() if "." in ip]      # Solo IPv4
    except OSError:
        return []


def make_card(title):
    """Crea una tarjeta de informacion: titulo chico arriba y valor grande abajo.
    Devuelve (tarjeta, label_del_valor, layout) para poder agregarle cosas."""
    card = QFrame()
    card.setObjectName("card")
    layout = QVBoxLayout(card)
    layout.setContentsMargins(14, 10, 14, 10)
    layout.setSpacing(2)

    title_label = QLabel(title)
    title_label.setObjectName("cardTitle")
    value_label = QLabel("--")
    value_label.setObjectName("cardValue")

    layout.addWidget(title_label)
    layout.addWidget(value_label)
    return card, value_label, layout


class ServerWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Servidor de Música en Red")
        self.resize(980, 700)
        self.setMinimumSize(760, 560)

        # Preferencias que se guardan entre usos (~/.config/TD3/ServidorMusica.conf)
        self.settings = QSettings("TD3", "ServidorMusica")
        self.dark = self.settings.value("dark_mode", True, type=bool)

        self.process = None         # El servidor en C, cuando esta corriendo
        self.pending = ""           # Pedazo de linea que todavia no termino de llegar
        self.rows = []              # Filas de la tabla que se estan recibiendo (STATS..END)
        self.ips = local_ips()

        self.build_ui()
        self.apply_theme()
        self.set_running(False)
        self.center_on_screen()

    # ------------------------------------------------------------------
    # Armado de la ventana
    # ------------------------------------------------------------------

    def build_ui(self):
        layout = QVBoxLayout()
        layout.setContentsMargins(16, 12, 16, 16)
        layout.setSpacing(12)
        layout.addLayout(self.build_header())
        layout.addLayout(self.build_cards())
        layout.addWidget(self.build_config())
        layout.addWidget(self.build_clients(), stretch=3)
        layout.addWidget(self.build_log(), stretch=2)

        central = QWidget()
        central.setLayout(layout)
        self.setCentralWidget(central)

    def build_header(self):
        """Titulo, indicador de estado y boton de modo oscuro / claro."""
        title = QLabel("♪  Servidor de Música en Red")
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

    def build_cards(self):
        """Tarjetas con la IP, la cantidad de clientes y las salas activas."""
        ip_card, self.ip_value, ip_layout = make_card("IP DEL SERVIDOR  ·  puerto 3490")
        self.ip_value.setText(self.ips[0] if self.ips else "sin red")
        self.ip_value.setTextInteractionFlags(Qt.TextSelectableByMouse)

        # Debajo de la IP: el comando para el cliente y un boton para copiarla
        self.ip_hint = QLabel()
        self.ip_hint.setObjectName("cardHint")
        copy_button = QPushButton("Copiar IP")
        copy_button.setObjectName("small")
        copy_button.clicked.connect(self.copy_ip)
        hint_row = QHBoxLayout()
        hint_row.addWidget(self.ip_hint)
        hint_row.addStretch()
        hint_row.addWidget(copy_button)
        ip_layout.addLayout(hint_row)
        self.update_ip_hint()

        clients_card, self.clients_value, clients_layout = make_card("CLIENTES CONECTADOS")
        clients_layout.addStretch()
        rooms_card, self.rooms_value, rooms_layout = make_card("SALAS ACTIVAS")
        rooms_layout.addStretch()

        cards = QHBoxLayout()
        cards.setSpacing(12)
        cards.addWidget(ip_card, stretch=2)
        cards.addWidget(clients_card, stretch=1)
        cards.addWidget(rooms_card, stretch=1)
        return cards

    def build_config(self):
        """Carpeta de canciones, maximo de clientes y botones Iniciar / Detener."""
        box = QGroupBox("Configuración")
        grid = QGridLayout(box)
        grid.setHorizontalSpacing(10)

        self.folder_edit = QLineEdit(self.settings.value("songs_dir", "songs"))
        self.folder_button = QPushButton("Elegir...")
        self.folder_button.clicked.connect(self.choose_folder)

        # Maximo de clientes: un numero con botones - y + al costado
        self.max_spin = QSpinBox()
        self.max_spin.setRange(1, 32)
        self.max_spin.setValue(self.settings.value("max_clients", 5, type=int))
        self.max_spin.setButtonSymbols(QAbstractSpinBox.NoButtons)
        self.max_spin.setAlignment(Qt.AlignCenter)
        self.max_spin.setFixedWidth(60)
        self.minus_button = QPushButton("−")
        self.minus_button.setObjectName("small")
        self.minus_button.clicked.connect(self.max_spin.stepDown)
        self.plus_button = QPushButton("+")
        self.plus_button.setObjectName("small")
        self.plus_button.clicked.connect(self.max_spin.stepUp)

        spin_row = QHBoxLayout()
        spin_row.setSpacing(4)
        spin_row.addWidget(self.minus_button)
        spin_row.addWidget(self.max_spin)
        spin_row.addWidget(self.plus_button)
        spin_row.addStretch()

        self.start_button = QPushButton("▶  Iniciar")
        self.start_button.setObjectName("primary")
        self.start_button.setMinimumWidth(130)
        self.start_button.clicked.connect(self.start_server)
        self.stop_button = QPushButton("■  Detener")
        self.stop_button.setMinimumWidth(130)
        self.stop_button.clicked.connect(self.stop_server)

        grid.addWidget(QLabel("Carpeta de canciones:"), 0, 0)
        grid.addWidget(self.folder_edit, 0, 1)
        grid.addWidget(self.folder_button, 0, 2)
        grid.addWidget(self.start_button, 0, 3)
        grid.addWidget(QLabel("Máximo de clientes:"), 1, 0)
        grid.addLayout(spin_row, 1, 1, 1, 2)
        grid.addWidget(self.stop_button, 1, 3)
        grid.setColumnStretch(1, 1)
        return box

    def build_clients(self):
        """Tabla de clientes (o un mensaje en el medio si no hay ninguno)."""
        box = QGroupBox("Clientes")

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["Cliente", "Canción", "Posición", "Estado", "Sala"])

        # Anchos fijos para todas las columnas menos "Cancion", que ocupa el resto
        header = self.table.horizontalHeader()
        for col, width in ((0, 190), (2, 120), (3, 150), (4, 110)):
            header.setSectionResizeMode(col, QHeaderView.Fixed)
            self.table.setColumnWidth(col, width)
        header.setSectionResizeMode(1, QHeaderView.Stretch)

        # Los titulos alineados igual que los datos de abajo
        for col in range(5):
            align = Qt.AlignCenter if col in (2, 3, 4) else Qt.AlignLeft | Qt.AlignVCenter
            self.table.horizontalHeaderItem(col).setTextAlignment(align)

        self.table.verticalHeader().setVisible(False)
        self.table.verticalHeader().setDefaultSectionSize(34)      # Filas mas altas
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionMode(QTableWidget.NoSelection)
        self.table.setFocusPolicy(Qt.NoFocus)
        self.table.setAlternatingRowColors(True)
        self.table.setShowGrid(False)

        self.empty_label = QLabel()
        self.empty_label.setObjectName("empty")
        self.empty_label.setAlignment(Qt.AlignCenter)

        # Se muestra una cosa u otra: el mensaje (pagina 0) o la tabla (pagina 1)
        self.clients_stack = QStackedWidget()
        self.clients_stack.addWidget(self.empty_label)
        self.clients_stack.addWidget(self.table)

        QVBoxLayout(box).addWidget(self.clients_stack)
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
        self.update_table()                 # Los colores de los estados cambian con el modo

    def toggle_theme(self):
        self.dark = not self.dark
        self.settings.setValue("dark_mode", self.dark)
        self.apply_theme()

    def set_pill(self, state, text):
        """Cambia el indicador de estado: state = stopped, waiting u online."""
        self.pill.setText(f"●  {text}")
        self.pill.setProperty("state", state)
        # Qt no vuelve a aplicar el estilo solo cuando cambia una propiedad
        self.pill.style().unpolish(self.pill)
        self.pill.style().polish(self.pill)

    def update_ip_hint(self):
        ip = self.ips[0] if self.ips else "<IP>"
        extra = f"   (otras: {', '.join(self.ips[1:])})" if len(self.ips) > 1 else ""
        self.ip_hint.setText(f"Cliente:  ./bin/client {ip}{extra}")

    def copy_ip(self):
        if self.ips:
            QGuiApplication.clipboard().setText(self.ips[0])
            self.add_log(f"IP copiada: {self.ips[0]}")

    def choose_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Carpeta de canciones", str(PROJECT_DIR))
        if folder:
            self.folder_edit.setText(folder)

    def set_running(self, running):
        """Habilita / deshabilita los controles segun si el servidor esta corriendo."""
        self.start_button.setEnabled(not running)
        self.stop_button.setEnabled(running)
        for widget in (self.folder_edit, self.folder_button, self.max_spin,
                       self.minus_button, self.plus_button):
            widget.setEnabled(not running)

        if not running:
            self.set_pill("stopped", "Detenido")
            self.clients_value.setText("--")
            self.rooms_value.setText("--")
            self.empty_label.setText("El servidor está detenido.\nApretá  ▶ Iniciar  para empezar.")
            self.rows = []
            self.update_table()

    def add_log(self, text, error=False):
        line = html.escape(f"{time.strftime('%H:%M:%S')}  {text}")
        if error:
            line = f'<span style="color:{theme.state_color("ERROR", self.dark)}">{line}</span>'
        self.log_view.appendHtml(line)

    # ------------------------------------------------------------------
    # Lanzar y detener el servidor en C
    # ------------------------------------------------------------------

    def start_server(self):
        if not SERVER_BIN.exists():
            QMessageBox.warning(self, "Falta el servidor",
                                f"No se encontró {SERVER_BIN}.\nCompilalo con: make server")
            return

        # Se guardan para la proxima vez
        self.settings.setValue("songs_dir", self.folder_edit.text())
        self.settings.setValue("max_clients", self.max_spin.value())

        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(PROJECT_DIR))     # Para que "songs" sea relativo al proyecto
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.readyReadStandardError.connect(self.read_errors)
        self.process.finished.connect(self.server_finished)

        args = [self.folder_edit.text(), str(self.max_spin.value()), "--gui"]
        self.process.start(str(SERVER_BIN), args)
        self.add_log(f"Iniciando servidor (carpeta {args[0]}, máximo {args[1]} clientes)")
        self.set_running(True)
        self.set_pill("waiting", "Iniciando...")

    def stop_server(self):
        if self.process is None:
            return
        # terminate() manda SIGTERM: el servidor termina a sus hijos y borra la
        # memoria compartida. Si no responde en 3 segundos, se lo mata.
        self.process.terminate()
        if not self.process.waitForFinished(3000):
            self.process.kill()

    def server_finished(self, exit_code, exit_status):
        self.add_log(f"Servidor detenido (código {exit_code})")
        self.process = None
        self.set_running(False)

    def closeEvent(self, event):
        """Al cerrar la ventana, se detiene el servidor."""
        self.stop_server()
        event.accept()

    # ------------------------------------------------------------------
    # Lectura de lo que imprime el servidor
    # ------------------------------------------------------------------

    def read_output(self):
        # Puede llegar mas de una linea, o una linea cortada: se juntan los
        # pedazos y solo se procesan las lineas completas
        self.pending += self.process.readAllStandardOutput().data().decode(errors="replace")
        *lines, self.pending = self.pending.split("\n")
        for line in lines:
            self.handle_line(line.strip())

    def read_errors(self):
        text = self.process.readAllStandardError().data().decode(errors="replace")
        for line in text.splitlines():
            if line.strip():
                self.add_log(f"ERROR: {line}", error=True)

    def handle_line(self, line):
        if line.startswith("LOG "):
            self.add_log(line[4:])

        elif line.startswith("READY "):
            _, port, maximum = line.split()
            self.add_log(f"Servidor escuchando en el puerto {port}")

        elif line.startswith("STATS "):
            _, clients, maximum, rooms = line.split()
            self.rows = []
            self.clients_value.setText(f"{clients} / {maximum}")
            self.rooms_value.setText(rooms)
            if clients == "0":
                self.set_pill("waiting", "Esperando clientes")
            else:
                self.set_pill("online", "En línea")

        elif line.startswith("CLIENT "):
            # CLIENT <ip:puerto> <estado> <pos> <dur> <sala> <cancion (puede tener espacios)>
            parts = line.split(" ", 6)
            if len(parts) == 7:
                self.rows.append(parts[1:])

        elif line == "END":
            self.update_table()

    def update_table(self):
        if not self.rows:
            if self.process is not None:
                ip = self.ips[0] if self.ips else "<IP>"
                self.empty_label.setText(f"Esperando clientes...\n\n./bin/client {ip}")
            self.clients_stack.setCurrentIndex(0)       # Mensaje
            return

        self.clients_stack.setCurrentIndex(1)           # Tabla
        self.table.setRowCount(len(self.rows))
        for i, (client, state, pos, dur, room, song) in enumerate(self.rows):
            pos, dur = int(pos), int(dur)

            if state == "NONE":
                song, position = "--", "--"
            else:
                position = format_time(pos) + (f" / {format_time(dur)}" if dur > 0 else "")

            values = [client, song, position, "●  " + STATE_TEXT.get(state, state),
                      "--" if room == "-" else room]
            for col, value in enumerate(values):
                item = QTableWidgetItem(value)
                if col in (2, 3, 4):
                    item.setTextAlignment(Qt.AlignCenter)
                else:
                    item.setTextAlignment(Qt.AlignLeft | Qt.AlignVCenter)
                if col == 3:
                    item.setForeground(QColor(theme.state_color(state, self.dark)))
                    font = item.font()
                    font.setBold(True)
                    item.setFont(font)
                self.table.setItem(i, col, item)


def main():
    app = QApplication(sys.argv)
    app.setWindowIcon(QIcon(str(Path(__file__).resolve().parent / "icon.svg")))   # Icono de la barra de tareas
    window = ServerWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
