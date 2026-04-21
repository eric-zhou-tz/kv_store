#ifndef KV_STORE_STORE_KV_STORE_H_
#define KV_STORE_STORE_KV_STORE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace kv {
namespace persistence {
class Snapshot;
struct SnapshotLoadResult;
class WriteAheadLog;
}  // namespace persistence
}  // namespace kv

namespace kv {
namespace store {

/**
 * @brief In-memory key-value store backed by an unordered map.
 */
class KVStore {
 public:
  /**
   * @brief Constructs an empty key-value store.
   */
  KVStore() = default;

  /**
   * @brief Constructs an empty key-value store with WAL persistence enabled.
   *
   * @param wal Write-ahead log used for future mutations.
   */
  explicit KVStore(persistence::WriteAheadLog* wal,
                   persistence::Snapshot* snapshot = nullptr);

  /**
   * @brief Inserts or updates a value for a key.
   *
   * @param key Key to insert or update.
   * @param value Value to associate with the key.
   */
  void Set(const std::string& key, const std::string& value);

  /**
   * @brief Retrieves the value stored for a key.
   *
   * @param key Key to look up.
   * @return Stored value when the key exists, otherwise `std::nullopt`.
   */
  std::optional<std::string> Get(const std::string& key) const;

  /**
   * @brief Removes a key from the store.
   *
   * @param key Key to remove.
   * @return `true` when an entry was removed, otherwise `false`.
   */
  bool Delete(const std::string& key);

  /**
   * @brief Checks whether a key exists in the store.
   *
   * @param key Key to search for.
   * @return `true` when the key exists, otherwise `false`.
   */
  bool Contains(const std::string& key) const;

  /**
   * @brief Returns the number of stored key-value pairs.
   *
   * @return Current entry count.
   */
  std::size_t Size() const;

  /**
   * @brief Removes all entries from the store.
   */
  void Clear();

  /**
   * @brief Clears durable WAL and snapshot files without clearing memory.
   *
   * Future writes continue to use the same WAL and snapshot objects. In-memory
   * data remains available until the process exits or the caller clears it.
   */
  void ClearPersistence();

  /**
   * @brief Writes the current in-memory state to the configured snapshot file.
   *
   * This is a public persistence operation so callers can checkpoint the store
   * without depending on the automatic snapshot interval.
   *
   * @return `true` when a snapshot was configured and written, otherwise false.
   */
  bool SaveSnapshot();

  /**
   * @brief Loads a persisted snapshot directly into this store.
   *
   * Snapshot recovery is separated from WAL replay so startup can restore the
   * latest checkpoint first, then apply WAL operations on top.
   *
   * @param snapshot Snapshot file to load.
   * @return Snapshot metadata, including entry count and covered WAL offset.
   */
  persistence::SnapshotLoadResult LoadSnapshot(
      const persistence::Snapshot& snapshot);

  /**
   * @brief Replays persisted WAL operations into this store.
   *
   * @param wal Write-ahead log to replay.
   * @param offset WAL byte offset to start replaying from.
   * @return Number of WAL operations applied.
   */
  std::size_t ReplayFromWal(const persistence::WriteAheadLog& wal,
                            std::uint64_t offset = 0);

 private:
  /** @brief Internal storage for key-value pairs. */
  std::unordered_map<std::string, std::string> data_;
  /** @brief Optional WAL used to persist future mutations. */
  persistence::WriteAheadLog* wal_ = nullptr;
  /** @brief Optional snapshot writer used to checkpoint the in-memory map. */
  persistence::Snapshot* snapshot_ = nullptr;
  /** @brief Number of write commands applied since the last snapshot. */
  std::size_t writes_since_snapshot_ = 0;

  static constexpr std::size_t kSnapshotInterval = 1000;
  /** @brief Saves a snapshot when enough writes have happened. */
  void MaybeSnapshot();
};

}  // namespace store
}  // namespace kv

#endif  // KV_STORE_STORE_KV_STORE_H_
