#include "persistence/binary_io.h"
#include "persistence/wal.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace kv {
namespace persistence {

namespace {

using LengthType = std::uint32_t;
using SizeType = binary_io::SizeType;
using OpType = std::uint8_t;

// Bound individual records so corrupt lengths cannot force unbounded memory
// allocation during replay.
constexpr std::size_t kMaxRecordLength = 64U * 1024U * 1024U;

enum class WalOp : OpType {
  Set = 1,
  Delete = 2,
};

LengthType checked_record_length(std::size_t record_length) {
  // Keep writer and reader limits aligned: if we will not replay it, do not
  // write it.
  if (record_length > kMaxRecordLength) {
    throw std::runtime_error("WAL record is too large");
  }
  return static_cast<LengthType>(record_length);
}

// Reconstruct the KV store by line
bool apply_record(const std::string& record,
                  std::unordered_map<std::string, std::string>& store) {
  std::size_t offset = 0;

  // All record variants start with an opcode and a key.

  // Operation
  OpType op = 0;
  if (!binary_io::consume_primitive(record, offset, op)) {
    return false;
  }

  // Key Size
  SizeType key_size = 0;
  if (!binary_io::consume_primitive(record, offset, key_size)) {
    return false;
  }

  // Key itself
  std::string key;
  if (!binary_io::consume_bytes(record, offset, key_size, key)) {
    return false;
  }

  // Do nothing if the op is GET
  if (op == static_cast<OpType>(WalOp::Set)) {
    // SET records carry exactly one value after the key. Extra trailing bytes
    // make the record malformed.
    SizeType value_size = 0;
    if (!binary_io::consume_primitive(record, offset, value_size)) {
      return false;
    }

    std::string value;
    if (!binary_io::consume_bytes(record, offset, value_size, value) ||
        offset != record.size()) {
      return false;
    }

    store[key] = value;
    return true;
  }

  if (op == static_cast<OpType>(WalOp::Delete)) {
    // DELETE records end immediately after the key.
    if (offset != record.size()) {
      return false;
    }

    store.erase(key);
    return true;
  }

  return false;
}

}  // namespace

// Constructor
WriteAheadLog::WriteAheadLog(std::string path)
    : path_(std::move(path)),
      output_(path_, std::ios::binary | std::ios::app) {
  if (!output_.is_open()) {
    throw std::runtime_error("failed to open WAL file: " + path_);
  }
}

// write in WAL functions
void WriteAheadLog::append_set(const std::string& key, const std::string& value) {
  const OpType op = static_cast<OpType>(WalOp::Set);
  const SizeType key_size = binary_io::checked_size(key, "WAL key");
  const SizeType value_size = binary_io::checked_size(value, "WAL value");
  // Length covers the payload after the length field itself:
  // [op][key_size][key][value_size][value].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size +
                            sizeof(value_size) + value_size);

  binary_io::write_primitive(output_, record_length, "WAL primitive");
  binary_io::write_primitive(output_, op, "WAL primitive");
  binary_io::write_primitive(output_, key_size, "WAL primitive");
  binary_io::write_bytes(output_, key, "WAL bytes");
  binary_io::write_primitive(output_, value_size, "WAL primitive");
  binary_io::write_bytes(output_, value, "WAL bytes");

  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL SET record");
  }
}

void WriteAheadLog::append_delete(const std::string& key) {
  const OpType op = static_cast<OpType>(WalOp::Delete);
  const SizeType key_size = binary_io::checked_size(key, "WAL key");
  // Length covers the payload after the length field itself:
  // [op][key_size][key].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size);

  binary_io::write_primitive(output_, record_length, "WAL primitive");
  binary_io::write_primitive(output_, op, "WAL primitive");
  binary_io::write_primitive(output_, key_size, "WAL primitive");
  binary_io::write_bytes(output_, key, "WAL bytes");

  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL DELETE record");
  }
}

// Replay the KV store after startup
std::size_t WriteAheadLog::replay(
    std::unordered_map<std::string, std::string>& store) const {
  std::ifstream input(path_, std::ios::binary);
  if (!input.is_open()) {
    return 0;
  }

  std::size_t recovered_operations = 0;

  while (true) {
    // Each iteration reads one framed record. A missing length means ordinary
    // EOF; a partial payload means the last write was torn and recovery stops.
    LengthType record_length = 0;
    if (!binary_io::read_primitive(input, record_length)) {
      break;
    }

    if (record_length > kMaxRecordLength) {
      break;
    }

    // Read the full payload before parsing so malformed records remain bounded
    // and cannot desynchronize the next framed record.
    std::string record(record_length, '\0');
    input.read(record.data(), static_cast<std::streamsize>(record.size()));
    if (!input) {
      break;
    }

    if (apply_record(record, store)) {
      ++recovered_operations;
    }
  }

  return recovered_operations;
}

}  // namespace persistence
}  // namespace kv
