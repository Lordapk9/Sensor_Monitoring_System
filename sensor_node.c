#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define BUFF_SIZE 1024
#define SERVER_IP "YOUR_SERVER_IP" // Replace with the actual server IP address

typedef struct
{
    float temperature;
    float humidity;
} SensorData;

// Generate random sensor data
SensorData generate_sensor_data(int sensor_id)
{
    srand(time(NULL) + sensor_id); // Seed with current time and sensor ID
    SensorData data;
    data.temperature = 15.0 + ((float)rand() / RAND_MAX) * 20.0; // 15-35°C
    data.humidity = 30.0 + ((float)rand() / RAND_MAX) * 70.0;    // 30-100%
    return data;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <sensor_id> <server_port>\n", argv[0]);
        printf("Example: %s 0 6000\n", argv[0]);
        exit(1);
    }

    int sensor_id = atoi(argv[1]); // Convert sensor ID from string to integer
    int server_port = atoi(argv[2]); // Convert server port from string to integer

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address");
        exit(1);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        exit(1);
    }

    printf("Sensor node %d connected to server on port %d\n", sensor_id, server_port);

    // Send sensor ID first
    char id_msg[10];
    sprintf(id_msg, "ID:%d", sensor_id);
    send(sock, id_msg, strlen(id_msg), 0);
    sleep(1);

    // Main loop - send sensor data
    while (1)
    {
        SensorData data = generate_sensor_data(sensor_id); // Generate random sensor data

        char message[BUFF_SIZE];
        sprintf(message, "SENSOR:%d,TEMP:%.2f,HUM:%.2f",
                sensor_id, data.temperature, data.humidity);

        if (send(sock, message, strlen(message), 0) < 0)
        {
            printf("Sensor node %d: Connection lost\n", sensor_id);
            break;
        }

        printf("Sensor %d sent - Temperature: %.2f°C, Humidity: %.2f%%\n",
               sensor_id, data.temperature, data.humidity);

        sleep(5); // Send data every 5 seconds
    }

    close(sock); // Close the socket
    return 0;
}
