#ifndef XPS_LISTENER_H
#define XPS_LISTENER_H

#include "../xps.h"

struct xps_listener_s {
	int epoll_fd;	  // epoll fd listener is attatched to.
	const char *host; // host ip string
	u_int port;		  // listening port
	u_int sock_fd;	  // listening socket fd
};

xps_listener_t *xps_listener_create(int epoll_fd, const char *host, u_int port);
void xps_listener_destroy(xps_listener_t *listener);
void xps_listener_connection_handler(xps_listener_t *listener);

#endif
