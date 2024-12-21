#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <pthread.h>
#include <sqlite3.h>
#include <netinet/in.h>

#define MAX_SENSORS 10

typedef struct
{
    int id;
    int socket_fd;
    char ip[INET_ADDRSTRLEN];
    int port;
    double humidity;
} SensorConnection;

typedef struct
{
    pthread_mutex_t mutex;
    int connected_sensors[MAX_SENSORS];
    double running_temps[MAX_SENSORS];
    double running_humidity[MAX_SENSORS];
    int sql_connected;
    int should_exit;
    SensorConnection sensor_connections[MAX_SENSORS];
    int connection_count;
    pthread_mutex_t conn_mutex;
    int port;
    sqlite3 *db;
    int sql_retry_count;
    pthread_mutex_t sql_mutex;
} SharedData;

#endif // SHARED_DATA_H