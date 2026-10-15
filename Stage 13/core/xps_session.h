#ifndef XPS_SESSION_H
#define XPS_SESSION_H

#include "../xps.h"

/*
 * @member *core : ptr to core instance
 * @member *client : ptr to client instance
 * @member *upstream : ptr to upstream instance
 * @member upstream_connected : whether upstream is connected for session or not
 * @member upstream_error_res_set : whether upstream error is set or not
 * @member upstream_write_bytes : number of bytes written to upstream till now
 * @member *file : ptr to file instance
 * @member *client_source : pipe source for client
 * @member *client_sink : pipe sink for client
 * @member *upstream_source : pipe source for upstream
 * @member *upstream_sink : pipe sink for upstream
 * @member *file_sink : pipe sink for file
 * @member *to_client_buff : buffer for data to be written to pipe from session
 * to client
 * @member *from_client_buff : buffer for data read from pipe between client and
 * session
 */
struct xps_session_s {
	xps_core_t *core;
	xps_connection_t *client;
	xps_connection_t *upstream;
	bool upstream_connected;
	bool upstream_error_res_set;
	u_long upstream_write_bytes;
	xps_file_t *file;
	xps_pipe_source_t *client_source;
	xps_pipe_sink_t *client_sink;
	xps_pipe_source_t *upstream_source;
	xps_pipe_sink_t *upstream_sink;
	xps_pipe_sink_t *file_sink;
	xps_buffer_t *to_client_buff;
	xps_buffer_t *from_client_buff;
};

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client);
void xps_session_destroy(xps_session_t *session);

#endif
