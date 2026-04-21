#ifndef KV_STORE_WAL_H_
#define KV_STORE_WAL_H_

#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>

namespace kv {
namespace persistence {

/**
 * @brief Append-only write-ahead log for durable SET and DELETE operations.
 */
class WriteAheadLog {
 public:
  /**
   * @brief Opens the WAL file in append mode.
   *
   * @param path Path to the WAL file.
   */
  explicit WriteAheadLog(std::string path = "kv_store.wal");

  /**
   * @brief Appends and flushes a SET record.
   *
   * @param key Key being written.
   * @param value Value being written.
   */
  void append_set(const std::string& key, const std::string& value);

  /**
   * @brief Appends and flushes a DELETE record.
   *
   * @param key Key being deleted.
   */
  void append_delete(const std::string& key);

  /**
   * @brief Replays valid WAL records into an in-memory map.
   *
   * Malformed bounded records are skipped. Replay stops at EOF, an incomplete
   * trailing record, or an impossible record length.
   *
   * @param store Store map to update while replaying the log.
   * @return Number of valid operations applied.
   */
  std::size_t replay(std::unordered_map<std::string, std::string>& store) const;

 private:
  /** @brief Filesystem path of the WAL file. */
  std::string path_;
  /** @brief Append stream kept open for write path operations. */
  std::ofstream output_;
};

}  // namespace persistence
}  // namespace kv

#endif  // KV_STORE_WAL_H_
