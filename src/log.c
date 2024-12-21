#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "log.h"

#define FIFO_NAME "logFifo"
#define MAX_LOG_MSG 256

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