#ifndef XPS_CORE_H
#define XPS_CORE_H

#include "../xps.h"
/*
 * @member loop: Pointer to loop instance associated to the core
 * @member listeners: List of all the listener instances attached to the core
 * @member connections: List of all the connection instances created by the listeners
 * @member n_null_listeners: Number of pointers in listener instances set to NULL
 * @member n_null_connections: Number of pointers in connection instances set to NULL
 */
struct xps_core_s {
	xps_loop_t *loop;
	vec_void_t listeners;
	vec_void_t connections;
	vec_void_t pipes;
	u_int n_null_listeners;
	u_int n_null_connections;
	u_int n_null_pipes;
};

xps_core_t *xps_core_create();
void xps_core_destroy(xps_core_t *core);
void xps_core_start(xps_core_t *core);

#endif
