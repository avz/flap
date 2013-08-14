#ifndef SERVERCLIENTSPOOL_H
#define	SERVERCLIENTSPOOL_H

#include <sys/types.h>
#include <pthread.h>

#include "ServerClient.h"

struct ServerClientsPool {
	struct ServerClient **clients;
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
void ServerClientsPool_init(struct ServerClientsPool *pool);
void ServerClientsPool_destroy(struct ServerClientsPool *pool);

int ServerClientsPool_attachClient(struct ServerClientsPool *pool, int sock, int mode, int flags);
void ServerClientsPool_lock(struct ServerClientsPool *pool);
void ServerClientsPool_unlock(struct ServerClientsPool *pool);
void ServerClientsPool_write(struct ServerClientsPool *pool, const void *buf, ssize_t size);

#endif	/* SERVERCLIENTSPOOL_H */
