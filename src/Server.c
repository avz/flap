#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include "Server.h"

/**
 * Поток, который принимает коннекты на сокет и добавляет их в пул
 * @param serverPtr
 * @return
 */
static void *Server__connectionPollerThread(void *serverPtr) {
	struct Server *server = (struct Server *)serverPtr;
	int s;
	int bufSize = 1024 * 1024;

	while((s = accept(server->sock, NULL, NULL)) >= 0) {
		if(setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) != 0)
			perror("setsockopt(s, SOL_SOCKET, SO_SNDBUF)");

		ServerClientsPool_lock(&server->pool);
		ServerClientsPool_attachClient(&server->pool, s, O_RDONLY, 0);
		ServerClientsPool_unlock(&server->pool);
	}

	return NULL;
}

void Server_init(struct Server *server) {
	server->sock = -1;
	ServerClientsPool_init(&server->pool);
}

void Server_bindUnix(struct Server *server, const char *path, char forceReuse) {
	struct sockaddr_un addr;

	if(strlen(path) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		perror(path);
		exit(errno);
	}

	server->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(server->sock == -1) {
		perror("socket(AF_UNIX, SOCK_STREAM, 0)");
		exit(errno);
	}

	strncpy(addr.sun_path, path, sizeof(addr.sun_path));
	addr.sun_family = AF_UNIX;

	if(forceReuse) {
		if(unlink(addr.sun_path) != 0 && errno != ENOENT) {
			perror(addr.sun_path);
			exit(errno);
		}
	}

	if(bind(server->sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror(addr.sun_path);
		exit(errno);
	}

	if(listen(server->sock, 32) != 0) {
		perror("listen()");
		exit(errno);
	}

	if(pthread_create(&server->connectionPollerThread, NULL, Server__connectionPollerThread, server) != 0) {
		perror("pthread_create(Server__connectionPollerThread)");
		exit(errno);
	}
}

void Server_write(struct Server *server, const void *buf, ssize_t len) {
	ServerClientsPool_write(&server->pool, buf, len);
}

void Server_destroy(struct Server *server) {
	if(server->sock >= 0)
		close(server->sock);

	pthread_cancel(server->connectionPollerThread);

	ServerClientsPool_destroy(&server->pool);
}
