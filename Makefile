CXX := g++
PGO_PROFILE_DIR := .pgo
BASE_CXXFLAGS := -O3 -Wall -Wextra -std=c++23 -Iinclude -march=native -flto -ffast-math -DNDEBUG
CXXFLAGS := $(BASE_CXXFLAGS)
PGO_GEN_FLAGS := $(BASE_CXXFLAGS) -fprofile-generate=$(PGO_PROFILE_DIR)
PGO_USE_FLAGS := $(BASE_CXXFLAGS) -fprofile-use=$(PGO_PROFILE_DIR) -fprofile-correction
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

pgo-generate: CXXFLAGS := $(PGO_GEN_FLAGS)
pgo-generate: clean $(TARGET)
	@echo -e "$(CYAN)📊 Generating profile data...$(RESET)"
	@mkdir -p $(PGO_PROFILE_DIR)
	@./$(TARGET) --bench 16 2500 > /dev/null

pgo-use: CXXFLAGS := $(PGO_USE_FLAGS)
pgo-use: clean-build $(TARGET)
	@echo -e "$(CYAN)⚡ Built with PGO profile-use flags.$(RESET)"

pgo:
	@$(MAKE) pgo-generate
	@$(MAKE) pgo-use

bench: $(TARGET)
	@./$(TARGET) --bench 12 2000

tune: $(TARGET)
	@./$(TARGET) --tune 24 120

$(TARGET): $(OBJ)
	@echo -e "$(CYAN)🔗 Linking $(TARGET)...$(RESET)"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@echo -e "$(GREEN)⚙️  Compiling $<...$(RESET)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	@echo -e "$(CYAN)🚀 Running Chess Game...$(RESET)"
	@./$(TARGET) --graphics

clean-build:
	@echo -e "$(CYAN)🧹 Cleaning build files...$(RESET)"
	@rm -rf $(BUILD_DIR) $(TARGET)

clean:
	@$(MAKE) clean-build
	@rm -rf $(PGO_PROFILE_DIR) *.gcda *.gcno

.PHONY: all run clean bench tune pgo pgo-generate pgo-use
