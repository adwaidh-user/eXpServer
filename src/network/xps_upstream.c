#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host,
									  u_int port) {
	assert(core != NULL);
	assert(host != NULL);
	is_valid_port(port);

	int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (sock_fd < 0) {
		logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
		perror("[ERROR]");
		return NULL;
	}

	struct addrinfo *addr_info = xps_getaddrinfo(host, port);
	if (addr_info == NULL) {
		logger(LOG_ERROR, "xps_listener_create()", "xps_getaddrinfo() failed");
		perror("[ERROR]");
		close(sock_fd);
		return NULL;
	}
	int connect_error =
		connect(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen);

	if (!(connect_error == 0 || errno == EINPROGRESS)) {
		logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
		perror("Error message");
		close(sock_fd);
		return NULL;
	}
    freeaddrinfo(addr_info);

    xps_connection_t *connection = xps_connection_create(core, sock_fd);
    if (connection == NULL) {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_connection_create failed()");
        close(sock_fd);
        return NULL;
    }

	return connection;
}
