#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10

void strrev(char *str) {
	for (int l = 0, r = strlen(str) - 2; l < r; l++, r--) {
		char temp = str[l];
		str[l] = str[r];
		str[r] = temp;
	}
}

int main() {
	int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock_fd == -1) {
		perror("[ SOCKET ERR ]");
		exit(EXIT_FAILURE);
	}

	// Setting socket to reuse addr in case restarted while in TIME_WAIT
	const int enable = 1;
	if (setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
				   sizeof(int))) {
		perror("[ SETSOCKOPT ERR ]");
		printf("Address will be unusable incase of restart.\n");
		printf("Stop program now? [Y/n] (default=Y) : ");
		char choice;
		scanf(" %c", &choice);
		if (choice != 'n' && choice != 'N')
			exit(EXIT_SUCCESS);
	}

	// Setting the server addr
	struct sockaddr_in server_addr = {.sin_family = AF_INET,
									  .sin_addr.s_addr = htonl(INADDR_ANY),
									  .sin_port = htons(PORT)};

	// binding the server
	if (bind(listen_sock_fd, (struct sockaddr *)&server_addr,
			 sizeof(server_addr)) == -1) {
		perror("[ BIND ERR ]");
		exit(EXIT_FAILURE);
	}

	// Start listening
	if (listen(listen_sock_fd, MAX_ACCEPT_BACKLOG)) {
		perror("[ SOCKET ERR ]");
		exit(EXIT_FAILURE);
	}
	printf("[INFO] Server listening on port %d\n", PORT);

	// defining epoll instance
	int epollfd = epoll_create1(0);
	if (epollfd == -1) {
		perror("[ EPOLL ERR ]");
		exit(EXIT_FAILURE);
	}
	struct epoll_event event, events[MAX_EPOLL_EVENTS];

	event.events = EPOLLIN;
	event.data.fd = listen_sock_fd;

	// Added listening socket to epoll monitor events
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock_fd, &event) == -1) {
		perror("[ EPOLL ERR ]");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	while (1) {
		printf("[DEBUG] Epoll wait\n");
		int n_ready_fds = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);
		if (n_ready_fds == -1) {
			perror("[ EPOLL ERR ]");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < n_ready_fds; ++i) {
			int cur_fd = events[i].data.fd;

			if (cur_fd == listen_sock_fd) { // cur event on listening socket
				int conn_sock_fd =
					accept(listen_sock_fd, (struct sockaddr *)&client_addr,
						   &client_addr_len);
				if (conn_sock_fd == -1) {
					perror("[ CONNECTION ERR]");
					continue;
				}
				printf("[INFO] New client connected to server\n");

				event.events = EPOLLIN;
				event.data.fd = conn_sock_fd;

				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock_fd, &event)) {
					perror("[ EPOLL ERR ]");
				}
			} else {
				char buf[BUFF_SIZE];
				memset(buf, 0, BUFF_SIZE);

				ssize_t read_n = recv(cur_fd, buf, BUFF_SIZE, 0);

				if (read_n < 0) {
					perror("[ READ ERR ]");
					printf("[INFO] Error occured. Closing connection\n");
					epoll_ctl(epollfd, EPOLL_CTL_DEL, cur_fd, NULL);
					close(cur_fd);
					continue;
				} else if (read_n == 0) {
					printf("[INFO] Client disconnected. Closing connection\n");
					epoll_ctl(epollfd, EPOLL_CTL_DEL, cur_fd, NULL);
					close(cur_fd);
					continue;
				}

				printf("[CLIENT MESSAGE] %s", buf);

				strrev(buf);

				if (send(cur_fd, buf, read_n, 0) == -1) {
					perror("[ SEND ERR ]");
					printf("[INFO] Error occured. Closing connection\n");
					epoll_ctl(epollfd, EPOLL_CTL_DEL, cur_fd, NULL);
					close(cur_fd);
				}
			}
		}
	}

	epoll_ctl(epollfd, EPOLL_CTL_DEL, listen_sock_fd, NULL);
	close(listen_sock_fd);

	return 0;
}
