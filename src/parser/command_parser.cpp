#include "parser/command_parser.h"

#include <sstream>
#include <vector>

#include "common/string_utils.h"

namespace kv {
namespace parser {

namespace {

/**
 * @brief Joins token views into a space-separated string.
 *
 * @param tokens Token sequence to join.
 * @param begin_index Index of the first token to include.
 * @return Joined string containing the selected tokens.
 */
std::string JoinTokens(const std::vector<std::string_view>& tokens, std::size_t begin_index) {
  std::ostringstream joined;
  for (std::size_t index = begin_index; index < tokens.size(); ++index) {
    if (index > begin_index) {
      joined << ' ';
    }
    joined << tokens[index];
  }
  return joined.str();
}

/**
 * @brief Constructs an invalid command with an error message.
 *
 * @param message Parsing failure description.
 * @return Invalid command populated with the supplied message.
 */
Command MakeInvalidCommand(const std::string& message) {
  Command command;
  command.type = CommandType::kInvalid;
  command.error_message = message;
  return command;
}

}  // namespace

bool Command::IsValid() const {
  return type != CommandType::kInvalid;
}

Command CommandParser::Parse(const std::string& input) const {
  const std::string trimmed = common::Trim(input);
  if (trimmed.empty()) {
    return MakeInvalidCommand("empty command");
  }

  const std::vector<std::string_view> tokens = common::SplitWhitespaceView(trimmed);
  const std::string verb = common::ToUpper(tokens.front());

  if (verb == "SET") {
    if (tokens.size() < 3) {
      return MakeInvalidCommand("usage: SET <key> <value>");
    }

    Command command;
    command.type = CommandType::kSet;
    command.key = std::string(tokens[1]);
    command.value = JoinTokens(tokens, 2);
    return command;
  }

  if (verb == "GET") {
    if (tokens.size() != 2) {
      return MakeInvalidCommand("usage: GET <key>");
    }

    Command command;
    command.type = CommandType::kGet;
    command.key = std::string(tokens[1]);
    return command;
  }

  if (verb == "DEL" || verb == "DELETE") {
    if (tokens.size() != 2) {
      return MakeInvalidCommand("usage: DELETE <key>");
    }

    Command command;
    command.type = CommandType::kDel;
    command.key = std::string(tokens[1]);
    return command;
  }

  if (verb == "HELP") {
    Command command;
    command.type = CommandType::kHelp;
    return command;
  }

  if (verb == "EXIT" || verb == "QUIT") {
    Command command;
    command.type = CommandType::kExit;
    return command;
  }

  return MakeInvalidCommand("unknown command");
}

}  // namespace parser
}  // namespace kv
