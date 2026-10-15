#include "xps_http.h"

bool http_strcmp(u_char *str, const char *method, size_t length) {
	for (int i = 0; i < length; i++) {
		u_char c1 = str[i] | 0x20;
		u_char c2 = (u_char)method[i] | 0x20;
		if (c1 != c2)
			return false;
	}
	return true;
}

int xps_http_parse_request_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
	xps_http_parser_state_t parser_state = http_req->parser_state;
	u_char *p_ch = buff->pos; // current buffer postion

	for (; p_ch < buff->data + buff->len; p_ch++) {
		char ch = *p_ch;
		switch (parser_state) {
		case RL_START:
			/*assign request_line_start, ignore CR and LF, fail if not upper
			 * case, assign method_start*/
			http_req->request_line_start = p_ch;
			if (ch == CR || ch == LF)
				break;
			if (ch < 'A' || ch > 'Z') {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Invalid request: unknown method");
				return E_FAIL;
			}
			http_req->method_start = p_ch;
			parser_state = RL_METHOD;
			break;

		case RL_METHOD:
			if (ch == ' ') {
				size_t method_len = p_ch - http_req->method_start;
				if (method_len == 3 &&
					http_strcmp(http_req->method_start, "GET", 3))
					http_req->method_n = HTTP_GET;
				else if (method_len == 4 &&
						 http_strcmp(http_req->method_start, "HEAD", 4))
					http_req->method_n = HTTP_HEAD;
				else if (method_len == 4 &&
						 http_strcmp(http_req->method_start, "POST", 4))
					http_req->method_n = HTTP_POST;
				else if (method_len == 3 &&
						 http_strcmp(http_req->method_start, "PUT", 3))
					http_req->method_n = HTTP_PUT;
				else if (method_len == 6 &&
						 http_strcmp(http_req->method_start, "DELETE", 6))
					http_req->method_n = HTTP_DELETE;
				else if (method_len == 7 &&
						 http_strcmp(http_req->method_start, "OPTIONS", 7))
					http_req->method_n = HTTP_OPTIONS;
				else if (method_len == 5 &&
						 http_strcmp(http_req->method_start, "TRACE", 5))
					http_req->method_n = HTTP_TRACE;
				else if (method_len == 7 &&
						 http_strcmp(http_req->method_start, "CONNECT", 7))
					http_req->method_n = HTTP_CONNECT;
				else {
					logger(LOG_ERROR, "xps_http_parse_request_line()",
						   "Invalid method");
					return E_FAIL;
				}

				http_req->method_end = p_ch;
				parser_state = RL_SP_AFTER_METHOD;
			}
			break;

		case RL_SP_AFTER_METHOD:
			if (ch == '/') {
				/*assign start and end of schema,host,port and start of
				 * uri,path,pathname*/
				/*next state is RL_PATH*/
				http_req->schema_start = p_ch;
				http_req->schema_end = p_ch;
				http_req->host_start = p_ch;
				http_req->host_end = p_ch;
				http_req->port_start = p_ch;
				http_req->port_end = p_ch;
				http_req->uri_start = p_ch;
				http_req->uri_end = p_ch;
				http_req->path_start = p_ch;
				http_req->path_end = p_ch;
				http_req->pathname_start = p_ch;
				http_req->pathname_end = p_ch;

				parser_state = RL_PATH;
			} else {
				char c = ch | 0x20; // convert to lower case
				/*if lower case alphabets, assign start of schema and uri, next
				 * state is RL_SCHEMA*/
				/*if not space(''), fails*/
				if (c < 'a' || c > 'z') {
					logger(LOG_ERROR, "xps_http_parse_request_line()",
						   "Invalid schema / uri");
					return E_FAIL;
				}
				http_req->schema_start = p_ch;
				http_req->uri_start = p_ch;

				parser_state = RL_SCHEMA;
			}
			break;

		case RL_SCHEMA:
			/*schema ends on ':' ,next state is RL_SCHEMA_SLASH*/
			/*on lower case alphabets break ie schema can have lower case
			 * alphabets*/
			if (ch == ':') {
				http_req->schema_end = p_ch;
				parser_state = RL_SCHEMA_SLASH;
			} else {
				char c = ch | 0x20;
				/*fails on all other inputs*/
				if (c < 'a' || c > 'z') {
					logger(LOG_ERROR, "xps_http_parse_request_line()",
						   "Invalid schema");
					return E_FAIL;
				}
			}
			break;

		case RL_SCHEMA_SLASH:
			/*on '/' assign next state, fails on all other inputs*/
			if (*p_ch != '/') {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Missing '/' after schema");
				return E_FAIL;
			}
			parser_state = RL_SCHEMA_SLASH_SLASH;
			break;

		case RL_SCHEMA_SLASH_SLASH:
			/*on '/' - assign next state, for start of host assign next
			 * position, fails on all other inputs*/
			if (*p_ch != '/') {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Missing '//' after schema");
				return E_FAIL;
			}
			parser_state = RL_HOST;
			http_req->host_start = p_ch + 1;
			break;

		case RL_HOST:
			/*host can have lower case alphabets, numbers, '-', '.' */
			/*on ':' - host ends, for start of port assign next position, next
			 * state is RL_PORT*/
			/*on '/' - host ends, assign start of path,pathname, end of port,
			 * next state is RL_PATH*/
			/*on ' ' - host ends, assign end of uri, start and end of
			 * port,path,pathname, next state is RL_VERSION_START*/
			/*on all other input, fails*/
			if (ch == ':') {
				http_req->host_end = p_ch;
				http_req->port_start = p_ch + 1;
				parser_state = RL_PORT;
			} else if (ch == '/') {
				http_req->host_end = p_ch;
				http_req->path_start = p_ch;
				http_req->pathname_start = p_ch;
				http_req->port_end = p_ch;
				parser_state = RL_PATH;
			} else if (ch == ' ') {
				http_req->host_end = p_ch;
				http_req->uri_end = p_ch;
				http_req->port_start = p_ch;
				http_req->port_end = p_ch;
				http_req->path_end = p_ch;
				http_req->pathname_end = p_ch;
				parser_state = RL_VERSION_START;
			} else {
				char c = ch | 0x20;
				if (('a' <= c && c <= 'z') || ('0' <= ch && ch <= '9') ||
					ch == '-' || ch == '.')
					break;

				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Invalid host");
				return E_FAIL;
			}
			break;

		case RL_PORT:
			/*port can only have numbers */
			/*on '/' - port ends, assign start of path,pathname, next state is
			 * RL_PATH*/
			/*on ' ' - port ends, assign end of uri, start and end of
			 * path,pathname, next state is RL_VERSION_START*/
			/*on all other input, fails*/
			if (ch == '/') {
				http_req->port_end = p_ch;
				http_req->path_start = p_ch;
				http_req->pathname_start = p_ch;
				parser_state = RL_PATH;
			} else if (ch == ' ') {
				http_req->port_end = p_ch;
				http_req->uri_end = p_ch;
				http_req->path_start = p_ch;
				http_req->pathname_start = p_ch;
				http_req->path_end = p_ch;
				http_req->pathname_end = p_ch;
				parser_state = RL_VERSION_START;
			} else if (ch < '0' || '9' < ch) {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Invalid port");
				return E_FAIL;
			}
			break;

		case RL_PATH:
			/*on ' ' - path ends, assign end of path,pathname,uri next state is
			 * RL_VERSION_START*/
			/*on '?'or'&'or'='or'#' - assign end of path, next state is
			 * RL_PATHNAME*/
			/*on CR or LF, fails*/
			if (ch == ' ') {
				http_req->path_end = p_ch;
				http_req->pathname_end = p_ch;
				http_req->uri_end = p_ch;
				parser_state = RL_VERSION_START;
			} else if (ch == '?' || ch == '&' || ch == '=' || ch == '#') {
				http_req->path_end = p_ch;
				parser_state = RL_PATHNAME;
			} else if (ch == CR || ch == LF) {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Path shouldnt have a CR or LR");
				return E_FAIL;
			}
			break;

		case RL_PATHNAME:
			/*on ' ' - assign end of uri,pathname, next state is
			 * RL_VERSION_START*/
			/*on CR or LF, fails*/
			if (ch == ' ') {
				http_req->pathname_end = p_ch;
				http_req->uri_end = p_ch;
				parser_state = RL_VERSION_START;
			} else if (ch == CR || ch == LF) {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Pathname shouldnt have a CR or LR");
				return E_FAIL;
			}
			break;

		case RL_VERSION_START:
			/*can have space*/
			/*on 'H' - next state is RL_VERSION_H*/
			/*fails on all other input*/
			if (ch == 'H')
				parser_state = RL_VERSION_H;
			else if (ch != ' ') {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected 'H'");
				return E_FAIL;
			}
			break;

		case RL_VERSION_H:
			if (ch == 'T')
				parser_state = RL_VERSION_HT;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected 'T'");
				return E_FAIL;
			}
			break;

		case RL_VERSION_HT:
			if (ch == 'T')
				parser_state = RL_VERSION_HTT;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected 'T'");
				return E_FAIL;
			}
			break;

		case RL_VERSION_HTT:
			if (ch == 'P')
				parser_state = RL_VERSION_HTTP;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected 'P'");
				return E_FAIL;
			}
			break;

		case RL_VERSION_HTTP:
			if (ch == '/')
				parser_state = RL_VERSION_HTTP_SLASH;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected '/'");
				return E_FAIL;
			}
			break;

		case RL_VERSION_HTTP_SLASH:
			/*on '1' - assign major, next state is RL_VERSION_MAJOR, fails on
			 * all other inputs*/
			if (ch == '1') {
				http_req->http_major = p_ch;
				parser_state = RL_VERSION_MAJOR;
			} else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected valid version major");
				return E_FAIL;
			}
			break;

		case RL_VERSION_MAJOR:
			if (ch == '.')
				parser_state = RL_VERSION_DOT;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected '.' after version major");
				return E_FAIL;
			}
			break;

		case RL_VERSION_DOT:
			/*on '0' or '1' - assign minor to next position, next state is
			 * RL_VERSION_MINOR, fails on other inputs*/
			if (ch == '0' || ch == '1') {
				http_req->http_minor = p_ch + 1;
				parser_state = RL_VERSION_MINOR;
			} else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected valid version minor");
				return E_FAIL;
			}
			break;

		case RL_VERSION_MINOR:
			if (ch == CR) {
				parser_state = RL_CR;
			} else if (ch == LF) {
				parser_state = RL_LF;
			} else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected CR or LF");
				return E_FAIL;
			}
			break;

		case RL_CR:
			if (ch == LF)
				parser_state = RL_LF;
			else {
				logger(LOG_ERROR, "xps_http_parse_request_line()",
					   "Expected LF");
				return E_FAIL;
			}
			break;

		case RL_LF:
			if (http_req->request_line_end == NULL) {
				http_req->request_line_end = p_ch;
			}
			http_req->parser_state = H_START;
			buff->pos = p_ch;
			return OK;

		default:
			logger(LOG_ERROR, "xps_http_parse_request_line()",
				   "An error occured while parsing");
			return E_FAIL;
		}
	}

	return E_AGAIN;
}

int xps_http_parse_header_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
	assert(http_req != NULL);
	assert(buff != NULL);

	u_char *p_ch = buff->pos;
	xps_http_parser_state_t parser_state = http_req->parser_state;

	for (; p_ch < buff->data + buff->len; p_ch++) {
		char ch = *p_ch;

		switch (parser_state) {
		case H_START:
			if (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')) {
				http_req->header_key_start = p_ch;
				parser_state = H_NAME;
			} else {
				logger(LOG_ERROR, "xps_http_parse_header_line()",
					   "Invalid header name start");
				return E_FAIL;
			}
			break;

		case H_NAME:
			if (ch == ':') {
				http_req->header_key_end = p_ch;
				parser_state = H_COLON;
			} else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
					   (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
				continue;
			} else {
				logger(LOG_ERROR, "xps_http_parse_header_line()",
					   "Invalid header name");
				return E_FAIL;
			}
			break;

		case H_COLON:
			if (ch == ' ') {
				parser_state = H_SP_AFTER_COLON;
			} else {
				http_req->header_val_start = p_ch;
				parser_state = H_VAL;
			}
			break;

		case H_SP_AFTER_COLON:
			if (ch != ' ') {
				http_req->header_val_start = p_ch;
				parser_state = H_VAL;
			}
			break;

		case H_VAL:
			if (ch == CR || ch == LF) {
				http_req->header_val_end = p_ch;
				parser_state = (ch == CR) ? H_CR : H_LF;
			}
			break;

		case H_CR:
			if (ch == LF)
				parser_state = H_LF;
			else {
				logger(LOG_ERROR, "xps_http_parse_header_line()",
					   "Expected LF");
				return E_FAIL;
			}
			break;

		case H_LF:
			if (ch == LF) {
				parser_state = H_LF_LF;
			} else if (ch == CR) {
				parser_state = H_LF_CR;
			} else {
				parser_state = H_START;
				buff->pos = p_ch;
				http_req->parser_state = parser_state;
				return E_NEXT;
			}
			break;

		case H_LF_LF:
			buff->pos = p_ch;
			http_req->parser_state = H_START;
			return OK; // HTTP complete header section done

		case H_LF_CR:
			if (ch == LF) {
				buff->pos = p_ch;
				http_req->parser_state = H_START;
				return OK; // HTTP complete header section done
			} else {
				logger(LOG_ERROR, "xps_http_parse_header_line()",
					   "Expected ending LF");
				return E_FAIL;
			}
			break;

		default:
			logger(LOG_ERROR, "xps_http_parse_header_line()",
				   "An unknown error occured while parsion headers");
			return E_FAIL;
		}
	}

	return E_AGAIN;
}

const char *xps_http_get_header(vec_void_t *headers, const char *key) {
	assert(headers != NULL);
	assert(key != NULL);
	for (int i = 0; i < headers->length; i++) {
		xps_keyval_t *header = (xps_keyval_t *)headers->data[i];
		if (strcmp(header->key, key) == 0)
			return header->val;
	}
	return NULL;
}

xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers) {
	assert(headers != NULL);
	xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
	if (buff == NULL) {
		logger(LOG_ERROR, "xps_http_serialize_headers()",
			   "xps_buffer_create() failed");
        return NULL;
	}
	buff->pos = buff->data;
	buff->data[0] = '\0';
	buff->len = 1;
	for (int i = 0; i < headers->length; i++) {
		xps_keyval_t *header = headers->data[i];

		size_t header_str_len = strlen(header->key) + strlen(header->val) + 5;
		char header_str[header_str_len];

		sprintf(header_str, "%s: %s\n", header->key, header->val);
		if ((buff->size - buff->len) < header_str_len) { // buffer is small
			u_char *new_data = realloc(buff->data, buff->size * 2);
			if (new_data == NULL) {
				xps_buffer_destroy(buff);
				logger(LOG_ERROR, "xps_http_serialize_headers()",
					   "realloc() failed for buff->data");
				return NULL;
			}
			buff->data = new_data;
			buff->size <<= 1;
		}
		strcat(buff->data, header_str);
		buff->len = strlen(buff->data);
	}
	return buff;
}
