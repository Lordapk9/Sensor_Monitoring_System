#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>
#include <math.h>
#include "storage_manager.h"
#include "log.h"

#define DUPLICATE_TIME_LIMIT "-10 seconds" // Prevent duplicate data within 10 seconds
#define FLOAT_TOLERANCE 0.01  // Precision tolerance for temperature/humidity

// Function to insert sensor data into the database
void insert_sensor_data(SharedData* shared, int sensor_id, double temperature, double humidity)
{
    if (!shared->sql_data.sql_connected)
    {
        return; // Exit if not connected to the database
    }

    pthread_mutex_lock(&shared->sql_data.mutex); // Lock the mutex to access the database

    // Check if the exact same data already exists (last inserted value)
    const char *check_sql = "SELECT COUNT(*) FROM sensor_data WHERE sensor_id = ? "
                            "AND temperature = ? AND humidity = ? "
                            "AND timestamp >= datetime('now', ?, 'localtime');";

    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(shared->sql_data.db, check_sql, -1, &check_stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int(check_stmt, 1, sensor_id);
        sqlite3_bind_double(check_stmt, 2, temperature);
        sqlite3_bind_double(check_stmt, 3, humidity);
        sqlite3_bind_text(check_stmt, 4, DUPLICATE_TIME_LIMIT, -1, SQLITE_STATIC);

        if (sqlite3_step(check_stmt) == SQLITE_ROW && sqlite3_column_int(check_stmt, 0) > 0)
        {
            write_log("Skipping duplicate data for sensor %d", sensor_id); // Log duplicate data
            sqlite3_finalize(check_stmt);
            pthread_mutex_unlock(&shared->sql_data.mutex); // Unlock the mutex
            return; // Exit if duplicate data is found
        }
    }
    sqlite3_finalize(check_stmt);

    // Insert new data if no duplicate found
    const char *insert_sql = "INSERT INTO sensor_data (sensor_id, temperature, humidity, timestamp) "
                             "VALUES (?, ?, ?, datetime('now', 'localtime'));";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(shared->sql_data.db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        write_log("Failed to prepare insert statement: %s", sqlite3_errmsg(shared->sql_data.db)); // Log error
        pthread_mutex_unlock(&shared->sql_data.mutex); // Unlock the mutex
        return;
    }

    sqlite3_bind_int(stmt, 1, sensor_id);
    sqlite3_bind_double(stmt, 2, temperature);
    sqlite3_bind_double(stmt, 3, humidity);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        write_log("Failed to insert data: %s", sqlite3_errmsg(shared->sql_data.db)); // Log error
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&shared->sql_data.mutex); // Unlock the mutex
}

// Function to manage storage of sensor data
void* storage_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    int retry_count = 0;
    const int MAX_RETRIES = 3;

    double last_temp[MAX_SENSORS] = {0.0}; // Array to store last temperature values
    double last_humidity[MAX_SENSORS] = {0.0}; // Array to store last humidity values

    while (!shared->should_exit)
    {
        pthread_mutex_lock(&shared->sensor_data.mutex); // Lock the mutex to access shared data

        if (!shared->sql_data.sql_connected)
        {
            if (retry_count < MAX_RETRIES)
            {
                if (shared->sql_data.db)
                {
                    sqlite3_close(shared->sql_data.db); // Close the database if open
                    shared->sql_data.db = NULL;
                }

                int rc = sqlite3_open("sensor_data.db", &shared->sql_data.db);
                if (rc == SQLITE_OK)
                {
                    sqlite3_busy_timeout(shared->sql_data.db, 5000);  // Prevent lock issues
                    shared->sql_data.sql_connected = 1;
                    retry_count = 0;
                    write_log("Connection to SQL server established");

                    const char *create_table_sql =
                        "CREATE TABLE IF NOT EXISTS sensor_data ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "sensor_id INTEGER,"
                        "temperature REAL,"
                        "humidity REAL,"
                        "timestamp TEXT UNIQUE DEFAULT (datetime('now', 'localtime')));";

                    char *err_msg = NULL;
                    rc = sqlite3_exec(shared->sql_data.db, create_table_sql, NULL, NULL, &err_msg);
                    if (rc != SQLITE_OK)
                    {
                        write_log("SQL error: %s", err_msg); // Log error
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

        if (shared->sql_data.sql_connected)
        {
            for (int i = 0; i < shared->sensor_data.connection_count; i++)
            {
                int sensor_id = shared->sensor_data.sensor_connections[i].id;
                if (shared->sensor_data.connected_sensors[sensor_id])
                {
                    double temp = shared->sensor_data.running_temps[sensor_id];
                    double humidity = shared->sensor_data.running_humidity[sensor_id];
                    // Only insert if there's a significant change in value
                    if (fabs(temp - last_temp[sensor_id]) > FLOAT_TOLERANCE ||
                        fabs(humidity - last_humidity[sensor_id]) > FLOAT_TOLERANCE)
                    {
                        insert_sensor_data(shared, sensor_id, temp, humidity);
                        last_temp[sensor_id] = temp;
                        last_humidity[sensor_id] = humidity;
                    }
                }
            }
        }

        pthread_mutex_unlock(&shared->sensor_data.mutex); // Unlock the mutex
        sleep(5); // Sleep for 5 seconds before the next iteration
    }

    if (shared->sql_data.db)
    {
        sqlite3_close(shared->sql_data.db); // Close the database when exiting
        shared->sql_data.db = NULL;
    }

    return NULL;
}
