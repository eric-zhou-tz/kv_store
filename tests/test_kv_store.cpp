#include <cassert>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "parser/command_parser.h"
#include "server/cli_server.h"
#include "store/kv_store.h"
#include "persistence/wal.h"

namespace {

template <typename T>
void WritePrimitive(std::ofstream& output, const T& value) {
  output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void AppendUnknownWalRecord(const std::string& path) {
  // Unknown operations are framed correctly so replay can skip them and keep
  // reading later records.
  std::ofstream output(path, std::ios::binary | std::ios::app);
  const std::uint32_t record_length = sizeof(std::uint8_t);
  const std::uint8_t op = 99;
  WritePrimitive(output, record_length);
  WritePrimitive(output, op);
}

void AppendTruncatedWalRecord(const std::string& path) {
  // The length promises more bytes than are written, simulating a crash during
  // the final WAL append.
  std::ofstream output(path, std::ios::binary | std::ios::app);
  const std::uint32_t record_length = 8;
  const std::uint8_t op = 1;
  WritePrimitive(output, record_length);
  WritePrimitive(output, op);
}

}  // namespace

int main() {
  // Basic in-memory store behavior without persistence.
  kv::store::KVStore store;
  store.Set("alpha", "1");
  assert(store.Contains("alpha"));
  assert(store.Get("alpha").value() == "1");
  assert(store.Delete("alpha"));
  assert(!store.Get("alpha").has_value());

  // Parser accepts command aliases and preserves SET values.
  kv::parser::CommandParser parser;
  const kv::parser::Command set_command = parser.Parse("SET project kv_store");
  assert(set_command.IsValid());
  assert(set_command.type == kv::parser::CommandType::kSet);
  assert(set_command.key == "project");
  assert(set_command.value == "kv_store");

  const kv::parser::Command delete_command = parser.Parse("DELETE project");
  assert(delete_command.IsValid());
  assert(delete_command.type == kv::parser::CommandType::kDel);
  assert(delete_command.key == "project");

  // Exercise the CLI loop through streams so the test stays deterministic.
  std::istringstream input("SET name codex\nGET name\nDEL name\nGET name\nEXIT\n");
  std::ostringstream output;
  kv::server::CliServer server(parser, store);
  server.Run(input, output);

  const std::string transcript = output.str();
  assert(transcript.find("OK") != std::string::npos);
  assert(transcript.find("codex") != std::string::npos);
  assert(transcript.find("(nil)") != std::string::npos);
  assert(transcript.find("Bye") != std::string::npos);

  const std::string wal_path = "/tmp/kv_store_wal_test.log";
  std::remove(wal_path.c_str());

  // Write a real WAL through the store, including a value with spaces and a
  // delete that must survive replay.
  {
    kv::persistence::WriteAheadLog wal(wal_path);
    kv::store::KVStore logged_store(&wal);
    logged_store.Set("alpha", "1");
    logged_store.Set("message", "hello world");
    assert(logged_store.Delete("alpha"));
  }

  // Reopen the WAL to mimic a process restart.
  {
    kv::persistence::WriteAheadLog wal(wal_path);
    kv::store::KVStore recovered_store(&wal);
    const std::size_t recovered_operations = recovered_store.ReplayFromWal(wal);
    assert(recovered_operations == 3);
    assert(!recovered_store.Contains("alpha"));
    assert(recovered_store.Get("message").value() == "hello world");
  }

  std::remove(wal_path.c_str());

  // Replay should count and apply valid records while skipping malformed
  // bounded records.
  {
    kv::persistence::WriteAheadLog wal(wal_path);
    wal.append_set("good", "value");
  }

  AppendUnknownWalRecord(wal_path);

  {
    kv::persistence::WriteAheadLog wal(wal_path);
    wal.append_delete("absent");
  }

  AppendTruncatedWalRecord(wal_path);

  // The truncated trailing record stops recovery after the earlier valid
  // records.
  {
    kv::persistence::WriteAheadLog wal(wal_path);
    std::unordered_map<std::string, std::string> recovered;
    const std::size_t recovered_operations = wal.replay(recovered);
    assert(recovered_operations == 2);
    assert(recovered["good"] == "value");
    assert(recovered.find("absent") == recovered.end());
  }

  std::remove(wal_path.c_str());

  return 0;
}
