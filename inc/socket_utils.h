#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <netinet/in.h>

int create_server_socket(int port);
int accept_client_connection(int server_fd, struct sockaddr_in *client_addr);

#endif // SOCKET_UTILS_H