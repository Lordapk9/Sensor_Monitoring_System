CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread -lsqlite3

# Targets
SERVER = server
SENSOR = sensor_node

all: $(SERVER) $(SENSOR)

# Server
$(SERVER): main.o
	$(CC) main.o -o $(SERVER) $(LDFLAGS)

# Sensor Node
$(SENSOR): sensor_node.o
	$(CC) sensor_node.o -o $(SENSOR)

# Object files
main.o: main.c
	$(CC) $(CFLAGS) -c main.c

sensor_node.o: sensor_node.c
	$(CC) $(CFLAGS) -c sensor_node.c

clean:
	rm -f *.o $(SERVER) $(SENSOR) gateway.log logFifo sensor_data.db

.PHONY: all clean