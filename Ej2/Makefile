CC = gcc

# Compilar todo
all: cliente server

# Compilación del cliente
cliente: tp2_client.o
	$(CC) -o cliente tp2_client.o

tp2_client.o: tp2_client.c
	$(CC) -c tp2_client.c

# Compilación del servidor
server: tp2_server.o
	$(CC) -o server tp2_server.o

tp2_server.o: tp2_server.c
	$(CC) -c tp2_server.c

programa: main.o
	$(CC) -o main main.o

main.o: main.c
	$(CC) -c main.c

# Limpiar todo
clean:
	rm -f cliente server tp2_client.o tp2_server.o main main.o
