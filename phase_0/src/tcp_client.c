#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define BUFF_SIZE 10000

int main() {
    int client_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Setting the server addr
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
        .sin_port = htons(SERVER_PORT)
    };

    if (connect(client_sock_fd, ( struct sockaddr * )&server_addr, sizeof(struct sockaddr_in))) {
        printf("[ERROR] Failed to connect to TCP server\n");
        exit(1);
    } else {
        printf("[INFO] Connected to TCP server\n");
    }

    while (1) {
        char* line;
        size_t line_len = 0, read_n;

        read_n = getline(&line, &line_len, stdin);

        send(client_sock_fd, line, read_n, 0);

        char buf[BUFF_SIZE];
        memset(buf, 0, BUFF_SIZE);

        read_n = recv(client_sock_fd, buf, sizeof(buf), 0);

        if (read_n < 0) {
            printf("[INFO] Error occured. Closing connection\n");
            close(client_sock_fd);
            exit(1);
        } else if (read_n == 0) {
            printf("[INFO] Server disconnected. Closing connection\n");
            close(client_sock_fd);
            exit(1);
        }

        printf("[SERVER MESSAGE] %s", buf);
    }

    return 0;
}
