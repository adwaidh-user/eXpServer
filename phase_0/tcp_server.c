#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5

int main() {
    int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Setting socket to reuse addr in case restarted while in TIME_WAIT
    int enable = 1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Setting the server addr
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT)
    };

    // binding the server
    int bind_output = bind(listen_sock_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));

    //Start listening
    listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
    printf("[INFO] Server listening on port %d\n", PORT);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    int conn_sock_fd = accept(listen_sock_fd, ( struct sockaddr * )&client_addr, &client_addr_len);
    printf("[INFO] Client connected to server\n");
}
