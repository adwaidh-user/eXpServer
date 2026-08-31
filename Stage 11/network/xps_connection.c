#include "xps_connection.h"

void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);

void connection_source_handler(void *ptr);
void connection_source_close_handler(void *ptr);
void connection_sink_handler(void *ptr);
void connection_sink_close_handler(void *ptr);
void connection_close(xps_connection_t *connection, bool peer_closed);

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
	assert(core != NULL);

	xps_connection_t *connection =
		(xps_connection_t *)malloc(sizeof(xps_connection_t));

	if (connection == NULL) {
		logger(LOG_ERROR, "xps_connection_create()",
			   "malloc() failed for 'connection'");
		return NULL;
	}

	/* Create source instance */
	xps_pipe_source_t *source = xps_pipe_source_create(
		connection, connection_source_handler, connection_source_close_handler);

	if (source == NULL) {
		logger(LOG_ERROR, "xps_connection_create()",
			   "xps_pipe_source_create() failed");
		free(connection);
		return NULL;
	}

	/* Create sink instance */
	xps_pipe_sink_t *sink = xps_pipe_sink_create(
		connection, connection_sink_handler, connection_sink_close_handler);

	if (sink == NULL) {
		logger(LOG_ERROR, "xps_connection_create()",
			   "xps_pipe_source_create() failed");
		xps_pipe_source_destroy(source);
		free(connection);
		return NULL;
	}

	connection->core = core;
	connection->sock_fd = sock_fd;
	connection->listener = NULL;
	connection->remote_ip = get_remote_ip(sock_fd);
	connection->source = source;
	connection->sink = sink;

	if ((xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET,
						 connection, connection_loop_read_handler,
						 connection_loop_write_handler,
						 connection_loop_close_handler)) != OK) {
		logger(LOG_ERROR, "xps_connection_create()",
			   "xps_loop_attach() failed");
		xps_pipe_source_destroy(source);
		xps_pipe_sink_destroy(sink);
		free(connection);
		return NULL;
	}

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

	if (connection->source)
		xps_pipe_source_destroy(connection->source);
	if (connection->sink)
		xps_pipe_sink_destroy(connection->sink);

	close(connection->sock_fd);

	free(connection->remote_ip);
	free(connection);

	logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_loop_read_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;
	connection->source->ready = true;
}

void connection_loop_write_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;
	connection->sink->ready = true;
}

void connection_loop_close_handler(void *ptr) {
	assert(ptr != NULL);
	xps_connection_t *connection = (xps_connection_t *)ptr;

	logger(LOG_INFO, "connection_loop_close_handler()", "closing connection");
	connection_close(connection, true);
}

void connection_source_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_connection_t *connection = (xps_connection_t *)source->ptr;

	xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
	if (buff == NULL) {
		logger(LOG_DEBUG, "connection_source_handler()",
			   "xps_buffer_create() failed");
		return;
	}

	int read_n = recv(connection->sock_fd, buff->data, buff->size, 0);
	buff->len = read_n;

	if (read_n < 0) {
		xps_buffer_destroy(buff);
		if (errno == EAGAIN || errno == EWOULDBLOCK) { // Socket would block
			source->ready = false;
			logger(LOG_DEBUG, "connection_source_handler()",
				   "Nothing to read try again");
		} else { // Socket error
			logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
			perror("[ERROR]");
			connection_close(connection, false);
		}
		return;
	}

	// Peer closed connection
	if (read_n == 0) {
		xps_buffer_destroy(buff);
		logger(LOG_DEBUG, "connection_source_handler()",
			   "Peer closed connection");
		connection_close(connection, false);
		return;
	}

	if (xps_pipe_source_write(source, buff) != OK) {
		logger(LOG_ERROR, "connection_source_handler()",
			   "xps_pipe_source_write() failed");
		xps_buffer_destroy(buff);
		connection_close(connection, false);
		return;
	}

	xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_connection_t *connection = (xps_connection_t *)source->ptr;

	if (source->active || (source->pipe->sink && source->pipe->sink->active))
		return;
	connection_close(connection, false);
}

void connection_sink_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_connection_t *connection = (xps_connection_t *)sink->ptr;
	ssize_t len = sink->pipe->buff_list->len;

	xps_buffer_t *buff = xps_pipe_sink_read(sink, len);
	if (buff == NULL) {
		logger(LOG_ERROR, "connection_sink_handler()",
			   "xps_pipe_sink_read() failed");
		return;
	}

	int write_n =
		send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);

	xps_buffer_destroy(buff);

	// Socket would block
	if (write_n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			logger(LOG_DEBUG, "connection_sink_handler()",
				   "connection buffer is full try again");
			sink->ready = false;
		} else {
			logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
			connection_close(connection, false);
		}
		return;
	}

	if (write_n == 0)
		return;

	if (xps_pipe_sink_clear(sink, write_n) != OK)
		logger(LOG_ERROR, "connection_sink_handler()",
			   "failed to clear %d bytes from sink", write_n);
}

void connection_sink_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = ptr;
	xps_connection_t *connection = sink->ptr;

	if (sink->active || (sink->pipe->source && sink->pipe->source->active))
		return;

	connection_close(connection, false);
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
	assert(connection != NULL);

	logger(LOG_INFO, "connection_close()",
		   peer_closed ? "peer closed connection" : "closing connection");
	xps_connection_destroy(connection);
}
