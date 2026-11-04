"""
theme.py
Estilo de las ventanas (modo oscuro y modo claro), con la misma paleta del
NanoVNA-UTN-Toolkit: azules / violetas grisados, bordes redondeados.

Hay una copia igual en server/gui/ y en client/gui/ (asi cada lado es
independiente): si se cambia el estilo, hay que cambiarlo en las dos.
Se usa asi:
    window.setStyleSheet(theme.stylesheet(dark=True))
"""

# Colores de cada modo
DARK = {
    "background": "#1e1e2e",    # Fondo de la ventana
    "surface":    "#252538",    # Campos de texto, tablas
    "control":    "#2e2e42",    # Botones, encabezados
    "hover":      "#3a3a54",
    "pressed":    "#1a1a2e",
    "border":     "#383850",
    "text":       "#ffffff",
    "text_dim":   "#8888aa",
    "disabled":   "#5a5a78",
    "accent":     "#6aa2ff",    # Azul de resaltado
    "scroll":     "#44446a",
    "scroll_hover": "#5a5a8a",
}

LIGHT = {
    "background": "#f0f0f8",
    "surface":    "#f8f8ff",
    "control":    "#e0e0f0",
    "hover":      "#d0d0e8",
    "pressed":    "#c0c0d8",
    "border":     "#c4c4d8",
    "text":       "#1e1e2e",
    "text_dim":   "#6a6a88",
    "disabled":   "#8888aa",
    "accent":     "#4d90fe",
    "scroll":     "#b8b8d0",
    "scroll_hover": "#9898b8",
}

# Colores de los estados de reproduccion en cada modo
STATE_COLORS = {
    True:  {"PLAYING": "#66bb6a", "PAUSED": "#ffa726", "FINISHED": "#8888aa",
            "NONE": "#6a6a88", "ERROR": "#ef5350"},
    False: {"PLAYING": "#2e7d32", "PAUSED": "#e65100", "FINISHED": "#6a6a88",
            "NONE": "#9e9e9e", "ERROR": "#c62828"},
}


def state_color(state, dark):
    """Color para un estado (PLAYING, PAUSED, ...) en el modo indicado."""
    return STATE_COLORS[dark].get(state, DARK["text"] if dark else LIGHT["text"])


def stylesheet(dark=True):
    """Hoja de estilo (QSS) de toda la ventana."""
    c = DARK if dark else LIGHT
    return f"""
    QWidget {{
        background-color: {c['background']};
        color: {c['text']};
        font-size: 10pt;
    }}

    /* QGroupBox con el titulo "metido" en la linea del borde: el borde
     * empieza a mitad de la altura del titulo (margin-top) y el titulo tiene
     * el mismo fondo que la ventana, asi tapa la linea detras del texto */
    QGroupBox {{
        border: 1px solid {c['border']};
        border-radius: 8px;
        margin-top: 9px;
        padding: 14px 8px 8px 8px;
    }}
    QGroupBox::title {{
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 12px;
        padding: 0 6px;
        background-color: {c['background']};
        color: {c['accent']};
        font-weight: bold;
    }}

    /* Adentro de un grupo, la tabla, el mensaje y el registro no llevan su
     * propio borde: asi queda un solo recuadro (el del grupo) */
    QGroupBox QTableWidget, QGroupBox QPlainTextEdit, QGroupBox QLabel#empty,
    QGroupBox QListWidget {{
        border: none;
        background-color: transparent;
    }}

    QLabel {{
        background: transparent;
    }}
    QLabel#title {{
        font-size: 15pt;
        font-weight: bold;
        color: {c['accent']};
    }}
    QLabel#ip {{
        font-size: 13pt;
        font-weight: bold;
        color: {c['accent']};
    }}
    QLabel#status {{
        font-size: 11pt;
        color: {c['text_dim']};
    }}

    QSpinBox {{
        qproperty-alignment: AlignCenter;
    }}

    QLineEdit, QSpinBox, QPlainTextEdit {{
        background-color: {c['surface']};
        border: 1px solid {c['border']};
        border-radius: 6px;
        padding: 4px;
        selection-background-color: {c['accent']};
    }}
    QLineEdit:focus, QSpinBox:focus {{
        border: 1px solid {c['accent']};
        background-color: {c['control']};
    }}
    QLineEdit:disabled, QSpinBox:disabled {{
        color: {c['disabled']};
    }}

    QPushButton {{
        background-color: {c['control']};
        border: 1px solid {c['border']};
        border-radius: 6px;
        padding: 6px 14px;
    }}
    QPushButton:hover {{
        background-color: {c['hover']};
    }}
    QPushButton:pressed {{
        background-color: {c['pressed']};
    }}
    QPushButton:disabled {{
        background-color: {c['background']};
        color: {c['disabled']};
        border: 1px solid {c['border']};
    }}
    QPushButton#primary {{
        background-color: {c['accent']};
        color: white;
        border: none;
        font-weight: bold;
    }}
    QPushButton#primary:hover {{
        background-color: {c['hover']};
        color: {c['text']};
    }}
    QPushButton#primary:disabled {{
        background-color: {c['control']};
        color: {c['disabled']};
    }}

    QTableWidget {{
        background-color: {c['surface']};
        alternate-background-color: {c['control']};
        border: 1px solid {c['border']};
        border-radius: 8px;
        gridline-color: {c['border']};
    }}
    QTableWidget::item {{
        padding: 0 10px;            /* Separacion entre columnas */
    }}
    QHeaderView::section {{
        background-color: {c['control']};
        color: {c['text']};
        border: none;
        border-bottom: 1px solid {c['border']};
        padding: 6px 10px;
        font-weight: bold;
    }}
    QTableCornerButton::section {{
        background-color: {c['control']};
        border: none;
    }}

    /* Tarjetas de informacion (IP, clientes, salas) */
    QFrame#card {{
        background-color: {c['surface']};
        border: 1px solid {c['border']};
        border-radius: 10px;
    }}
    QLabel#cardTitle {{
        color: {c['text_dim']};
        font-size: 9pt;
    }}
    QLabel#cardValue {{
        color: {c['accent']};
        font-size: 16pt;
        font-weight: bold;
    }}
    QLabel#cardHint {{
        color: {c['text_dim']};
        font-size: 9pt;
    }}

    /* Indicador de estado (el color depende de la propiedad "state") */
    QLabel#pill {{
        border-radius: 11px;
        padding: 3px 12px;
        font-weight: bold;
        background-color: {c['control']};
        color: {c['text_dim']};
    }}
    QLabel#pill[state="waiting"] {{
        background-color: {"#3d3222" if dark else "#fff0dc"};
        color: {STATE_COLORS[dark]['PAUSED']};
    }}
    QLabel#pill[state="online"] {{
        background-color: {"#22382a" if dark else "#e3f4e4"};
        color: {STATE_COLORS[dark]['PLAYING']};
    }}

    /* Mensaje que se muestra en lugar de la tabla cuando esta vacia */
    QLabel#empty {{
        background-color: {c['surface']};
        border: 1px solid {c['border']};
        border-radius: 8px;
        color: {c['text_dim']};
        font-size: 12pt;
    }}

    /* Botones chicos (copiar, - y +) */
    QPushButton#small {{
        padding: 2px 8px;
        min-width: 22px;
    }}

    QPlainTextEdit#log {{
        font-family: "DejaVu Sans Mono", "Ubuntu Mono", monospace;
        font-size: 9pt;
    }}

    /* Listas (canciones y salas) */
    QListWidget {{
        background-color: transparent;
        border: none;
        outline: none;
    }}
    QListWidget::item {{
        padding: 7px 10px;
        border-radius: 6px;
    }}
    QListWidget::item:hover {{
        background-color: {c['control']};
    }}
    QListWidget::item:selected {{
        background-color: {c['hover']};
        color: {c['text']};
    }}

    /* Cancion que suena (en el reproductor) */
    QLabel#songTitle {{
        font-size: 14pt;
        font-weight: bold;
    }}
    QLabel#time {{
        color: {c['text_dim']};
        font-family: "DejaVu Sans Mono", "Ubuntu Mono", monospace;
    }}

    /* Barra de posicion (con alto suficiente para que no se corte el circulo) */
    QSlider:horizontal {{
        min-height: 24px;
    }}
    /* El circulo: Qt calcula su ALTO como la linea (6) + 2 x el margen negativo
     * (4) = 14, igual que el ancho, asi es un circulo. El margen de la linea
     * (4) tiene que ser igual, porque el circulo solo puede salirse de la
     * linea hasta ese margen (si no, se corta). */
    QSlider::groove:horizontal {{
        height: 6px;
        border-radius: 3px;
        background: {c['control']};
        margin: 4px 0;
    }}
    QSlider::sub-page:horizontal {{
        height: 6px;
        border-radius: 3px;
        background: {c['accent']};
        margin: 4px 0;      /* Igual que la linea, si no la parte azul queda mas gruesa */
    }}
    QSlider::handle:horizontal {{
        width: 14px;
        height: 14px;       /* Algunas versiones de Qt lo necesitan para dibujarlo */
        margin: -4px 0;
        border-radius: 7px;
        background: {c['text']};
    }}
    QSlider::handle:horizontal:disabled {{
        background: transparent;    /* Sin cancion no se muestra el circulo */
    }}
    QSlider::sub-page:horizontal:disabled {{
        background: transparent;
    }}

    /* Botones del reproductor (atrasar, play/pausa, adelantar, detener) */
    QPushButton#transport {{
        min-width: 46px;
        max-width: 46px;
        min-height: 36px;
        max-height: 36px;
        border-radius: 18px;
        padding: 0;
    }}
    QPushButton#playButton {{
        font-size: 15pt;
        min-width: 58px;
        min-height: 42px;
        border-radius: 21px;
        padding: 0;
        background-color: {c['accent']};
        color: white;
        border: none;
    }}
    QPushButton#playButton:hover {{
        background-color: {c['hover']};
        color: {c['text']};
    }}
    QPushButton#playButton:disabled {{
        background-color: {c['control']};
        color: {c['disabled']};
    }}

    QScrollBar:vertical, QScrollBar:horizontal {{
        background: {c['background']};
        border: none;
        width: 10px;
        height: 10px;
    }}
    QScrollBar::handle {{
        background: {c['scroll']};
        border-radius: 5px;
        min-height: 20px;
        min-width: 20px;
    }}
    QScrollBar::handle:hover {{
        background: {c['scroll_hover']};
    }}
    QScrollBar::add-line, QScrollBar::sub-line {{
        width: 0px;
        height: 0px;
    }}
    """
