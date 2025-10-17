// tp2_server.c
// Compilar: gcc -std=c11 -O2 -Wall tp2_server.c -o tp2_server
// Uso: ./tp2_server <ip> <port> <max_clients>
// Ejemplo: ./tp2_server 0.0.0.0 9000 10

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define BUFSIZE 4096
#define CSVPATH "registros.csv"
#define SEP_COLUMNAS ";"

typedef struct
{
    int fd;
    char buf[BUFSIZE];
    int buflen;
    int in_transaction; // 0/1
    // Buffer de transacciones (simple): guardar cada comando de agregar o eliminar
    char txn_buffer[BUFSIZE * 4];
    int txn_len;
} client_t;

int listen_fd;
int max_clients;
client_t *clients;
int transaction_owner = -1; // fd del cliente dueño de transacción, -1 = ninguno
int csv_fd = -1;

void trim(char *s)
{
    while (*s && (*s == '\r' || *s == '\n'))
        s++;
    size_t L = strlen(s);
    while (L && (s[L - 1] == '\r' || s[L - 1] == '\n'))
        s[--L] = '\0';
}

ssize_t enviarLinea(int fd, const char *s)
{
    size_t n = strlen(s);
    return send(fd, s, n, 0);
}

int lock_file_exclusive(int fd)
{
    struct flock fl = {0};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; // whole file
    return fcntl(fd, F_SETLK, &fl);
}

int unlock_file(int fd)
{
    struct flock fl = {0};
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    return fcntl(fd, F_SETLK, &fl);
}

void handle_query_id(int cli_fd, const char *arg) {
    // simple: search for line starting with id,
    FILE *f = fopen(CSVPATH, "r");
    if (!f) {
        enviarLinea(cli_fd, "ERROR: no se encuentra la base CSV\n");
        return;
    }

    char line[1024];
    int found = 0;

    // Saltear cabecera
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        enviarLinea(cli_fd, "ERROR: archivo vacío o ilegible\n");
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        printf("%ld - Línea\n", clock());
        char copy[1024];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = 0;
        char *tok = strtok(copy, SEP_COLUMNAS);
        if (tok && strcmp(tok, arg) == 0) {
            // Retornar línea
            enviarLinea(cli_fd, line);
            found = 1;
            break;
        }
    }

    if (!found)
        enviarLinea(cli_fd, "NOTFOUND: No existe la ID\n");

    fclose(f);
}

void apply_txn_and_commit(client_t *c)
{
    // For simplicity, txn buffer contains newline separated operations of type:
    // INSERT:<line>
    // DELETE:<id>
    // We will read current file into memory, apply operations, and overwrite file atomically.
    FILE *f = fopen(CSVPATH, "r");
    if (!f)
    {
        enviarLinea(c->fd, "ERROR: No se pudo abrir el archivo CSV\n");
        return;
    }

    // Leer todas las líneas
    char **lines = NULL;
    size_t lines_n = 0;
    char buff[2048];

    // Ignorar cabecera
    char header[2048];
    if (!fgets(header, sizeof(header), f))
        header[0] = 0;

    while (fgets(buff, sizeof(buff), f))
    {
        lines = realloc(lines, sizeof(char *) * (lines_n + 1));
        lines[lines_n] = strdup(buff);
        lines_n++;
    }

    fclose(f);

    // Aplicar operaciones
    char *p = c->txn_buffer;
    while (*p)
    {
        char *nl = strchr(p, '\n');
        if (!nl)
            break;

        *nl = 0;
        if (strncmp(p, "INSERT:", 7) == 0)
        {
            char *line = p + 7;
            lines = realloc(lines, sizeof(char *) * (lines_n + 1));
            lines[lines_n] = strdup(line);
            lines_n++;
        }
        else if (strncmp(p, "DELETE:", 7) == 0)
        {
            char *id = p + 7;

            // remove matches
            for (size_t i = 0; i < lines_n; i++)
            {
                char copy[1024];
                strncpy(copy, lines[i], sizeof(copy) - 1);
                char *tok = strtok(copy, SEP_COLUMNAS);
                if (tok && strcmp(tok, id) == 0)
                {
                    free(lines[i]);
                    // shift
                    for (size_t j = i; j + 1 < lines_n; j++)
                        lines[j] = lines[j + 1];

                    lines_n--;
                    i--;
                }
            }
        }
        p = nl + 1;
    }

    // Escribir archivo temporal y renombrar
    char tmpname[] = "datos_tmp_XXXXXX";
    int tmpfd = mkstemp(tmpname);
    if (tmpfd < 0)
    {
        enviarLinea(c->fd, "ERROR: Falló mkstemp\n");
        // free
        for (size_t i = 0; i < lines_n; i++)
            free(lines[i]);
        free(lines);
        return;
    }

    FILE *tf = fdopen(tmpfd, "w");
    if (header[0])
        fprintf(tf, "%s", header);

    for (size_t i = 0; i < lines_n; i++)
        fprintf(tf, "%s", lines[i]);

    fflush(tf);
    fsync(tmpfd);
    fclose(tf);

    // Reemplazar atómicamente
    rename(tmpname, CSVPATH);

    for (size_t i = 0; i < lines_n; i++)
        free(lines[i]);
    free(lines);

    enviarLinea(c->fd, "COMMIT_OK: Transacción aplicada exitosamente\n");

    // Limpiar txn
    c->txn_len = 0;
    c->txn_buffer[0] = 0;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s <ip> <puerto> <max_clientes>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    max_clients = atoi(argv[3]);
    if (max_clients <= 0)
        max_clients = 10;

    // Si no existe el CSV tirar error
    if (access(CSVPATH, F_OK) != 0)
    {
        perror("Error: No existe el archivo de base de datos (debe llamarse registros.csv)");
        return 1;
    }

    // Abrir socket de servidor
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("Hubo un error al abrir socket de escucha");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("No se pudo asociar el socket a la IP");
        return 1;
    }

    if (listen(listen_fd, max_clients) < 0)
    {
        perror("No se pudo poner el socket en escucha");
        return 1;
    }

    clients = calloc(max_clients, sizeof(client_t));
    for (int i = 0; i < max_clients; i++)
        clients[i].fd = -1;

    fd_set readset;
    int maxfd = listen_fd;

    printf("Servidor escuchando en %s:%d\n", ip, port);

    while (1)
    {
        FD_ZERO(&readset);
        FD_SET(listen_fd, &readset);
        for (int i = 0; i < max_clients; i++)
            if (clients[i].fd != -1)
                FD_SET(clients[i].fd, &readset);

        int rv = select(maxfd + 1, &readset, NULL, NULL, NULL);
        if (rv < 0)
        {
            if (errno == EINTR)
                continue;

            perror("Error al seleccionar siguiente entrada");
            break;
        }

        if (FD_ISSET(listen_fd, &readset))
        {
            struct sockaddr_in cliaddr;
            socklen_t clilen = sizeof(cliaddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&cliaddr, &clilen);
            printf("aca1\n");
            if (cfd >= 0)
            {
                int slot = -1;
                printf("aca2\n");
                for (int i = 0; i < max_clients; i++)
                    if (clients[i].fd == -1)
                    {
                        printf("aca3 %d\n", i);
                        slot = i;

                        break;
                    }


                printf("aca4\n");
                if (slot == -1)
                {
                    printf("aca5\n");
                    enviarLinea(cfd, "Error: El servidor está lleno\n");
                    close(cfd);
                }
                else
                {
                    printf("aca6\n");
                    clients[slot].fd = cfd;
                    clients[slot].buflen = 0;
                    clients[slot].in_transaction = 0;
                    clients[slot].txn_len = 0;
                    char welcome[128];
                    snprintf(welcome, sizeof(welcome), "BIENVENIDO %d\nUsa el comando HELP para obtener ayuda.\n", cfd);
                    enviarLinea(cfd, welcome);
                    if (cfd > maxfd)
                        maxfd = cfd;
                    printf("Cliente conectado fd=%d slot=%d\n", cfd, slot);
                }
            }
        }

        // Manejar entrada de clientes
        for (int i = 0; i < max_clients; i++)
        {
            if (clients[i].fd == -1)
                continue;

            int fd = clients[i].fd;
            if (!FD_ISSET(fd, &readset))
                continue;

            // Leer entrada
            char tmp[BUFSIZE];
            printf("aca7\n");
            ssize_t r = recv(fd, tmp, sizeof(tmp) - 1, 0);
            if (r <= 0)
            {
                printf("Cliente fd=%d desconectado\n", fd);
                if (transaction_owner == fd)
                {
                    // Abortar transacción
                    transaction_owner = -1;

                    // Desbloquear archivo si está bloqueado
                    if (csv_fd >= 0)
                    {
                        unlock_file(csv_fd);
                        close(csv_fd);
                        csv_fd = -1;
                    }
                }
                close(fd);
                clients[i].fd = -1;
                continue;
            }

            tmp[r] = '\0';

            // Sumar a búfer de cliente y procesar líneas
            strncat(clients[i].buf + clients[i].buflen, tmp, sizeof(clients[i].buf) - clients[i].buflen - 1);
            clients[i].buflen += strlen(tmp);

            // Procesar líneas completas
            char *line;
            while ((line = strstr(clients[i].buf, "\n")) != NULL)
            {
                size_t len = line - clients[i].buf + 1;
                char cmdline[2048];
                strncpy(cmdline, clients[i].buf, len);
                cmdline[len] = 0;

                // shift buffer left
                memmove(clients[i].buf, clients[i].buf + len, clients[i].buflen - len + 1);
                clients[i].buflen -= len;
                trim(cmdline);
                if (strlen(cmdline) == 0)
                    continue;

                // Parsear
                if (strcasecmp(cmdline, "BEGIN") == 0)
                {
                    if (transaction_owner != -1 && transaction_owner != fd)
                    {
                        enviarLinea(fd, "ERROR: Transacción activa por otro cliente\n");
                    }
                    else
                    {
                        // Intentar bloquear archivo
                        csv_fd = open(CSVPATH, O_RDWR);
                        if (csv_fd < 0)
                        {
                            enviarLinea(fd, "ERROR: No se pudo abrir CSV para bloqueo\n");
                        }
                        else if (lock_file_exclusive(csv_fd) != 0)
                        {
                            enviarLinea(fd, "ERROR: No se pudo obtener lock (intente luego)\n");
                            close(csv_fd);
                            csv_fd = -1;
                        }
                        else
                        {
                            transaction_owner = fd;
                            clients[i].in_transaction = 1;
                            clients[i].txn_len = 0;
                            clients[i].txn_buffer[0] = 0;
                            enviarLinea(fd, "BEGIN_OK: Transacción iniciada\n");
                        }
                    }
                }
                else if (strcasecmp(cmdline, "COMMIT") == 0)
                {
                    if (transaction_owner != fd)
                    {
                        enviarLinea(fd, "ERROR: No posee transacción\n");
                    }
                    else
                    {
                        // apply txn actions and unlock
                        apply_txn_and_commit(&clients[i]);
                        if (csv_fd >= 0)
                        {
                            unlock_file(csv_fd);
                            close(csv_fd);
                            csv_fd = -1;
                        }
                        transaction_owner = -1;
                        clients[i].in_transaction = 0;
                    }
                }
                else if (strncasecmp(cmdline, "QUERY ", 6) == 0)
                {
                    if (transaction_owner != -1 && transaction_owner != fd)
                    {
                        enviarLinea(fd, "ERROR: Transacción activa por otro cliente\n");
                    }
                    else
                    {
                        // for simplicity support "QUERY id"
                        char *arg = cmdline + 6;
                        trim(arg);
                        handle_query_id(fd, arg);
                    }
                }
                else if (strncasecmp(cmdline, "INSERT ", 7) == 0)
                {
                    if (transaction_owner != -1 && transaction_owner != fd)
                    {
                        enviarLinea(fd, "ERROR: Transacción activa por otro cliente\n");
                    }
                    else if (clients[i].in_transaction)
                    {
                        // buffer insert
                        char *rest = cmdline + 7;
                        strcat(clients[i].txn_buffer, "INSERT:");
                        strcat(clients[i].txn_buffer, rest);
                        strcat(clients[i].txn_buffer, "\n");
                        enviarLinea(fd, "INSERT_BUFFERED: Comando agregado a transacción\n");
                    }
                    else
                    {
                        // non-transaction immediate insert with file lock
                        int fdcsv = open(CSVPATH, O_RDWR | O_APPEND);
                        if (fdcsv < 0)
                        {
                            enviarLinea(fd, "ERROR: Abrir CSV\n");
                        }
                        else
                        {
                            if (lock_file_exclusive(fdcsv) != 0)
                            {
                                enviarLinea(fd, "ERROR: No se pudo bloquear\n");
                                close(fdcsv);
                            }
                            else
                            {
                                dprintf(fdcsv, "%s\n", cmdline + 7);
                                fsync(fdcsv);
                                unlock_file(fdcsv);
                                close(fdcsv);
                                enviarLinea(fd, "INSERT_OK: Inserción exitosa\n");
                            }
                        }
                    }
                }
                else if (strncasecmp(cmdline, "DELETE ", 7) == 0)
                {
                    if (transaction_owner != -1 && transaction_owner != fd)
                    {
                        enviarLinea(fd, "ERROR: Transacción activa por otro cliente\n");
                    }
                    else if (clients[i].in_transaction)
                    {
                        strcat(clients[i].txn_buffer, "DELETE:");
                        strcat(clients[i].txn_buffer, cmdline + 7);
                        strcat(clients[i].txn_buffer, "\n");
                        enviarLinea(fd, "DELETE_BUFFERED: Comando agregado a transacción\n");
                    }
                    else
                    {
                        // immediate delete: read whole file, rewrite without id
                        int fdcsv = open(CSVPATH, O_RDWR);
                        if (fdcsv < 0)
                        {
                            enviarLinea(fd, "ERROR: Abrir CSV\n");
                        }
                        else
                        {
                            if (lock_file_exclusive(fdcsv) != 0)
                            {
                                enviarLinea(fd, "ERROR: No se pudo bloquear\n");
                                close(fdcsv);
                            }
                            else
                            {
                                // do simple in-memory delete (reuse apply_txn_and_commit mechanism)
                                client_t tmpc = {0};
                                tmpc.fd = fd;
                                tmpc.txn_buffer[0] = 0;
                                tmpc.txn_len = 0;
                                strcat(tmpc.txn_buffer, "DELETE:");
                                strcat(tmpc.txn_buffer, cmdline + 7);
                                strcat(tmpc.txn_buffer, "\n");
                                apply_txn_and_commit(&tmpc);
                                unlock_file(fdcsv);
                                close(fdcsv);
                                enviarLinea(fd, "DELETE_OK: Borrado exitoso\n");
                            }
                        }
                    }
                }
                else if (strcasecmp(cmdline, "EXIT") == 0)
                {
                    enviarLinea(fd, "HASTA LUEGO\n");
                    close(fd);
                    clients[i].fd = -1;
                    if (transaction_owner == fd)
                    {
                        transaction_owner = -1;
                        if (csv_fd >= 0)
                        {
                            unlock_file(csv_fd);
                            close(csv_fd);
                            csv_fd = -1;
                        }
                    }
                }
                else if (strcasecmp(cmdline, "HELP") == 0)
                {
                    char res[1200];
                    *res = 0;

                    strcat(res, "  BEGIN          Inicia una nueva transacción.\n");
                    strcat(res, "  COMMIT         Aplica todos los cambios.\n");
                    strcat(res, "  QUERY <id>     Busca una ID determinada y devuelve su fila.\n");
                    strcat(res, "  INSERT <fila>  Inserta una fila completa en la base de datos.\n");
                    strcat(res, "  DELETE <id>    Busca una ID y elimina su fila.\n");
                    strcat(res, "  HELP           Muestra esta pantalla de ayuda.\n");
                    strcat(res, "  EXIT           Desconecta el cliente y cierra el programa.\n");

                    enviarLinea(fd, res);
                }
                else
                {
                    enviarLinea(fd, "ERROR: Comando desconocido\n");
                }
            }
        }
    }

    // cleanup
    close(listen_fd);
    return 0;
}
