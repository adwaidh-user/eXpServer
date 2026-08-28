#ifndef XPS_LOOP_H
#define XPS_LOOP_H

#include "../xps.h"

/*
 * xps_loop_s is a wrapper for all data relater to loop instances
 * @member core : Pointer to the core instance to which the loop belongs to (1 -
 * 1 relation)
 * @member epoll_fd : FD of the epoll instance
 * @member epoll_events : Array to store events reported by epoll during
 * epoll_wait()
 * @member events : Array to hold pointers to loop_event_t structs
 * @member n_null_events : Number of NULL events
 */
struct xps_loop_s {
	xps_core_t *core;
	u_int epoll_fd;
	struct epoll_event epoll_events[MAX_EPOLL_EVENTS];
	vec_void_t events;
	u_int n_null_events;
};

/*
 * @member fd : the fd to be attatched to epoll
 * @member read_cb : Callback function to be called when read event occurs
 * @member ptr : Pointer to instance of xps_listener_t or xps_connection_t
 */
struct loop_event_s {
	u_int fd;
	xps_handler_t read_cb;
	xps_handler_t write_cb;
	xps_handler_t close_cb;
	void *ptr;
};

typedef struct loop_event_s loop_event_t;

xps_loop_t *xps_loop_create(xps_core_t *core);
void xps_loop_destroy(xps_loop_t *loop);
int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr,
					xps_handler_t read_cb, xps_handler_t write_cb,
					xps_handler_t close_cb);
int xps_loop_detach(xps_loop_t *loop, u_int fd);
void xps_loop_run(xps_loop_t *loop);

#endif
