CC = gcc
CFLAGS = -Wall -Wextra -pthread -I$(INC_DIR)
LDFLAGS = -pthread -lsqlite3

CUR_DIR := .
INC_DIR := $(CUR_DIR)/inc
SRC_DIR := $(CUR_DIR)/src
OBJ_DIR := $(CUR_DIR)/obj
BIN_DIR := $(CUR_DIR)/bin
# Object files
OBJ_FILES = $(OBJ_DIR)/log.o $(OBJ_DIR)/connection_manager.o $(OBJ_DIR)/sensor_handler.o $(OBJ_DIR)/storage_manager.o
# Targets
SERVER = server
SENSOR = sensor_node

make_dir:
	mkdir -p $(OBJ_DIR) $(BIN_DIR)


# Server
$(SERVER): $(CUR_DIR)/main.o $(OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)
# Sensor Node
$(SENSOR): $(CUR_DIR)/sensor_node.o $(OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)

# Object files
create_obj:
	$(CC) $(CFLAGS) -c -fPIC $(SRC_DIR)/log.c -o $(OBJ_DIR)/log.o
	$(CC) $(CFLAGS) -c -fPIC $(SRC_DIR)/connection_manager.c -o $(OBJ_DIR)/connection_manager.o
	$(CC) $(CFLAGS) -c -fPIC $(SRC_DIR)/sensor_handler.c -o $(OBJ_DIR)/sensor_handler.o
	$(CC) $(CFLAGS) -c -fPIC $(SRC_DIR)/storage_manager.c -o $(OBJ_DIR)/storage_manager.o
	$(CC) $(CFLAGS) -c -fPIC $(CUR_DIR)/main.c -o $(CUR_DIR)/main.o
	$(CC) $(CFLAGS) -c -fPIC $(CUR_DIR)/sensor_node.c -o $(CUR_DIR)/sensor_node.o 

all: create_obj make_dir $(SERVER) $(SENSOR)  
clean:
	rm -f *.o $(SERVER) $(SENSOR) gateway.log logFifo sensor_data.db
	rm -rf $(OBJ_DIR)/*.o
	rm -rf $(BIN_DIR)/*
.PHONY: all clean make_dir create_obj