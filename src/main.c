#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

struct Client {
	int sock;

	/**
	 * only O_RDONLY allowed now
	 */
	int mode;

	int flags;
};

void Client_init(struct Client *client, int sock, int mode, int flags) {
	client->sock = sock;
	client->mode = mode;
	client->flags = flags;
}

void Client_destroy(struct Client *client) {
	if(client->sock >= 0)
		close(client->sock);

	client->sock = -1;
}

struct ClientsPool {
	struct Client **clients;
	/**
	 * number of allocated pointers
	 */
	size_t clientsAllocated;

	/**
	 * number of used Client structs
	 */
	size_t clientsCount;

	pthread_mutex_t mutex;
};

void ClientsPool_init(struct ClientsPool *pool);
int ClientsPool_attachClient(struct ClientsPool *pool, int sock, int mode, int flags);

void ClientsPool__enlarge(struct ClientsPool *pool);
void ClientsPool__removeClient(struct ClientsPool *pool, struct Client *client) {
	size_t i;
	for(i = 0; i < pool->clientsAllocated; i++) {
		if(pool->clients[i] != client)
			continue;

		Client_destroy(client);
		free(client);
		pool->clients[i] = NULL;
		pool->clientsCount--;

		break;
	}
}

void ClientsPool_init(struct ClientsPool *pool) {
	pool->clients = NULL;
	pool->clientsAllocated = 0;
	pool->clientsCount = 0;

	pthread_mutex_init(&pool->mutex, NULL);

	ClientsPool__enlarge(pool);
}

void ClientsPool_destroy(struct ClientsPool *pool) {
	size_t i;

	if(!pool->clients)
		return;

	for(i = 0; i < pool->clientsAllocated; i++) {
		if(!pool->clients[i])
			continue;

		Client_destroy(pool->clients[i]);
		free(pool->clients[i]);
	}

	free(pool->clients);
	pool->clients = NULL;
	pool->clientsAllocated = 0;
	pool->clientsCount = 0;

	pthread_mutex_destroy(&pool->mutex);
}

void ClientsPool__enlarge(struct ClientsPool *pool) {
	struct Client **newArray;

	size_t newSize = pool->clientsAllocated * 2;
	if(!newSize)
		newSize = 2;

	newArray = (struct Client **)malloc(sizeof(*newArray) * newSize);
	memset(newArray, 0, (sizeof(*newArray) * newSize));

	if(pool->clients)
		memcpy(newArray, pool->clients, pool->clientsAllocated * sizeof(*pool->clients));

	pool->clients = newArray;
	pool->clientsAllocated = newSize;
}

int ClientsPool_attachClient(struct ClientsPool *pool, int sock, int mode, int flags) {
	size_t i;
	struct Client *client;

	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		perror("fcntl(sock, F_SETFL, O_NONBLOCK)");
		return errno;
	}

	if(pool->clientsCount + 1 > pool->clientsAllocated)
		ClientsPool__enlarge(pool);

	client = malloc(sizeof(*client));
	Client_init(client, sock, mode, flags);

	for(i = 0; i < pool->clientsAllocated; i++) {
		if(!pool->clients[i]) {
			pool->clients[i] = client;
			break;
		}
	}

	pool->clientsCount++;

	return 0;
}

void ClientsPool__lock(struct ClientsPool *pool) {
	if(pthread_mutex_lock(&pool->mutex) != 0) {
		perror("pthread_mutex_lock()");
		exit(errno);
	}
}

void ClientsPool__unlock(struct ClientsPool *pool) {
	if(pthread_mutex_unlock(&pool->mutex) != 0) {
		perror("pthread_mutex_unlock()");
		exit(errno);
	}
}

void ClientsPool_write(struct ClientsPool *pool, const void *buf, ssize_t size) {
	size_t i;
	ssize_t w;

	size_t count = 0;

	ClientsPool__lock(pool);

	for(i = 0; i < pool->clientsAllocated && count < pool->clientsCount; i++) {
		if(!pool->clients[i])
			continue;

		count++;

		w = write(pool->clients[i]->sock, buf, (size_t)size);
		/*
		 * не страшно если записалось не всё или прервали сигналом,
		 * эту ситуацию мы просто пропускаем и считаем, что клиентне успел
		 * прочитать данные.
		 * В случае любой другой ошибки закрываем сокет
		 */

		if(w == -1) {
			if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
				continue;

			/*perror("write(sock)");*/

			ClientsPool__removeClient(pool, pool->clients[i]);
		}
	}

	ClientsPool__unlock(pool);
}

struct Server {
	int sock;

	struct ClientsPool pool;

	pthread_t connectionPollerThread;
};

/**
 * Поток, который принимает коннекты на сокет и добавляет их в пул
 * @param serverPtr
 * @return
 */
void *Server__connectionPollerThread(void *serverPtr) {
	struct Server *server = (struct Server *)serverPtr;
	int s;

	while((s = accept(server->sock, NULL, NULL)) >= 0) {
		ClientsPool__lock(&server->pool);
		ClientsPool_attachClient(&server->pool, s, O_RDONLY, 0);
		ClientsPool__unlock(&server->pool);
		fprintf(stderr, "New connection: %d\n", s);
	}

	return NULL;
}

void Server_init(struct Server *server) {
	server->sock = -1;
	ClientsPool_init(&server->pool);
}

void Server_bindUnix(struct Server *server, const char *path, char forceReuse) {
	struct sockaddr_un addr;

	if(strlen(path) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		perror("Path to Unix socket is too long");
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
	ClientsPool_write(&server->pool, buf, len);
}

void Server_destroy(struct Server *server) {
	if(server->sock >= 0)
		close(server->sock);

	pthread_cancel(server->connectionPollerThread);

	ClientsPool_destroy(&server->pool);
}

int main(int argc, char *argv[]) {
	char buf[16 * 1024];
	struct Server server;
	char eof = 0;

	signal(SIGPIPE, SIG_IGN);

	Server_init(&server);
	Server_bindUnix(&server, argv[1], 1);

	while(!eof) {
		ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
		ssize_t written = 0;

		if(r <= 0) {
			if(r == 0)
				break;

			if(errno == EINTR)
				continue;

			perror("read(STDIN_FILENO)");
			return errno;
		}

		while(written < r && !eof) {
			ssize_t w = write(STDOUT_FILENO, buf + written, (size_t)(r - written));
			if(w <= 0) {
				if(w == -1 && errno == EPIPE) {
					eof = 1;
					break;
				}

				perror("read(STDIN_FILENO)");
				return errno;
			} else {
				written += w;
			}
		}

		Server_write(&server, buf, r);
	}

	Server_destroy(&server);

	return EXIT_SUCCESS;
}
