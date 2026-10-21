#include "xps_http_res.h"

xps_http_res_t *xps_http_res_create(xps_core_t *core, u_int code) {
	assert(core != NULL);

	xps_http_res_t *http_res = (xps_http_res_t *)malloc(sizeof(xps_http_res_t));
	if (http_res == NULL) {
		logger(LOG_ERROR, "xps_http_res_create()",
			   "failed to alloc memory for http_res. malloc() returned NULL");
		return NULL;
	}

	memset(http_res, 0, sizeof(xps_http_res_t));
	http_res->body = NULL;
	vec_init(&http_res->headers);

	const char *status_line;
	switch (code) {
	case 200:
		status_line = "OK";
		break;
	case 201:
		status_line = "CREATED";
		break;

	case 301:
		status_line = "MOVED PERMANENTLY";
		break;
	case 302:
		status_line = "MOVED TEMPORARILY";
		break;
	case 304:
		status_line = "NOT MODIFIED";
		break;
	case 307:
		status_line = "TEMPORARY REDIRECT";
		break;
	case 308:
		status_line = "PERMANENT REDIRECT";
		break;

	case 400:
		status_line = "BAD REQUEST";
		break;
	case 401:
		status_line = "UNAUTHORIZED";
		break;
	case 403:
		status_line = "FORBIDDEN";
		break;
	case 404:
		status_line = "NOT FOUND";
		break;
	case 408:
		status_line = "REQUEST TIME OUT";
		break;
	case 429:
		status_line = "TOO MANY REQUESTS";
		break;

	case 500:
		status_line = "INTERNAL SERVER ERROR";
		break;
	case 501:
		status_line = "NOT IMPLEMENTED";
		break;
	case 502:
		status_line = "BAD GATEWAY";
		break;
	case 503:
		status_line = "SERVICE UNAVAILABLE";
		break;
	case 504:
		status_line = "GATEWAY TIMEOUT";
		break;
	case 505:
		status_line = "HTTP VERSION NOT SUPPORTED";
		break;

	default:
		status_line = "OK";
		break;
	}

	sprintf(http_res->response_line, "HTTP/1.1 %d %s", code, status_line);

	char time_buf[32];
	time_t now = time(NULL);
	struct tm *time_info = gmtime(&now);
	strftime(time_buf, 32, "%a, %d %b %Y %H:%M:%S GMT", time_info);

	xps_http_set_header(&(http_res->headers), "Date", time_buf);
	xps_http_set_header(&(http_res->headers), "Server", SERVER_NAME);
	xps_http_set_header(&(http_res->headers), "Access-Control-Allow-Origin",
						"*");

	return http_res;
}

void xps_http_res_destroy(xps_http_res_t *res) {
	assert(res != NULL);

	if (res->body)
		xps_buffer_destroy(res->body);

	vec_void_t *headers = &res->headers;
	for (int i = 0; i < headers->length; i++) {
		xps_keyval_t *header = headers->data[i];
		if (header) {
			free(header->key);
			free(header->val);
			free(header);
		}
	}
	vec_deinit(headers);

	free(res);
}

xps_buffer_t *xps_http_res_serialize(xps_http_res_t *http_res) {
	assert(http_res != NULL);

	// Serialize headers
	xps_buffer_t *headers_str =
		xps_http_serialize_headers(&(http_res->headers));
	if (headers_str == NULL) {
		logger(LOG_ERROR, "xps_http_res_serialize()",
			   "failed to serialize headers");
		return NULL;
	}

	size_t response_len = strlen(http_res->response_line);
	size_t header_len = headers_str->len;
	size_t body_len = http_res->body ? http_res->body->len : 0;
	/* status line + CRLF, headers (each header already ends with CRLF)
	 * + blank line (CRLF), body */
	size_t final_len = response_len + 2 + header_len + 2 + body_len;

	xps_buffer_t *buff = xps_buffer_create(final_len, final_len, NULL);
	if (buff == NULL) {
		logger(LOG_ERROR, "xps_http_res_serialize()",
			   "failed to create buffer instance");
		xps_buffer_destroy(headers_str);
		return NULL;
	}

	// Copy everything
	u_char *p = buff->data;
	/* copy response line */
	memcpy(p, http_res->response_line, response_len);
	p += response_len;
	memcpy(p, "\r\n", 2);
	p += 2;
	/* copy headers (each header already ends with CRLF) */
	memcpy(p, headers_str->data, header_len);
	p += header_len;
	memcpy(p, "\r\n", 2);
	p += 2;
	/* copy response body */
	if (body_len > 0) {
		memcpy(p, http_res->body->data, body_len);
		p += body_len;
	}

	buff->pos = buff->data;

	xps_buffer_destroy(headers_str);

	return buff;
}

// void xps_http_res_set_body(xps_http_res_t *http_res, xps_buffer_t *buff);
