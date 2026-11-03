CC     = gcc
CFLAGS = -Wall -g

all: servidor cliente

servidor: servidor.c
	$(CC) $(CFLAGS) servidor.c -o servidor

cliente: cliente.c
	$(CC) $(CFLAGS) cliente.c -o cliente

clean:
	rm -f servidor cliente
