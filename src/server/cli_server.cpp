#include "server/cli_server.h"

#include <exception>
#include <iostream>
#include <string>

namespace kv {
namespace server {

CliServer::CliServer(parser::CommandParser& parser, store::KVStore& store)
  : parser_(parser), store_(store) {}

void CliServer::Run(std::istream& input, std::ostream& output) {
  bool running = true;
  std::string line;

  while (running) {
    output << "kv-store> ";
    output.flush();

    if (!std::getline(input, line)) {
      output << '\n';
      break;
    }

    const parser::Command command = parser_.Parse(line);
    Execute(command, output, running);
  }
}

void CliServer::Execute(const parser::Command& command, std::ostream& output, bool& running) {
  if (!command.IsValid()) {
    output << "ERR " << command.error_message << '\n';
    return;
  }

  try {
    switch (command.type) {
      case parser::CommandType::kSet:
        store_.Set(command.key, command.value);
        output << "OK\n";
        break;
      case parser::CommandType::kGet: {
        const auto value = store_.Get(command.key);
        output << (value.has_value() ? *value : "(nil)") << '\n';
        break;
      }
      case parser::CommandType::kDel:
        output << (store_.Delete(command.key) ? "1" : "0") << '\n';
        break;
      case parser::CommandType::kHelp:
        PrintHelp(output);
        break;
      case parser::CommandType::kExit:
        output << "Bye\n";
        running = false;
        break;
      case parser::CommandType::kInvalid:
        output << "ERR invalid command\n";
        break;
    }
  } catch (const std::exception& error) {
    output << "ERR " << error.what() << '\n';
  }
}

void CliServer::PrintHelp(std::ostream& output) const {
  output << "Commands:\n";
  output << "  SET <key> <value>\n";
  output << "  GET <key>\n";
  output << "  DEL|DELETE <key>\n";
  output << "  HELP\n";
  output << "  EXIT\n";
}

}  // namespace server
}  // namespace kv
