#include "persistence/wal.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kv {
namespace persistence {

namespace {

using LengthType = std::uint32_t;
using SizeType = std::uint32_t;
using OpType = std::uint8_t;

// Bound individual records so corrupt lengths cannot force unbounded memory
// allocation during replay.
constexpr std::size_t kMaxRecordLength = 64U * 1024U * 1024U;

enum class WalOp : OpType {
  Set = 1,
  Delete = 2,
};

// helper for writing fixed values
template <typename T>
void write_primitive(std::ofstream& out, const T& value) {
  // WAL integers are written in the host byte order for now. This is fine for a
  // local toy store, but a portable format would pin an explicit endianness.
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  if (!out) {
    throw std::runtime_error("failed to write WAL primitive");
  }
}

template <typename T>
bool read_primitive(std::ifstream& in, T& value) {
  // Return false for short reads so replay can stop cleanly at a torn trailing
  // record.
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

// helper for writing dynamic values
void write_bytes(std::ofstream& out, const std::string& s) {
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
  if (!out) {
    throw std::runtime_error("failed to write WAL bytes");
  }
}

// Ensure inputs do not exceed allocated size to avoid truncation
SizeType checked_size(const std::string& bytes, const char* field_name) {
  // The file format stores dynamic field lengths as uint32_t, so reject values
  // that would truncate during serialization.
  if (bytes.size() > std::numeric_limits<SizeType>::max()) {
    throw std::runtime_error(std::string("WAL ") + field_name + " is too large");
  }
  return static_cast<SizeType>(bytes.size());
}

LengthType checked_record_length(std::size_t record_length) {
  // Keep writer and reader limits aligned: if we will not replay it, do not
  // write it.
  if (record_length > kMaxRecordLength) {
    throw std::runtime_error("WAL record is too large");
  }
  return static_cast<LengthType>(record_length);
}

// Parser Helpers
template <typename T>
bool consume_primitive(const std::string& record, std::size_t& offset, T& value) {
  // Parse from an already-bounded record buffer. The offset check keeps malformed
  // payloads from reading past the record boundary.
  if (offset > record.size() || record.size() - offset < sizeof(T)) {
    return false;
  }
  std::memcpy(&value, record.data() + offset, sizeof(T));
  offset += sizeof(T);
  return true;
}

bool consume_bytes(const std::string& record, std::size_t& offset, SizeType size,
                   std::string& value) {
  // Dynamic fields are trusted only after confirming the declared length fits
  // inside the current record payload.
  if (offset > record.size() || record.size() - offset < size) {
    return false;
  }
  value.assign(record.data() + offset, size);
  offset += size;
  return true;
}

// Reconstruct the KV store by line
bool apply_record(const std::string& record,
                  std::unordered_map<std::string, std::string>& store) {
  std::size_t offset = 0;

  // All record variants start with an opcode and a key.

  // Operation
  OpType op = 0;
  if (!consume_primitive(record, offset, op)) {
    return false;
  }

  // Key Size
  SizeType key_size = 0;
  if (!consume_primitive(record, offset, key_size)) {
    return false;
  }

  // Key itself
  std::string key;
  if (!consume_bytes(record, offset, key_size, key)) {
    return false;
  }

  // Do nothing if the op is GET
  if (op == static_cast<OpType>(WalOp::Set)) {
    // SET records carry exactly one value after the key. Extra trailing bytes
    // make the record malformed.
    SizeType value_size = 0;
    if (!consume_primitive(record, offset, value_size)) {
      return false;
    }

    std::string value;
    if (!consume_bytes(record, offset, value_size, value) ||
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
  const SizeType key_size = checked_size(key, "key");
  const SizeType value_size = checked_size(value, "value");
  // Length covers the payload after the length field itself:
  // [op][key_size][key][value_size][value].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size +
                            sizeof(value_size) + value_size);

  write_primitive(output_, record_length);
  write_primitive(output_, op);
  write_primitive(output_, key_size);
  write_bytes(output_, key);
  write_primitive(output_, value_size);
  write_bytes(output_, value);

  output_.flush();
  if (!output_) {
    throw std::runtime_error("failed to write WAL SET record");
  }
}

void WriteAheadLog::append_delete(const std::string& key) {
  const OpType op = static_cast<OpType>(WalOp::Delete);
  const SizeType key_size = checked_size(key, "key");
  // Length covers the payload after the length field itself:
  // [op][key_size][key].
  const LengthType record_length =
      checked_record_length(sizeof(op) + sizeof(key_size) + key_size);

  write_primitive(output_, record_length);
  write_primitive(output_, op);
  write_primitive(output_, key_size);
  write_bytes(output_, key);

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
    if (!read_primitive(input, record_length)) {
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
