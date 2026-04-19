#include <iostream>

#include "parser/command_parser.h"
#include "server/cli_server.h"
#include "store/kv_store.h"
#include "persistence/wal.h"

/**
 * @brief Creates the application components and starts the CLI loop.
 *
 * @return Exit status code for the operating system.
 */
int main() {
  kv::persistence::WriteAheadLog wal;
  kv::store::KVStore store(&wal);

  std::cout << "Replaying WAL...\n";
  const std::size_t recovered_operations = store.ReplayFromWal(wal);
  std::cout << "Recovered " << recovered_operations << " operation(s)\n";

  kv::parser::CommandParser parser;
  kv::server::CliServer server(parser, store);

  server.Run(std::cin, std::cout);
  return 0;
}
