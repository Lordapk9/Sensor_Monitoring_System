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

#define FIFO_NAME "logFifo"
#define MAX_LOG_MSG 256
#define MAX_SENSORS 10

// Cấu trúc dữ liệu chia sẻ giữa các luồng
typedef struct {
    pthread_mutex_t mutex;
    int connected_sensors[MAX_SENSORS];  // Theo dõi trạng thái kết nối của các cảm biến
    double running_temps[MAX_SENSORS];   // Lưu trữ nhiệt độ của các cảm biến
    int sql_connected;                   // Trạng thái kết nối SQL
    int should_exit;                     // Cờ báo hiệu kết thúc chương trình
} SharedData;

// Hàm ghi log với định dạng tùy chỉnh
void write_log(const char* format, ...) {
    static pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
    char message[MAX_LOG_MSG];
    va_list args;
    
    memset(message, 0, sizeof(message));
    
    va_start(args, format);
    vsnprintf(message, sizeof(message) - 2, format, args);
    va_end(args);
    
    size_t len = strlen(message);
    if (len > 0 && message[len-1] != '\n') {
        strcat(message, "\n");
    }
    
    pthread_mutex_lock(&fifo_mutex);
    int fd = open(FIFO_NAME, O_WRONLY);
    if (fd != -1) {
        ssize_t bytes_written = write(fd, message, strlen(message));
        if (bytes_written < 0) {
            perror("write to FIFO failed");
        }
        close(fd);
    }
    pthread_mutex_unlock(&fifo_mutex);
}

// Luồng quản lý kết nối của các cảm biến
void* connection_manager(void* arg) {
    SharedData* shared = (SharedData*)arg;
    
    while (!shared->should_exit) {
        for (int sensor_id = 0; sensor_id < MAX_SENSORS; sensor_id++) {
            pthread_mutex_lock(&shared->mutex);
            if (!shared->connected_sensors[sensor_id] && (rand() % 10 == 0)) {
                shared->connected_sensors[sensor_id] = 1;
                pthread_mutex_unlock(&shared->mutex);
                write_log("Sensor node %d has opened a new connection", sensor_id);
            } else if (shared->connected_sensors[sensor_id] && (rand() % 20 == 0)) {
                shared->connected_sensors[sensor_id] = 0;
                pthread_mutex_unlock(&shared->mutex);
                write_log("Sensor node %d has closed the connection", sensor_id);
            } else {
                pthread_mutex_unlock(&shared->mutex);
            }
        }
        sleep(1);
    }
    return NULL;
}

// Luồng quản lý dữ liệu từ các cảm biến
void* data_manager(void* arg) {
    SharedData* shared = (SharedData*)arg;
    
    while (!shared->should_exit) {
        pthread_mutex_lock(&shared->mutex);
        for (int sensor_id = 0; sensor_id < MAX_SENSORS; sensor_id++) {
            if (shared->connected_sensors[sensor_id]) {
                shared->running_temps[sensor_id] = 15.0 + (rand() % 20);
                double temp = shared->running_temps[sensor_id];
                
                if (temp < 18.0) {
                    write_log("Sensor node %d reports it's too cold (running avg temperature = %.1f)", 
                             sensor_id, temp);
                } else if (temp > 25.0) {
                    write_log("Sensor node %d reports it's too hot (running avg temperature = %.1f)", 
                             sensor_id, temp);
                }
            }
        }
        pthread_mutex_unlock(&shared->mutex);
        sleep(1);
    }
    return NULL;
}

// Luồng quản lý lưu trữ dữ liệu
void* storage_manager(void* arg) {
    SharedData* shared = (SharedData*)arg;
    
    while (!shared->should_exit) {
        pthread_mutex_lock(&shared->mutex);
        if (!shared->sql_connected && (rand() % 5 == 0)) {
            shared->sql_connected = 1;
            write_log("Connection to SQL server established");
            write_log("New table sensor_data created");
        } else if (shared->sql_connected && (rand() % 10 == 0)) {
            shared->sql_connected = 0;
            write_log("Connection to SQL server lost");
            write_log("Unable to connect to SQL server");
        }
        pthread_mutex_unlock(&shared->mutex);
        sleep(1);
    }
    return NULL;
}

// Tiến trình ghi log
void log_process() {
    FILE* log_file = fopen("gateway.log", "a");
    if (!log_file) {
        perror("Failed to open log file");
        exit(1);
    }

    int fd = open(FIFO_NAME, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open FIFO");
        exit(1);
    }

    char buffer[MAX_LOG_MSG];
    char line[MAX_LOG_MSG];
    char timestamp[32];
    static int seq_num = 1;
    size_t line_pos = 0;
    
    while (1) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            for (size_t i = 0; i < (size_t)bytes_read; i++) {
                if (line_pos < sizeof(line) - 1) {
                    line[line_pos++] = buffer[i];
                    
                    if (buffer[i] == '\n' || line_pos >= sizeof(line) - 1) {
                        line[line_pos] = '\0';
                        
                        time_t now = time(NULL);
                        struct tm *tm_info = localtime(&now);
                        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                        
                        fprintf(log_file, "%d %s %s", seq_num++, timestamp, line);
                        fflush(log_file);
                        
                        line_pos = 0;
                        memset(line, 0, sizeof(line));
                    }
                }
            }
        }
    }
}

int main() {
    // Tạo FIFO nếu chưa tồn tại
    if (mkfifo(FIFO_NAME, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(1);
        }
    }

    // Tạo tiến trình con để xử lý log
    pid_t pid = fork();
    if (pid == 0) {
        log_process();
        exit(0);
    }

    // Khởi tạo dữ liệu chia sẻ
    SharedData shared;
    pthread_mutex_init(&shared.mutex, NULL);
    memset(shared.connected_sensors, 0, sizeof(shared.connected_sensors));
    memset(shared.running_temps, 0, sizeof(shared.running_temps));
    shared.sql_connected = 0;
    shared.should_exit = 0;

    // Tạo các luồng
    pthread_t conn_thread, data_thread, storage_thread;
    pthread_create(&conn_thread, NULL, connection_manager, &shared);
    pthread_create(&data_thread, NULL, data_manager, &shared);
    pthread_create(&storage_thread, NULL, storage_manager, &shared);

    // Đợi các luồng kết thúc
    pthread_join(conn_thread, NULL);
    pthread_join(data_thread, NULL);
    pthread_join(storage_thread, NULL);

    pthread_mutex_destroy(&shared.mutex);
    return 0;
}
