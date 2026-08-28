#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define UPSTREAM_PORT 3000
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_SOCKS 10
#define MAX_EPOLL_EVENTS 10

int listen_sock_fd, epoll_fd;
int route_table_size = 0;

enum connection_type_t { CONN_LISTEN, CONN_CLIENT, CONN_UPSTREAM };

typedef struct connection_t {
	int fd;
	enum connection_type_t conn_type;
	struct connection_t *peer;
	int closed;
} connection_t;

connection_t *pending_rm[2 * MAX_EPOLL_EVENTS];
int rm_pending_connections = 0;

int connect_upstream() {
	int upstream_sock = socket(AF_INET, SOCK_STREAM, 0);

	if (upstream_sock == -1) {
		perror("[ SOCK ERR ]");
		return upstream_sock;
	}

	struct sockaddr_in upstream_addr = {.sin_family = AF_INET,
										.sin_addr.s_addr =
											inet_addr("127.0.0.1"),
										.sin_port = htons(UPSTREAM_PORT)};

	if (connect(upstream_sock, (struct sockaddr *)&upstream_addr,
				sizeof(upstream_addr)) == -1) {
		perror("[ CONN ERR ]");
		return -1;
	}

	return upstream_sock;
}

void rm_conn(int epoll_fd, connection_t *conn) {
	if (conn->closed)
		return;

	connection_t *peer = conn->peer;

	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
	conn->closed = 1;
	pending_rm[rm_pending_connections++] = conn;

	if (peer && !peer->closed) {
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, peer->fd, NULL);
		peer->closed = 1;
		pending_rm[rm_pending_connections++] = peer;
	}

	route_table_size -= 1;
}

int loop_attach(int epoll_fd, connection_t *conn, int events) {
	struct epoll_event event = {.events = events, .data.ptr = conn};

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &event) == -1) {
		perror("[ EPOLL ERR ]");
		return 1;
	}

	return 0;
}

void accept_connection(int listen_sock_fd) {
	if (route_table_size >= MAX_SOCKS) {
		fprintf(stderr, "[ CONN ERR ] : at maximum connection limit\n");
		return;
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int conn_sock_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr,
							  &client_addr_len);
	if (conn_sock_fd == -1) {
		perror("[ CONN ERR ]");
		return;
	}

	int upstream_sock_fd = connect_upstream();
	if (upstream_sock_fd == -1) {
		close(conn_sock_fd);
		return;
	}

	connection_t *client, *upstream;
	client = (connection_t *)malloc(sizeof(connection_t));
	upstream = (connection_t *)malloc(sizeof(connection_t));

	client->fd = conn_sock_fd;
	client->conn_type = CONN_CLIENT;
	client->peer = upstream;
	client->closed = 0;

	upstream->conn_type = CONN_UPSTREAM;
	upstream->fd = upstream_sock_fd;
	upstream->peer = client;
	upstream->closed = 0;

	if (loop_attach(epoll_fd, client, EPOLLIN)) {
		close(conn_sock_fd);
		close(upstream_sock_fd);
		free(client);
		free(upstream);
		return;
	}

	if (loop_attach(epoll_fd, upstream, EPOLLIN)) {
		client->peer = NULL;
		rm_conn(epoll_fd, client);
		close(upstream_sock_fd);
		free(upstream);
		return;
	}

	route_table_size += 1;
}

int create_server() {
	/* create listening socket and return it */
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

int create_loop() {
	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		perror("[ EPOLL ERR ]");
		exit(EXIT_FAILURE);
	}
	return epoll_fd;
}

void handle_client(connection_t *const client_conn) {
	char buf[BUFF_SIZE];
	memset(buf, 0, BUFF_SIZE);

	int read_n = recv(client_conn->fd, buf, BUFF_SIZE, 0);

	if (read_n == 0) {
		printf("[INFO] Client disconnected. Closing connection\n");
		rm_conn(epoll_fd, client_conn);
		return;
	} else if (read_n < 0) {
		perror("[ READ ERR ]");
		printf("[INFO] Error occured. Closing connection\n");
		rm_conn(epoll_fd, client_conn);
		return;
	}

	printf("[CLIENT MESSAGE] %s", buf);

	int upstream_sock_fd = client_conn->peer->fd;

	int bytes_written = 0;
	int msg_len = read_n;
	while (bytes_written < msg_len) {
		int n = send(upstream_sock_fd, buf + bytes_written,
					 msg_len - bytes_written, 0);
		bytes_written += n;
	}
}

void handle_upstream(connection_t *const upstream_conn) {
	char buf[BUFF_SIZE];
	memset(buf, 0, BUFF_SIZE);

	int read_n = recv(upstream_conn->fd, buf, BUFF_SIZE, 0);

	if (read_n == 0) {
		printf("[INFO] Upstream disconnected. Closing connection\n");
		rm_conn(epoll_fd, upstream_conn);
		return;
	} else if (read_n < 0) {
		perror("[ READ ERR ]");
		printf("[INFO] Error occured. Closing connection\n");
		rm_conn(epoll_fd, upstream_conn);
		return;
	}

	printf("[CLIENT MESSAGE] %s", buf);

	int client_sock_fd = upstream_conn->peer->fd;

	int bytes_written = 0;
	int msg_len = read_n;
	while (bytes_written < msg_len) {
		int n = send(client_sock_fd, buf + bytes_written,
					 msg_len - bytes_written, 0);
		bytes_written += n;
	}
}

void loop_run(int epoll_fd) { /* infinite loop and for loop*/
	while (1) {
		printf("[DEBUG] Epoll Wait\n");
		struct epoll_event events[MAX_EPOLL_EVENTS];

		int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
		if (n_ready_fds == -1) {
			perror("[ EPOLL ERR ]");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < n_ready_fds; ++i) {
			connection_t *cur_conn = events[i].data.ptr;
			if (cur_conn->closed)
				continue;

			if (cur_conn->conn_type == CONN_LISTEN) {
				accept_connection(listen_sock_fd);
				continue;
			} else if (cur_conn->conn_type == CONN_CLIENT)
				handle_client(cur_conn);
			else
				handle_upstream(cur_conn);
		}

		while (rm_pending_connections) {
			connection_t *conn = pending_rm[--rm_pending_connections];
			close(conn->fd);
			free(conn);
		};
	}
}

int main() { /* initialize proxy */
	listen_sock_fd = create_server();
	epoll_fd = create_loop();

	connection_t listener = {
		.fd = listen_sock_fd, .conn_type = CONN_LISTEN, .peer = NULL};
	loop_attach(epoll_fd, &listener, EPOLLIN);

	loop_run(epoll_fd);

	return 0;
}
