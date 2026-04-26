CXX ?= g++
PKG_CONFIG ?= pkg-config

CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
JSON_CPPFLAGS :=
DEPFLAGS := -MMD -MP
LDFLAGS ?=
LDLIBS ?=
RUN_ARGS ?=

APP_TARGET := bin/kv_store
TEST_TARGET := bin/kv_store_tests
STRESS_TARGET := bin/kv_store_stress_tests
LIVE_DATA_TARGET := bin/kv_store_live_data_tests
BENCHMARK_TARGET := benchmark

APP_SRCS := \
	src/main.cpp \
	src/common/string_utils.cpp \
	src/parser/command_parser.cpp \
	src/command/command.cpp \
	src/store/kv_store.cpp \
	src/persistence/snapshot.cpp \
	src/persistence/wal.cpp

COMMAND_LAYER_SRCS := \
	src/parser/command_parser.cpp \
	src/command/command.cpp

PERSISTENCE_SRCS := \
	src/store/kv_store.cpp \
	src/persistence/snapshot.cpp \
	src/persistence/wal.cpp

TEST_HELPER_SRCS := \
	tests/helpers/temp_dir.cpp \
	tests/helpers/file_utils.cpp

TEST_SRCS := \
	tests/test_main.cpp \
	tests/integration/test_agent_overlay_persistence.cpp \
	tests/unit/test_command.cpp \
	tests/unit/test_command_parser.cpp \
	tests/unit/test_kv_store.cpp \
	tests/unit/test_wal.cpp \
	tests/unit/test_snapshot.cpp \
	tests/integration/test_recovery.cpp \
	$(TEST_HELPER_SRCS) \
	$(COMMAND_LAYER_SRCS) \
	$(PERSISTENCE_SRCS)

STRESS_SRCS := \
	tests/test_main.cpp \
	tests/stress/test_agent_overlay_stress.cpp \
	tests/stress/test_stress.cpp \
	$(TEST_HELPER_SRCS) \
	$(PERSISTENCE_SRCS)

BENCHMARK_SRCS := \
	bench/benchmark.cpp \
	bench/benchmark_utils.cpp \
	bench/workloads.cpp \
	$(PERSISTENCE_SRCS)

LIVE_DATA_SRCS := \
	tests/test_main.cpp \
	tests/stress/test_agent_overlay_live_data.cpp

GTEST_ROOT ?=
GTEST_VENDOR_DIR := $(firstword $(wildcard external/googletest vendor/googletest))
GTEST_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && $(PKG_CONFIG) --exists gtest && echo yes)

ifneq ($(GTEST_ROOT),)
GTEST_CPPFLAGS := -I$(GTEST_ROOT)/include
GTEST_LDFLAGS := -L$(GTEST_ROOT)/lib -Wl,-rpath,$(GTEST_ROOT)/lib
GTEST_LDLIBS := -lgtest -pthread
GTEST_AVAILABLE := 1
else ifneq ($(GTEST_VENDOR_DIR),)
GTEST_CPPFLAGS := -I$(GTEST_VENDOR_DIR)/googletest/include -I$(GTEST_VENDOR_DIR)/googletest
GTEST_OBJS := build/gtest/gtest-all.o
GTEST_LDLIBS := -pthread
GTEST_AVAILABLE := 1
else ifeq ($(GTEST_PKG_CONFIG),yes)
GTEST_CPPFLAGS := $(shell $(PKG_CONFIG) --cflags gtest)
GTEST_LDFLAGS := $(shell $(PKG_CONFIG) --libs-only-L --libs-only-other gtest)
GTEST_LDLIBS := $(shell $(PKG_CONFIG) --libs-only-l gtest) -pthread
GTEST_AVAILABLE := 1
else ifneq ($(wildcard /opt/homebrew/include/gtest/gtest.h),)
GTEST_CPPFLAGS := -I/opt/homebrew/include
GTEST_LDFLAGS := -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib
GTEST_LDLIBS := -lgtest -pthread
GTEST_AVAILABLE := 1
else ifneq ($(wildcard /usr/local/include/gtest/gtest.h),)
GTEST_CPPFLAGS := -I/usr/local/include
GTEST_LDFLAGS := -L/usr/local/lib -Wl,-rpath,/usr/local/lib
GTEST_LDLIBS := -lgtest -pthread
GTEST_AVAILABLE := 1
else ifneq ($(wildcard /opt/miniconda3/include/gtest/gtest.h),)
GTEST_CPPFLAGS := -I/opt/miniconda3/include
GTEST_LDFLAGS := -L/opt/miniconda3/lib -Wl,-rpath,/opt/miniconda3/lib
GTEST_LDLIBS := -lgtest -pthread
GTEST_AVAILABLE := 1
else
GTEST_AVAILABLE := 0
endif

ifneq ($(wildcard /opt/homebrew/include/nlohmann/json.hpp),)
JSON_CPPFLAGS := -I/opt/homebrew/include
else ifneq ($(wildcard /usr/local/include/nlohmann/json.hpp),)
JSON_CPPFLAGS := -I/usr/local/include
else ifneq ($(wildcard /opt/miniconda3/include/nlohmann/json.hpp),)
JSON_CPPFLAGS := -I/opt/miniconda3/include
endif

CPPFLAGS += $(JSON_CPPFLAGS)

APP_OBJS := $(patsubst %.cpp,build/app/%.o,$(APP_SRCS))
TEST_OBJS := $(patsubst %.cpp,build/test/%.o,$(TEST_SRCS))
STRESS_OBJS := $(patsubst %.cpp,build/stress/%.o,$(STRESS_SRCS))
BENCHMARK_OBJS := $(patsubst %.cpp,build/bench/%.o,$(BENCHMARK_SRCS))
LIVE_DATA_OBJS := $(patsubst %.cpp,build/live_data/%.o,$(LIVE_DATA_SRCS))

APP_DEPS := $(APP_OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
STRESS_DEPS := $(STRESS_OBJS:.o=.d)
BENCHMARK_DEPS := $(BENCHMARK_OBJS:.o=.d)
LIVE_DATA_DEPS := $(LIVE_DATA_OBJS:.o=.d)
GTEST_DEPS := $(GTEST_OBJS:.o=.d)

.PHONY: all clean run test test_verbose test_stress test_live_data run_benchmark check_gtest

all: $(APP_TARGET)

$(APP_TARGET): $(APP_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(APP_OBJS) -o $@ $(LDLIBS)

$(TEST_TARGET): $(APP_TARGET) check_gtest $(TEST_OBJS) $(GTEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(GTEST_LDFLAGS) $(TEST_OBJS) $(GTEST_OBJS) -o $@ $(LDLIBS) $(GTEST_LDLIBS)

$(STRESS_TARGET): $(APP_TARGET) check_gtest $(STRESS_OBJS) $(GTEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(GTEST_LDFLAGS) $(STRESS_OBJS) $(GTEST_OBJS) -o $@ $(LDLIBS) $(GTEST_LDLIBS)

$(LIVE_DATA_TARGET): $(APP_TARGET) check_gtest $(LIVE_DATA_OBJS) $(GTEST_OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(GTEST_LDFLAGS) $(LIVE_DATA_OBJS) $(GTEST_OBJS) -o $@ $(LDLIBS) $(GTEST_LDLIBS)

$(BENCHMARK_TARGET): $(BENCHMARK_OBJS)
	$(CXX) $(LDFLAGS) $(BENCHMARK_OBJS) -o $@ $(LDLIBS)

$(TEST_OBJS) $(STRESS_OBJS): | check_gtest

build/app/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

build/test/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests $(GTEST_CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

build/stress/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests $(GTEST_CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

build/bench/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Ibench $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

build/live_data/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests $(GTEST_CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

build/gtest/gtest-all.o: $(GTEST_VENDOR_DIR)/googletest/src/gtest-all.cc
	mkdir -p $(dir $@)
	$(CXX) $(GTEST_CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

check_gtest:
ifeq ($(GTEST_AVAILABLE),0)
	@echo "GoogleTest not found."
	@echo "Run ./scripts/bootstrap_gtest.sh, install googletest, or set GTEST_ROOT=/path/to/gtest."
	@exit 1
endif

test: $(TEST_TARGET)
	./$(TEST_TARGET) --gtest_color=yes

test_verbose: $(TEST_TARGET)
	./$(TEST_TARGET) --gtest_color=yes --gtest_print_time=1

test_stress: $(STRESS_TARGET)
	./$(STRESS_TARGET) --gtest_color=yes

test_live_data: $(LIVE_DATA_TARGET)
	./$(LIVE_DATA_TARGET) --gtest_color=yes

run_benchmark: $(BENCHMARK_TARGET)
	./$(BENCHMARK_TARGET)

run: $(APP_TARGET)
	./$(APP_TARGET) $(RUN_ARGS)

clean:
	rm -rf build bin $(BENCHMARK_TARGET)

-include $(APP_DEPS)
-include $(TEST_DEPS)
-include $(STRESS_DEPS)
-include $(BENCHMARK_DEPS)
-include $(LIVE_DATA_DEPS)
-include $(GTEST_DEPS)
