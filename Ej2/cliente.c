// tp2_client.c
// Compilar: gcc -std=c11 -O2 -Wall tp2_client.c -o tp2_client
// Uso: ./tp2_client <server_ip> <port>
// Luego interactuar por consola, comandos: BEGIN, COMMIT, QUERY <id>, INSERT <csv_line>, DELETE <id>, EXIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFSIZE 4096

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        return 1;
    }

    char inbuf[BUFSIZE];
    // read welcome
    ssize_t r = recv(s, inbuf, sizeof(inbuf) - 1, 0);
    if (r > 0)
    {
        inbuf[r] = 0;
        printf("%s", inbuf);
    }

    while (1)
    {
        printf("> ");
        fflush(stdout);
        char line[2048];
        if (!fgets(line, sizeof(line), stdin))
            break;
        if (send(s, line, strlen(line), 0) < 0)
            break;
        // read response (simple)
        ssize_t n = recv(s, inbuf, sizeof(inbuf) - 1, 0);
        if (n <= 0)
            break;
        inbuf[n] = 0;
        printf("%s", inbuf);
        if (strncmp(line, "EXIT", 4) == 0)
            break;
    }
    close(s);
    return 0;
}
