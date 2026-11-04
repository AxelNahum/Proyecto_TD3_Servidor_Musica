CC     = gcc
CFLAGS = -Wall -g -Icommon

BIN = bin

COMMON = common/protocol.c
SERVER = server/server_main.c server/server_commands.c $(COMMON)
CLIENT = client/client_main.c client/client_commands.c $(COMMON)

all: $(BIN)/server $(BIN)/client

$(BIN)/server: $(SERVER) common/*.h server/*.h | $(BIN)
	$(CC) $(CFLAGS) $(SERVER) -o $@

$(BIN)/client: $(CLIENT) common/*.h client/*.h | $(BIN)
	$(CC) $(CFLAGS) $(CLIENT) -o $@

$(BIN):
	mkdir -p $(BIN)

# Genera las canciones de prueba (tonos) en la carpeta songs/
tones:
	bash scripts/generate_tones.sh

clean:
	rm -rf $(BIN) received

.PHONY: all tones clean
