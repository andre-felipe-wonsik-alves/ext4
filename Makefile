SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR	:= $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj

TARGET := $(BIN_DIR)/ext4_shell
img ?= resources/myext4image4k.img

CXX := g++
CXXFLAGS := -Wall -Wextra -I$(INC_DIR)
LDLIBS := -lm

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SOURCES))

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

.PHONY: all build run clean

all: clean build run

build: $(TARGET)

run: $(TARGET)
	$(TARGET) $(img)

clean:
	rm -rf $(BUILD_DIR)
