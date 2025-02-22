CC = gcc
CFLAGS = -Wall -Wextra -pthread -I$(INC_DIR)
LDFLAGS = -pthread -lsqlite3

CUR_DIR := .
INC_DIR := $(CUR_DIR)/inc
SRC_DIR := $(CUR_DIR)/src
OBJ_DIR := $(CUR_DIR)/obj
BIN_DIR := $(CUR_DIR)/bin
LIB_DIR := $(CUR_DIR)/lib

# Object files
OBJ_FILES = $(OBJ_DIR)/log.o $(OBJ_DIR)/connection_manager.o $(OBJ_DIR)/sensor_handler.o $(OBJ_DIR)/storage_manager.o
LIB_SOCKET_UTILS = $(LIB_DIR)/libsocket_utils.so

# Targets
SERVER = $(BIN_DIR)/server
SENSOR = $(BIN_DIR)/sensor_node

make_dir:
	mkdir -p $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)

# Server
$(SERVER): $(CUR_DIR)/main.o $(OBJ_FILES) $(LIB_SOCKET_UTILS)
	$(CC) $(CUR_DIR)/main.o $(OBJ_FILES) -o $@ $(LDFLAGS) -L$(LIB_DIR) -lsocket_utils -Wl,-rpath,$(LIB_DIR)

# Sensor Node
$(SENSOR): $(CUR_DIR)/sensor_node.o $(OBJ_FILES) $(LIB_SOCKET_UTILS)
	$(CC) $(CUR_DIR)/sensor_node.o $(OBJ_FILES) -o $@ $(LDFLAGS) -L$(LIB_DIR) -lsocket_utils -Wl,-rpath,$(LIB_DIR)

# Shared library
$(LIB_SOCKET_UTILS): $(OBJ_DIR)/socket_utils.o
	$(CC) -shared -o $@ $^

# Object files
create_obj: $(OBJ_DIR)/log.o $(OBJ_DIR)/connection_manager.o $(OBJ_DIR)/sensor_handler.o $(OBJ_DIR)/storage_manager.o $(OBJ_DIR)/socket_utils.o $(CUR_DIR)/main.o $(CUR_DIR)/sensor_node.o

$(OBJ_DIR)/log.o: $(SRC_DIR)/log.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(OBJ_DIR)/connection_manager.o: $(SRC_DIR)/connection_manager.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(OBJ_DIR)/sensor_handler.o: $(SRC_DIR)/sensor_handler.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(OBJ_DIR)/storage_manager.o: $(SRC_DIR)/storage_manager.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(OBJ_DIR)/socket_utils.o: $(SRC_DIR)/socket_utils.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(CUR_DIR)/main.o: $(CUR_DIR)/main.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

$(CUR_DIR)/sensor_node.o: $(CUR_DIR)/sensor_node.c
	$(CC) $(CFLAGS) -c -fPIC $< -o $@

all: make_dir create_obj $(LIB_SOCKET_UTILS) $(SERVER) $(SENSOR)

clean:
	rm -f *.o $(SERVER) $(SENSOR) gateway.log logFifo sensor_data.db
	rm -rf $(OBJ_DIR)/*.o
	rm -rf $(BIN_DIR)/*
	rm -rf $(LIB_DIR)/*.so
    
.PHONY: all clean make_dir create_obj