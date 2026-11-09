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
void session_process_request(xps_session_t *session);
void set_err_msg(xps_session_t *session, int err_code);

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
	session->http_req = NULL;
	session->file = NULL;
	session->lookup = NULL;
	session->gzip = NULL;

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

	if (session->http_req == NULL) {
		int error;
		xps_http_req_t *http_req =
			xps_http_req_create(session->core, buff, &error);

		xps_buffer_destroy(buff);

		if (error == E_FAIL) {
			error = HTTP_BAD_REQUEST;
			logger(LOG_ERROR, "client_sink_handler()",
				   "xps_http_req_create() failed with err code: %d", error);
			session_process_request(session);
			return;
		}

		if (error == E_AGAIN) {
			logger(
				LOG_ERROR, "client_sink_handler()",
				"xps_http_req_create() returned E_AGAIN waiting for more data");
			return;
		}

		session->http_req = http_req;
		xps_buffer_t *http_req_buff = xps_http_req_serialize(session->http_req);

		if (http_req_buff == NULL) {
			logger(LOG_ERROR, "client_sink_handler()",
				   "xps_http_req_serialize() failed");
			xps_http_req_destroy(session->core, session->http_req);
			session->http_req = NULL;
			session_process_request(session);
			return;
		}

		set_from_client_buff(session, http_req_buff);

		size_t to_clear_n = sink->pipe->buff_list->len;
		xps_pipe_sink_clear(sink, to_clear_n);
		session_process_request(session);
	} else {
		set_from_client_buff(session, buff);
		xps_pipe_sink_clear(sink, to_read_n);
	}
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
	if (session->http_req != NULL)
		xps_http_req_destroy(session->core, session->http_req);

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

void session_process_request(xps_session_t *session) {
	assert(session != NULL);

	// BAD REQUEST
	if (session->http_req == NULL || session->http_req->path == NULL) {
		set_err_msg(session, HTTP_BAD_REQUEST);
		return;
	}

	int error;
	xps_config_lookup_t *lookup = xps_config_lookup(
		session->core->config, session->http_req, session->client, &error);
	if (lookup == NULL) {
		logger(LOG_ERROR, "session_process_request()",
			   "xps_config_lookup() returned NULl. err code: %d", error);
		if (error == E_NOTFOUND)
			set_err_msg(session, HTTP_NOT_FOUND);
		else if (error == E_PERMISSION)
			set_err_msg(session, HTTP_FORBIDDEN);
		else
			set_err_msg(session, HTTP_INTERNAL_SERVER_ERROR);

		return;
	}

	const char *client_ip = session->client->remote_ip;
	// check whitelist exist
	if (lookup->ip_whitelist.length > 0) {
		bool is_allowed = false;
		for (size_t i = 0; i < lookup->ip_whitelist.length; i++) {
			const char *ip_w = lookup->ip_whitelist.data[i];
			if (ip_w == NULL)
				continue;

			if (strcmp(client_ip, ip_w) == 0) {
				is_allowed = true;
				break;
			}
		}
		if (!is_allowed) {
			logger(LOG_DEBUG, "session_process_request()",
				   "client ip %s is not whitelisted", client_ip);
			xps_config_lookup_destroy(lookup, session->core);
			set_err_msg(session, HTTP_FORBIDDEN);
			return;
		}
	}

	// check in blacklist
	if (lookup->ip_blacklist.length > 0) {
		for (size_t i = 0; i < lookup->ip_blacklist.length; i++) {
			const char *ip_b = lookup->ip_blacklist.data[i];

			if (strcmp(client_ip, ip_b) == 0) {
				logger(LOG_DEBUG, "session_process_request()",
					   "client ip %s is blacklisted", client_ip);
				xps_config_lookup_destroy(lookup, session->core);
				set_err_msg(session, HTTP_FORBIDDEN);
				return;
			}
		}
	}

	session->lookup = lookup;

	const char *accept_encoding =
		xps_http_get_header(&(session->http_req->headers), "Accept-Encoding");
	bool client_accepts_gzip = false;

	if (accept_encoding != NULL && strcmp(accept_encoding, "gzip") == 0)
		client_accepts_gzip = true;

	bool use_gzip_compression = client_accepts_gzip && lookup->gzip_enable;

	if (lookup->type == REQ_FILE_SERVE) {
		if (lookup->dir_path != NULL) {
			xps_buffer_t *dir_html = xps_directory_browsing(
				lookup->dir_path, session->http_req->pathname);
			xps_config_lookup_destroy(lookup, session->core);

			if (dir_html == NULL) {
				set_err_msg(session, HTTP_INTERNAL_SERVER_ERROR);
				return;
			}

			xps_http_res_t *http_res =
				xps_http_res_create(session->core, HTTP_OK);
			if (http_res == NULL) {
				logger(LOG_ERROR, "session_process_request()",
					   "xps_http_res_create() failed for HTTP_OK");
				return;
			}
			xps_http_res_set_body(http_res, dir_html);
			char contentLength[24];
			snprintf(contentLength, sizeof(contentLength), "%zu",
					 dir_html->len);
			xps_buffer_destroy(dir_html);
			xps_http_set_header(&(http_res->headers), "Content-Length",
								contentLength);
			xps_http_set_header(&(http_res->headers), "Content-Type",
								"text/html");

			xps_buffer_t *buff = xps_http_res_serialize(http_res);
			xps_http_res_destroy(http_res);
			if (buff == NULL) {
				logger(LOG_ERROR, "session_process_request()",
					   "xps_http_res_serialize() failed for HTTP_OK");
				return;
			}
			set_to_client_buff(session, buff);
			return;
		}

		xps_file_t *file =
			xps_file_create(session->core, lookup->file_path, &error);
		if (file == NULL) {
			if (error == E_NOTFOUND)
				set_err_msg(session, HTTP_NOT_FOUND);
			else if (error == E_PERMISSION)
				set_err_msg(session, HTTP_FORBIDDEN);
			else
				set_err_msg(session, HTTP_INTERNAL_SERVER_ERROR);

			xps_config_lookup_destroy(lookup, session->core);
			return;
		}

		if (session->file != NULL)
			xps_file_destroy(session->file);
		session->file = file;

		xps_http_res_t *http_res = xps_http_res_create(session->core, HTTP_OK);
		if (http_res == NULL) {
			logger(LOG_ERROR, "session_process_request()",
				   "xps_http_res_create() failed for HTTP_OK");
			xps_config_lookup_destroy(lookup, session->core);
			return;
		}

		if (use_gzip_compression) {
			xps_http_set_header(&(http_res->headers), "Content-Encoding",
								"gzip");
		} else {
			char contentLength[24];
			snprintf(contentLength, sizeof(contentLength), "%zu", file->size);
			xps_http_set_header(&(http_res->headers), "Content-Length",
								contentLength);
		}

		xps_http_set_header(&(http_res->headers), "Content-Type",
							session->file->mime_type ? session->file->mime_type
													 : "text/plain");

		xps_buffer_t *buff = xps_http_res_serialize(http_res);
		xps_http_res_destroy(http_res);
		if (buff == NULL) {
			logger(LOG_ERROR, "session_process_request()",
				   "xps_http_res_serialize() failed for HTTP_OK");
			xps_config_lookup_destroy(lookup, session->core);
			return;
		}

		if (use_gzip_compression) {
			xps_gzip_t *gzip = xps_gzip_create(lookup->gzip_level);
			if (gzip == NULL) {
				logger(LOG_ERROR, "session_process_request()",
					   "xps_gzip_create() failed to create a gzip compression "
					   "instance");
				return;
			}

			xps_pipe_t *fsource_gsink_pipe =
				xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH,
								session->file->source, gzip->sink);
			xps_pipe_t *fsink_gsource_pipe =
				xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH,
								gzip->source, session->file_sink);

			if (fsource_gsink_pipe == NULL || fsink_gsource_pipe == NULL) {
				logger(LOG_ERROR, "session_process_request()",
					   "xps_pipe_create() failed for gzip-file pipe");
				if (fsource_gsink_pipe)
					xps_pipe_destroy(fsource_gsink_pipe);
				if (fsink_gsource_pipe)
					xps_pipe_destroy(fsink_gsource_pipe);

				xps_config_lookup_destroy(lookup, session->core);
				xps_gzip_destroy(gzip);
				xps_file_destroy(file);
				session->file = NULL;
				xps_buffer_destroy(buff);
				return;
			}

			session->gzip = gzip;
		} else {
			/*create pipe with session->file->source and session->file_sink*/
			if (xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH,
								session->file->source,
								session->file_sink) == NULL) {
				xps_file_destroy(file);
				session->file = NULL;
				xps_buffer_destroy(buff);
				xps_config_lookup_destroy(lookup, session->core);
				logger(LOG_ERROR, "session_process_request()",
					   "xps_pipe_create() failed: file source and session file "
					   "sink");
				return;
			}
		}
		xps_config_lookup_destroy(lookup, session->core);
		set_to_client_buff(session, buff);
	} else if (lookup->type == REQ_REVERSE_PROXY) {
		char host[256];
		u_int port;

		sscanf(lookup->upstream, "%[^:]:%u", host, &port);

		xps_config_lookup_destroy(lookup, session->core);
		xps_connection_t *upstream =
			xps_upstream_create(session->core, host, port);

		if (upstream == NULL) {
			logger(LOG_ERROR, "session_process_request()",
				   "xps_upstream_create() returned NULL");
			set_err_msg(session, HTTP_BAD_GATEWAY);
			return;
		}

		if (xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH,
							session->upstream_source, upstream->sink) == NULL ||
			xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH,
							upstream->source, session->upstream_sink) == NULL) {
			logger(LOG_ERROR, "session_process_request()",
				   "xps_pipe_create() failed: Unable to create bidirectional "
				   "pipe between upstream and session");
			xps_connection_destroy(upstream);
		}

		session->upstream = upstream;
	} else if (lookup->type == REQ_REDIRECT) {
		xps_http_res_t *http_res =
			xps_http_res_create(session->core, lookup->http_status_code);
		xps_http_set_header(&http_res->headers, "Location",
							lookup->redirect_url);

		xps_config_lookup_destroy(lookup, session->core);
		xps_buffer_t *http_res_buff = xps_http_res_serialize(http_res);
		set_to_client_buff(session, http_res_buff);
		xps_http_res_destroy(http_res);
	} else {
		xps_config_lookup_destroy(lookup, session->core);
		logger(LOG_ERROR, "session_process_request()", "invalid lookup type");
		xps_session_destroy(session);
	}
}

void set_err_msg(xps_session_t *session, int err_code) {
	xps_http_res_t *http_res = xps_http_res_create(session->core, err_code);

	if (http_res == NULL) {
		logger(LOG_ERROR, "session_process_request()",
			   "xps_http_res_create() failed for %d error message.", err_code);
		return;
	}
	xps_buffer_t *buff = xps_http_res_serialize(http_res);
	xps_http_res_destroy(http_res);
	if (buff == NULL) {
		logger(LOG_ERROR, "session_process_request()",
			   "xps_http_res_create() failed for %d error message.", err_code);
		return;
	}
	set_to_client_buff(session, buff);
}
