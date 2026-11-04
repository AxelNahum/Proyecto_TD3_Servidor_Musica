#!/bin/bash
# client_gui.sh
# Abre la interfaz grafica del cliente (el reproductor). Lo usa el acceso
# directo del menu (lo crea "make install-gui-client"), pero tambien se
# puede correr a mano:
#     ./client/gui/client_gui.sh

# Va a la carpeta del proyecto (dos carpetas arriba de este script), desde donde sea
cd "$(dirname "$(readlink -f "$0")")/../.." || exit 1

# Si el cliente en C no esta compilado, lo compila
if [ ! -x bin/client ]; then
    make client || exit 1
fi

if [ ! -d venv ]; then
    echo "Falta el entorno de Python. Corre primero: make install-gui-client"
    exit 1
fi

# Activa el entorno de Python (donde esta PySide6) y abre la ventana
source venv/bin/activate
exec python3 client/gui/client_gui.py
