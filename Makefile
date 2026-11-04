CC     = gcc
CFLAGS = -Wall -g -Icommon

BIN = bin

COMMON = common/protocol.c
SERVER = server/server_main.c server/server_commands.c server/session_table.c \
         server/mp3_info.c common/ipc.c $(COMMON)
CLIENT = client/client_main.c client/receiver.c client/playback.c client/player.c \
         client/ring_buffer.c common/ipc.c $(COMMON)

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

.PHONY: all server client tones clean
