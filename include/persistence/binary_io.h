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

/**
 * @brief Small binary serialization helpers shared by persistence formats.
 *
 * These helpers intentionally stay format-neutral. WAL and snapshot code define
 * their own record layouts, while this namespace provides the common operations
 * for fixed-size primitives, length-prefixed byte strings, and bounded parsing
 * from in-memory buffers.
 */
namespace binary_io {

/**
 * @brief Integer type used to encode dynamic byte-field lengths.
 */
using SizeType = std::uint32_t;

/**
 * @brief Writes a trivially copyable primitive value to a binary stream.
 *
 * The value is written in the host byte order used by the current process. A
 * future portable on-disk format should replace this with explicit endianness.
 *
 * @tparam T Primitive value type to write.
 * @param out Destination stream.
 * @param value Value to serialize.
 * @param description Human-readable value description used in error messages.
 * @throws std::runtime_error if the stream write fails.
 */
template <typename T>
void WritePrimitive(std::ostream& out, const T& value,
                     const char* description = "binary primitive") {
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  if (!out) {
    throw std::runtime_error(std::string("failed to write ") + description);
  }
}

/**
 * @brief Reads a fixed-size primitive value from a binary stream.
 *
 * @tparam T Primitive value type to read.
 * @param in Source stream.
 * @param value Output location for the decoded value.
 * @return `true` when a complete value was read, otherwise `false`.
 */
template <typename T>
bool ReadPrimitive(std::istream& in, T& value) {
  // Return false for short reads so callers can stop cleanly at torn trailing
  // records.
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

/**
 * @brief Writes raw string bytes to a binary stream.
 *
 * The caller is responsible for writing any length prefix before calling this
 * helper.
 *
 * @param out Destination stream.
 * @param bytes Bytes to write.
 * @param description Human-readable byte-field description used in errors.
 * @throws std::runtime_error if the stream write fails.
 */
inline void WriteBytes(std::ostream& out, const std::string& bytes,
                        const char* description = "binary bytes") {
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    throw std::runtime_error(std::string("failed to write ") + description);
  }
}

/**
 * @brief Validates that a string length fits in SizeType.
 *
 * @param bytes String whose byte length will be encoded.
 * @param field_name Field name used in error messages.
 * @return The string length cast to SizeType.
 * @throws std::runtime_error if the string is too large to encode.
 */
inline SizeType CheckedSize(const std::string& bytes,
                             const char* field_name) {
  if (bytes.size() > std::numeric_limits<SizeType>::max()) {
    throw std::runtime_error(std::string(field_name) + " is too large");
  }
  return static_cast<SizeType>(bytes.size());
}

/**
 * @brief Consumes a primitive value from an in-memory byte buffer.
 *
 * The offset is advanced only after enough bytes are available for the full
 * value.
 *
 * @tparam T Primitive value type to consume.
 * @param buffer Source byte buffer.
 * @param offset Current read offset, updated on success.
 * @param value Output location for the decoded value.
 * @return `true` when a complete value was consumed, otherwise `false`.
 */
template <typename T>
bool ConsumePrimitive(const std::string& buffer, std::size_t& offset,
                       T& value) {
  if (offset > buffer.size() || buffer.size() - offset < sizeof(T)) {
    return false;
  }
  std::memcpy(&value, buffer.data() + offset, sizeof(T));
  offset += sizeof(T);
  return true;
}

/**
 * @brief Consumes a fixed number of bytes from an in-memory byte buffer.
 *
 * The offset is advanced only after the requested byte range fits inside the
 * buffer.
 *
 * @param buffer Source byte buffer.
 * @param offset Current read offset, updated on success.
 * @param size Number of bytes to consume.
 * @param value Output string populated with the consumed bytes.
 * @return `true` when the bytes were consumed, otherwise `false`.
 */
inline bool ConsumeBytes(const std::string& buffer, std::size_t& offset,
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
