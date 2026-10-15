#include "xps_http_req.h"

int http_process_request_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
	int error = xps_http_parse_request_line(http_req, buff);
	if (error != OK) {
		logger(LOG_ERROR, "http_process_request_line()",
			   "xps_http_parse_request_line() failed");
		return error;
	}
	http_req->request_line =
		str_from_ptrs(http_req->request_line_start, http_req->request_line_end);
	http_req->method =
		str_from_ptrs(http_req->method_start, http_req->method_end);
	http_req->uri = str_from_ptrs(http_req->uri_start, http_req->uri_end);
	http_req->schema =
		str_from_ptrs(http_req->schema_start, http_req->schema_end);
	http_req->host = str_from_ptrs(http_req->host_start, http_req->host_end);
	http_req->path = str_from_ptrs(http_req->path_start, http_req->path_end);
	http_req->pathname =
		str_from_ptrs(http_req->pathname_start, http_req->pathname_end);
	http_req->http_version =
		str_from_ptrs(http_req->http_major, http_req->http_minor);

	http_req->port = -1;
	char *port_str = str_from_ptrs(http_req->port_start, http_req->port_end);
	if (port_str == NULL || strcmp(port_str, "") == 0) {
		if (http_req->schema != NULL &&
			strncmp(http_req->schema, "https", 5) == 0)
			http_req->port = 443;
		else
			http_req->port = 80;
	} else {
		http_req->port = atoi(port_str);
		free(port_str);
	}

	logger(LOG_DEBUG, "http_process_request_line()",
		   "\nREQUEST LINE: %s\n"
		   "\tMETHOD: %s\n"
		   "\tURI: %s\n"
		   "\tSCHEMA: %s\n"
		   "\tHOST: %s\n"
		   "\tPORT: %d\n"
		   "\tPATH: %s\n"
		   "\tPATHNAME: %s\n"
		   "\tHTTP VERSION: %s\n",
		   http_req->request_line, http_req->method, http_req->uri,
		   http_req->schema, http_req->host, http_req->port, http_req->path,
		   http_req->pathname, http_req->http_version);
	return OK;
}

int http_process_headers(xps_http_req_t *http_req, xps_buffer_t *buff) {
	assert(http_req != NULL);
	assert(buff != NULL);

	vec_init(&(http_req->headers));
	int rc;
	while (1) {
		rc = xps_http_parse_header_line(http_req, buff);
		if (rc == E_FAIL || rc == E_AGAIN)
			break;
		if (rc == OK || rc == E_NEXT) {
			/* Alloc memory for new header*/
			/*assign key,val from their corresponding start and end pointers*/
			/*push this header into headers list of http_req*/
			/*if error is E_NEXT continue*/
			xps_keyval_t *header = (xps_keyval_t *)malloc(sizeof(xps_keyval_t));
			if (header == NULL) {
				logger(LOG_ERROR, "http_process_headers()",
					   "malloc() failed for 'header'");
				return E_FAIL;
			}
			header->key = str_from_ptrs(http_req->header_key_start,
										http_req->header_key_end);
			if (header->key == NULL) {
				logger(LOG_ERROR, "http_process_headers()",
					   "error extracting header key");
				free(header);
				return E_FAIL;
			}
			header->val = str_from_ptrs(http_req->header_val_start,
										http_req->header_val_end);
			if (header->val == NULL) {
				logger(LOG_ERROR, "http_process_headers()",
					   "error extracting header val");
				free(header->key);
				free(header);
				return E_FAIL;
			}

			vec_push(&http_req->headers, header);

			if (rc == E_NEXT)
				continue;
		}

		vec_void_t *headers = &http_req->headers;
		logger(LOG_DEBUG, "http_process_headers()",
			   "**** Request * Headers ****");
		for (int i = 0; i < headers->length; i++) {
			xps_keyval_t *header = (xps_keyval_t *)headers->data[i];
			logger(LOG_DEBUG, "http_process_headers()", "%s: %s", header->key,
				   header->val);
		}
		return OK;
	}

	logger(LOG_ERROR, "http_process_headers()",
		   "An error occured while parsing headers");
	/*error occurs, thus iterate through header list, free each header*/
	vec_void_t *headers = &http_req->headers;
	for (int i = 0; i < headers->length; i++) {
		xps_keyval_t *header = (xps_keyval_t *)headers->data[i];
		free(header->key);
		free(header->val);
		free(header);
	}
	/*deinitialize headers list*/
	vec_deinit(headers);
	return rc;
}

xps_buffer_t *xps_http_req_serialize(xps_http_req_t *http_req) {
	assert(http_req != NULL);
	/* Serialize headers into a buffer headers_str*/
	xps_buffer_t *headers_str = xps_http_serialize_headers(&http_req->headers);
	if (headers_str == NULL) {
		logger(LOG_ERROR, "xps_http_req_serialize()",
			   "xps_http_serialize_headers() failed");
		return NULL;
	}
	size_t final_len =
		strlen(http_req->request_line) + 1 + headers_str->len + 1;
	xps_buffer_t *buff = xps_buffer_create(final_len + 1, final_len, NULL);
	if (buff == NULL) {
		logger(LOG_ERROR, "xps_http_req_serialize()",
			   "xps_buffer_create() failed");
		xps_buffer_destroy(headers_str);
		return NULL;
	}
	buff->pos = buff->data;

	memcpy(buff->pos, http_req->request_line, strlen(http_req->request_line));
	buff->pos += strlen(http_req->request_line);
	memcpy(buff->pos, "\n", 1);
	buff->pos += 1;
	memcpy(buff->pos, headers_str->data, headers_str->len);
	buff->pos += headers_str->len;
	memcpy(buff->pos, "\n", 1);
	buff->pos += 1;

	xps_buffer_destroy(headers_str);
	return buff;
}

xps_http_req_t *xps_http_req_create(xps_core_t *core, xps_buffer_t *buff,
									int *error) {
	assert(core != NULL);
	assert(buff != NULL);
	assert(error != NULL);
	*error = E_FAIL;
	/* Alloc memory for http_req instance*/
	xps_http_req_t *http_req = (xps_http_req_t *)malloc(sizeof(xps_http_req_t));
	if (http_req == NULL) {
		logger(LOG_ERROR, "xps_http_req_create()",
			   "malloc() failed for 'http_req'");
		return NULL;
	}
	memset(http_req, 0, sizeof(xps_http_req_t));
	http_req->parser_state = RL_START;
	int rc = http_process_request_line(http_req, buff);
	if (rc != OK) {
		logger(LOG_ERROR, "xps_http_req_create()",
			   "http_process_request_line() failed");
		free(http_req);
		*error = rc;
		return NULL;
	}
	/*Process headers and handle possible errors*/
	rc = http_process_headers(http_req, buff);
	if (rc != OK) {
		logger(LOG_ERROR, "xps_http_req_create()",
			   "http_process_headers() failed");
		xps_http_req_destroy(core, http_req);
		*error = rc;
		return NULL;
	}
	// Header length
	http_req->header_len = (size_t)(buff->pos - buff->data);
	// Body length is retrieved from header Content-Length
	http_req->body_len = 0;
	const char *body_len_str =
		xps_http_get_header(&http_req->headers, "Content-Length");
	if (body_len_str != NULL)
		http_req->body_len = (size_t)atoi(body_len_str);
	*error = OK;
	return http_req;
}

void xps_http_req_destroy(xps_core_t *core, xps_http_req_t *http_req) {
	assert(http_req != NULL);

	free(http_req->request_line);
	free(http_req->method);
	free(http_req->uri);
	free(http_req->schema);
	free(http_req->host);
	free(http_req->path);
	free(http_req->pathname);
	free(http_req->http_version);

	vec_void_t *headers = &http_req->headers;
	for (int i = 0; i < headers->length; i++) {
		xps_keyval_t *header = (xps_keyval_t *)headers->data[i];
		free(header->key);
		free(header->val);
		free(header);
	}
	/*deinitialize headers list*/
	vec_deinit(headers);

	free(http_req);
}
