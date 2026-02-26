CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++23 -Iinclude
LDFLAGS := -lsfml-graphics -lsfml-window -lsfml-system

SRC_DIR := src
BUILD_DIR := build
SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

TARGET := chessgame

GREEN := \033[1;32m
CYAN := \033[1;36m
RESET := \033[0m

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo -e "$(CYAN)🔗 Linking $(TARGET)...$(RESET)"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo -e "$(GREEN)⚙️  Compiling $<...$(RESET)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	@echo -e "$(CYAN)🚀 Running Chess Game...$(RESET)"
	@./$(TARGET)

clean:
	@echo -e "$(CYAN)🧹 Cleaning build files...$(RESET)"
	@rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all run clean
