#include "xps_config.h"

xps_config_server_t *parse_server(JSON_Object *server_object);
xps_config_listener_t *parse_listener(JSON_Object *listener_object);
xps_config_route_t *parse_route(JSON_Object *route_object);
// xps_config_route_t *parse_hostname(JSON_Object *hostname_object);
void parse_all_listeners(xps_config_t *config);

static const char *default_gzip_mimes[] = {"text/html",
										   "text/css",
										   "text/plain",
										   "text/javascript",
										   "application/javascript",
										   "application/json",
										   "application/xml",
										   "text/xml",
										   "image/svg+xml"};
static const size_t n_default_mimes =
	sizeof(default_gzip_mimes) / sizeof(default_gzip_mimes[0]);

bool is_gzip_allowed_mime(const char *mime_type, vec_void_t *custom_mimes) {
	if (mime_type == NULL)
		return false;

	for (int i = 0; i < n_default_mimes; i++) {
		if (strcmp(mime_type, default_gzip_mimes[i]) == 0)
			return true;
	}

	for (int i = 0; i < custom_mimes->length; i++) {
		if (strcmp(mime_type, custom_mimes->data[i]) == 0)
			return true;
	}

	return false;
}

xps_config_t *xps_config_create(const char *config_path) {
	assert(config_path != NULL);

	xps_config_t *config = (xps_config_t *)malloc(sizeof(xps_config_t));
	if (config == NULL) {
		logger(LOG_ERROR, "xps_config_create()",
			   "failed to alloc memory for config. malloc() returned NULL");
		return NULL;
	}
	/*get config_json using json_parse_file*/
	JSON_Value *config_json = json_parse_file(config_path);
	if (config_json == NULL) {
		logger(LOG_ERROR, "xps_config_create()",
			   "failed to parse config file at %s. json_parse_file() returned "
			   "NULL",
			   config_path);
	}
	/*initialize fields of config object*/
	config->config_path = config_path;
	config->_config_json = config_json;
	vec_init(&(config->servers));
	vec_init(&(config->_all_listeners));

	JSON_Object *root_object = json_value_get_object(config_json);

	config->server_name = json_object_get_string(root_object, "server_name");
	JSON_Array *servers = json_object_get_array(root_object, "servers");

	/*initialize and parse the server and push to servers list of config*/
	for (size_t i = 0; i < json_array_get_count(servers); i++) {
		JSON_Object *server_object = json_array_get_object(servers, i);
		xps_config_server_t *server = parse_server(server_object);
		if (server == NULL) {
			logger(LOG_ERROR, "xps_config_create()",
				   "parse_server failed for server %lu", i);
			json_value_free(config_json);
			free(config);
			return NULL;
		}
		vec_push(&(config->servers), server);
	}
	/*initialize and set up the _all_listeners array*/
	parse_all_listeners(config);
	return config;
}

void xps_config_destroy(xps_config_t *config) {
	assert(config != NULL);

	vec_void_t *server_list = &config->servers;
	for (int i = 0; i < server_list->length; i++) {
		xps_config_server_t *server = server_list->data[i];
		if (!server)
			continue;

		vec_void_t *listener_list = &server->listeners;
		if (listener_list) {
			for (int j = 0; j < listener_list->length; j++) {
				xps_config_listener_t *listener = listener_list->data[j];
				if (listener)
					free(listener);
			}
			vec_deinit(listener_list);
		}

		vec_void_t *route_list = &server->routes;
		if (route_list) {
			for (int j = 0; j < route_list->length; j++) {
				xps_config_route_t *route = route_list->data[j];
				if (route) {
					vec_deinit(&(route->index));
					vec_deinit(&(route->upstreams));
					vec_deinit(&(route->ip_whitelist));
					vec_deinit(&(route->ip_blacklist));
					vec_deinit(&(route->gzip_mime_types));
					free(route);
				}
			}
			vec_deinit(route_list);
		}

		vec_void_t *hostname_list = &server->hostnames;
		if (hostname_list) {
			for (int j = 0; j < hostname_list->length; j++) {
				xps_config_route_t *hostname = hostname_list->data[j];
				if (hostname)
					free(hostname);
			}
			vec_deinit(hostname_list);
		}

		free(server);
	}
	vec_deinit(server_list);
	vec_deinit(&config->_all_listeners);
	json_value_free(config->_config_json);
	free(config);
}

xps_config_lookup_t *xps_config_lookup(xps_config_t *config,
									   xps_http_req_t *http_req,
									   xps_connection_t *client, int *error) {
	assert(config != NULL);
	assert(http_req != NULL);
	assert(client != NULL);
	assert(error != NULL);

	*error = E_FAIL;
	/*get host,accept encoding,pathname from http_req*/
	const char *host = http_req->host;
	const char *pathname = http_req->pathname;
	const char *accept_encoding =
		xps_http_get_header(&http_req->headers, "Accept-Encoding");

	// Step 1: Find matching server block
	int target_server_index = -1;
	vec_void_t *server_list = &config->servers;
	for (int i = 0; i < server_list->length; i++) {
		xps_config_server_t *server = server_list->data[i];
		// Check if client listener is present in server
		bool has_matching_listener = false;
		vec_void_t *listener_list = &server->listeners;
		for (int j = 0; j < listener_list->length; j++) {
			xps_config_listener_t *listener = listener_list->data[j];
			if (strcmp(listener->host, client->listener->host) == 0 &&
				listener->port == client->listener->port) {
				has_matching_listener = true;
				break;
			}
		}
		if (!has_matching_listener)
			continue;
		/* Check if host header matches any hostname*/
		// NOTE: if not hostnames provided, it is considered a match

		bool has_matching_hostname = false;
		vec_void_t *hostname_list = &server->hostnames;
		if (hostname_list->length == 0)
			has_matching_hostname = true;
		else {
			for (int j = 0; j < hostname_list->length; j++) {
				const char *hostname = hostname_list->data[j];
				if (strcmp(hostname, host) == 0) {
					has_matching_hostname = true;
					break;
				}
			}
		}

		if (has_matching_hostname) {
			target_server_index = i;
			break;
		}
	}

	if (target_server_index == -1) {
		logger(LOG_ERROR, "xps_config_lookup()",
			   "No matching server found for host: %s and listener: %s:%u",
			   host, client->listener->host, client->listener->port);
		*error = E_NOTFOUND;
		return NULL;
	}

	xps_config_server_t *server = config->servers.data[target_server_index];

	/*Find matching route block*/
	// Route matching uses prefix matching with longest-match-first strategy.
	// This is important because:
	// - For file serving routes (e.g., "/"), we need to match any path under it
	//   (e.g., "/index.html", "/css/style.css" should all match route "/")
	// - For specific routes (e.g., "/api"), we want them to take precedence
	// over "/"
	//
	// Example: If we have routes "/" and "/api"
	// - Request "/index.html" matches "/" only → serves file from "/"
	// - Request "/api/users" matches both "/" and "/api" → use "/api" (longest
	// match)

	xps_config_route_t *route = NULL;
	size_t best_match_len = 0; // Track the longest matching route path

	for (size_t i = 0, pathlen = strlen(pathname); i < server->routes.length;
		 i++) {
		xps_config_route_t *current_route = server->routes.data[i];
		logger(LOG_DEBUG, "xps_config_lookup()", "checking route %s for %s",
			   current_route->req_path, pathname);
		size_t route_path_len = strlen(current_route->req_path);

		// Check if this route's path is a prefix of the request path
		if (!str_starts_with(pathname, current_route->req_path))
			continue;
		// If this is a longer match than we've found so far, use it
		if (route_path_len > best_match_len) {
			route = current_route;
			best_match_len = route_path_len;
		}

		if (route_path_len == pathlen)
			break;
	}

	if (route == NULL) {
		logger(LOG_ERROR, "xps_config_lookup()",
			   "No matching route block found.");
		*error = E_NOTFOUND; // No matching route found - 404
		return NULL;
	}
	logger(LOG_DEBUG, "xps_config_lookup()", "using route %s", route->req_path);
	/* Init values of lookup*/
	xps_config_lookup_t *lookup =
		(xps_config_lookup_t *)malloc(sizeof(xps_config_lookup_t));
	if (lookup == NULL) {
		logger(LOG_ERROR, "xps_config_lookup()",
			   "failed to alloc memory for lookup. malloc returned NULL");
		*error = E_FAIL;
		return NULL;
	}

	lookup->dir_path = NULL;
	lookup->file_path = NULL;
	lookup->redirect_url = NULL;
	lookup->upstream = NULL;
	lookup->http_status_code = 0;
	lookup->ip_whitelist = route->ip_whitelist;
	lookup->ip_blacklist = route->ip_blacklist;
	lookup->gzip_enable = false;
	lookup->gzip_level = route->gzip_level;
	lookup->gzip_mime_types = route->gzip_mime_types;

	if (strcmp(route->type, "file_serve") == 0)
		lookup->type = REQ_FILE_SERVE;
	else if (strcmp(route->type, "reverse_proxy") == 0)
		lookup->type = REQ_REVERSE_PROXY;
	else if (strcmp(route->type, "redirect") == 0)
		lookup->type = REQ_REDIRECT;
	else if (strcmp(route->type, "metrics") == 0)
		lookup->type = REQ_METRICS;
	else
		lookup->type = REQ_INVALID;

	// File serve
	if (lookup->type == REQ_FILE_SERVE) {
		char *resource_path = path_join(route->dir_path, pathname);
		if (resource_path == NULL) {
			logger(LOG_ERROR, "xps_config_lookup()", "join_path failed");
			free(lookup);
			*error = E_FAIL;
			return NULL;
		}
		char *abs_path = realpath(resource_path, NULL);
		if (abs_path == NULL) {
			if (errno == ENOENT) {
				*error = E_NOTFOUND;
				logger(LOG_ERROR, "xps_config_lookup()",
					   "Resource not found: %s", abs_path);
			} else if (errno == EACCES) {
				*error = E_PERMISSION;
				logger(LOG_ERROR, "xps_config_lookup()",
					   "Permission denied for %s", abs_path);
			} else {
				*error = E_FAIL;
				logger(LOG_ERROR, "xps_config_lookup()",
					   "Unable to resolve path: %s. realpath() returned NULL",
					   resource_path);
			}

			free(resource_path);
			free(lookup);
			return NULL;
		}

		free(resource_path);
		resource_path = abs_path;
		// is file
		if (is_file(resource_path)) {
			lookup->file_path = resource_path;
		} else if (is_dir(resource_path)) { // is directory
			bool index_file_found = false;
			for (int i = 0; i < route->index.length; i++) {
				char *index_file =
					path_join(resource_path, route->index.data[i]);
				logger(LOG_DEBUG, "xps_config_lookup()", "looking for %s",
					   index_file);
				if (is_file(index_file)) {
					lookup->file_path = index_file;
					index_file_found = true;
					break;
				}
				free(index_file);
			}
			if (!index_file_found) {
				lookup->dir_path = resource_path;
				*error = OK;
				return lookup;
			}
			free(resource_path);
		} else {
			logger(LOG_ERROR, "xps_config_lookup()",
				   "Undefined resource: neither file nor directory");
			free(resource_path);
			free(lookup);
			*error = E_NOTFOUND;
			return NULL;
		}

		if (route->gzip_enable && lookup->file_path != NULL) {
			const char *mime_type = xps_get_mime(lookup->file_path);
			lookup->gzip_enable =
				is_gzip_allowed_mime(mime_type, &route->gzip_mime_types);
		}

		*error = OK;
		return lookup;
	} else if (lookup->type == REQ_REVERSE_PROXY) {
		if (route->upstreams.length == 0) {
			logger(LOG_ERROR, "xps_config_lookup()",
				   "reverse proxy requires upstream");
			free(lookup);
			*error = E_FAIL;
			return NULL;
		}
		lookup->upstream = route->upstreams.data[0];
		*error = OK;
		return lookup;
	} else if (lookup->type == REQ_REDIRECT) {
		if (route->redirect_url == NULL || route->http_status_code == 0) {
			logger(LOG_ERROR, "xps_config_lookup()",
				   "redirect_url and http_status_code must be defined for "
				   "redirect");
			*error = E_FAIL;
			free(lookup);
			return NULL;
		}

		lookup->redirect_url = route->redirect_url;
		lookup->http_status_code = route->http_status_code;
		return lookup;
	}
	logger(LOG_ERROR, "xps_config_lookup()", "invalid route type");
	*error = E_FAIL;
	free(lookup);
	return NULL;
}

void xps_config_lookup_destroy(xps_config_lookup_t *config_lookup,
							   xps_core_t *core) {
	assert(config_lookup);
	assert(core);

	if (config_lookup->file_path)
		free(config_lookup->file_path);
	if (config_lookup->dir_path)
		free(config_lookup->dir_path);

	free(config_lookup);
}

xps_config_server_t *parse_server(JSON_Object *server_object) {
	assert(server_object != NULL);

	xps_config_server_t *server =
		(xps_config_server_t *)malloc(sizeof(xps_config_server_t));
	if (server == NULL) {
		logger(LOG_ERROR, "parse_server()",
			   "failed to alloc memory for server. malloc() returned NULL");
		return NULL;
	}

	vec_init(&(server->listeners));
	vec_init(&(server->routes));
	vec_init(&(server->hostnames));

	JSON_Array *listeners = json_object_get_array(server_object, "listeners");
	if (listeners) {
		for (int i = 0; i < json_array_get_count(listeners); i++) {
			JSON_Object *listener_object = json_array_get_object(listeners, i);
			xps_config_listener_t *listener = parse_listener(listener_object);
			if (listener)
				vec_push(&(server->listeners), listener);
			else
				logger(LOG_ERROR, "parse_server()",
					   "error creating listener config: listener %lu", i);
		}
	} else {
		logger(LOG_WARNING, "parse_server()",
			   "no listeners defined for server");
	}

	JSON_Array *routes = json_object_get_array(server_object, "routes");
	if (routes) {
		for (int i = 0; i < json_array_get_count(routes); i++) {
			JSON_Object *route_object = json_array_get_object(routes, i);
			xps_config_route_t *route = parse_route(route_object);
			if (route)
				vec_push(&(server->routes), route);
			else
				logger(LOG_ERROR, "parse_server()",
					   "error creating route config: route %lu", i);
		}
	} else {
		logger(LOG_WARNING, "parse_server()", "no routes defined for server");
	}

	// JSON_Array *hostnames = json_object_get_array(server_object,
	// "hostnames"); if (hostnames) { 	for (int i = 0; i <
	// json_array_get_count(hostnames); i++) { 		JSON_Object *hostname_object
	// = json_array_get_object(hostnames, i); 		xps_config_listener_t
	// *hostname = parse_listener(hostname_object); 		if (hostname)
	// 			vec_push(&(server->hostnames), hostname);
	// 		else
	// 			logger(LOG_ERROR, "parse_server()",
	// 				   "error creating hostname: hostname %lu", i);
	// 	}
	// } else {
	// 	logger(LOG_WARNING, "parse_server()",
	// 		   "no hostnames defined for server");
	// }

	return server;
}

xps_config_listener_t *parse_listener(JSON_Object *listener_object) {
	assert(listener_object != NULL);

	xps_config_listener_t *listener =
		(xps_config_listener_t *)malloc(sizeof(xps_config_listener_t));
	if (listener == NULL) {
		logger(LOG_ERROR, "parse_listener()",
			   "failed to alloc memory for listener. malloc() returned NULL");
		return NULL;
	}

	listener->host = json_object_get_string(listener_object, "host");
	listener->port = (u_int)json_object_get_number(listener_object, "port");

	if (listener->host == NULL || listener->port == 0) {
		if (listener->host == NULL)
			logger(LOG_ERROR, "parse_listener()",
				   "err parsing listener: host not defined");
		if (listener->port == 0)
			logger(LOG_ERROR, "parse_listener()",
				   "err parsing listener: port not defined");

		free(listener);
		return NULL;
	}

	return listener;
}

xps_config_route_t *parse_route(JSON_Object *route_object) {
	assert(route_object != NULL);

	xps_config_route_t *route =
		(xps_config_route_t *)malloc(sizeof(xps_config_route_t));
	if (route == NULL) {
		logger(LOG_ERROR, "parse_route()",
			   "failed to alloc memory for route. malloc() returned NULL");
		return NULL;
	}

	route->req_path = json_object_get_string(route_object, "req_path");
	route->type = json_object_get_string(route_object, "type");

	assert(route->req_path != NULL);
	assert(route->type != NULL);

	route->dir_path = NULL;
	route->redirect_url = NULL;
	route->http_status_code = 200;
	route->gzip_enable = false;
	route->gzip_level = -1;

	vec_init(&(route->index));
	vec_init(&(route->upstreams));
	vec_init(&(route->ip_whitelist));
	vec_init(&(route->ip_blacklist));
	vec_init(&(route->gzip_mime_types));

	if (strcmp(route->type, "file_serve") == 0) {
		route->dir_path = json_object_get_string(route_object, "dir_path");
		assert(route->dir_path != NULL);

		JSON_Array *index_arr = json_object_get_array(route_object, "index");
		assert(index_arr != NULL);

		for (int i = 0; i < json_array_get_count(index_arr); i++) {
			const char *index = json_array_get_string(index_arr, i);
			vec_push(&(route->index), (void *)index);
		}
	} else if (strcmp(route->type, "reverse_proxy") == 0) {
		JSON_Array *upstream_arr =
			json_object_get_array(route_object, "upstreams");
		assert(upstream_arr != NULL);

		for (int i = 0; i < json_array_get_count(upstream_arr); i++) {
			const char *upstream = json_array_get_string(upstream_arr, i);
			vec_push(&(route->upstreams), (void *)upstream);
		}
	} else if (strcmp(route->type, "redirect") == 0) {
		route->http_status_code =
			(u_int)json_object_get_number(route_object, "http_status_code");
		route->redirect_url =
			json_object_get_string(route_object, "redirect_url");

		assert(route->http_status_code != 0);
		assert(route->redirect_url != NULL);
	}

	JSON_Array *ip_whitelist =
		json_object_get_array(route_object, "ip_whitelist");
	JSON_Array *ip_blacklist =
		json_object_get_array(route_object, "ip_blacklist");

	int ip_whitelist_len = json_array_get_count(ip_whitelist);
	int ip_blacklist_len = json_array_get_count(ip_blacklist);

	if (ip_whitelist) {
		for (int i = 0; i < ip_whitelist_len; i++) {
			const char *ip = json_array_get_string(ip_whitelist, i);
			if (!ip)
				continue;

			vec_push(&(route->ip_whitelist), (void *)ip);
		}
	}
	if (ip_blacklist) {
		for (int i = 0; i < ip_blacklist_len; i++) {
			const char *ip = json_array_get_string(ip_blacklist, i);

			if (!ip)
				continue;

			for (int j = 0; j < ip_whitelist_len; j++) {
				const char *whitelisted_ip = route->ip_whitelist.data[j];
				if (whitelisted_ip == NULL)
					continue;
				if (strcmp(whitelisted_ip, ip) == 0) {
					route->ip_whitelist.data[j] = NULL;
					break;
				}
			}
			vec_push(&(route->ip_blacklist), (void *)ip);
		}
	}
	vec_filter_null(&(route->ip_whitelist));

	if (json_object_has_value_of_type(route_object, "gzip_enable", JSONBoolean))
		route->gzip_enable =
			json_object_get_boolean(route_object, "gzip_enable");
	if (json_object_has_value_of_type(route_object, "gzip_level", JSONNumber))
		route->gzip_level =
			(int)json_object_get_number(route_object, "gzip_level");

	JSON_Array *gzip_mime_types =
		json_object_get_array(route_object, "gzip_mime_types");
	if (gzip_mime_types) {
		for (int i = 0; i < json_array_get_count(gzip_mime_types); i++) {
			const char *mime_type = json_array_get_string(gzip_mime_types, i);
			if (mime_type)
				vec_push(&(route->gzip_mime_types), (void *)mime_type);
		}
	}

	return route;
}

void parse_all_listeners(xps_config_t *config) {
	assert(config != NULL);

	for (int i = 0; i < config->servers.length; i++) {
		xps_config_server_t *server = config->servers.data[i];
		for (int j = 0; j < server->listeners.length; j++) {
			xps_config_listener_t *listener = server->listeners.data[j];
			bool seen = false;

			for (int k = 0; k < config->_all_listeners.length; k++) {
				xps_config_listener_t *existing_listener =
					config->_all_listeners.data[k];
				if (strcmp(listener->host, existing_listener->host) == 0 &&
					listener->port == existing_listener->port) {
					seen = true;
					break;
				}
			}

			if (!seen)
				vec_push(&(config->_all_listeners), listener);
		}
	}
}
