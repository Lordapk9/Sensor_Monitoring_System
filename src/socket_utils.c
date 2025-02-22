#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "socket_utils.h"
#include "log.h"

#define LISTEN_BACKLOG 5 // Maximum length to which the queue of pending connections may grow

// Function to create a server socket
int create_server_socket(int port)
{
    int server_fd;
    struct sockaddr_in server_addr;

    // Create a socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        write_log("Failed to create socket"); // Log if socket creation fails
        return -1;
    }

    int opt = 1;
    // Set socket options to reuse the address
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        write_log("Failed to set socket options"); // Log if setting socket options fails
        close(server_fd); // Close the socket
        return -1;
    }

    // Initialize the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket to the specified port
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        write_log("Failed to bind socket"); // Log if binding fails
        close(server_fd); // Close the socket
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, LISTEN_BACKLOG) == -1)
    {
        write_log("Failed to listen on socket"); // Log if listening fails
        close(server_fd); // Close the socket
        return -1;
    }

    write_log("Server listening on port %d", port); // Log that the server is listening
    return server_fd; // Return the server file descriptor
}

// Function to accept a client connection
int accept_client_connection(int server_fd, struct sockaddr_in *client_addr)
{
    socklen_t client_len = sizeof(*client_addr);
    // Accept an incoming connection
    int client_fd = accept(server_fd, (struct sockaddr*)client_addr, &client_len);
    if (client_fd == -1)
    {
        write_log("Failed to accept connection"); // Log if accepting connection fails
    }
    return client_fd; // Return the client file descriptor
}