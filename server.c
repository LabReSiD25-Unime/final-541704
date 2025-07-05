#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#define PORT 8080
#define MAX_EVENTS 10
#define BUFFER_SIZE 1024
#define RESOURCE_FILE "risorsa.html"

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int fd;
    char read_buffer[BUFFER_SIZE];
    int read_pos;
    char write_buffer[BUFFER_SIZE];
    int write_pos;
    int total_to_write;
} ConnectionState;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

void close_connection(int epoll_fd, ConnectionState *state) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, state->fd, NULL);
    close(state->fd);
    free(state);
    printf("Connection closed\n");
}

void prepare_response(ConnectionState *state, int status_code, const char *status_text, const char *body) {
    state->total_to_write = snprintf(state->write_buffer, BUFFER_SIZE,
        "HTTP/1.1 %d %s\r\n"
        "Server: Server C Paladino\r\n"
        "\r\n"
        "%s",
        status_code, status_text, body ? body : "");
    state->write_pos = 0;
}

void handle_request(ConnectionState *state) {
    char method[16], uri[256], version[16];
    sscanf(state->read_buffer, "%15s %255s %15s", method, uri, version);

    printf("Request: %s %s %s\n", method, uri, version);

    pthread_mutex_lock(&file_mutex);

    if (strcmp(method, "GET") == 0) {
        FILE *f = fopen(RESOURCE_FILE, "r");
        if (!f) {
            prepare_response(state, 404, "Not Found", "Resource not found.");
        } else {
            char file_buffer[BUFFER_SIZE] = {0};
            fread(file_buffer, 1, BUFFER_SIZE - 1, f);
            fclose(f);
            prepare_response(state, 200, "OK", file_buffer);
        }
    } else if (strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) {
        const char *body_start = strstr(state->read_buffer, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            const char* mode = (strcmp(method, "PUT") == 0) ? "w" : "a";
            FILE *f = fopen(RESOURCE_FILE, mode);
            if(f) {
                fputs(body_start, f);
                fclose(f);
                int status_code = (strcmp(method, "PUT") == 0) ? 201 : 200;
                const char* status_text = (strcmp(method, "PUT") == 0) ? "Created" : "OK";
                prepare_response(state, status_code, status_text, "Resource updated.");
            } else {
                prepare_response(state, 500, "Internal Server Error", "Could not write to resource.");
            }
        } else {
            prepare_response(state, 400, "Bad Request", "Missing body for PUT/POST.");
        }
    } else if (strcmp(method, "DELETE") == 0) {
        if (remove(RESOURCE_FILE) == 0) {
            prepare_response(state, 204, "No Content", NULL);
        } else {
            prepare_response(state, 404, "Not Found", "Resource not found, cannot delete.");
        }
    } else {
        prepare_response(state, 501, "Not Implemented", "Method not implemented.");
    }

    pthread_mutex_unlock(&file_mutex);
}

int main() {
    int listen_fd, epoll_fd;
    struct sockaddr_in server_addr;
    struct epoll_event event, events[MAX_EVENTS];

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblocking(listen_fd);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    epoll_fd = epoll_create1(0);
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);

    printf("Server listening on port %d\n", PORT);

    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                set_nonblocking(conn_fd);

                ConnectionState *state = (ConnectionState *)malloc(sizeof(ConnectionState));
                memset(state, 0, sizeof(ConnectionState));
                state->fd = conn_fd;

                event.events = EPOLLIN;
                event.data.ptr = state;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event);
                printf("New connection accepted\n");
            } else {
                ConnectionState *state = (ConnectionState *)events[i].data.ptr;
                if (events[i].events & EPOLLIN) {
                    int bytes_read = read(state->fd, state->read_buffer + state->read_pos, BUFFER_SIZE - state->read_pos);
                    if (bytes_read <= 0) {
                        close_connection(epoll_fd, state);
                        continue;
                    }
                    state->read_pos += bytes_read;
                    if (strstr(state->read_buffer, "\r\n\r\n")) {
                        handle_request(state);
                        event.events = EPOLLOUT;
                        event.data.ptr = state;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, state->fd, &event);
                    }
                } else if (events[i].events & EPOLLOUT) {
                    int bytes_written = write(state->fd, state->write_buffer + state->write_pos, state->total_to_write - state->write_pos);
                    if (bytes_written < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            close_connection(epoll_fd, state);
                        }
                        continue;
                    }
                    state->write_pos += bytes_written;
                    if (state->write_pos >= state->total_to_write) {
                        close_connection(epoll_fd, state);
                    }
                }
            }
        }
    }

    close(listen_fd);
    pthread_mutex_destroy(&file_mutex);
    return 0;
}
