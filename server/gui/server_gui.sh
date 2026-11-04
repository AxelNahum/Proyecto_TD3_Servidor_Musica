#!/bin/bash
# server_gui.sh
# Abre la interfaz grafica del servidor. Lo usa el acceso directo del menu
# (lo crea "make install-gui-server"), pero tambien se puede correr a mano:
#     ./server/gui/server_gui.sh

# Va a la carpeta del proyecto (dos carpetas arriba de este script), desde donde sea
cd "$(dirname "$(readlink -f "$0")")/../.." || exit 1

# Si el servidor en C no esta compilado, lo compila
if [ ! -x bin/server ]; then
    make server || exit 1
fi

if [ ! -d venv ]; then
    echo "Falta el entorno de Python. Corre primero: make install-gui-server"
    exit 1
fi

# Activa el entorno de Python (donde esta PySide6) y abre la ventana
source venv/bin/activate
exec python3 server/gui/server_gui.py
