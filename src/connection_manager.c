#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "connection_manager.h"
#include "log.h"
#include "sensor_handler.h"
#include "socket_utils.h"

#define BUFF_SIZE 1024
#define LISTEN_BACKLOG 5

// Function to manage connections from sensor nodes
void* connection_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg; // Shared data between threads
    struct sockaddr_in client_addr; // Client address structure

    // Create server socket
    int server_fd = create_server_socket(shared->port);
    if (server_fd == -1)
    {
        return NULL; // Exit if socket creation fails
    }

    // Main loop to accept connections from sensor nodes
    while (!shared->should_exit)
    {
        // Accept connection from client
        int client_fd = accept_client_connection(server_fd, &client_addr);
        if (client_fd == -1)
        {
            continue; // Continue loop if connection acceptance fails
        }

        char buffer[BUFF_SIZE];
        memset(buffer, 0, BUFF_SIZE); // Clear buffer
        int bytes_read = read(client_fd, buffer, BUFF_SIZE); // Read data from client
        if (bytes_read <= 0)
        {
            close(client_fd); // Close connection if read fails
            continue;
        }

        int sensor_id;
        // Check sensor ID format
        if (sscanf(buffer, "ID:%d", &sensor_id) != 1)
        {
            write_log("Invalid sensor ID format"); // Log if ID format is invalid
            close(client_fd); // Close connection
            continue;
        }

        pthread_mutex_lock(&shared->sensor_data.mutex); // Lock mutex to access shared data

        int id_exists = 0;
        // Check if sensor ID already exists
        for (int i = 0; i < shared->sensor_data.connection_count; i++)
        {
            if (shared->sensor_data.sensor_connections[i].id == sensor_id)
            {
                id_exists = 1;
                break;
            }
        }

        if (id_exists)
        {
            write_log("Sensor node %d already connected", sensor_id); // Log if sensor is already connected
            close(client_fd); // Close connection
            pthread_mutex_unlock(&shared->sensor_data.mutex); // Unlock mutex
            continue;
        }

        // Check if maximum number of sensors is reached
        if (shared->sensor_data.connection_count >= MAX_SENSORS)
        {
            write_log("Maximum number of sensors reached"); // Log if max sensors reached
            close(client_fd); // Close connection
            pthread_mutex_unlock(&shared->sensor_data.mutex); // Unlock mutex
            continue;
        }

        // Add new sensor connection to the list
        SensorConnection* new_conn = &shared->sensor_data.sensor_connections[shared->sensor_data.connection_count];
        new_conn->id = sensor_id;
        new_conn->socket_fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, new_conn->ip, INET_ADDRSTRLEN); // Get client IP address
        new_conn->port = ntohs(client_addr.sin_port); // Get client port

        shared->sensor_data.connected_sensors[sensor_id] = 1; // Mark sensor as connected
        write_log("Sensor node %d has opened a new connection from %s:%d",
                  sensor_id, new_conn->ip, new_conn->port); // Log new connection info

        pthread_t tid;
        // Create thread to handle messages from sensor
        if (pthread_create(&tid, NULL, handle_sensor_messages, shared) != 0)
        {
            write_log("Failed to create handler thread for sensor %d", sensor_id); // Log if thread creation fails
            close(client_fd); // Close connection
            shared->sensor_data.connected_sensors[sensor_id] = 0; // Mark sensor as not connected
        }
        else
        {
            pthread_detach(tid); // Detach thread to automatically free resources when done
            shared->sensor_data.connection_count++; // Increase sensor connection count
        }

        pthread_mutex_unlock(&shared->sensor_data.mutex); // Unlock mutex
    }

    close(server_fd); // Close server socket when exiting loop
    return NULL;
}