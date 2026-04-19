CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -MMD -MP
LDFLAGS :=

APP_TARGET := bin/kv_store
TEST_TARGET := bin/kv_store_tests

APP_SRCS := \
	src/main.cpp \
	src/common/string_utils.cpp \
	src/parser/command_parser.cpp \
	src/server/cli_server.cpp \
	src/store/kv_store.cpp \
	src/persistence/wal.cpp

TEST_SRCS := \
	tests/test_kv_store.cpp \
	src/common/string_utils.cpp \
	src/parser/command_parser.cpp \
	src/server/cli_server.cpp \
	src/store/kv_store.cpp \
	src/persistence/wal.cpp

APP_OBJS := $(patsubst %.cpp,build/app/%.o,$(APP_SRCS))
TEST_OBJS := $(patsubst %.cpp,build/test/%.o,$(TEST_SRCS))

APP_DEPS := $(APP_OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)

.PHONY: all clean test run

all: $(APP_TARGET)

$(APP_TARGET): $(APP_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(APP_OBJS) -o $@ $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(TEST_OBJS) -o $@ $(LDFLAGS)

build/app/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/test/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(APP_TARGET)
	./$(APP_TARGET)

clean:
	rm -rf build bin

-include $(APP_DEPS)
-include $(TEST_DEPS)
