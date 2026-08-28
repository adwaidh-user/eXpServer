#include "xps_connection.h"
#include <sys/epoll.h>

void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);

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
	xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT, connection,
					connection_loop_read_handler, connection_loop_write_handler,
					connection_loop_close_handler);

	// Init values
	connection->core = core;
	connection->sock_fd = sock_fd;
	connection->listener = NULL;
	connection->remote_ip = get_remote_ip(sock_fd);
	connection->write_buff_list = xps_buffer_list_create();

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

	xps_buffer_list_destroy(connection->write_buff_list);
	close(connection->sock_fd);

	free(connection->remote_ip);
	free(connection);

	logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_loop_read_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;

	char buff[DEFAULT_BUFFER_SIZE];
	memset(buff, 0, DEFAULT_BUFFER_SIZE);

	long read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE - 1, 0);

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

	xps_buffer_t *buffer = xps_buffer_create(read_n, read_n, NULL);
	memcpy(buffer->data, buff, read_n);
	xps_buffer_list_append(connection->write_buff_list, buffer);
}

void connection_loop_write_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;

	xps_buffer_list_t *buff_list = connection->write_buff_list;
	if (!buff_list || buff_list->len == 0) {
		logger(LOG_DEBUG, "connection_loop_write_handler()",
			   "no data to write");
		return;
	}

	xps_buffer_t *buffer = xps_buffer_list_read(buff_list, buff_list->len);
	if (buffer == NULL) {
		logger(LOG_DEBUG, "connection_loop_write_handler()",
			   "xps_buffer_list_read() failed");
		return;
	}

	long bytes_written = 0;
	long message_len = buffer->len;
	while (bytes_written < message_len) {
		long write_n = send(connection->sock_fd, buffer->data + bytes_written,
							message_len - bytes_written, 0);
		if (write_n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				logger(LOG_DEBUG, "xps_connection_read_handler()",
					   "send() will block try again later");
				xps_buffer_destroy(buffer);
				return;
			}
			logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
			perror("[ERROR]");
			xps_buffer_destroy(buffer);
			xps_connection_destroy(connection);
			return;
		}
		bytes_written += write_n;
		xps_buffer_list_clear(buff_list, write_n);
	}
	xps_buffer_destroy(buffer);
}

void connection_loop_close_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;

	logger(LOG_INFO, "connection_loop_close_handler()", "closing connection");
	xps_connection_destroy(connection);
}
