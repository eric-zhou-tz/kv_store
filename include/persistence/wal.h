#ifndef KV_STORE_WAL_H_
#define KV_STORE_WAL_H_

#include <cstddef>
#include <cstdint>
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
  void AppendSet(const std::string& key, const std::string& value);

  /**
   * @brief Appends and flushes a DELETE record.
   *
   * @param key Key being deleted.
   */
  void AppendDelete(const std::string& key);

  /**
   * @brief Returns the current durable end offset of the WAL file.
   *
   * The stream is flushed before reporting the offset so snapshots can record a
   * byte position that includes all WAL records written so far.
   *
   * @return Current byte offset from the beginning of the WAL file.
   */
  std::uint64_t CurrentOffset();

  /**
   * @brief Truncates the WAL file and reopens it for future append records.
   *
   * This clears durable history without changing any in-memory store state.
   */
  void Clear();

  /**
   * @brief Replays valid WAL records into an in-memory map.
   *
   * Malformed bounded records are skipped. Replay stops at EOF, an incomplete
   * trailing record, or an impossible record length.
   *
   * @param store Store map to update while replaying the log.
   * @return Number of valid operations applied.
   */
  std::size_t Replay(std::unordered_map<std::string, std::string>& store) const;

  /**
   * @brief Replays valid WAL records starting at a byte offset.
   *
   * This is used with snapshots: the snapshot stores the WAL offset it covers,
   * and recovery replays only records written after that point.
   *
   * @param offset Byte offset to start replay from.
   * @param store Store map to update while replaying the log.
   * @return Number of valid operations applied.
   */
  std::size_t ReplayFrom(
      std::uint64_t offset,
      std::unordered_map<std::string, std::string>& store) const;

 private:
  /** @brief Filesystem path of the WAL file. */
  std::string path_;
  /** @brief Append stream kept open for write path operations. */
  std::ofstream output_;
};

}  // namespace persistence
}  // namespace kv

#endif  // KV_STORE_WAL_H_
