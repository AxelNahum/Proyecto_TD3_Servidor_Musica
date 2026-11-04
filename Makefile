CC     = gcc
CFLAGS = -Wall -g -Icommon

BIN = bin

COMMON = common/protocol.c
SERVER = server/server_main.c server/server_commands.c server/session_table.c \
         server/mp3_info.c server/server_log.c common/ipc.c $(COMMON)
CLIENT = client/client_main.c client/receiver.c client/playback.c client/player.c \
         client/ring_buffer.c client/output.c common/ipc.c $(COMMON)

# Librerias del cliente: decodificar MP3 y reproducir audio (sudo apt install libmpg123-dev)
CLIENT_LIBS = -lmpg123 -lout123

all: $(BIN)/server $(BIN)/client

# Para compilar solo uno (por ejemplo, el servidor no necesita las librerias de audio)
server: $(BIN)/server
client: $(BIN)/client

$(BIN)/server: $(SERVER) common/*.h server/*.h | $(BIN)
	$(CC) $(CFLAGS) $(SERVER) -o $@

$(BIN)/client: $(CLIENT) common/*.h client/*.h | $(BIN)
	$(CC) $(CFLAGS) $(CLIENT) -o $@ $(CLIENT_LIBS)

$(BIN):
	mkdir -p $(BIN)

# Genera las canciones de prueba (tonos) en la carpeta songs/
tones:
	bash scripts/generate_tones.sh

clean:
	rm -rf $(BIN) received

# ---------------------------------------------------------------------------
# Paquetes del sistema (una sola vez por maquina; piden la contrasenia)
#
#   make deps-server    lo que necesita la maquina servidor
#   make deps-client    lo que necesita la maquina cliente
#
# libmpg123-dev: decodificar y reproducir MP3 (solo el cliente)
# python3-venv, python3-pip: entorno de Python para las ventanas
# libxcb-cursor0: Qt 6.5 o mas nuevo la necesita para abrir ventanas (sin
#                 ella, la ventana se cierra al arrancar)
# ---------------------------------------------------------------------------
DEPS_COMMON = build-essential python3-venv python3-pip libxcb-cursor0

deps-server:
	sudo apt update
	sudo apt install -y $(DEPS_COMMON)

deps-client:
	sudo apt update
	sudo apt install -y $(DEPS_COMMON) libmpg123-dev

# ---------------------------------------------------------------------------
# Interfaces graficas (Python + PySide6)
#
#   make install-gui-server   ventana del servidor  -> "Servidor de Musica"
#   make install-gui-client   ventana del cliente   -> "Reproductor de Musica"
#   make install-gui          las dos
#   make uninstall-gui        saca los accesos directos
#
# Cada una crea (si hace falta) el entorno de Python venv/, instala PySide6 y
# agrega el acceso directo al menu de aplicaciones y al escritorio.
# La ventana del servidor esta en server/gui/ y la del cliente en client/gui/:
# cada maquina solo necesita la carpeta de su lado.
#
# Antes, una sola vez: make deps-server  (o make deps-client)
# ---------------------------------------------------------------------------
APPS_DIR   = $(HOME)/.local/share/applications
SERVER_APP = servidor-musica.desktop
CLIENT_APP = reproductor-musica.desktop

# Crea un acceso directo.
# $(1) = archivo .desktop, $(2) = nombre que se ve, $(3) = lanzador, $(4) = icono (.svg, ruta completa)
define make_shortcut
@echo "== Creando el acceso directo '$(2)' =="
chmod +x $(3)
mkdir -p $(APPS_DIR)
printf '[Desktop Entry]\nType=Application\nName=%s\nComment=Proyecto TD3 - Servidor de Musica en Red\nExec="%s/%s"\nPath=%s\nIcon=%s\nTerminal=false\nCategories=AudioVideo;\n' \
	"$(2)" "$(CURDIR)" "$(3)" "$(CURDIR)" "$(4)" > $(APPS_DIR)/$(1)
chmod +x $(APPS_DIR)/$(1)
@desktop=$$(xdg-user-dir DESKTOP 2>/dev/null || echo $$HOME/Desktop); \
if [ -d "$$desktop" ]; then \
	cp $(APPS_DIR)/$(1) "$$desktop/"; \
	chmod +x "$$desktop/$(1)"; \
	gio set "$$desktop/$(1)" metadata::trusted true 2>/dev/null || true; \
	echo "Acceso directo copiado al escritorio: $$desktop/$(1)"; \
fi
@echo "Listo: busca '$(2)' en el menu de aplicaciones o en el escritorio"
endef

# Entorno de Python con PySide6 (venv/, en la raiz del proyecto). Si ya
# existe con PySide6 instalado, no se vuelve a instalar.
# $(1) = requirements.txt de ese lado
define make_venv
@ldconfig -p | grep -q libxcb-cursor.so.0 || \
	{ echo "ATENCION: falta libxcb-cursor0 (sin ella la ventana no abre)."; \
	  echo "          Instalala con: sudo apt install libxcb-cursor0   (o make deps-...)"; }
@echo "== Entorno de Python con PySide6 (la primera vez tarda: descarga ~90 MB) =="
test -d venv || python3 -m venv venv
venv/bin/python -c "import PySide6" 2>/dev/null || \
	{ venv/bin/pip install --upgrade pip && venv/bin/pip install -r $(1); }
endef
# (pip se actualiza antes: un pip viejo no reconoce los paquetes nuevos de
#  PySide6 e instala una version muy vieja)

install-gui-server: $(BIN)/server
	$(call make_venv,server/gui/requirements.txt)
	$(call make_shortcut,$(SERVER_APP),Servidor de Musica,server/gui/server_gui.sh,$(CURDIR)/server/gui/icon.svg)

install-gui-client: $(BIN)/client
	$(call make_venv,client/gui/requirements.txt)
	$(call make_shortcut,$(CLIENT_APP),Reproductor de Musica,client/gui/client_gui.sh,$(CURDIR)/client/gui/icon.svg)

install-gui: install-gui-server install-gui-client

uninstall-gui:
	rm -f $(APPS_DIR)/$(SERVER_APP) $(APPS_DIR)/$(CLIENT_APP)
	@desktop=$$(xdg-user-dir DESKTOP 2>/dev/null || echo $$HOME/Desktop); \
	rm -f "$$desktop/$(SERVER_APP)" "$$desktop/$(CLIENT_APP)"

.PHONY: all server client tones clean deps-server deps-client install-gui install-gui-server install-gui-client uninstall-gui
