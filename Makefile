# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++23 -Iinclude
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio

# Source and output
SRC = src/main.cpp src/engine.cpp src/chess.cpp
TARGET = chessgame

# Default target builds and runs
all: $(TARGET)
	@echo "🚀 Running $(TARGET)..."
	@./$(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
