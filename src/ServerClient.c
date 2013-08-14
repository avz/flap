#include <unistd.h>

#include "ServerClient.h"

void ServerClient_init(struct ServerClient *client, int sock, int mode, int flags) {
	client->sock = sock;
	client->mode = mode;
	client->flags = flags;
}

void ServerClient_destroy(struct ServerClient *client) {
	if(client->sock >= 0)
		close(client->sock);

	client->sock = -1;
}
