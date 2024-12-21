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
#define FIFO_NAME "logFifo"
static volatile int keep_running = 1;

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

    if (mkfifo(FIFO_NAME, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo");
            exit(1);
        }
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        log_process();
        exit(0);
    }
    SharedData shared;
    pthread_mutex_init(&shared.mutex, NULL);
    pthread_mutex_init(&shared.conn_mutex, NULL);
    pthread_mutex_init(&shared.sql_mutex, NULL);
    memset(shared.connected_sensors, 0, sizeof(shared.connected_sensors));
    memset(shared.running_temps, 0, sizeof(shared.running_temps));
    memset(shared.running_humidity, 0, sizeof(shared.running_humidity));
    shared.sql_connected = 0;
    shared.should_exit = 0;
    shared.connection_count = 0;
    shared.port = atoi(argv[1]);
    shared.sql_retry_count = 0;

    int rc = sqlite3_open("sensor_data.db", &shared.db);
    if (rc)
    {
        write_log("Can't open database: %s", sqlite3_errmsg(shared.db));
        return 1;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS sensor_data ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "sensor_id INTEGER,"
                      "temperature REAL,"
                      "humidity REAL,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char *err_msg = NULL;
    rc = sqlite3_exec(shared.db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        write_log("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        return 1;
    }
    write_log("Server started on port %d", shared.port);

    pthread_t conn_thread, storage_thread;

    if (pthread_create(&conn_thread, NULL, connection_manager, &shared) != 0)
    {
        write_log("Failed to create connection manager thread");
        return 1;
    }

    if (pthread_create(&storage_thread, NULL, storage_manager, &shared) != 0)
    {
        write_log("Failed to create storage manager thread");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    pthread_join(conn_thread, NULL);
    pthread_join(storage_thread, NULL);

    pthread_mutex_destroy(&shared.mutex);
    pthread_mutex_destroy(&shared.conn_mutex);
    pthread_mutex_destroy(&shared.sql_mutex);
    sqlite3_close(shared.db);
    unlink(FIFO_NAME);

    return 0;
}