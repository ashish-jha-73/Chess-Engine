# =======================
# Chess Game - Makefile
# =======================

# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++23 -Iinclude
LDFLAGS := -lsfml-graphics -lsfml-window -lsfml-system

# Directories
SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin

# Automatically find source files
SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

# Output binary
TARGET := $(BIN_DIR)/chessgame

# Colors for pretty output
GREEN := \033[1;32m
CYAN := \033[1;36m
RESET := \033[0m

# Default target
all: $(TARGET)

# Link the program
$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	@echo "$(CYAN)🔗 Linking $(TARGET)...$(RESET)"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo "$(GREEN)⚙️  Compiling $<...$(RESET)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Run the program
run: $(TARGET)
	@echo "$(CYAN)🚀 Running Chess Game...$(RESET)"
	@./$(TARGET)

# Clean build files
clean:
	@echo "$(CYAN)🧹 Cleaning build files...$(RESET)"
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

# Phony targets (not real files)
.PHONY: all run clean
