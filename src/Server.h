#ifndef SERVER_H
#define	SERVER_H

#include <pthread.h>

#include "ServerClientsPool.h"

struct Server {
	int sock;

	struct ServerClientsPool pool;

	pthread_t connectionPollerThread;
};

void Server_init(struct Server *server);
void Server_destroy(struct Server *server);
void Server_bindUnix(struct Server *server, const char *path, char forceReuse);
void Server_write(struct Server *server, const void *buf, ssize_t len);

#endif	/* SERVER_H */
