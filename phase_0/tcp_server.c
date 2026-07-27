#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5

void strrev(char* str) {
    for (int l = 0, r = strlen(str) - 2; l < r; l++, r--) {
        char temp = str[l];
        str[l] = str[r];
        str[r] = temp;
    }
}

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

    while (1) {
        char buf[BUFF_SIZE]; 
        memset(buf, 0, BUFF_SIZE);

        ssize_t read_n = recv(conn_sock_fd, buf, sizeof(buf), 0);

        if (read_n < 0) {
            printf("[INFO] Error occured. Closing server\n");
            close(conn_sock_fd);
            exit(1);
        } else if (read_n == 0) {
            printf("[INFO] Client disconnected. Closing server\n");
            close(conn_sock_fd);
            exit(1);
        }

        printf("[Client message] %s", buf);

        strrev(buf);

        send(conn_sock_fd, buf, read_n, 0);
    }
}
