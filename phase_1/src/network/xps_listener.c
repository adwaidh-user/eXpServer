#include "xps_listener.h"

xps_listener_t *xps_listener_create(int epoll_fd, const char *host,
									u_int port) {
	assert(host != NULL);
	assert(is_valid_port(port));

	int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sock_fd < 0) {
		logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
		perror("[ERROR]");
		return NULL;
	}

	const int enable = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
				   sizeof(enable))) {
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

	listener->epoll_fd = epoll_fd;
	listener->sock_fd = sock_fd;
	listener->host = host;
	listener->port = port;

	xps_loop_attach(epoll_fd, sock_fd, EPOLLIN);

	vec_push(&listeners, listener);

	logger(LOG_DEBUG, "xps_listener_create()", "created listener on port %u",
		   port);

	return listener;
}

void xps_listener_destroy(xps_listener_t *listener) {
	assert(listener != NULL);

	xps_loop_detach(listener->epoll_fd, listener->sock_fd);
	for (int i = 0; i < listeners.length; i++) {
		xps_listener_t *cur = listeners.data[i];
		if (cur == listener) {
			listeners.data[i] = NULL;
			break;
		}
	}

	close(listener->sock_fd);

	logger(LOG_DEBUG, "xps_listener_destroy()", "destroyed listener on port %u",
		   listener->port);

	free(listener);
}

void xps_listener_connection_handler(xps_listener_t *listener) {
	assert(listener != NULL);

	struct sockaddr conn_addr;
	socklen_t conn_addr_len = sizeof(conn_addr);

	int conn_sock_fd = accept(listener->sock_fd, (struct sockaddr *)&conn_addr,
							  &conn_addr_len);
	if (conn_sock_fd < 0) {
		logger(LOG_ERROR, "xps_listener_connection_handler()",
			   "accept() failed");
		close(conn_sock_fd);
		return;
	}

	xps_connection_t *client =
		xps_connection_create(listener->epoll_fd, conn_sock_fd);
	if (client == NULL) {
		logger(LOG_ERROR, "xps_listener_connection_handler()",
			   "xps_connection_create() failed");
		close(conn_sock_fd);
		return;
	}

	client->listener = listener;
	logger(LOG_INFO, "xps_listener_connection_handler()", "new connection");
}
