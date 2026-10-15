#include "xps_session.h"

void client_source_handler(void *ptr);
void client_source_close_handler(void *ptr);
void client_sink_handler(void *ptr);
void client_sink_close_handler(void *ptr);
void upstream_source_handler(void *ptr);
void upstream_source_close_handler(void *ptr);
void upstream_sink_handler(void *ptr);
void upstream_sink_close_handler(void *ptr);
void upstream_error_res(xps_session_t *session);
void file_sink_handler(void *ptr);
void file_sink_close_handler(void *ptr);
void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff);
void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff);
void session_check_destroy(xps_session_t *session);

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client) {
	assert(core);
	assert(client);

	// Alloc memory for session instance
	xps_session_t *session = (xps_session_t *)malloc(sizeof(xps_session_t));
	if (session == NULL) {
		logger(LOG_ERROR, "xps_session_create()",
			   "malloc() failed for 'session'");
		return NULL;
	}

	session->client_source = xps_pipe_source_create(
		session, client_source_handler, client_source_close_handler);
	session->client_sink = xps_pipe_sink_create(session, client_sink_handler,
												client_sink_close_handler);
	session->upstream_source = xps_pipe_source_create(
		session, upstream_source_handler, upstream_source_close_handler);
	session->upstream_sink = xps_pipe_sink_create(
		session, upstream_sink_handler, upstream_sink_close_handler);
	session->file_sink = xps_pipe_sink_create(session, file_sink_handler,
											  file_sink_close_handler);

	if (!(session->client_source && session->client_sink &&
		  session->upstream_source && session->upstream_sink &&
		  session->file_sink)) {
		logger(LOG_ERROR, "xps_session_create()",
			   "failed to create some sources/sinks");

		if (session->client_source)
			xps_pipe_source_destroy(session->client_source);
		if (session->client_sink)
			xps_pipe_sink_destroy(session->client_sink);
		if (session->upstream_source)
			xps_pipe_source_destroy(session->upstream_source);
		if (session->upstream_sink)
			xps_pipe_sink_destroy(session->upstream_sink);
		if (session->file_sink)
			xps_pipe_sink_destroy(session->file_sink);

		free(session);
		return NULL;
	}

	// Init values
	session->core = core;
	session->client = client;
	session->upstream = NULL;
	session->upstream_connected = false;
	session->upstream_error_res_set = false;
	session->upstream_write_bytes = 0;
	session->file = NULL;
	session->to_client_buff = NULL;
	session->from_client_buff = NULL;
	session->client_sink->ready = true;
	session->upstream_sink->ready = true;
	session->file_sink->ready = true;

	// Attach client
	if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, client->source,
						session->client_sink) == NULL ||
		xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->client_source,
						client->sink) == NULL) {
		logger(LOG_ERROR, "xps_session_create()",
			   "failed to create client pipes");

		if (session->client_source)
			xps_pipe_source_destroy(session->client_source);
		if (session->client_sink)
			xps_pipe_sink_destroy(session->client_sink);
		if (session->upstream_source)
			xps_pipe_source_destroy(session->upstream_source);
		if (session->upstream_sink)
			xps_pipe_sink_destroy(session->upstream_sink);
		if (session->file_sink)
			xps_pipe_sink_destroy(session->file_sink);

		free(session);
		return NULL;
	}

	vec_push(&(core->sessions), session);

	logger(LOG_DEBUG, "xps_session_create()", "created session");

	/* UPSTREAM CONNECTION */
	if (client->listener->port == 8001) {
		xps_connection_t *upstream = xps_upstream_create(core, "0.0.0.0", 3000);
		if (upstream == NULL) {
			logger(LOG_ERROR, "xps_session_create()",
				   "xps_upstream_create() failed");
			perror("Error message");
			xps_session_destroy(session);
			return NULL;
		}
		session->upstream = upstream;

		if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH,
							session->upstream_source, upstream->sink) == NULL ||
			xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, upstream->source,
							session->upstream_sink) == NULL) {

			logger(LOG_ERROR, "xps_session_create()",
				   "failed creating upstream pipes");
			xps_session_destroy(session);
			return NULL;
		}
	}
	/* FILE SERVER */
	else if (client->listener->port == 8002) {
		int error;
		xps_file_t *file =
			xps_file_create(core, "../public/sample.txt", &error);
		if (file == NULL) {
			logger(LOG_ERROR, "xps_session_create()",
				   "xps_file_create() failed");
			perror("Error message");
			xps_session_destroy(session);
			return NULL;
		}

		session->file = file;
		if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, file->source,
							session->file_sink) == NULL) {
			logger(LOG_ERROR, "xps_session_create()",
				   "failed creating upstream pipes");
			xps_session_destroy(session);
			return NULL;
		}
	}

	return session;
}

void client_source_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_session_t *session = (xps_session_t *)source->ptr;

	logger(LOG_DEBUG, "client_source_handler()",
		   "before return: ready=%d active=%d", source->ready, source->active);

	logger(LOG_DEBUG, "client_source_handler()", "session=%p to_client_buff=%p",
		   (void *)session, (void *)session->to_client_buff);

	if (session->to_client_buff == NULL) {
		logger(LOG_ERROR, "client_source_handler()",
			   "previous data not yet sent to client");
        source->ready = false;
		return;
	}
	// write to session->to_client_buff
	if (xps_pipe_source_write(source, session->to_client_buff) != OK) {
		logger(LOG_ERROR, "client_source_handler()",
			   "xps_pipe_source_write() failed");
		return;
	}
	xps_buffer_destroy(session->to_client_buff);

	set_to_client_buff(session, NULL);
	session_check_destroy(session);
}

void client_source_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_session_t *session = (xps_session_t *)source->ptr;

	session_check_destroy(session);
}

void client_sink_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	size_t to_read_n = sink->pipe->buff_list->len;
	if (to_read_n == 0) {
		logger(LOG_ERROR, "client_sink_handler()", "no data to read from pipe");
		return;
	}

	xps_buffer_t *buff = xps_pipe_sink_read(sink, to_read_n);
	if (buff == NULL) {
		logger(LOG_ERROR, "client_sink_handler()",
			   "xps_pipe_sink_read() failed");
		return;
	}

	set_from_client_buff(session, buff);
	xps_pipe_sink_clear(sink, to_read_n);
}

void client_sink_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	session_check_destroy(session);
}

void upstream_source_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_session_t *session = (xps_session_t *)source->ptr;

	if (session->from_client_buff == NULL) {
		logger(LOG_ERROR, "upstream_source_handler()",
			   "no data to be written to upstream");
        source->ready = false;
		return;
	}

	if (xps_pipe_source_write(source, session->from_client_buff) != OK) {
		logger(LOG_ERROR, "upstream_source_handler()",
			   "xps_pipe_source_write() failed");
		return;
	}

	// Checking if upstream is connected
	if (session->upstream_connected == false) {
		session->upstream_write_bytes += session->from_client_buff->len;
		if (session->upstream_write_bytes >
			session->upstream_source->pipe->buff_list->len)
			session->upstream_connected = true;
	}

	xps_buffer_destroy(session->from_client_buff);
	source->ready = false;

	set_from_client_buff(session, NULL);
	session_check_destroy(session);
}

void upstream_source_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
	xps_session_t *session = (xps_session_t *)source->ptr;

	if (!session->upstream_connected && !session->upstream_error_res_set) {
		upstream_error_res(session);
	}

	session_check_destroy(session);
}

void upstream_sink_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	size_t to_read_n = sink->pipe->buff_list->len;
	if (to_read_n == 0) {
		logger(LOG_DEBUG, "upstream_sink_handler()",
			   "no data to read from upstream");
		return;
	}

	session->upstream_connected = true;

	xps_buffer_t *buff = xps_pipe_sink_read(sink, to_read_n);
	if (buff == NULL) {
		logger(LOG_ERROR, "upstream_sink_handler()",
			   "xps_pipe_sink_read() failed");
		return;
	}

	set_to_client_buff(session, buff);
	xps_pipe_sink_clear(sink, to_read_n);
}

void upstream_sink_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	if (!session->upstream_connected && !session->upstream_error_res_set) {
		upstream_error_res(session);
	}

	session_check_destroy(session);
}

void upstream_error_res(xps_session_t *session) {
	assert(session != NULL);

	session->upstream_error_res_set = true;
}

void file_sink_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	size_t to_read_n = sink->pipe->buff_list->len;
	if (to_read_n == 0) {
		logger(LOG_DEBUG, "file_sink_handler()", "no data to read from pipe");
		return;
	}

	xps_buffer_t *buff = xps_pipe_sink_read(sink, to_read_n);
	if (buff == NULL) {
		logger(LOG_ERROR, "file_sink_handler()", "xps_pipe_sink_read() failed");
		return;
	}

	set_to_client_buff(session, buff);
	xps_pipe_sink_clear(sink, to_read_n);
}

void file_sink_close_handler(void *ptr) {
	assert(ptr != NULL);

	xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
	xps_session_t *session = (xps_session_t *)sink->ptr;

	session_check_destroy(session);
}

void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff) {
	assert(session != NULL);

	session->to_client_buff = buff;

	if (buff == NULL) {
		session->client_source->ready = false;
		session->upstream_sink->ready = true;
		session->file_sink->ready = true;
	} else {
		session->client_source->ready = true;
		session->upstream_sink->ready = false;
		session->file_sink->ready = false;
	}
}

void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff) {
	assert(session != NULL);

	session->from_client_buff = buff;

	if (buff == NULL) {
		session->client_sink->ready = true;
		session->upstream_source->ready = false;
	} else {
		session->client_sink->ready = false;
		session->upstream_source->ready = true;
	}
}

void session_check_destroy(xps_session_t *session) {
	assert(session != NULL);

	bool c2u_flow = session->upstream_source->active &&
					(session->client_sink->active || session->from_client_buff);

	bool u2c_flow = session->client_source->active &&
					(session->upstream_sink->active || session->to_client_buff);

	bool f2c_flow = session->client_source->active &&
					(session->file_sink->active || session->to_client_buff);

	bool flowing = c2u_flow || u2c_flow || f2c_flow;

	if (!flowing)
		xps_session_destroy(session);
}

void xps_session_destroy(xps_session_t *session) {
	assert(session != NULL);

	xps_pipe_source_destroy(session->client_source);
	xps_pipe_source_destroy(session->upstream_source);
	xps_pipe_sink_destroy(session->client_sink);
	xps_pipe_sink_destroy(session->upstream_sink);
	xps_pipe_sink_destroy(session->file_sink);

	if (session->to_client_buff != NULL)
		xps_buffer_destroy(session->to_client_buff);
	if (session->from_client_buff != NULL)
		xps_buffer_destroy(session->from_client_buff);

	xps_core_t *core = session->core;
	vec_void_t *sessions = &(core->sessions);

	for (int i = 0; i < sessions->length; i++) {
		xps_session_t *cur = sessions->data[i];
		if (cur == session) {
			sessions->data[i] = NULL;
			core->n_null_sessions++;
			break;
		}
	}

	free(session);

	logger(LOG_DEBUG, "xps_session_destroy()", "destroyed session");
}
