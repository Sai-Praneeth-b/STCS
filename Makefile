# Makefile -- Shared Task Coordination Service (STCS), Phase 2
#
#   make            build bin/server and bin/client
#   make tests      build the test programs (in build/)
#   make check      build everything and run every test
#   make clean      remove build products and runtime logs
#
# Requires: a C++17 compiler (g++ or clang++) and make. No other libraries.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic
CPPFLAGS += -I. -MMD -MP

BUILD := build
BIN   := bin

SHARED_SRC := shared/utf8.cpp shared/json.cpp shared/framing.cpp shared/protocol.cpp \
              shared/validation.cpp shared/net.cpp shared/timeutil.cpp
SERVER_SRC := server/logger.cpp server/session.cpp server/task_manager.cpp \
              server/request_validation.cpp server/dispatcher.cpp server/server.cpp
CLIENT_SRC := client/connection.cpp client/input_parser.cpp client/display.cpp \
              client/client_app.cpp

SHARED_OBJ := $(SHARED_SRC:%.cpp=$(BUILD)/%.o)
SERVER_OBJ := $(SERVER_SRC:%.cpp=$(BUILD)/%.o)
CLIENT_OBJ := $(CLIENT_SRC:%.cpp=$(BUILD)/%.o)

TEST_PROGRAMS := $(BUILD)/test_framing $(BUILD)/test_units $(BUILD)/test_integration

.PHONY: all tests check clean
all: $(BIN)/server $(BIN)/client

$(BIN)/server: $(BUILD)/server/main.o $(SERVER_OBJ) $(SHARED_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BIN)/client: $(BUILD)/client/main.o $(CLIENT_OBJ) $(SHARED_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

# --- tests -------------------------------------------------------------
tests: $(TEST_PROGRAMS)

$(BUILD)/test_framing: $(BUILD)/tests/test_framing.o $(SHARED_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/test_units: $(BUILD)/tests/test_units.o $(SERVER_OBJ) $(CLIENT_OBJ) $(SHARED_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/test_integration: $(BUILD)/tests/test_integration.o $(SHARED_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

check: all tests
	sh tests/run_all.sh

# --- generic rules -----------------------------------------------------
$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD) $(BIN)/server $(BIN)/client logs/server.log logs/test_*.log

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
