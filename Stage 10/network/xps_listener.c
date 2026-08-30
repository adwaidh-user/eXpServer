#include "xps_listener.h"

void listener_connection_handler(void *ptr);

xps_listener_t *xps_listener_create(xps_core_t *core, const char *host,
									u_int port) {
	assert(core != NULL);
	assert(host != NULL);
	assert(is_valid_port(port));

	int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sock_fd < 0) {
		logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
		perror("[ERROR]");
		return NULL;
	}

	const int enable = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) <
		0) {
		logger(LOG_ERROR, "xps_listener_create()", "setsockopt() failed");
		perror("[ERROR]");
		close(sock_fd);
		return NULL;
	}

	struct addrinfo *addr_info = xps_getaddrinfo(host, port);
	if (addr_info == NULL) {
		logger(LOG_ERROR, "xps_listener_create()", "xps_getaddrinfo() failed");
		perror("[ERROR]");
		close(sock_fd);
		return NULL;
	}

	if (bind(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0) {
		logger(LOG_ERROR, "xps_listener_create()", "bind() failed");
		perror("[ERROR]");
		freeaddrinfo(addr_info);
		close(sock_fd);
		return NULL;
	}
	freeaddrinfo(addr_info);

	if (listen(sock_fd, DEFAULT_BACKLOG) < 0) {
		logger(LOG_ERROR, "xps_listener_create()", "listen() failed");
		perror("[ERROR]");
		close(sock_fd);
		return NULL;
	}

	xps_listener_t *listener = (xps_listener_t *)malloc(sizeof(xps_listener_t));
	if (listener == NULL) {
		logger(LOG_ERROR, "xps_listener_create()",
			   "malloc() failed for 'listener'");
		close(sock_fd);
		return NULL;
	}

	listener->core = core;
	listener->sock_fd = sock_fd;
	listener->host = host;
	listener->port = port;

	xps_loop_attach(core->loop, sock_fd, EPOLLIN, listener,
					listener_connection_handler, NULL, NULL);

	vec_push(&(core->listeners), listener);

	logger(LOG_DEBUG, "xps_listener_create()", "created listener on port %u",
		   port);

	return listener;
}

void xps_listener_destroy(xps_listener_t *listener) {
	assert(listener != NULL);

	xps_core_t *core = listener->core;

	xps_loop_detach(core->loop, listener->sock_fd);
	for (int i = 0; i < core->listeners.length; i++) {
		xps_listener_t *cur = core->listeners.data[i];
		if (cur == listener) {
			core->listeners.data[i] = NULL;
			break;
		}
	}

	close(listener->sock_fd);

	logger(LOG_DEBUG, "xps_listener_destroy()", "destroyed listener on port %u",
		   listener->port);

	free(listener);
}

void listener_connection_handler(void *ptr) {
	assert(ptr != NULL);
	xps_listener_t *listener = (xps_listener_t *)ptr;

	while (1) {
		struct sockaddr conn_addr;
		socklen_t conn_addr_len = sizeof(conn_addr);

		int conn_sock_fd = accept(
			listener->sock_fd, (struct sockaddr *)&conn_addr, &conn_addr_len);

		if (conn_sock_fd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				logger(LOG_DEBUG, "listener_connection_handler()",
					   "no pending connections to accept");
				break;
			}
			logger(LOG_ERROR, "listener_connection_handler()",
				   "accept() failed");
			perror("[ERROR]");
			return;
		}

		if (make_socket_non_blocking(conn_sock_fd) != OK) {
			logger(LOG_ERROR, "listener_connection_handler()",
				   "failed to make socket non-blocking");
			close(conn_sock_fd);
			return;
		}

		xps_connection_t *client =
			xps_connection_create(listener->core, conn_sock_fd);
		if (client == NULL) {
			logger(LOG_ERROR, "listener_connection_handler()",
				   "xps_connection_create() failed");
			close(conn_sock_fd);
			return;
		}

		client->listener = listener;
        xps_pipe_create(listener->core, DEFAULT_PIPE_BUFF_THRESH, client->source, client->sink);

		logger(LOG_INFO, "listener_connection_handler()", "new connection");
	}
}
