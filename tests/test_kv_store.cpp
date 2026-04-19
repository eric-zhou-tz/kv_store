#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "parser/command_parser.h"
#include "server/cli_server.h"
#include "store/kv_store.h"
#include "persistence/wal.h"

int main() {
  kv::store::KVStore store;
  store.Set("alpha", "1");
  assert(store.Contains("alpha"));
  assert(store.Get("alpha").value() == "1");
  assert(store.Delete("alpha"));
  assert(!store.Get("alpha").has_value());

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

  {
    kv::persistence::WriteAheadLog wal(wal_path);
    kv::store::KVStore logged_store(&wal);
    logged_store.Set("alpha", "1");
    logged_store.Set("message", "hello world");
    assert(logged_store.Delete("alpha"));
  }

  {
    kv::persistence::WriteAheadLog wal(wal_path);
    kv::store::KVStore recovered_store(&wal);
    const std::size_t recovered_operations = recovered_store.ReplayFromWal(wal);
    assert(recovered_operations == 3);
    assert(!recovered_store.Contains("alpha"));
    assert(recovered_store.Get("message").value() == "hello world");
  }

  std::remove(wal_path.c_str());

  {
    std::ofstream malformed_wal(wal_path);
    malformed_wal << "SET missing_value\n";
    malformed_wal << "UNKNOWN key value\n";
    malformed_wal << "DELETE key extra\n";
    malformed_wal << "SET good value\n";
    malformed_wal << "DELETE absent\n";
  }

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
