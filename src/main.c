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

#include "Server.h"

void usageAndExit(const char *cmd) {
	fprintf(stderr,
		"Usage: %s (-s|-c) /path/to/socket.sock\n"
		"	-s: server mode\n"
		"	-c: client mode (read-only)\n"
		, cmd
	);

	exit(255);
}

int runClientMode(const char *sockPath) {
	char buf[16 * 1024];
	struct sockaddr_un addr;
	int sock;
	int sockBufSize = 1024 * 1024;

	if(strlen(sockPath) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		perror(sockPath);
		exit(errno);
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock == -1) {
		perror("socket(AF_UNIX, SOCK_STREAM, 0)");
		exit(errno);
	}

	strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path));
	addr.sun_family = AF_UNIX;

	if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror(sockPath);
		return errno;
	}

	if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sockBufSize, sizeof(sockBufSize)) != 0)
		perror("setsockopt(sock, SOL_SOCKET, SO_SNDBUF)");

	while(1) {
		ssize_t written = 0;
		ssize_t r = read(sock, buf, sizeof(buf));
		if(r <= 0) {
			if(r < 0 && errno == EINTR)
				continue;

			return r ? errno : 0;
		}

		while(written < r) {
			ssize_t w = write(STDOUT_FILENO, buf + written, (size_t)(r - written));
			if(w <= 0) {
				perror("read(STDIN_FILENO)");
				return errno;
			} else {
				written += w;
			}
		}
	}

	/* never reached */
	return 0;
}

int runServerMode(const char *sockPath) {
	char buf[32 * 1024];
	struct Server server;
	char eof = 0;

	signal(SIGPIPE, SIG_IGN);

	Server_init(&server);
	Server_bindUnix(&server, sockPath, 1);

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

				perror("write(STDOUT_FILENO)");
				return errno;
			} else {
				written += w;
			}
		}

		Server_write(&server, buf, r);
	}

	Server_destroy(&server);

	return 0;
}

int main(int argc, char *argv[]) {
	int opt;
	char serverMode = 0;
	char clientMode = 0;

	while((opt = getopt(argc, argv, "sc")) != -1) {
		switch(opt) {
			case 's':
				serverMode = 1;
			break;
			case 'c':
				clientMode = 1;
			break;
			default:
				usageAndExit(argv[0]);
		}
	}

	if(optind > argc)
		usageAndExit(argv[0]);

	if((serverMode && clientMode) || (!serverMode && !clientMode))
		usageAndExit(argv[0]);

	if(serverMode) {
		runServerMode(argv[optind]);
	} else if(clientMode) {
		runClientMode(argv[optind]);
	}

	return EXIT_SUCCESS;
}
