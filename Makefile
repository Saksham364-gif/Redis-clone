CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -Iinclude
SRCS = src/server.cpp src/resp.cpp src/storage.cpp src/commands.cpp src/persistence.cpp
TARGET = my_redis_server

all:
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean