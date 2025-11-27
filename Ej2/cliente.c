// tp2_client.c
// Compilar: gcc -std=c11 -O2 -Wall tp2_client.c -o tp2_client
// Uso: ./tp2_client <server_ip> <port>
// Luego interactuar por consola, comandos: BEGIN, COMMIT, QUERY <id>, INSERT <csv_line>, DELETE <id>, EXIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFSIZE 4096

int main(int argc, char **argv)
{
    char inbuf[BUFSIZE], linea[2048];
    const char *ip;
    int port, sock, res;

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    ip = argv[1];
    port = atoi(argv[2]);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("No se pudo abrir el socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("No se pudo conectar al servidor");
        return 1;
    }

    // Leer bienvenida
    ssize_t r = recv(sock, inbuf, sizeof(inbuf) - 1, 0);
    if (r > 0) {
        inbuf[r] = 0;
        printf("%s", inbuf);
    }

    while (1) {
        printf("> ");
        fflush(stdout);

        // Leer comando de entrada
        if (!fgets(linea, sizeof(linea), stdin))
            break;

        // Enviar por socket
        if (send(sock, linea, strlen(linea), 0) < 0)
            break;

        // Leer respuesta
        ssize_t res = recv(sock, inbuf, sizeof(inbuf) - 1, 0);
        if (res <= 0)
            break;

        inbuf[res] = 0;
        printf("%s", inbuf);

        if (strcasecmp(linea, "EXIT\n") == 0)
            break;
    }

    close(sock);
    return 0;
}
