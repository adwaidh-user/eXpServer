#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5

void strrev(char *str) {
	for (int l = 0, r = strlen(str) - 2; l < r; l++, r--) {
		char temp = str[l];
		str[l] = str[r];
		str[r] = temp;
	}
}

void write_to_file(int conn_sock_fd) {
	char buffer[BUFF_SIZE];
	ssize_t bytes_received;

	// Open the file to which the data from the client is being written
	FILE *fp;
	const char *filename = "t2.txt";
	fp = fopen(filename, "w");
	if (fp == NULL) {
		perror("[-]Error in creating file");
		exit(EXIT_FAILURE);
	}

	printf("[INFO] Receiving data from client...\n");
	while ((bytes_received = recv(conn_sock_fd, buffer, BUFF_SIZE, 0)) > 0) {
		printf("[FILE DATA] %s", buffer); // Print received data to the console
		fprintf(fp, "%s", buffer);		  // Write data to file
		memset(buffer, 0, BUFF_SIZE);	  // Clear the buffer
	}

	if (bytes_received < 0) {
		perror("[-]Error in receiving data");
	}

	fclose(fp);
	printf("[INFO] Data written to file successfully.\n");
}

int main() {
	int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	// Setting socket to reuse addr in case restarted while in TIME_WAIT
	const int enable = 1;
	setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

	// Setting the server addr
	struct sockaddr_in server_addr = {.sin_family = AF_INET,
									  .sin_addr.s_addr = htonl(INADDR_ANY),
									  .sin_port = htons(PORT)};

	// binding the server
	int bind_output = bind(listen_sock_fd, (struct sockaddr *)&server_addr,
						   sizeof(struct sockaddr_in));

	// Start listening
	listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
	printf("[INFO] Server listening on port %d\n", PORT);

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int conn_sock_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr,
							  &client_addr_len);
	printf("[INFO] Client connected to server\n");

    write_to_file(conn_sock_fd);

	close(listen_sock_fd);

	return 0;
}
