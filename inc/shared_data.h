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
} SensorConnection;

typedef struct
{
    pthread_mutex_t mutex;
    SensorConnection sensor_connections[MAX_SENSORS];
    int connected_sensors[MAX_SENSORS];
    double running_temps[MAX_SENSORS];
    double running_humidity[MAX_SENSORS];
    int connection_count;
} SensorData;

typedef struct
{
    pthread_mutex_t mutex;
    sqlite3 *db;
    int sql_connected;
    int sql_retry_count;
} SQLData;

typedef struct
{
    pthread_mutex_t mutex;
    int should_exit;
    int port;
    SensorData sensor_data;
    SQLData sql_data;
} SharedData;

#endif // SHARED_DATA_H