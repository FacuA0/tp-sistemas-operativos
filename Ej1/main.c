#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define TAM_REGISTRO 128

typedef struct {
    int cuentaIds;
    int ids[10];
    char registro[TAM_REGISTRO];
} Memoria;

typedef struct {
    pid_t pid;
    Memoria *memoria;
    sem_t *semId, *semMemGen, *semMemCons;
} Proceso;

int manejarArgumentos(int argc, char* argv[], int* procesos, int* registros);
void mainGenerador();
void generador(Proceso* proceso);
int coordinador(FILE *archRegistros, Proceso* procesos, int cantProcesos, int cantRegistros);

/*
    Proceso coordinador para 4 generadores:
    1: Mandar 10 ids a proc 1
    2: Mandar 10 ids a proc 2
    ...
    5: Recibir reg. 1 de proc 1
    6: Escribir reg. 1 en archivo
    7: Recibir reg. 2 de proc 1
    8: Escribir reg. 2 en archivo
    ...
    25: Mandar 10 ids a proc 1

    26: Recibir reg. 1 de proc 2
    27: Escribir reg. 1 en archivo
    ...
    46: Mandar 10 ids a proc 2
    ...
    ...: Al no haber más registros, dejar de mandar IDs nuevas, y seguir recibiendo en círculo
    Luego mandar señal de terminación de procesos, esperar y salir.


    Pseudocódigo:
    for i in range(cantProcesos) && regsEnviados < registros:
        for j in range(10):
            send(proc[i], curId++)

    while (regsEnviads < registros):
        for i in range(cantProcesos):
            for j in range(cantRegs):
                recv(proc[i], reg)
                write(csv, reg)

            for j in range(10) && regsEnviados < registros:
                send(proc[i], curId++)

    for i in range(cantProcesos):
        kill(proc[i], SIGTERM);

    for i in range(cantProcesos):
        wait(proc[i]);

    Proceso generador:
    1: Esperar nuevas IDs
    2: Tomar ID 1 y generar registro.
    3: Escribir en SHM y esperar ser leido.
    4: Tomar ID 2 y generar registro.
    5: Escribir en SHM y esperar ser leido.
    ...
    20: Tomar ID 10 y generar registro.
    21: Escribir en SHM y esperar ser leido.

    22: Esperar nuevas IDs

    ...

    Eventualmente llega señal de terminación.

*/

int main(int argc, char* argv[])
{
    int i, idMem, padre, cantProcesos = -1, cantRegistros = -1;
    Memoria *mem;
    Proceso *procesos, proceso;
    pid_t pid;
    char nombreSem[24];

    if (manejarArgumentos(argc, argv, &cantProcesos, &cantRegistros)) {
        return 1;
    }

    procesos = malloc(sizeof(Proceso) * cantProcesos);
    if (!procesos) {
        perror("No se pudo reservar memoria para info. de procesos");
        return 1;
    }

    FILE *archRegistros = fopen("registros.csv", "wt");
    if (!archRegistros) {
        perror("No se pudo arir el archivo");
        free(procesos);
        return 1;
    }

    idMem = shm_open("/mem_tpso", O_CREAT | O_RDWR, 0600);
    ftruncate(idMem, sizeof(Memoria) * cantProcesos);
    mem = mmap(NULL, sizeof(Memoria) * cantProcesos, PROT_READ | PROT_WRITE, MAP_SHARED, idMem, 0);

    printf("Ejercicio 1!\n");

    padre = getpid();

    for (i = 0; i < cantProcesos; i++) {
        proceso.memoria = mem + i;

        printf("Main %d: Inciando semáforos i = %d\n", getpid(), i);
        sprintf(nombreSem, "/sem_tpso_%d_id", i);
        proceso.semId = sem_open(nombreSem, O_CREAT | O_RDWR, 0600, 0);
        sprintf(nombreSem, "/sem_tpso_%d_gen", i);
        proceso.semMemGen = sem_open(nombreSem, O_CREAT | O_RDWR, 0600, 1);
        sprintf(nombreSem, "/sem_tpso_%d_cons", i);
        proceso.semMemCons = sem_open(nombreSem, O_CREAT | O_RDWR, 0600, 0);

        pid = fork();
        if (pid == 0) {
            printf("PID %d (ret %d), soy el hijo.\n", getpid(), pid);

            free(procesos);

            generador(&proceso);
            break;
        }

        printf("Main %d: Creado proceso %d\n", getpid(), pid);

        proceso.pid = pid;
        procesos[i] = proceso;
    }

    if (getpid() == padre) {

        coordinador(archRegistros, procesos, cantProcesos, cantRegistros);

        printf("Main %d: Limpiando semáforos\n", getpid());
        // Cerrando semáforos de coordinador
        for (i = 0; i < cantProcesos; i++) {
            sem_close(procesos[i].semId);
            sprintf(nombreSem, "/sem_tpso_%d_id", i);
            sem_unlink(nombreSem);

            sem_close(procesos[i].semMemGen);
            sprintf(nombreSem, "/sem_tpso_%d_gen", i);
            sem_unlink(nombreSem);

            sem_close(procesos[i].semMemCons);
            sprintf(nombreSem, "/sem_tpso_%d_cons", i);
            sem_unlink(nombreSem);
        }
    }

    printf("Main %d: Limpiando memoria\n", getpid());
    munmap(mem, sizeof(Memoria) * cantProcesos);
    shm_unlink("/mem_tpso");

    return 0;
}

void mainGenerador() {
    Memoria* mem;
    int idMem;
    Proceso proc;
    char nombreMem[24];

    sprintf(nombreMem, "/mem_tpso_%d", getpid());
    idMem = shm_open(nombreMem, O_CREAT | O_RDWR, 0600);
    ftruncate(idMem, sizeof(Memoria));
    mem = mmap(NULL, sizeof(Memoria), PROT_READ | PROT_WRITE, MAP_SHARED, idMem, 0);

}

void generador(Proceso* proceso) {
    Memoria *mem;
    char registros[10][TAM_REGISTRO];
    int i;

    mem = proceso->memoria;

    printf("Gen %d: Inicio generador\n", getpid());

    while (1) {

        printf("Gen %d: Esperando IDs...\n", getpid());
        sem_wait(proceso->semId);

        for (i = 0; i < mem->cuentaIds; i++) {
            printf("Gen %d: Creando registro %d con ID %d\n", getpid(), i, mem->ids[i]);
            sprintf(registros[i], "%d;Registro;%d\n", mem->ids[i], i);
        }

        for (i = 0; i < mem->cuentaIds; i++) {
            printf("Gen %d: Esperando pasar registro i = %d\n", getpid(), i);
            sem_wait(proceso->semMemGen);

            printf("Gen %d: Copiando registro i = %d\n", getpid(), i);
            memcpy(mem->registro, registros[i], TAM_REGISTRO);

            printf("Gen %d: Cediendo turno de registro i = %d\n", getpid(), i);
            sem_post(proceso->semMemCons);
        }
    }
}

int coordinador(FILE *archRegistros, Proceso* procesos, int cantProcesos, int cantRegistros) {
    int id = 1, idCosechada = 0, i, j;
    char registro[TAM_REGISTRO];
    Memoria *mem;

    printf("Cor %d: Iniciando coordinación\n", getpid());

    for (i = 0; i < cantProcesos; i++) {
        mem = procesos[i].memoria;

        printf("Cor %d: Estableciendo cuenta ID en proceso %d\n", getpid(), procesos[i].pid);
        mem->cuentaIds = cantRegistros - id + 1 <= 10 ? cantRegistros - id + 1 : 10;

        for (j = 0; j < mem->cuentaIds; j++) {
            printf("Cor %d: Nueva ID %d (j = %d) en proceso %d\n", getpid(), id, j, procesos[i].pid);
            mem->ids[j] = id++;
        }

        printf("Cor %d: Levantando bandera de ID en proceso %d\n", getpid(), procesos[i].pid);
        sem_post(procesos[i].semId);
    }

    do {
        printf("Cor %d: Cosechando registros...\n", getpid());
        for (i = 0; i < cantProcesos; i++) {
            mem = procesos[i].memoria;

            for (j = 0; j < mem->cuentaIds; j++) {
                printf("Cor %d: Esperando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                sem_wait(procesos[i].semMemCons);

                printf("Cor %d: Copiando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                memcpy(registro, mem->registro, TAM_REGISTRO);

                printf("Cor %d: Cediendo turno al proceso %d\n", getpid(), procesos[i].pid);
                sem_post(procesos[i].semMemGen);

                idCosechada++;

                printf("Cor %d: Guardando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                fputs(registro, archRegistros);
            }

            // Asignar nuevas IDs
            printf("Cor %d: Asignando nuevas IDs al proceso %d\n", getpid(), procesos[i].pid);
            mem->cuentaIds = cantRegistros - id + 1 <= 10 ? cantRegistros - id + 1 : 10;

            for (j = 0; j < mem->cuentaIds; j++) {
                printf("Cor %d: Nueva ID %d (j = %d) para proceso %d\n", getpid(), id, j, procesos[i].pid);
                mem->ids[j] = id++;
            }

            printf("Cor %d: Cediendo creación de registros a proceso %d\n", getpid(), j, procesos[i].pid);
            sem_post(procesos[i].semId);
        }
    }
    while (idCosechada < cantRegistros);


    for (i = 0; i < cantProcesos; i++) {
        printf("Cor %d: Matando proceso %d\n", getpid(), procesos[i].pid);
        kill(procesos[i].pid, SIGTERM);
    }

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
