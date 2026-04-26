#include "command/command.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "store/kv_store.h"

namespace {

using kv::command::Json;
using kv::command::execute_command;
using kv::store::KVStore;

TEST(CommandExecutionTest, PutStoresRawStringValue) {
  KVStore store;
  const Json response = execute_command(
      Json{{"action", "put"},
           {"params", Json{{"key", "alpha"}, {"value", "hello world"}}}},
      store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  EXPECT_EQ("put", response.at("action").get<std::string>());
  EXPECT_EQ("alpha", response.at("key").get<std::string>());
  ASSERT_TRUE(store.Get("alpha").has_value());
  EXPECT_EQ("hello world", store.Get("alpha").value());
}

TEST(CommandExecutionTest, PutSerializesStructuredJsonValues) {
  KVStore store;
  const Json response = execute_command(
      Json{{"action", "put"},
           {"params",
            Json{{"key", "config"},
                 {"value", Json{{"enabled", true}, {"retries", 3}}}}}},
      store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  ASSERT_TRUE(store.Get("config").has_value());
  EXPECT_EQ(Json({{"enabled", true}, {"retries", 3}}).dump(),
            store.Get("config").value());
}

TEST(CommandExecutionTest, GetReturnsStoredValueWhenPresent) {
  KVStore store;
  store.Set("alpha", "1");

  const Json response = execute_command(
      Json{{"action", "get"}, {"params", Json{{"key", "alpha"}}}}, store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  EXPECT_EQ("1", response.at("value").get<std::string>());
}

TEST(CommandExecutionTest, GetReturnsNotFoundResponseWhenKeyIsMissing) {
  KVStore store;

  const Json response = execute_command(
      Json{{"action", "get"}, {"params", Json{{"key", "missing"}}}}, store);

  EXPECT_FALSE(response.at("ok").get<bool>());
  EXPECT_EQ("not found", response.at("error").get<std::string>());
}

TEST(CommandExecutionTest, DeleteRemovesStoredKey) {
  KVStore store;
  store.Set("alpha", "1");

  const Json response = execute_command(
      Json{{"action", "delete"}, {"params", Json{{"key", "alpha"}}}}, store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  EXPECT_FALSE(store.Contains("alpha"));
}

TEST(CommandExecutionTest, LogStepStoresParamsUnderHierarchicalRunKey) {
  KVStore store;
  const Json params = {
      {"run_id", "run_123"},
      {"step_id", 4},
      {"status", "success"},
      {"input", "agent input here"},
      {"output", "agent output here"},
      {"timestamp", "2026-04-26T12:00:00Z"},
  };

  const Json response =
      execute_command(Json{{"action", "log_step"}, {"params", params}}, store);

  EXPECT_EQ("runs/run_123/steps/4",
            response.at("key").get<std::string>());
  ASSERT_TRUE(store.Get("runs/run_123/steps/4").has_value());
  EXPECT_EQ(params.dump(), store.Get("runs/run_123/steps/4").value());
}

TEST(CommandExecutionTest, SaveAndGetMemoryRoundTripStructuredJson) {
  KVStore store;
  const Json save_params = {
      {"memory_id", "mem_123"},
      {"type", "short_term"},
      {"content", "User prefers concise summaries."},
      {"metadata", Json{{"source", "agent"}}},
      {"created_at", "2026-04-26T12:00:00Z"},
  };

  execute_command(
      Json{{"action", "save_memory"}, {"params", save_params}}, store);
  const Json response = execute_command(
      Json{{"action", "get_memory"},
           {"params", Json{{"memory_id", "mem_123"}}}},
      store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  EXPECT_EQ("memory/mem_123", response.at("key").get<std::string>());
  EXPECT_EQ(save_params, response.at("value"));
}

TEST(CommandExecutionTest, SaveAndGetRunStateRoundTripStructuredJson) {
  KVStore store;
  const Json save_params = {
      {"run_id", "run_123"},
      {"status", "running"},
      {"current_step", 4},
      {"state", Json{{"foo", "bar"}}},
      {"updated_at", "2026-04-26T12:00:00Z"},
  };

  execute_command(
      Json{{"action", "save_run_state"}, {"params", save_params}}, store);
  const Json response = execute_command(
      Json{{"action", "get_run_state"},
           {"params", Json{{"run_id", "run_123"}}}},
      store);

  EXPECT_TRUE(response.at("ok").get<bool>());
  EXPECT_EQ("runs/run_123/state", response.at("key").get<std::string>());
  EXPECT_EQ(save_params, response.at("value"));
}

TEST(CommandExecutionTest, MissingRequiredParamThrowsClearError) {
  KVStore store;

  EXPECT_THROW(
      {
        try {
          execute_command(
              Json{{"action", "put"},
                   {"params", Json{{"value", "missing key"}}}},
              store);
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request.params.key is required", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandExecutionTest, UnsupportedActionThrowsClearError) {
  KVStore store;

  EXPECT_THROW(
      {
        try {
          execute_command(
              Json{{"action", "unknown"}, {"params", Json::object()}}, store);
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("unsupported action: unknown", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

}  // namespace
