#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "connection_manager.h"
#include "log.h"
#include "sensor_handler.h"

#define BUFF_SIZE 1024
#define LISTEN_BACKLOG 5

void* connection_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        write_log("Failed to create socket");
        return NULL;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        write_log("Failed to set socket options");
        close(server_fd);
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(shared->port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        write_log("Failed to bind socket");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, LISTEN_BACKLOG) == -1)
    {
        write_log("Failed to listen on socket");
        close(server_fd);
        return NULL;
    }

    write_log("Connection manager listening on port %d", shared->port);

    while (!shared->should_exit)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1)
        {
            write_log("Failed to accept connection");
            continue;
        }

        char buffer[BUFF_SIZE];
        memset(buffer, 0, BUFF_SIZE);
        int bytes_read = read(client_fd, buffer, BUFF_SIZE);
        if (bytes_read <= 0)
        {
            close(client_fd);
            continue;
        }

        int sensor_id;
        if (sscanf(buffer, "ID:%d", &sensor_id) != 1)
        {
            write_log("Invalid sensor ID format");
            close(client_fd);
            continue;
        }

        pthread_mutex_lock(&shared->mutex);

        int id_exists = 0;
        for (int i = 0; i < shared->connection_count; i++)
        {
            if (shared->sensor_connections[i].id == sensor_id)
            {
                id_exists = 1;
                break;
            }
        }

        if (id_exists)
        {
            write_log("Sensor node %d already connected", sensor_id);
            close(client_fd);
            pthread_mutex_unlock(&shared->mutex);
            continue;
        }

        if (shared->connection_count >= MAX_SENSORS)
        {
            write_log("Maximum number of sensors reached");
            close(client_fd);
            pthread_mutex_unlock(&shared->mutex);
            continue;
        }

        SensorConnection* new_conn = &shared->sensor_connections[shared->connection_count];
        new_conn->id = sensor_id;
        new_conn->socket_fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, new_conn->ip, INET_ADDRSTRLEN);
        new_conn->port = ntohs(client_addr.sin_port);

        shared->connected_sensors[sensor_id] = 1;
        write_log("Sensor node %d has opened a new connection from %s:%d",
                  sensor_id, new_conn->ip, new_conn->port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_sensor_messages, shared) != 0)
        {
            write_log("Failed to create handler thread for sensor %d", sensor_id);
            close(client_fd);
            shared->connected_sensors[sensor_id] = 0;
        }
        else
        {
            pthread_detach(tid);
            shared->connection_count++;
        }

        pthread_mutex_unlock(&shared->mutex);
    }

    close(server_fd);
    return NULL;
}