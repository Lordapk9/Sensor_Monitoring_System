#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "sensor_handler.h"
#include "log.h"
#include "storage_manager.h"

#define BUFF_SIZE 1024 // Buffer size for reading messages

// Function to handle messages from a sensor node
void* handle_sensor_messages(void* arg)
{
    SharedData* shared = (SharedData*)arg; // Shared data between threads
    SensorConnection* conn = &shared->sensor_data.sensor_connections[shared->sensor_data.connection_count - 1]; // Get the latest sensor connection
    char buffer[BUFF_SIZE]; // Buffer to hold incoming messages

    while (!shared->should_exit)
    {
        memset(buffer, 0, BUFF_SIZE); // Clear the buffer
        int bytes_read = read(conn->socket_fd, buffer, BUFF_SIZE); // Read data from the sensor node

        if (bytes_read <= 0)
        {
            // If read fails, log the disconnection and update the shared data
            pthread_mutex_lock(&shared->sensor_data.mutex);
            write_log("Sensor node %d has closed the connection", conn->id);
            shared->sensor_data.connected_sensors[conn->id] = 0;
            pthread_mutex_unlock(&shared->sensor_data.mutex);
            close(conn->socket_fd); // Close the socket
            break; // Exit the loop
        }

        pthread_mutex_lock(&shared->sensor_data.mutex); // Lock the mutex to access shared data
        int sensor_id;
        double temperature, humidity;
        // Parse the incoming message
        if (sscanf(buffer, "SENSOR:%d,TEMP:%lf,HUM:%lf", &sensor_id, &temperature, &humidity) == 3)
        {
            // Update the shared data with the new sensor readings
            shared->sensor_data.running_temps[sensor_id] = temperature;
            shared->sensor_data.running_humidity[sensor_id] = humidity;
            write_log("Sensor node %d reports temperature: %.1f, humidity: %.1f",
                      sensor_id, temperature, humidity);

            // Insert the sensor data into the database immediately
            insert_sensor_data(shared, sensor_id, temperature, humidity);
        }
        else
        {
            // Log an error if the data format is invalid
            write_log("Invalid data format from sensor node %d", conn->id);
        }
        pthread_mutex_unlock(&shared->sensor_data.mutex); // Unlock the mutex
    }
    return NULL; // Return NULL when done
}