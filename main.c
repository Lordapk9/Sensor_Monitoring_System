#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sqlite3.h>
#include <signal.h>

#define FIFO_NAME "logFifo"
#define MAX_LOG_MSG 256
#define MAX_SENSORS 10
#define BUFF_SIZE 1024
#define LISTEN_BACKLOG 5

// Function declarations
void write_log(const char* format, ...);

// Global variables
static volatile int keep_running = 1;

// Function to handle signals and perform cleanup
void handle_signal(int signum)
{
    write_log("Received signal %d, cleaning up...", signum);
    keep_running = 0;
}

// Define the sensor connection structure
typedef struct
{
    int id;  // Sensor ID
    int socket_fd;  // Socket file descriptor
    char ip[INET_ADDRSTRLEN];  // IP address
    int port;  // Port number
    double humidity;  // Humidity value
} SensorConnection;

// Forward declaration of the handler function
void *handle_sensor_messages(void *arg);

// Define the shared data structure
typedef struct
{
    pthread_mutex_t mutex;  // Mutex for thread synchronization
    int connected_sensors[MAX_SENSORS];  // Track sensor connection status
    double running_temps[MAX_SENSORS];  // Store sensor temperatures
    double running_humidity[MAX_SENSORS];  // Store sensor humidity
    int sql_connected;  // SQL connection status
    int should_exit;  // Program exit flag
    SensorConnection sensor_connections[MAX_SENSORS];  // Sensor connections
    int connection_count;  // Number of active connections
    pthread_mutex_t conn_mutex;  // Mutex for connection management
    int port;  // Server port
    sqlite3 *db;  // SQLite database connection
    int sql_retry_count;  // SQL retry count
    pthread_mutex_t sql_mutex;  // Mutex for SQL operations
} SharedData;

// Function to write logs with a custom format
void write_log(const char* format, ...)
{
    static pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
    char message[MAX_LOG_MSG];
    va_list args;

    memset(message, 0, sizeof(message));

    va_start(args, format);
    vsnprintf(message, sizeof(message) - 2, format, args);
    va_end(args);

    size_t len = strlen(message);
    if (len > 0 && message[len-1] != '\n')
    {
        strcat(message, "\n");
    }

    pthread_mutex_lock(&fifo_mutex);
    int fd = open(FIFO_NAME, O_WRONLY);
    if (fd != -1)
    {
        ssize_t bytes_written = write(fd, message, strlen(message));
        if (bytes_written < 0)
        {
            perror("write to FIFO failed");
        }
        close(fd);
    }
    pthread_mutex_unlock(&fifo_mutex);
}

// Thread function to manage sensor connections
void* connection_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Initialize server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        write_log("Failed to create socket");
        return NULL;
    }

    // Set socket to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        write_log("Failed to set socket options");
        close(server_fd);
        return NULL;
    }

    // Setup server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(shared->port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        write_log("Failed to bind socket");
        close(server_fd);
        return NULL;
    }

    // Listen for connections
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

        // Read the sensor ID from the connection
        char buffer[BUFF_SIZE];
        memset(buffer, 0, BUFF_SIZE);
        int bytes_read = read(client_fd, buffer, BUFF_SIZE);
        if (bytes_read <= 0)
        {
            close(client_fd);
            continue;
        }

        // Parse sensor ID từ message "ID:X"
        int sensor_id;
        if (sscanf(buffer, "ID:%d", &sensor_id) != 1)
        {
            write_log("Invalid sensor ID format");
            close(client_fd);
            continue;
        }

        pthread_mutex_lock(&shared->mutex);

        // Kiểm tra xem sensor ID đã tồn tại chưa
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

        // Thêm sensor mới với ID được chỉ định
        SensorConnection* new_conn = &shared->sensor_connections[shared->connection_count];
        new_conn->id = sensor_id;  // Sử dụng ID từ sensor node
        new_conn->socket_fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, new_conn->ip, INET_ADDRSTRLEN);
        new_conn->port = ntohs(client_addr.sin_port);

        shared->connected_sensors[sensor_id] = 1;
        write_log("Sensor node %d has opened a new connection from %s:%d",
                  sensor_id, new_conn->ip, new_conn->port);

        // Tạo thread để xử lý messages
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


// Helper function to insert sensor data into the database
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

// Thread function to manage storage operations
void* storage_manager(void* arg)
{
    SharedData* shared = (SharedData*)arg;
    int retry_count = 0;
    const int MAX_RETRIES = 3;

    while (!shared->should_exit)
    {
        pthread_mutex_lock(&shared->mutex);

        // Attempt to reconnect if connection is lost
        if (!shared->sql_connected)
        {
            if (retry_count < MAX_RETRIES)
            {
                // Close existing connection if present
                if (shared->db)
                {
                    sqlite3_close(shared->db);
                    shared->db = NULL;
                }

                // Attempt to establish new connection
                int rc = sqlite3_open("sensor_data.db", &shared->db);
                if (rc == SQLITE_OK)
                {
                    shared->sql_connected = 1;
                    retry_count = 0;
                    write_log("Connection to SQL server established");

                    // Create table if not exists
                    const char *create_table_sql =
                        "CREATE TABLE IF NOT EXISTS sensor_data ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "sensor_id INTEGER,"
                        "temperature REAL,"
                        "humidity REAL,"
                        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

                    // Execute table creation
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

        // Store data for all connected sensors
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
        sleep(5);  // Store data every 5 seconds
    }

    // Cleanup database connection
    if (shared->db)
    {
        sqlite3_close(shared->db);
        shared->db = NULL;
    }

    return NULL;
}

// Process to handle logging
void log_process()
{
    FILE* log_file = fopen("gateway.log", "a");
    if (!log_file)
    {
        perror("Failed to open log file");
        exit(1);
    }

    int fd = open(FIFO_NAME, O_RDONLY);
    if (fd == -1)
    {
        perror("Failed to open FIFO");
        exit(1);
    }

    char buffer[MAX_LOG_MSG];
    char line[MAX_LOG_MSG];
    char timestamp[32];
    static int seq_num = 1;
    size_t line_pos = 0;

    while (1)
    {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0)
        {
            for (size_t i = 0; i < (size_t)bytes_read; i++)
            {
                if (line_pos < sizeof(line) - 1)
                {
                    line[line_pos++] = buffer[i];

                    if (buffer[i] == '\n' || line_pos >= sizeof(line) - 1)
                    {
                        line[line_pos] = '\0';

                        // Chỉ ghi log nếu message chứa "Sensor node" và "reports"
                        if (strstr(line, "Sensor node") && strstr(line, "reports"))
                        {
                            time_t now = time(NULL);
                            struct tm *tm_info = localtime(&now);
                            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

                            fprintf(log_file, "%d %s %s", seq_num++, timestamp, line);
                            fflush(log_file);
                        }

                        line_pos = 0;
                        memset(line, 0, sizeof(line));
                    }
                }
            }
        }
    }
}

// Thread function to handle messages from sensors
void *handle_sensor_messages(void *arg)
{
    SharedData* shared = (SharedData*)arg;
    SensorConnection* conn = &shared->sensor_connections[shared->connection_count - 1];
    char buffer[BUFF_SIZE];

    while (!shared->should_exit)
    {
        // Clear buffer and read incoming data
        memset(buffer, 0, BUFF_SIZE);
        int bytes_read = read(conn->socket_fd, buffer, BUFF_SIZE);

        // Handle disconnection
        if (bytes_read <= 0)
        {
            pthread_mutex_lock(&shared->mutex);
            write_log("Sensor node %d has closed the connection", conn->id);
            shared->connected_sensors[conn->id] = 0;
            pthread_mutex_unlock(&shared->mutex);
            close(conn->socket_fd);
            break;
        }

        // Process received sensor data
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

int main(int argc, char *argv[])
{
    // Check for correct number of arguments
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Create FIFO if it does not exist
    if (mkfifo(FIFO_NAME, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo");
            exit(1);
        }
    }

    // Create a child process to handle logging
    pid_t pid = fork();
    if (pid == 0)
    {
        log_process();
        exit(0);
    }

    // Initialize shared data
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

    // Initialize SQLite connection
    int rc = sqlite3_open("sensor_data.db", &shared.db);
    if (rc)
    {
        write_log("Can't open database: %s", sqlite3_errmsg(shared.db));
        return 1;
    }

    // Create table if it does not exist
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

    // Create threads
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

    // Handle signals for safe exit
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Wait for threads to finish
    pthread_join(conn_thread, NULL);
    pthread_join(storage_thread, NULL);

    // Cleanup
    pthread_mutex_destroy(&shared.mutex);
    pthread_mutex_destroy(&shared.conn_mutex);
    pthread_mutex_destroy(&shared.sql_mutex);
    sqlite3_close(shared.db);
    unlink(FIFO_NAME);  // Remove FIFO file

    return 0;
}