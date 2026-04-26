#include "parser/command_parser.h"

#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using Json = kv::parser::Json;

constexpr char kBinaryPath[] = "./bin/kv_store";

struct ProcessResult {
  int exit_code = -1;
  std::string output;
};

std::string TrimTrailingWhitespace(std::string text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  return text;
}

ProcessResult RunOneShotRequest(const std::string& db_path,
                                const std::string& raw_request) {
  int stdin_pipe[2];
  int stdout_pipe[2];
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    throw std::runtime_error("failed to create process pipes");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork process");
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    execl(kBinaryPath, kBinaryPath, "--db", db_path.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);

  const std::string request_with_newline = raw_request + "\n";
  const ssize_t written =
      write(stdin_pipe[1], request_with_newline.data(),
            static_cast<size_t>(request_with_newline.size()));
  (void)written;
  close(stdin_pipe[1]);

  std::string output;
  char buffer[512];
  ssize_t read_count = 0;
  while ((read_count = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<size_t>(read_count));
  }
  close(stdout_pipe[0]);

  int status = 0;
  waitpid(pid, &status, 0);

  ProcessResult result;
  result.exit_code =
      WIFEXITED(status) ? WEXITSTATUS(status) : (128 + WTERMSIG(status));
  result.output = TrimTrailingWhitespace(output);
  return result;
}

Json ParseJsonOutput(const ProcessResult& result) {
  return Json::parse(result.output);
}

class AgentOverlayPersistenceTest : public ::testing::Test {
 protected:
  AgentOverlayPersistenceTest()
      : db_root_(std::filesystem::path("data") /
                 "test_agent_overlay_persistence") {}

  void SetUp() override {
    std::filesystem::remove_all(db_root_);
    std::filesystem::create_directories(db_root_);
  }

  void TearDown() override {
    std::filesystem::remove_all(db_root_);
  }

  Json Execute(const Json& request) const {
    const ProcessResult result =
        RunOneShotRequest(db_root_.string(), request.dump());
    EXPECT_EQ(0, result.exit_code) << result.output;
    return ParseJsonOutput(result);
  }

  std::filesystem::path db_root_;
};

TEST_F(AgentOverlayPersistenceTest,
       DirectAgentRequestsPersistAcrossRealProcessRestarts) {
  Json response = Execute(
      Json{{"action", "put"},
           {"params", Json{{"key", "agent/name"}, {"value", "contextkv"}}}});
  EXPECT_TRUE(response.at("ok").get<bool>());

  response = Execute(
      Json{{"action", "save_memory"},
           {"params",
            Json{{"memory_id", "mem_123"},
                 {"type", "short_term"},
                 {"content", "User prefers concise summaries."},
                 {"metadata", Json{{"source", "agent"}}},
                 {"created_at", "2026-04-26T12:00:00Z"}}}});
  EXPECT_TRUE(response.at("ok").get<bool>());

  response = Execute(
      Json{{"action", "save_run_state"},
           {"params",
            Json{{"run_id", "run_123"},
                 {"status", "running"},
                 {"current_step", 4},
                 {"state", Json{{"foo", "bar"}}},
                 {"updated_at", "2026-04-26T12:00:00Z"}}}});
  EXPECT_TRUE(response.at("ok").get<bool>());

  response = Execute(
      Json{{"action", "log_step"},
           {"params",
            Json{{"run_id", "run_123"},
                 {"step_id", 4},
                 {"status", "success"},
                 {"input", "agent input here"},
                 {"output", "agent output here"},
                 {"timestamp", "2026-04-26T12:00:00Z"}}}});
  EXPECT_TRUE(response.at("ok").get<bool>());

  const Json key_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "agent/name"}}}});
  EXPECT_EQ("contextkv", key_response.at("value").get<std::string>());

  const Json memory_response = Execute(
      Json{{"action", "get_memory"},
           {"params", Json{{"memory_id", "mem_123"}}}});
  EXPECT_EQ("User prefers concise summaries.",
            memory_response.at("value").at("content").get<std::string>());

  const Json run_state_response = Execute(
      Json{{"action", "get_run_state"},
           {"params", Json{{"run_id", "run_123"}}}});
  EXPECT_EQ(4, run_state_response.at("value").at("current_step").get<int>());

  EXPECT_TRUE(std::filesystem::exists(db_root_ / "kv_store.wal"));
}

}  // namespace
