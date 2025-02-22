#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include "log.h"

#define FIFO_NAME "logFifo" // Name of the FIFO (named pipe) for logging
#define MAX_LOG_MSG 256 // Maximum length of a log message

static volatile int keep_running = 1; // Flag to control the running state of the log process

// Signal handler to set the keep_running flag to 0
void log_handle_signal(int signum)
{
    (void)signum; 
    keep_running = 0;
}

// Function to write a log message to the FIFO
void write_log(const char* format, ...)
{
    static pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect FIFO access
    char message[MAX_LOG_MSG]; // Buffer to hold the log message
    va_list args;

    memset(message, 0, sizeof(message)); // Clear the message buffer

    va_start(args, format); // Initialize the variable argument list
    vsnprintf(message, sizeof(message) - 2, format, args); // Format the log message
    va_end(args); // End the variable argument list

    size_t len = strlen(message);
    if (len > 0 && message[len-1] != '\n')
    {
        strcat(message, "\n"); // Ensure the message ends with a newline
    }

    pthread_mutex_lock(&fifo_mutex); // Lock the mutex
    int fd = open(FIFO_NAME, O_WRONLY | O_NONBLOCK); // Open the FIFO for writing
    if (fd != -1)
    {
        ssize_t bytes_written = write(fd, message, strlen(message)); // Write the message to the FIFO
        if (bytes_written < 0)
        {
            perror("write to FIFO failed"); // Print an error message if the write fails
        }
        close(fd); // Close the FIFO
    }
    pthread_mutex_unlock(&fifo_mutex); // Unlock the mutex
}

// Function to process log messages from the FIFO and write them to a log file
void log_process()
{
    signal(SIGINT, log_handle_signal); // Set up signal handler for SIGINT
    signal(SIGTERM, log_handle_signal); // Set up signal handler for SIGTERM

    FILE* log_file = fopen("gateway.log", "a"); // Open the log file for appending
    if (!log_file)
    {
        perror("Failed to open log file"); // Print an error message if the log file cannot be opened
        exit(1);
    }

    int fd = open(FIFO_NAME, O_RDONLY | O_NONBLOCK); // Open the FIFO for reading
    if (fd == -1)
    {
        perror("Failed to open FIFO"); // Print an error message if the FIFO cannot be opened
        exit(1);
    }

    char buffer[MAX_LOG_MSG]; // Buffer to hold data read from the FIFO
    char line[MAX_LOG_MSG]; // Buffer to hold a single log line
    char timestamp[32]; // Buffer to hold the timestamp
    static int seq_num = 1; // Sequence number for log entries
    size_t line_pos = 0; // Position in the line buffer

    while (keep_running)
    {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1); // Read data from the FIFO
        if (bytes_read > 0)
        {
            for (size_t i = 0; i < (size_t)bytes_read; i++)
            {
                if (line_pos < sizeof(line) - 1)
                {
                    line[line_pos++] = buffer[i]; // Add the character to the line buffer

                    if (buffer[i] == '\n' || line_pos >= sizeof(line) - 1)
                    {
                        line[line_pos] = '\0'; // Null-terminate the line

                        if (strstr(line, "Sensor node") && strstr(line, "reports"))
                        {
                            time_t now = time(NULL);
                            struct tm *tm_info = localtime(&now);
                            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info); // Format the timestamp

                            fprintf(log_file, "%d %s %s", seq_num++, timestamp, line); // Write the log entry to the file
                            fflush(log_file); // Flush the log file
                        }

                        line_pos = 0; // Reset the line position
                        memset(line, 0, sizeof(line)); // Clear the line buffer
                    }
                }
            }
        }
        else
        {
            usleep(100000); // Sleep for 100ms to avoid busy-waiting
        }
    }

    close(fd); // Close the FIFO
    fclose(log_file); // Close the log file
    unlink(FIFO_NAME); // Remove the FIFO
}