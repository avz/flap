#ifndef SERVERCLIENT_H
#define	SERVERCLIENT_H

struct ServerClient {
	int sock;

	/**
	 * only O_RDONLY allowed now
	 */
	int mode;

	int flags;
};

void ServerClient_init(struct ServerClient *client, int sock, int mode, int flags);
void ServerClient_destroy(struct ServerClient *client);

#endif	/* SERVERCLIENT_H */
