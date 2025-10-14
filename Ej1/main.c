#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct {
    int id;
    int num1, num2;
    char nombre[31];
    int num4;
} Registro;

int manejarArgumentos(int argc, char* argv[], int* procesos, int* registros);

int main(int argc, char* argv[])
{
    int pid, i, idMem, cantProcesos = -1, cantRegistros = -1;
    void *mem;

    if (manejarArgumentos(argc, argv, &cantProcesos, &cantRegistros)) {
        return 1;
    }

    idMem = shm_open("/mem_tpso", O_CREAT | O_RDWR, 0600);
    ftruncate(idMem, sizeof(Registro) * cantProcesos);
    mem = mmap(NULL, sizeof(Registro) * cantProcesos, PROT_READ | PROT_WRITE, MAP_SHARED, idMem, 0);

    printf("Ejercicio 1!\n");

    pid = fork();
    if (pid == 0) {

        sprintf(mem, "Hola a todos\n");
        printf("PID 0, soy el hijo.\n");
        sleep(10);

    }
    else {
        printf("PID %d, soy el padre.\n", pid);
        sleep(11);
        printf("%s", mem);
    }

    munmap(mem, sizeof(Registro) * cantProcesos);
    shm_unlink("/mem_tpso");

    return 0;
}

int manejarArgumentos(int argc, char* argv[], int* procesos, int* registros) {
    int i;

    if (argc < 3) {
        printf("Pocos parámetros.\n");
        printf("Uso: Ej1 --procesos=<procesos> --registros=<registros>\n");
        printf("  o: Ej1 -p=<procesos> -r=<registros>\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (strstr(argv[i], "-p=") == argv[i]) {
            *procesos = atoi(argv[i] + 3);
        }
        else if (strstr(argv[i], "-r=") == argv[i]) {
            *registros = atoi(argv[i] + 3);
        }
        else if (strstr(argv[i], "--procesos=") == argv[i]) {
            *procesos = atoi(argv[i] + 11);
        }
        else if (strstr(argv[i], "--registros=") == argv[i]) {
            *registros = atoi(argv[i] + 12);
        }
    }

    if (*procesos <= 0) {
        printf("Faltó especificar cantidad de procesos o se especifico un numero menor a 0.\n");
        return 1;
    }

    if (*registros <= 0) {
        printf("Faltó especificar cantidad de registros o se especifico un numero menor a 0.\n");
        return 1;
    }

    return 0;
}
