#include <arpa/inet.h>
#include <asm-generic/socket.h>
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
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

int listen_socket_fd, epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0;

enum connection_type_t { CLIENT, UPSTREAM, UNDEF };

int create_loop() {
	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		perror("[ EPOLL ERR ]");
		exit(EXIT_FAILURE);
	}
	return epoll_fd;
}

void loop_attach(int epoll_fd, int fd, int events) {
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		perror("[ EPOLL ERR ]");
	}
}

int create_server() {
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

	return listen_sock_fd;
}

int connect_upstream() {
	int upstream_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in upstream_addr = {.sin_family = AF_INET,
										.sin_addr.s_addr =
											inet_addr("127.0.0.1"),
										.sin_port = htons(UPSTREAM_PORT)};
	if (connect(upstream_sock_fd, (struct sockaddr *)&upstream_addr,
				sizeof(upstream_addr)) == -1) {
        close(upstream_sock_fd);
		return -1;
	}
	return upstream_sock_fd;
}

int find_route_table_entry(int fd, enum connection_type_t type) {
	if (type != UNDEF) {
		for (int i = 0; i < route_table_size; i++)
			if (route_table[i][type] == fd)
				return i;
	} else {
		for (int i = 0; i < route_table_size; i++) {
			if (route_table[i][CLIENT] == fd || route_table[i][UPSTREAM] == fd)
				return i;
		}
	}

	return -1;
}

void accept_connection(int listen_socket_fd) {
	if (route_table_size >= MAX_SOCKS) {
		printf("[INFO] Max open connections reached\n");
		return;
	}
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int conn_sock_fd = accept(listen_socket_fd, (struct sockaddr *)&client_addr,
							  &client_addr_len);
	if (conn_sock_fd == -1) {
		perror("[ CONNECTION ERR]");
		return;
	}

	int upstream_sock_fd = connect_upstream();
    if (upstream_sock_fd == -1)
        return;

	loop_attach(epoll_fd, upstream_sock_fd, EPOLLIN);
	loop_attach(epoll_fd, conn_sock_fd, EPOLLIN);

	route_table[route_table_size][CLIENT] = conn_sock_fd;
	route_table[route_table_size][UPSTREAM] = upstream_sock_fd;
	route_table_size++;

	printf("[INFO] Client connected to server\n");
}

void handle_client(int conn_sock_fd) {
	char buff[BUFF_SIZE];
	memset(buff, 0, BUFF_SIZE);

	int read_n = recv(conn_sock_fd, buff, sizeof(buff), 0);

	int ind = find_route_table_entry(conn_sock_fd, CLIENT);
	int upstream_sock_fd = route_table[ind][UPSTREAM];

	if (read_n <= 0) {
		route_table[ind][0] = -1;
		route_table[ind][1] = -1;
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn_sock_fd, NULL);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, upstream_sock_fd, NULL);
		close(upstream_sock_fd);
		close(conn_sock_fd);
		if (read_n == 0)
			printf("[INFO] Client disconnected. Closing connection\n");
		else {
			perror("[ READ ERR ]");
			printf("[INFO] Error occured. Closing connection\n");
		}
		return;
	}

	printf("[CLIENT MESSAGE] %s", buff);

	int bytes_written = 0;
	int message_len = read_n;
	while (bytes_written < message_len) {
		int n = send(upstream_sock_fd, buff + bytes_written,
					 message_len - bytes_written, 0);
		bytes_written += n;
	}
}

void handle_upstream(int upstream_sock_fd) {
	char buff[BUFF_SIZE];
	memset(buff, 0, BUFF_SIZE);

	int read_n = recv(upstream_sock_fd, buff, sizeof(buff), 0);

	int ind = find_route_table_entry(upstream_sock_fd, UPSTREAM);
	int client_sock_fd = route_table[ind][CLIENT];

	if (read_n <= 0) {
		route_table[ind][0] = -1;
		route_table[ind][1] = -1;
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, upstream_sock_fd, NULL);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock_fd, NULL);
		close(upstream_sock_fd);
		close(client_sock_fd);
		if (read_n == 0)
			printf("[INFO] Upstream disconnected. Closing connection\n");
		else {
			perror("[ READ ERR ]");
			printf("[INFO] Error occured. Closing connection\n");
		}
		return;
	}

	printf("[UPSTREAM MESSAGE] %s", buff);

	int bytes_written = 0;
	int message_len = read_n;
	while (bytes_written < message_len) {
		int n = send(client_sock_fd, buff + bytes_written,
					 message_len - bytes_written, 0);
		bytes_written += n;
	}
}

int get_conn_type(int fd) {
	for (int i = 0; i < route_table_size; i++) {
		if (route_table[i][CLIENT] == fd)
			return CLIENT;
	}

	return UPSTREAM;
}

void loop_run(int epoll_fd) {
	struct epoll_event events[MAX_EPOLL_EVENTS];
	while (1) {
		printf("[DEBUG] Epoll wait\n");
		int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
		for (int i = 0; i < n_ready_fds; i++) {
			int conn_sock_fd = events[i].data.fd;

			if (conn_sock_fd == listen_socket_fd) {
				accept_connection(listen_socket_fd);
			} else if (get_conn_type(conn_sock_fd) == CLIENT) {
				handle_client(conn_sock_fd);
			} else {
				handle_upstream(conn_sock_fd);
			}
		}
	}
}

int main() {
	listen_socket_fd = create_server();

	epoll_fd = create_loop();

	loop_attach(epoll_fd, listen_socket_fd, EPOLLIN);

	loop_run(epoll_fd);
}
