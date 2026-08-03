#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000

void strrev(char *str) {
	for (int l = 0, r = strlen(str) - 2; l < r; l++, r--) {
		char temp = str[l];
		str[l] = str[r];
		str[r] = temp;
	}
}

typedef struct {
	char message[BUFF_SIZE];
	struct sockaddr_in client_addr;
	int sockfd;
	socklen_t addr_len;
} client_data_t;

void *handle_client(void *arg) {
	client_data_t *data = (client_data_t *)arg;
	printf("[CLIENT MESSAGE] %s", data->message);

	strrev(data->message);

	sendto(data->sockfd, data->message, strlen(data->message), 0,
		   (struct sockaddr *)&data->client_addr, data->addr_len);

	free(data);
	pthread_exit(NULL);
}

int main() {
	int sockfd;
	char buffer[BUFF_SIZE];
	struct sockaddr_in server_addr, client_addr;
	pthread_t thread_id;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PORT);

	// binding the server
	if (bind(sockfd, (struct sockaddr *)&server_addr,
			 sizeof(struct sockaddr_in))) {
		printf("[ERROR] Unable to bind server to port: %d\n", PORT);
		exit(1);
	}

	printf("[INFO] Server bounded to port: %d\n", PORT);

	while (1) {
		socklen_t client_addr_len = sizeof(client_addr);
		ssize_t n = recvfrom(sockfd, buffer, BUFF_SIZE, 0,
							 (struct sockaddr *)&client_addr, &client_addr_len);
		buffer[n] = '\0';

		client_data_t *data = (client_data_t *)malloc(sizeof(client_data_t));
		strcpy(data->message, buffer);
		data->client_addr = client_addr;
		data->addr_len = client_addr_len;
		data->sockfd = sockfd;

		if (pthread_create(&thread_id, NULL, handle_client, (void *)data)) {
			perror("Failed to create thread\n");
			free(data);
		}

		pthread_detach(thread_id);
	}

	close(sockfd);

	return 0;
}
