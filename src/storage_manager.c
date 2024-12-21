#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>
#include "storage_manager.h"
#include "log.h"

void insert_sensor_data(SharedData* shared, int sensor_id, double temperature, double humidity)
{
    if (!shared->sql_connected)
    {
        return;
    }

    pthread_mutex_lock(&shared->sql_mutex);

    const char *sql = "INSERT INTO sensor_data (sensor_id, temperature, humidity, timestamp) "
                      "VALUES (?, ?, ?, datetime('now', '+7 hours'));";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(shared->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        write_log("Failed to prepare statement: %s", sqlite3_errmsg(shared->db));
        pthread_mutex_unlock(&shared->sql_mutex);
        return;
    }

    sqlite3_bind_int(stmt, 1, sensor_id);
    sqlite3_bind_double(stmt, 2, temperature);
    sqlite3_bind_double(stmt, 3, humidity);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        write_log("Failed to insert data: %s", sqlite3_errmsg(shared->db));
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&shared->sql_mutex);
}

void* storage_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    int retry_count = 0;
    const int MAX_RETRIES = 3;

    while (!shared->should_exit)
    {
        pthread_mutex_lock(&shared->mutex);

        if (!shared->sql_connected)
        {
            if (retry_count < MAX_RETRIES)
            {
                if (shared->db)
                {
                    sqlite3_close(shared->db);
                    shared->db = NULL;
                }

                int rc = sqlite3_open("sensor_data.db", &shared->db);
                if (rc == SQLITE_OK)
                {
                    shared->sql_connected = 1;
                    retry_count = 0;
                    write_log("Connection to SQL server established");

                    const char *create_table_sql =
                        "CREATE TABLE IF NOT EXISTS sensor_data ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "sensor_id INTEGER,"
                        "temperature REAL,"
                        "humidity REAL,"
                        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

                    char *err_msg = NULL;
                    rc = sqlite3_exec(shared->db, create_table_sql, NULL, NULL, &err_msg);
                    if (rc != SQLITE_OK)
                    {
                        write_log("SQL error: %s", err_msg);
                        sqlite3_free(err_msg);
                    }
                    else
                    {
                        write_log("New table sensor_data created");
                    }
                }
                else
                {
                    retry_count++;
                    write_log("Unable to connect to SQL server (attempt %d of %d)",
                              retry_count, MAX_RETRIES);
                }
            }
        }

        if (shared->sql_connected)
        {
            for (int i = 0; i < shared->connection_count; i++)
            {
                int sensor_id = shared->sensor_connections[i].id;
                if (shared->connected_sensors[sensor_id])
                {
                    double temp = shared->running_temps[sensor_id];
                    double humidity = shared->running_humidity[sensor_id];
                    insert_sensor_data(shared, sensor_id, temp, humidity);
                }
            }
        }

        pthread_mutex_unlock(&shared->mutex);
        sleep(5);
    }

    if (shared->db)
    {
        sqlite3_close(shared->db);
        shared->db = NULL;
    }

    return NULL;
}