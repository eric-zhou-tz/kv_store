#include "store/kv_store.h"

#include "persistence/snapshot.h"
#include "persistence/wal.h"

namespace kv {
namespace store {

KVStore::KVStore(persistence::WriteAheadLog* wal,
                 persistence::Snapshot* snapshot)
    : wal_(wal), snapshot_(snapshot) {}

void KVStore::MaybeSnapshot() {
  if (snapshot_ != nullptr && writes_since_snapshot_ >= kSnapshotInterval) {
    // The snapshot must record the WAL byte position that its in-memory map
    // covers. WAL writes are flushed before memory mutation, so by the time we
    // snapshot here the current WAL offset safely includes all checkpointed
    // writes.
    const std::uint64_t wal_offset =
        (wal_ != nullptr) ? wal_->CurrentOffset() : 0;
    snapshot_->Save(data_, wal_offset);
    writes_since_snapshot_ = 0;
  }
}

void KVStore::Set(const std::string& key, const std::string& value) {
  if (wal_ != nullptr) {
    // Persist before mutating memory so a successful in-memory write has an
    // earlier durable record to replay after a crash.
    wal_->AppendSet(key, value);
  }
  data_[key] = value;
  ++writes_since_snapshot_;
  MaybeSnapshot();
}

std::optional<std::string> KVStore::Get(const std::string& key) const {
  const auto it = data_.find(key);
  if (it == data_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool KVStore::Delete(const std::string& key) {
  if (wal_ != nullptr) {
    // Log delete attempts even when the key is absent; replaying the same
    // operation is idempotent and preserves command ordering.
    wal_->AppendDelete(key);
  }
  const bool erased = data_.erase(key) > 0;
  ++writes_since_snapshot_;
  MaybeSnapshot();
  return erased;
}

bool KVStore::Contains(const std::string& key) const {
  return data_.find(key) != data_.end();
}

std::size_t KVStore::Size() const {
  return data_.size();
}

void KVStore::Clear() {
  data_.clear();
  writes_since_snapshot_ = 0;
}

void KVStore::ClearPersistence() {
  // This is an administrative durability reset: it removes persisted recovery
  // files but intentionally leaves the live in-memory map untouched.
  if (wal_ != nullptr) {
    wal_->Clear();
  }
  if (snapshot_ != nullptr) {
    snapshot_->Clear();
  }
  writes_since_snapshot_ = 0;
}

kv::persistence::SnapshotLoadResult KVStore::LoadSnapshot(
    const persistence::Snapshot& snapshot) {
  // Snapshot loading writes directly into the backing map without going through
  // Set/Delete, because recovery should not append recovered data back to WAL.
  return snapshot.Load(data_);
}

std::size_t KVStore::ReplayFromWal(const persistence::WriteAheadLog& wal,
                                   std::uint64_t offset) {
  // Recovery applies directly to the backing map so it does not append the
  // recovered operations back into the WAL.
  return wal.ReplayFrom(offset, data_);
}

}  // namespace store
}  // namespace kv
