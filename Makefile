# Makefile for TCP Group Chat
# Supports building with debugging symbols, running tests, and memory checking

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
INCLUDES = -Iinclude
LDFLAGS = -lpthread

# Debug flags for CGDB
DEBUG_FLAGS = -g3 -O0 -DDEBUG

# Release flags
RELEASE_FLAGS = -O2 -DNDEBUG

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
INCLUDE_DIR = include

# Source files
COMMON_SRC = $(SRC_DIR)/common.c
SERVER_SRC = $(SRC_DIR)/server.c
CLIENT_SRC = $(SRC_DIR)/client.c
INTERACTIVE_CLIENT_SRC = $(SRC_DIR)/interactive_client.c

# Object files
COMMON_OBJ = $(BUILD_DIR)/common.o
SERVER_OBJ = $(BUILD_DIR)/server.o
CLIENT_OBJ = $(BUILD_DIR)/client.o
INTERACTIVE_CLIENT_OBJ = $(BUILD_DIR)/interactive_client.o

# Executables
SERVER = server
CLIENT = client
INTERACTIVE_CLIENT = chat
TEST_RUNNER = test_runner

# Default target
.PHONY: all
all: release

# Release build
.PHONY: release
release: CFLAGS += $(RELEASE_FLAGS)
release: $(SERVER) $(CLIENT) $(INTERACTIVE_CLIENT)

# Debug build (for use with CGDB)
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(SERVER) $(CLIENT) $(INTERACTIVE_CLIENT)
	@echo "Debug build complete. Use 'make gdb-server' or 'make gdb-client' to debug"

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build common object
$(COMMON_OBJ): $(COMMON_SRC) $(INCLUDE_DIR)/common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build server
$(SERVER_OBJ): $(SERVER_SRC) $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SERVER): $(SERVER_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build client
$(CLIENT_OBJ): $(CLIENT_SRC) $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(CLIENT): $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build interactive client
$(INTERACTIVE_CLIENT_OBJ): $(INTERACTIVE_CLIENT_SRC) $(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(INTERACTIVE_CLIENT): $(INTERACTIVE_CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Testing
.PHONY: test
test: $(TEST_RUNNER)
	./$(TEST_RUNNER)

$(TEST_RUNNER): $(TEST_DIR)/test_main.c $(TEST_DIR)/test_protocol.c $(COMMON_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(INCLUDES) -I$(TEST_DIR) $^ -o $@ $(LDFLAGS)

# Run integration test (requires tmux or separate terminals)
.PHONY: integration-test
integration-test: release
	@echo "Running integration test..."
	@./$(SERVER) 8080 5 > server.log 2>&1 & echo $$! > server.pid
	@sleep 1
	@./$(CLIENT) 127.0.0.1 8080 alice 10 client_alice.log &
	@./$(CLIENT) 127.0.0.1 8080 bob 10 client_bob.log &
	@./$(CLIENT) 127.0.0.1 8080 charlie 10 client_charlie.log &
	@sleep 3
	@kill `cat server.pid` 2>/dev/null || true
	@rm -f server.pid
	@echo "Integration test complete. Check server.log and client_*.log"

# Debugging with CGDB
.PHONY: gdb-server
gdb-server: debug
	cgdb ./$(SERVER)

.PHONY: gdb-client
gdb-client: debug
	cgdb ./$(CLIENT)

# Memory leak detection with Valgrind
.PHONY: valgrind-server
valgrind-server: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	         --verbose --log-file=valgrind-server.log \
	         ./$(SERVER) 8080 5

.PHONY: valgrind-client
valgrind-client: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	         --verbose --log-file=valgrind-client.log \
	         ./$(CLIENT) 127.0.0.1 8080 testuser 5 test.log

# Run server in background for testing
.PHONY: run-server
run-server: release
	./$(SERVER) 8080 10

# Run a test client
.PHONY: run-client
run-client: release
	./$(CLIENT) 127.0.0.1 8080 testuser 20 client.log

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SERVER) $(CLIENT) $(INTERACTIVE_CLIENT) $(TEST_RUNNER)
	rm -f *.log *.pid
	rm -f client_*.log server.log
	rm -f valgrind-*.log

# Clean everything including downloaded dependencies
.PHONY: distclean
distclean: clean
	rm -rf tests/unity

# Format code (requires clang-format)
.PHONY: format
format:
	find $(SRC_DIR) $(INCLUDE_DIR) -name '*.c' -o -name '*.h' | xargs clang-format -i

# Static analysis (requires cppcheck)
.PHONY: analyze
analyze:
	cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR)

# Show help
.PHONY: help
help:
	@echo "TCP Group Chat - Makefile targets:"
	@echo ""
	@echo "Building:"
	@echo "  make              - Build release version"
	@echo "  make release      - Build optimized release version"
	@echo "  make debug        - Build with debug symbols for CGDB"
	@echo ""
	@echo "Testing:"
	@echo "  make test         - Run unit tests"
	@echo "  make integration-test - Run full integration test"
	@echo ""
	@echo "Debugging:"
	@echo "  make gdb-server   - Debug server with CGDB"
	@echo "  make gdb-client   - Debug client with CGDB"
	@echo "  make valgrind-server - Check server for memory leaks"
	@echo "  make valgrind-client - Check client for memory leaks"
	@echo ""
	@echo "Running:"
	@echo "  make run-server   - Start server on port 8080"
	@echo "  make run-client   - Connect a test client"
	@echo ""
	@echo "Maintenance:"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove everything including dependencies"
	@echo "  make format       - Format code with clang-format"
	@echo "  make analyze      - Run static analysis with cppcheck"
	@echo ""

.DEFAULT_GOAL := all
