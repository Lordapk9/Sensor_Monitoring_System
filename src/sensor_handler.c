#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "sensor_handler.h"
#include "log.h"

#define BUFF_SIZE 1024

void* handle_sensor_messages(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    SensorConnection* conn = &shared->sensor_connections[shared->connection_count - 1];
    char buffer[BUFF_SIZE];

    while (!shared->should_exit)
    {
        memset(buffer, 0, BUFF_SIZE);
        int bytes_read = read(conn->socket_fd, buffer, BUFF_SIZE);

        if (bytes_read <= 0)
        {
            pthread_mutex_lock(&shared->mutex);
            write_log("Sensor node %d has closed the connection", conn->id);
            shared->connected_sensors[conn->id] = 0;
            pthread_mutex_unlock(&shared->mutex);
            close(conn->socket_fd);
            break;
        }

        pthread_mutex_lock(&shared->mutex);
        int sensor_id;
        double temperature, humidity;
        if (sscanf(buffer, "SENSOR:%d,TEMP:%lf,HUM:%lf", &sensor_id, &temperature, &humidity) == 3)
        {
            shared->running_temps[sensor_id] = temperature;
            shared->running_humidity[sensor_id] = humidity;
            write_log("Sensor node %d reports temperature: %.1f, humidity: %.1f",
                      sensor_id, temperature, humidity);
        }
        else
        {
            write_log("Invalid data format from sensor node %d", conn->id);
        }
        pthread_mutex_unlock(&shared->mutex);
    }
    return NULL;
}