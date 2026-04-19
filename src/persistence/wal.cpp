#include "persistence/wal.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace kv {
namespace persistence {

WriteAheadLog::WriteAheadLog(std::string path)
    : path_(std::move(path)), output_(path_, std::ios::app) {
  // Open once in append mode so every new mutation is added after prior WAL records.
  if (!output_.is_open()) {
    throw std::runtime_error("failed to open WAL file: " + path_);
  }
}

void WriteAheadLog::append_set(const std::string& key, const std::string& value) {
  append_line("SET " + key + " " + value);
}

void WriteAheadLog::append_delete(const std::string& key) {
  append_line("DELETE " + key);
}

std::size_t WriteAheadLog::replay(
    std::unordered_map<std::string, std::string>& store) const {
  std::ifstream input(path_);
  if (!input.is_open()) {
    return 0;
  }

  std::size_t recovered_operations = 0;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream parser(line);
    std::string command;
    std::string key;
    if (!(parser >> command >> key)) {
      continue;
    }

    if (command == "SET") {
      std::string value;
      // Preserve spaces inside values by taking the rest of the line after the key.
      std::getline(parser, value);
      if (value.empty()) {
        continue;
      }
      if (value.front() == ' ') {
        value.erase(0, 1);
      }
      if (value.empty()) {
        continue;
      }

      store[key] = value;
      ++recovered_operations;
      continue;
    }

    if (command == "DELETE") {
      std::string extra;
      // DELETE records should contain exactly one key argument.
      if (parser >> extra) {
        continue;
      }

      store.erase(key);
      ++recovered_operations;
    }
  }

  return recovered_operations;
}

void WriteAheadLog::append_line(const std::string& line) {
  output_ << line << '\n';
  // Flush before the caller updates memory so the WAL is durable first.
  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL record");
  }
}

}  // namespace persistence
}  // namespace kv
