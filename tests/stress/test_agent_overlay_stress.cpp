#include "parser/command_parser.h"

#include <gtest/gtest.h>

#if defined(__linux__)
#include <pty.h>
#else
#include <util.h>
#endif

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Json = kv::parser::Json;

constexpr char kBinaryPath[] = "./bin/kv_store";
constexpr char kPrompt[] = "agentkv> ";

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

Json ExtractLastJsonLine(const std::string& text) {
  std::size_t line_begin = 0;
  Json last_json;
  bool found = false;

  while (line_begin < text.size()) {
    const std::size_t line_end = text.find('\n', line_begin);
    std::string line = text.substr(
        line_begin,
        line_end == std::string::npos ? std::string::npos : line_end - line_begin);
    line = TrimTrailingWhitespace(line);
    if (!line.empty() && line.front() == '{') {
      try {
        last_json = Json::parse(line);
        found = true;
      } catch (const Json::parse_error&) {
      }
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_begin = line_end + 1;
  }

  if (!found) {
    throw std::runtime_error("failed to find JSON response in interactive output");
  }
  return last_json;
}

class InteractiveShellSession {
 public:
  explicit InteractiveShellSession(const std::string& db_path) {
    pid_ = forkpty(&master_fd_, nullptr, nullptr, nullptr);
    if (pid_ < 0) {
      throw std::runtime_error("failed to fork interactive shell");
    }

    if (pid_ == 0) {
      execl(kBinaryPath, kBinaryPath, "--db", db_path.c_str(),
            static_cast<char*>(nullptr));
      _exit(127);
    }

    ReadUntilPrompt();
  }

  ~InteractiveShellSession() {
    if (master_fd_ >= 0) {
      close(master_fd_);
    }
    if (pid_ > 0) {
      int status = 0;
      waitpid(pid_, &status, WNOHANG);
    }
  }

  Json SendRequest(const Json& request) {
    const std::string payload = request.dump() + "\n";
    WriteAll(payload);
    return ExtractLastJsonLine(ReadUntilPrompt());
  }

  void WriteRaw(const std::string& text) {
    WriteAll(text);
  }

  void KillNow() {
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
    if (master_fd_ >= 0) {
      close(master_fd_);
      master_fd_ = -1;
    }
  }

 private:
  void WriteAll(const std::string& text) {
    const char* cursor = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
      const ssize_t written = write(master_fd_, cursor, remaining);
      if (written < 0) {
        throw std::runtime_error("failed to write to interactive shell");
      }
      cursor += written;
      remaining -= static_cast<std::size_t>(written);
    }
  }

  std::string ReadUntilPrompt() {
    std::string output;
    char buffer[512];

    while (output.find(kPrompt) == std::string::npos) {
      fd_set read_set;
      FD_ZERO(&read_set);
      FD_SET(master_fd_, &read_set);

      timeval timeout;
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;

      const int ready =
          select(master_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        throw std::runtime_error("timed out waiting for interactive prompt");
      }

      const ssize_t read_count = read(master_fd_, buffer, sizeof(buffer));
      if (read_count <= 0) {
        throw std::runtime_error("interactive shell closed unexpectedly");
      }
      output.append(buffer, static_cast<std::size_t>(read_count));
    }

    return output;
  }

  pid_t pid_ = -1;
  int master_fd_ = -1;
};

class AgentOverlayRepoStressTest : public ::testing::Test {
 protected:
  AgentOverlayRepoStressTest()
      : db_root_(std::filesystem::path("data") / "test_agent_overlay_stress") {}

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

TEST_F(AgentOverlayRepoStressTest,
       RepeatedOneShotRestartsPreserveLatestValuesInRepoDataFolder) {
  constexpr int kWrites = 250;

  for (int index = 0; index < kWrites; ++index) {
    const Json response = Execute(
        Json{{"action", "put"},
             {"params",
              Json{{"key", "stress/key/" + std::to_string(index)},
                   {"value", "value-" + std::to_string(index)}}}});
    ASSERT_TRUE(response.at("ok").get<bool>());
  }

  for (int index = 0; index < kWrites; index += 25) {
    const Json response = Execute(
        Json{{"action", "get"},
             {"params", Json{{"key", "stress/key/" + std::to_string(index)}}}});
    EXPECT_EQ("value-" + std::to_string(index),
              response.at("value").get<std::string>());
  }

  EXPECT_TRUE(std::filesystem::exists(db_root_ / "kv_store.wal"));
}

TEST_F(AgentOverlayRepoStressTest,
       CrashMidInteractiveSessionRecoversCommittedWritesAndIgnoresPartialInput) {
  {
    InteractiveShellSession shell(db_root_.string());
    ASSERT_TRUE(shell.SendRequest(
                    Json{{"action", "put"},
                         {"params", Json{{"key", "crash/key"}, {"value", "1"}}}})
                    .at("ok")
                    .get<bool>());
    ASSERT_TRUE(shell.SendRequest(
                    Json{{"action", "save_memory"},
                         {"params",
                          Json{{"memory_id", "mem_crash"},
                               {"type", "short_term"},
                               {"content", "persist me"},
                               {"metadata", Json{{"source", "stress"}}},
                               {"created_at", "2026-04-26T12:00:00Z"}}}})
                    .at("ok")
                    .get<bool>());

    shell.WriteRaw(
        R"({"action":"put","params":{"key":"partial/key","value":"no-commit"}})");
    shell.KillNow();
  }

  const Json key_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "crash/key"}}}});
  EXPECT_EQ("1", key_response.at("value").get<std::string>());

  const Json memory_response = Execute(
      Json{{"action", "get_memory"},
           {"params", Json{{"memory_id", "mem_crash"}}}});
  EXPECT_EQ("persist me",
            memory_response.at("value").at("content").get<std::string>());

  const Json partial_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "partial/key"}}}});
  EXPECT_FALSE(partial_response.at("ok").get<bool>());
}

TEST_F(AgentOverlayRepoStressTest,
       CrashAfterLargeInteractiveWriteBurstRecoversSnapshotAndWalTail) {
  constexpr int kWrites = 1050;

  {
    InteractiveShellSession shell(db_root_.string());
    for (int index = 0; index < kWrites; ++index) {
      const Json response = shell.SendRequest(
          Json{{"action", "put"},
               {"params",
                Json{{"key", "burst/key/" + std::to_string(index)},
                     {"value", "burst-value-" + std::to_string(index)}}}});
      ASSERT_TRUE(response.at("ok").get<bool>());
    }

    shell.WriteRaw(
        R"({"action":"put","params":{"key":"burst/partial","value":"pending"}})");
    shell.KillNow();
  }

  EXPECT_TRUE(std::filesystem::exists(db_root_ / "kv_store.snapshot"));
  EXPECT_TRUE(std::filesystem::exists(db_root_ / "kv_store.wal"));

  const Json first_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "burst/key/0"}}}});
  EXPECT_EQ("burst-value-0", first_response.at("value").get<std::string>());

  const Json last_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "burst/key/1049"}}}});
  EXPECT_EQ("burst-value-1049",
            last_response.at("value").get<std::string>());

  const Json partial_response = Execute(
      Json{{"action", "get"},
           {"params", Json{{"key", "burst/partial"}}}});
  EXPECT_FALSE(partial_response.at("ok").get<bool>());
}

}  // namespace
