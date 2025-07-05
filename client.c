#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <host> <port> <METHOD> [content]\n", prog_name);
    fprintf(stderr, "Methods: GET, POST, PUT, DELETE\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  %s 127.0.0.1 8080 GET\n", prog_name);
    fprintf(stderr, "  %s 127.0.0.1 8080 PUT '<h1>New Content</h1>'\n", prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        usage(argv[0]);
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *method = argv[3];
    const char *content = (argc == 5) ? argv[4] : "";

    int sock;
    struct sockaddr_in serv_addr;
    char request[BUFFER_SIZE] ={0};
    char response[BUFFER_SIZE] = {0};

    int req_len = snprintf(request, BUFFER_SIZE,
        "%s / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "\r\n"
        "%s",
        method, host, port, content);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("--- Sending Request ---\n%s\n-----------------------\n\n", request);
    send(sock, request, req_len, 0);

    printf("--- Server Response ---\n");
    int bytes_read;
    while ((bytes_read = read(sock, response, BUFFER_SIZE - 1)) > 0) {
        response[bytes_read] = '\0';
        printf("%s", response);
    }
    printf("\n-----------------------\n");

    close(sock);
    return 0;
}
