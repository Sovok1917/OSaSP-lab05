# Makefile for Producer/Consumer Demo using POSIX Threads (Semaphores or Condvars)

# Compiler and base flags
CC = gcc
# Base flags adhere to requirements: C11, pedantic, common warnings, POSIX.1-2008
BASE_CFLAGS = -std=c11 -pedantic -W -Wall -Wextra \
              -Wmissing-prototypes -Wstrict-prototypes \
              -D_POSIX_C_SOURCE=200809L
# Optional allowed flags (uncomment if needed during development)
# BASE_CFLAGS += -Wno-unused-parameter -Wno-unused-variable

# Linker flags - IMPORTANT: Link with -pthread for thread functions
LDFLAGS = -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build
DEBUG_DIR = $(BUILD_DIR)/debug
RELEASE_DIR = $(BUILD_DIR)/release

# --- Configuration: Default to Debug ---
CURRENT_MODE = debug
CFLAGS = $(BASE_CFLAGS) -g3 -ggdb # Debugging symbols
OUT_DIR = $(DEBUG_DIR)

# --- Configuration: Adjust for Release Mode ---
# Override defaults if MODE=release is passed via command line (e.g., make MODE=release ...)
ifeq ($(MODE), release)
  CURRENT_MODE = release
  CFLAGS = $(BASE_CFLAGS) -O2 # Optimization level 2
  CFLAGS += -Werror # Treat warnings as errors in release
  OUT_DIR = $(RELEASE_DIR)
endif

# Ensure output directories exist before compiling/linking
# Using .SECONDEXPANSION allows OUT_DIR to be evaluated correctly per target
.SECONDEXPANSION:
$(shell mkdir -p $(DEBUG_DIR) $(RELEASE_DIR))

# Source files (Renamed)
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/queue_manager.c $(SRC_DIR)/producer.c $(SRC_DIR)/consumer.c $(SRC_DIR)/utils.c

# Object files (paths automatically use the correct OUT_DIR based on MODE)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OUT_DIR)/%.o, $(SRCS))

# Executable (paths automatically use the correct OUT_DIR based on MODE)
TARGET_NAME = prod_cons_threads
TARGET = $(OUT_DIR)/$(TARGET_NAME)


# Phony targets (targets that don't represent files)
.PHONY: all clean run run-sem run-cond run-release run-release-sem run-release-cond debug-build release-build help

# Default target: build debug version
all: debug-build

# Help target - Explains usage and run targets clearly
help:
	@echo "Makefile Usage:"
	@echo "  make                Build debug version (default, same as make debug-build)"
	@echo "  make debug-build    Build debug version into $(DEBUG_DIR)"
	@echo "  make release-build  Build release version into $(RELEASE_DIR)"
	@echo "                      (Warnings will be treated as errors: CFLAGS += -Werror)"
	@echo "  make run            Build and run DEBUG version (default: semaphores)."
	@echo "  make run-sem        Build and run DEBUG version using Semaphores (-m sem)."
	@echo "  make run-cond       Build and run DEBUG version using Condition Variables (-m cond)."
	@echo "  make run-release    Build and run RELEASE version (default: semaphores)."
	@echo "  make run-release-sem Build and run RELEASE version using Semaphores (-m sem)."
	@echo "  make run-release-cond Build and run RELEASE version using Condition Variables (-m cond)."
	@echo "  make clean          Remove all build artifacts (rm -rf $(BUILD_DIR))"
	@echo "  make help           Show this help message"


# --- Build Targets ---

# Target to build the debug version
debug-build: MODE=debug
debug-build: $$(TARGET)
	@echo "Debug build complete in $(DEBUG_DIR)"

# Target to build the release version
release-build: MODE=release
release-build: $$(TARGET)
	@echo "Release build complete in $(RELEASE_DIR)"


# --- Compilation and Linking Rules ---

$(TARGET): $(OBJS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c | $$(@D)/.
	@echo "Compiling $< -> $@..."
	$(CC) $(CFLAGS) -c $< -o $@

%/.:
	@mkdir -p $(@)


# --- Execution Targets ---

# Default run uses semaphores
run: debug-build
	@echo "Running DEBUG version $(TARGET) (Default: Semaphores)..."
	$(TARGET) -m sem

run-sem: debug-build
	@echo "Running DEBUG version $(TARGET) using Semaphores..."
	$(TARGET) -m sem

run-cond: debug-build
	@echo "Running DEBUG version $(TARGET) using Condition Variables..."
	$(TARGET) -m cond

# Default release run uses semaphores
run-release: release-build
	@echo "Running RELEASE version $(TARGET) (Default: Semaphores)..."
	$(TARGET) -m sem

run-release-sem: release-build
	@echo "Running RELEASE version $(TARGET) using Semaphores..."
	$(TARGET) -m sem

run-release-cond: release-build
	@echo "Running RELEASE version $(TARGET) using Condition Variables..."
	$(TARGET) -m cond


# --- Clean Target ---

clean:
	@echo "Cleaning build directories..."
	rm -rf $(BUILD_DIR)
