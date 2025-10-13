#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    int pid, i, cantProcesos = -1, cantRegistros = -1;

    if (argc < 3) {
        printf("Pocos parametros.\n");
        printf("Uso: Ej1 --procesos=<procesos> --registros=<registros>\n");
        printf("  o: Ej1 -p=<procesos> -r=<registros>\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (strstr(argv[i], "-p=") == argv[i]) {
            cantProcesos = atoi(argv[i] + 3);
        }
        else if (strstr(argv[i], "-r=") == argv[i]) {
            cantRegistros = atoi(argv[i] + 3);
        }
        else if (strstr(argv[i], "--procesos=") == argv[i]) {
            cantProcesos = atoi(argv[i] + 11);
        }
        else if (strstr(argv[i], "--registros=") == argv[i]) {
            cantRegistros = atoi(argv[i] + 12);
        }
    }

    if (cantProcesos <= 0) {
        printf("Falto especificar cantidad de procesos o se especifico un numero menor a 0.\n");
        return 1;
    }

    if (cantRegistros <= 0) {
        printf("Falto especificar cantidad de registros o se especifico un numero menor a 0.\n");
        return 1;
    }

    printf("Ejercicio 1!\n");

    pid = fork();
    if (pid == 0) {
        printf("PID 0, soy el hijo.\n");
    }
    else {
        printf("PID %d, soy el padre.\n", pid);
    }

    return 0;
}
