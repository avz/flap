#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "ServerClientsPool.h"

static void ServerClientsPool__removeClient(struct ServerClientsPool *pool, struct ServerClient *client) {
	size_t i;
	for(i = 0; i < pool->clientsAllocated; i++) {
		if(pool->clients[i] != client)
			continue;

		ServerClient_destroy(client);
		free(client);
		pool->clients[i] = NULL;
		pool->clientsCount--;

		break;
	}
}

static void ServerClientsPool__enlarge(struct ServerClientsPool *pool) {
	struct ServerClient **newArray;

	size_t newSize = pool->clientsAllocated * 2;
	if(!newSize)
		newSize = 2;

	newArray = (struct ServerClient **)malloc(sizeof(*newArray) * newSize);
	memset(newArray, 0, (sizeof(*newArray) * newSize));

	if(pool->clients)
		memcpy(newArray, pool->clients, pool->clientsAllocated * sizeof(*pool->clients));

	pool->clients = newArray;
	pool->clientsAllocated = newSize;
}

void ServerClientsPool_init(struct ServerClientsPool *pool) {
	pool->clients = NULL;
	pool->clientsAllocated = 0;
	pool->clientsCount = 0;

	pthread_mutex_init(&pool->mutex, NULL);

	ServerClientsPool__enlarge(pool);
}

void ServerClientsPool_destroy(struct ServerClientsPool *pool) {
	size_t i;

	if(!pool->clients)
		return;

	for(i = 0; i < pool->clientsAllocated; i++) {
		if(!pool->clients[i])
			continue;

		ServerClient_destroy(pool->clients[i]);
		free(pool->clients[i]);
	}

	free(pool->clients);
	pool->clients = NULL;
	pool->clientsAllocated = 0;
	pool->clientsCount = 0;

	pthread_mutex_destroy(&pool->mutex);
}

int ServerClientsPool_attachClient(struct ServerClientsPool *pool, int sock, int mode, int flags) {
	size_t i;
	struct ServerClient *client;

	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		perror("fcntl(sock, F_SETFL, O_NONBLOCK)");
		return errno;
	}

	if(pool->clientsCount + 1 > pool->clientsAllocated)
		ServerClientsPool__enlarge(pool);

	client = malloc(sizeof(*client));
	ServerClient_init(client, sock, mode, flags);

	for(i = 0; i < pool->clientsAllocated; i++) {
		if(!pool->clients[i]) {
			pool->clients[i] = client;
			break;
		}
	}

	pool->clientsCount++;

	return 0;
}

void ServerClientsPool_lock(struct ServerClientsPool *pool) {
	if(pthread_mutex_lock(&pool->mutex) != 0) {
		perror("pthread_mutex_lock()");
		exit(errno);
	}
}

void ServerClientsPool_unlock(struct ServerClientsPool *pool) {
	if(pthread_mutex_unlock(&pool->mutex) != 0) {
		perror("pthread_mutex_unlock()");
		exit(errno);
	}
}

void ServerClientsPool_write(struct ServerClientsPool *pool, const void *buf, ssize_t size) {
	size_t i;
	ssize_t w;

	size_t count = 0;

	ServerClientsPool_lock(pool);

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

			ServerClientsPool__removeClient(pool, pool->clients[i]);
		}
	}

	ServerClientsPool_unlock(pool);
}
