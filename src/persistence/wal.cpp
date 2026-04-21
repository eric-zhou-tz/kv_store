#include "persistence/binary_io.h"
#include "persistence/wal.h"

#include <cstdint>
#include <limits>
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

// Applies a single framed WAL record to the store map.
bool apply_record(const std::string& record,
                  std::unordered_map<std::string, std::string>& store) {
  std::size_t offset = 0;

  // All record variants start with an opcode and a key.

  // Operation
  OpType op = 0;
  if (!binary_io::ConsumePrimitive(record, offset, op)) {
    return false;
  }

  // Key Size
  SizeType key_size = 0;
  if (!binary_io::ConsumePrimitive(record, offset, key_size)) {
    return false;
  }

  // Key itself
  std::string key;
  if (!binary_io::ConsumeBytes(record, offset, key_size, key)) {
    return false;
  }

  // Do nothing if the op is GET
  if (op == static_cast<OpType>(WalOp::Set)) {
    // SET records carry exactly one value after the key. Extra trailing bytes
    // make the record malformed.
    SizeType value_size = 0;
    if (!binary_io::ConsumePrimitive(record, offset, value_size)) {
      return false;
    }

    std::string value;
    if (!binary_io::ConsumeBytes(record, offset, value_size, value) ||
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

WriteAheadLog::WriteAheadLog(std::string path)
    : path_(std::move(path)),
      output_(path_, std::ios::binary | std::ios::app) {
  if (!output_.is_open()) {
    throw std::runtime_error("failed to open WAL file: " + path_);
  }
}

void WriteAheadLog::AppendSet(const std::string& key, const std::string& value) {
  const OpType op = static_cast<OpType>(WalOp::Set);
  const SizeType key_size = binary_io::CheckedSize(key, "WAL key");
  const SizeType value_size = binary_io::CheckedSize(value, "WAL value");
  // Length covers the payload after the length field itself:
  // [op][key_size][key][value_size][value].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size +
                            sizeof(value_size) + value_size);

  binary_io::WritePrimitive(output_, record_length, "WAL primitive");
  binary_io::WritePrimitive(output_, op, "WAL primitive");
  binary_io::WritePrimitive(output_, key_size, "WAL primitive");
  binary_io::WriteBytes(output_, key, "WAL bytes");
  binary_io::WritePrimitive(output_, value_size, "WAL primitive");
  binary_io::WriteBytes(output_, value, "WAL bytes");

  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL SET record");
  }
}

void WriteAheadLog::AppendDelete(const std::string& key) {
  const OpType op = static_cast<OpType>(WalOp::Delete);
  const SizeType key_size = binary_io::CheckedSize(key, "WAL key");
  // Length covers the payload after the length field itself:
  // [op][key_size][key].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size);

  binary_io::WritePrimitive(output_, record_length, "WAL primitive");
  binary_io::WritePrimitive(output_, op, "WAL primitive");
  binary_io::WritePrimitive(output_, key_size, "WAL primitive");
  binary_io::WriteBytes(output_, key, "WAL bytes");

  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL DELETE record");
  }
}

std::uint64_t WriteAheadLog::CurrentOffset() {
  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to flush WAL before reading offset");
  }

  output_.seekp(0, std::ios::end);
  if (!output_) {
    throw std::runtime_error("failed to seek WAL output stream");
  }

  const std::streampos position = output_.tellp();
  if (position == std::streampos(-1)) {
    throw std::runtime_error("failed to read WAL offset");
  }

  return static_cast<std::uint64_t>(position);
}

void WriteAheadLog::Clear() {
  // The WAL keeps an append stream open for normal writes. Close and reopen it
  // around truncation so future SET/DELETE records continue using the same WAL
  // object after persistence has been cleared.
  output_.close();
  output_.clear();

  {
    std::ofstream truncated(path_, std::ios::binary | std::ios::trunc);
    if (!truncated.is_open()) {
      throw std::runtime_error("failed to truncate WAL file: " + path_);
    }

    truncated.flush();
    if (!truncated) {
      throw std::runtime_error("failed to clear WAL file: " + path_);
    }
  }

  output_.open(path_, std::ios::binary | std::ios::app);
  if (!output_.is_open()) {
    throw std::runtime_error("failed to reopen WAL file: " + path_);
  }
}

std::size_t WriteAheadLog::Replay(
    std::unordered_map<std::string, std::string>& store) const {
  return ReplayFrom(0, store);
}

std::size_t WriteAheadLog::ReplayFrom(
    std::uint64_t offset,
    std::unordered_map<std::string, std::string>& store) const {
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    throw std::runtime_error("WAL replay offset is too large");
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input.is_open()) {
    return 0;
  }

  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) {
    return 0;
  }

  std::size_t recovered_operations = 0;

  while (true) {
    // Each iteration reads one framed record. A missing length means ordinary
    // EOF; a partial payload means the last write was torn and recovery stops.
    LengthType record_length = 0;
    if (!binary_io::ReadPrimitive(input, record_length)) {
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
