#include <netinet/in.h>
#include <stdio.h>
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

	// Setting socket to reuse addr in case restarted while in TIME_WAIT
	const int enable = 1;
	setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

	// Setting the server addr
	struct sockaddr_in server_addr = {.sin_family = AF_INET,
									  .sin_addr.s_addr = htonl(INADDR_ANY),
									  .sin_port = htons(PORT)};

	// binding the server
	int bind_output = bind(listen_sock_fd, (struct sockaddr *)&server_addr,
						   sizeof(struct sockaddr_in));

	// Start listening
	listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
	printf("[INFO] Server listening on port %d\n", PORT);

	// defining epoll instance
	int epollfd = epoll_create1(0);
	struct epoll_event event, events[MAX_EPOLL_EVENTS];

	event.events = EPOLLIN;
	event.data.fd = listen_sock_fd;

	// Added listening socket to epoll monitor events
	epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock_fd, &event);

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	while (1) {
        printf("[DEBUG] Epoll wait\n");
		int n_ready_fds = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);

		for (int i = 0; i < n_ready_fds; ++i) {
			int cur_fd = events[i].data.fd;

			if (cur_fd == listen_sock_fd) { // cur event on listening socket
				int conn_sock_fd =
					accept(listen_sock_fd, (struct sockaddr *)&client_addr,
						   &client_addr_len);
				printf("[INFO] Client connected to server\n");

				event.events = EPOLLIN;
				event.data.fd = conn_sock_fd;

				epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock_fd, &event);
			} else {
				char buf[BUFF_SIZE];
				memset(buf, 0, BUFF_SIZE);

				ssize_t read_n = recv(cur_fd, buf, sizeof(buf), 0);

				if (read_n < 0) {
					printf("[INFO] Error occured. Closing connection\n");
					close(cur_fd);
					break;
				} else if (read_n == 0) {
					printf("[INFO] Client disconnected. Closing connection\n");
					close(cur_fd);
					break;
				}

				printf("[CLIENT MESSAGE] %s", buf);

				strrev(buf);

				send(cur_fd, buf, read_n, 0);
			}
		}
	}

	close(listen_sock_fd);

	return 0;
}
