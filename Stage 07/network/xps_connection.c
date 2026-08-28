#include "xps_connection.h"

void connection_read_handler(void *ptr);

void strrev(char *str) {
	int len = strlen(str);
	int l = 0, r = len - 1;

	if (len > 0 && str[r] == '\n')
		r--;

	while (l < r) {
		char temp = str[l];
		str[l] = str[r];
		str[r] = temp;
		l++;
		r--;
	}
}

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd) {
	xps_connection_t *connection =
		(xps_connection_t *)malloc(sizeof(xps_connection_t));

	if (connection == NULL) {
		logger(LOG_ERROR, "xps_connection_create()",
			   "malloc() failed for 'connection'");
		return NULL;
	}

	/* attach sock_fd to epoll */
	xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection,
					connection_read_handler);

	// Init values
	connection->core = core;
	connection->sock_fd = sock_fd;
	connection->listener = NULL;
	connection->remote_ip = get_remote_ip(sock_fd);

	vec_push(&(core->connections), connection);

	logger(LOG_DEBUG, "xps_connection_create()", "created connection");
	return connection;
}

void xps_connection_destroy(xps_connection_t *connection) {
	assert(connection != NULL);

	xps_core_t *core = connection->core;
	for (int i = 0; i < core->connections.length; i++) {
		xps_connection_t *cur = core->connections.data[i];
		if (cur == connection) {
			core->connections.data[i] = NULL;
			break;
		}
	}

	xps_loop_detach(core->loop, connection->sock_fd);

	close(connection->sock_fd);

	free(connection->remote_ip);
	free(connection);

	logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_read_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;

	char buff[DEFAULT_BUFFER_SIZE];
	memset(buff, 0, DEFAULT_BUFFER_SIZE);

	long read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE, 0);

	if (read_n < 0) {
		logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
		perror("Error message");
		xps_connection_destroy(connection);
		return;
	}

	if (read_n == 0) {
		logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
		xps_connection_destroy(connection);
		return;
	}

	buff[read_n] = '\0';

	printf("[CLIENT MESSAGE] %s", buff);

	strrev(buff);

	// Sending reversed message to client
	long bytes_written = 0;
	long message_len = read_n;
	while (bytes_written < message_len) {
		long write_n = send(connection->sock_fd, buff + bytes_written,
							message_len - bytes_written, 0);
		if (write_n < 0) {
			logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
			perror("Error message");
			xps_connection_destroy(connection);
			return;
		}
		bytes_written += write_n;
	}
}
