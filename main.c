#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "log.h"
#include "connection_manager.h"
#include "storage_manager.h"
#include "sensor_handler.h"

#define FIFO_NAME "logFifo" // Name of the FIFO (named pipe) for logging
static volatile int keep_running = 1; // Flag to control the running state of the main process

// Signal handler to set the keep_running flag to 0
void handle_signal(int signum)
{
    write_log("Received signal %d, cleaning up...", signum);
    keep_running = 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Create the FIFO for logging
    if (mkfifo(FIFO_NAME, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo");
            exit(1);
        }
    }

    // Fork a new process to handle logging
    pid_t pid = fork();
    if (pid == 0)
    {
        log_process(); // Child process runs the log process
        exit(0);
    }

    SharedData shared; // Shared data between threads
    pthread_mutex_init(&shared.sensor_data.mutex, NULL); // Initialize sensor data mutex
    pthread_mutex_init(&shared.sql_data.mutex, NULL); // Initialize SQL data mutex
    memset(shared.sensor_data.connected_sensors, 0, sizeof(shared.sensor_data.connected_sensors)); // Initialize connected sensors array
    memset(shared.sensor_data.running_temps, 0, sizeof(shared.sensor_data.running_temps)); // Initialize running temperatures array
    memset(shared.sensor_data.running_humidity, 0, sizeof(shared.sensor_data.running_humidity)); // Initialize running humidity array
    shared.sql_data.sql_connected = 0; // Initialize SQL connection status
    shared.should_exit = 0; // Initialize should_exit flag
    shared.sensor_data.connection_count = 0; // Initialize connection count
    shared.port = atoi(argv[1]); // Set the server port
    shared.sql_data.sql_retry_count = 0; // Initialize SQL retry count

    // Open the SQLite database
    int rc = sqlite3_open("sensor_data.db", &shared.sql_data.db);
    if (rc)
    {
        write_log("Can't open database: %s", sqlite3_errmsg(shared.sql_data.db));
        return 1;
    }

    // Create the sensor_data table if it doesn't exist
    const char *sql = "CREATE TABLE IF NOT EXISTS sensor_data ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "sensor_id INTEGER,"
                      "temperature REAL,"
                      "humidity REAL,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char *err_msg = NULL;
    rc = sqlite3_exec(shared.sql_data.db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        write_log("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        return 1;
    }
    write_log("Server started on port %d", shared.port);

    pthread_t conn_thread, storage_thread;

    // Create the connection manager thread
    if (pthread_create(&conn_thread, NULL, connection_manager, &shared) != 0)
    {
        write_log("Failed to create connection manager thread");
        return 1;
    }

    // Create the storage manager thread
    if (pthread_create(&storage_thread, NULL, storage_manager, &shared) != 0)
    {
        write_log("Failed to create storage manager thread");
        return 1;
    }

    // Set up signal handlers for SIGINT and SIGTERM
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Wait for the threads to finish
    pthread_join(conn_thread, NULL);
    pthread_join(storage_thread, NULL);

    // Clean up resources
    pthread_mutex_destroy(&shared.sensor_data.mutex);
    pthread_mutex_destroy(&shared.sql_data.mutex);
    sqlite3_close(shared.sql_data.db);
    unlink(FIFO_NAME);

    return 0;
}