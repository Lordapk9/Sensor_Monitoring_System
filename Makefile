CC = gcc
CFLAGS = -Wall -pthread
TARGET = gateway
SRCS = main.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

.PHONY: run clean

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) logFifo gateway.log
