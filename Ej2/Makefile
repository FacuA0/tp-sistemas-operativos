CC = gcc

# Compilar todo
all: cliente servidor

# Compilación del cliente
cliente: cliente.o
	$(CC) -o cliente cliente.o

cliente.o: cliente.c
	$(CC) -c cliente.c

# Compilación del servidor
servidor: servidor.o
	$(CC) -o servidor servidor.o

servidor.o: servidor.c
	$(CC) -c servidor.c

programa: main.o
	$(CC) -o main main.o

main.o: main.c
	$(CC) -c main.c

# Limpiar todo
clean:
	rm -f cliente server tp2_client.o tp2_server.o main main.o
