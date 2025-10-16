#include <errno.h>
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
#include <sys/wait.h>
#include <time.h>

#define TAM_REGISTRO 128

typedef struct {
    int cuentaIds;
    int ids[10];
    char registro[TAM_REGISTRO];
} Memoria;

typedef struct {
    pid_t pid;
    int vivo;
    Memoria *memoria;
    sem_t *semId, *semMemGen, *semMemCons;
} Proceso;

typedef struct {
    int cantProcs;
    Proceso* procs;
    void* shm;
    int largoShm;
    int apagado;
} Recursos;

int manejarArgumentos(int argc, char* argv[], int* procesos, int* registros);
void mainGenerador();
void generador(Proceso* proceso);
int coordinador(FILE *archRegistros, Proceso* procesos, int cantProcesos, int cantRegistros);
void manejarSigtermGen(int signal);
void manejarSigtermCord(int signal);
void manejarSigchld(int signal);

Proceso miProceso;
Recursos recursos;

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

int main(int argc, char* argv[]) {
    int i, idMem, padre, cantProcesos = -1, cantRegistros = -1;
    Memoria *mem;
    Proceso proceso;
    pid_t pid;
    char nombreSem[24];

    if (manejarArgumentos(argc, argv, &cantProcesos, &cantRegistros)) {
        return 1;
    }

    recursos.cantProcs = cantProcesos;
    recursos.procs = malloc(sizeof(Proceso) * cantProcesos);
    if (!recursos.procs) {
        perror("No se pudo reservar memoria para info. de procesos");
        return 1;
    }

    FILE *archRegistros = fopen("registros.csv", "wt");
    if (!archRegistros) {
        perror("No se pudo abrir el archivo");
        free(recursos.procs);
        return 1;
    }

    idMem = shm_open("/mem_tpso", O_CREAT | O_RDWR, 0600);
    if (idMem == -1) {
        perror("No se pudo crear un bloque de memoria compartida");
        fclose(archRegistros);
        free(recursos.procs);
    }

    ftruncate(idMem, sizeof(Memoria) * cantProcesos);
    mem = mmap(NULL, sizeof(Memoria) * cantProcesos, PROT_READ | PROT_WRITE, MAP_SHARED, idMem, 0);

    close(idMem);

    recursos.apagado = 0;
    recursos.shm = mem;
    recursos.largoShm = sizeof(Memoria) * cantProcesos;

    signal(SIGCHLD, manejarSigchld);
    signal(SIGTERM, manejarSigtermCord);

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

            srand(time(NULL) + i * 2);

            signal(SIGTERM, manejarSigtermGen);

            miProceso = proceso;

            free(recursos.procs);

            generador(&proceso);

            break;
        }

        printf("Main %d: Creado proceso %d\n", getpid(), pid);

        proceso.pid = pid;
        proceso.vivo = 1;
        recursos.procs[i] = proceso;
    }

    if (getpid() == padre) {
        coordinador(archRegistros, recursos.procs, cantProcesos, cantRegistros);

        printf("Main %d: Limpiando semáforos\n", getpid());
        // Cerrando semáforos de coordinador
        for (i = 0; i < cantProcesos; i++) {
            Proceso proc = recursos.procs[i];

            sem_close(proc.semId);
            sprintf(nombreSem, "/sem_tpso_%d_id", i);
            sem_unlink(nombreSem);

            sem_close(proc.semMemGen);
            sprintf(nombreSem, "/sem_tpso_%d_gen", i);
            sem_unlink(nombreSem);

            sem_close(proc.semMemCons);
            sprintf(nombreSem, "/sem_tpso_%d_cons", i);
            sem_unlink(nombreSem);
        }
    }

    printf("Main %d: Limpiando memoria\n", getpid());
    munmap(mem, sizeof(Memoria) * cantProcesos);
    shm_unlink("/mem_tpso");

    //sleep(300);

    return 0;
}

void generador(Proceso* proceso) {
    Memoria *mem;
    char registros[10][TAM_REGISTRO];
    int i, dni, altura;
    char *apellido, *nombre, *calle;

    char apellidos[7][20] = {
        "Perez",
        "Gonzalez",
        "Gomez",
        "Torres",
        "Altamirano",
        "Ceballos",
        "Garcia"
    };

    char nombres[7][20] = {
        "Pedro",
        "Pablo",
        "Facundo",
        "Lucia",
        "Maria",
        "Andres",
        "Leandro"
    };

    char calles[7][20] = {
        "Constitucion",
        "25 de mayo",
        "Peron",
        "Corrientes",
        "Belgrano",
        "Independencia",
        "San Martin"
    };

    mem = proceso->memoria;

    //printf("Gen %d: Inicio generador\n", getpid());

    while (1) {

        //printf("Gen %d: Esperando IDs...\n", getpid());
        sem_wait(proceso->semId);

        for (i = 0; i < mem->cuentaIds; i++) {
            //printf("Gen %d: Creando registro %d con ID %d\n", getpid(), i, mem->ids[i]);
            dni = rand() % 60000000 + 1;
            apellido = apellidos[rand() % 7];
            nombre = nombres[rand() % 7];
            calle = calles[rand() % 7];
            altura = rand() % 10000 + 1;

            sprintf(registros[i], "%d;%d;%s;%s;%s;%d\n", mem->ids[i], dni, apellido, nombre, calle, altura);
        }

        for (i = 0; i < mem->cuentaIds; i++) {
            //printf("Gen %d: Esperando pasar registro i = %d\n", getpid(), i);
            sem_wait(proceso->semMemGen);

            //printf("Gen %d: Copiando registro i = %d\n", getpid(), i);
            memcpy(mem->registro, registros[i], TAM_REGISTRO);

            //printf("Gen %d: Cediendo turno de registro i = %d\n", getpid(), i);
            sem_post(proceso->semMemCons);
        }
    }
}

int coordinador(FILE *archRegistros, Proceso* procesos, int cantProcesos, int cantRegistros) {
    int id = 1, idCosechada = 0, rollback = -1, alguienVivo = 0, i, j;
    char registro[TAM_REGISTRO];
    Memoria *mem;

    // Cabecera CSV
    fprintf(archRegistros, "ID;DNI;Apellido;Nombre;Calle;Altura\n");

    printf("Cor %d: Iniciando coordinación\n", getpid());

    for (i = 0; i < cantProcesos; i++) {
        mem = procesos[i].memoria;

        //printf("Cor %d: Estableciendo cuenta ID en proceso %d\n", getpid(), procesos[i].pid);
        mem->cuentaIds = cantRegistros - id + 1 <= 10 ? cantRegistros - id + 1 : 10;

        for (j = 0; j < mem->cuentaIds; j++) {
            //printf("Cor %d: Nueva ID %d (j = %d) en proceso %d\n", getpid(), id, j, procesos[i].pid);
            mem->ids[j] = id++;
        }

        //printf("Cor %d: Levantando bandera de ID en proceso %d\n", getpid(), procesos[i].pid);
        sem_post(procesos[i].semId);
    }

    do {
        alguienVivo = 0;

        //printf("Cor %d: Cosechando registros con ID hasta %d...\n", getpid(), id);
        for (i = 0; i < cantProcesos; i++) {
            mem = procesos[i].memoria;

            alguienVivo = alguienVivo || procesos[i].vivo;

            //Si un proceso murió, desechar IDs posteriores y reasignar desde ese punto
            if (!procesos[i].vivo) {
                if (i == rollback)
                    rollback = -1;

                if (mem->cuentaIds > 0) {
                    rollback = i;
                    id = mem->ids[0];

                    mem->cuentaIds = 0;
                }
                continue;
            }

            for (j = 0; j < mem->cuentaIds; j++) {
                //printf("Cor %d: Esperando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                sem_wait(procesos[i].semMemCons);

                if (!procesos[i].vivo) {
                    rollback = i;
                    id = mem->ids[j];

                    mem->cuentaIds = 0;
                    break;
                }

                //printf("Cor %d: Copiando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                memcpy(registro, mem->registro, TAM_REGISTRO);

                //printf("Cor %d: Cediendo turno al proceso %d\n", getpid(), procesos[i].pid);
                sem_post(procesos[i].semMemGen);

                if (rollback == -1) {
                    //printf("Cor %d: Guardando registro %d de proceso %d\n", getpid(), j, procesos[i].pid);
                    fputs(registro, archRegistros);
                    idCosechada++;
                }
            }

            if (!procesos[i].vivo) continue;

            // Asignar nuevas IDs
            //printf("Cor %d: Asignando nuevas IDs al proceso %d\n", getpid(), procesos[i].pid);
            mem->cuentaIds = cantRegistros - id + 1 <= 10 ? cantRegistros - id + 1 : 10;

            for (j = 0; j < mem->cuentaIds; j++) {
                //printf("Cor %d: Nueva ID %d (j = %d) para proceso %d\n", getpid(), id, j, procesos[i].pid);
                mem->ids[j] = id++;
            }

            //printf("Cor %d: Cediendo creación de registros a proceso %d\n", getpid(), procesos[i].pid);
            sem_post(procesos[i].semId);
        }
    }
    while (idCosechada < cantRegistros && alguienVivo);

    recursos.apagado = 1;

    for (i = 0; i < cantProcesos; i++) {
        printf("Cor %d: Matando proceso %d\n", getpid(), procesos[i].pid);
        kill(procesos[i].pid, SIGTERM);
        waitpid(procesos[i].pid, NULL, 0);
    }

    return 0;
}

void manejarSigchld(int signal) {
    int i;

    if (recursos.apagado) return;

    pid_t hijo = wait(NULL);

    printf("Cor %d: Atendiendo muerte de %d.\n", getpid(), hijo);
    for (i = 0; i < recursos.cantProcs; i++) {
        if (recursos.procs[i].pid == hijo) {
            recursos.procs[i].vivo = 0;
            sem_post(recursos.procs[i].semMemCons);
        }
    }
}

void manejarSigtermGen(int signal) {
    printf("Gen %d: Respondiendo a mi muerte\n", getpid());

    sem_close(miProceso.semId);
    sem_close(miProceso.semMemGen);
    sem_close(miProceso.semMemCons);

    munmap(recursos.shm, recursos.largoShm);

    _exit(1);
}

void manejarSigtermCord(int signal) {
    printf("Cor %d: Respondiendo a mi muerte\n", getpid());
    Proceso proc;
    int i;
    char nombreSem[24];

    recursos.apagado = 1;

    for (i = 0; i < recursos.cantProcs; i++) {
        proc = recursos.procs[i];

        printf("Cor %d: Matando proceso %d\n", getpid(), proc.pid);
        kill(proc.pid, SIGTERM);
        waitpid(proc.pid, NULL, 0);
    }

    printf("Main %d: Limpiando semáforos\n", getpid());
    // Cerrando semáforos de coordinador
    for (i = 0; i < recursos.cantProcs; i++) {
        proc = recursos.procs[i];

        sem_close(proc.semId);
        sprintf(nombreSem, "/sem_tpso_%d_id", i);
        sem_unlink(nombreSem);

        sem_close(proc.semMemGen);
        sprintf(nombreSem, "/sem_tpso_%d_gen", i);
        sem_unlink(nombreSem);

        sem_close(proc.semMemCons);
        sprintf(nombreSem, "/sem_tpso_%d_cons", i);
        sem_unlink(nombreSem);
    }

    printf("Main %d: Limpiando memoria\n", getpid());
    munmap(recursos.shm, recursos.largoShm);
    shm_unlink("/mem_tpso");

    _exit(1);
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
