#include "store/kv_store.h"

#include "persistence/wal.h"

namespace kv {
namespace store {

KVStore::KVStore(persistence::WriteAheadLog* wal) : wal_(wal) {}

void KVStore::Set(const std::string& key, const std::string& value) {
  if (wal_ != nullptr) {
    wal_->append_set(key, value);
  }
  data_[key] = value;
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
    wal_->append_delete(key);
  }
  return data_.erase(key) > 0;
}

bool KVStore::Contains(const std::string& key) const {
  return data_.find(key) != data_.end();
}

std::size_t KVStore::Size() const {
  return data_.size();
}

void KVStore::Clear() {
  data_.clear();
}

std::size_t KVStore::ReplayFromWal(const persistence::WriteAheadLog& wal) {
  return wal.replay(data_);
}

}  // namespace store
}  // namespace kv
