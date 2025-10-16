CC = gcc

# Compilación del programa principal
programa: main.o
	$(CC) -o main main.o

main.o: main.c
	$(CC) -c main.c

# Limpiar todo
clean:
	rm -rf main main.o bin obj
