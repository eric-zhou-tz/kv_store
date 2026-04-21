#ifndef KV_STORE_PERSISTENCE_BINARY_IO_H_
#define KV_STORE_PERSISTENCE_BINARY_IO_H_

#include <cstdint>
#include <cstring>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

namespace kv {
namespace persistence {
namespace binary_io {

using SizeType = std::uint32_t;

template <typename T>
void write_primitive(std::ostream& out, const T& value,
                     const char* description = "binary primitive") {
  // Persistence integers are written in host byte order for now. 
  // MAKE SURE TO ENCORPORATE ENDIANNESS IN THE FUTURE
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  if (!out) {
    throw std::runtime_error(std::string("failed to write ") + description);
  }
}

template <typename T>
bool read_primitive(std::istream& in, T& value) {
  // Return false for short reads so callers can stop cleanly at torn trailing
  // records.
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

inline void write_bytes(std::ostream& out, const std::string& bytes,
                        const char* description = "binary bytes") {
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    throw std::runtime_error(std::string("failed to write ") + description);
  }
}

inline SizeType checked_size(const std::string& bytes,
                             const char* field_name) {
  if (bytes.size() > std::numeric_limits<SizeType>::max()) {
    throw std::runtime_error(std::string(field_name) + " is too large");
  }
  return static_cast<SizeType>(bytes.size());
}

template <typename T>
bool consume_primitive(const std::string& buffer, std::size_t& offset,
                       T& value) {
  if (offset > buffer.size() || buffer.size() - offset < sizeof(T)) {
    return false;
  }
  std::memcpy(&value, buffer.data() + offset, sizeof(T));
  offset += sizeof(T);
  return true;
}

inline bool consume_bytes(const std::string& buffer, std::size_t& offset,
                          SizeType size, std::string& value) {
  if (offset > buffer.size() || buffer.size() - offset < size) {
    return false;
  }
  value.assign(buffer.data() + offset, size);
  offset += size;
  return true;
}

}  // namespace binary_io
}  // namespace persistence
}  // namespace kv

#endif  // KV_STORE_PERSISTENCE_BINARY_IO_H_
