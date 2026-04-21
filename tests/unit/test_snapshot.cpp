#include "persistence/snapshot.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "helpers/file_utils.h"
#include "helpers/temp_dir.h"

namespace {

using kv::persistence::Snapshot;
using kv::persistence::SnapshotLoadResult;
using kv::tests::AppendPrimitive;
using kv::tests::FileExists;
using kv::tests::RemoveIfExists;
using kv::tests::TempDir;
using kv::tests::WriteBinaryFile;

constexpr std::uint32_t kSnapshotMagic = 0x3153564BU;
constexpr std::uint32_t kSnapshotVersion = 1;
constexpr std::size_t kMaxSnapshotFieldLength = 64U * 1024U * 1024U;

void AppendEntry(std::string& bytes, const std::string& key,
                 const std::string& value) {
  AppendPrimitive<std::uint32_t>(bytes, static_cast<std::uint32_t>(key.size()));
  bytes.append(key);
  AppendPrimitive<std::uint32_t>(bytes,
                                 static_cast<std::uint32_t>(value.size()));
  bytes.append(value);
}

std::string LegacySnapshotBytes(
    const std::initializer_list<std::pair<std::string, std::string>>& entries) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, static_cast<std::uint32_t>(entries.size()));
  for (const auto& entry : entries) {
    AppendEntry(bytes, entry.first, entry.second);
  }
  return bytes;
}

class SnapshotTest : public ::testing::Test {
 protected:
  SnapshotTest() : snapshot_path_(temp_dir_.FilePath("store.snapshot")) {}

  TempDir temp_dir_;
  std::string snapshot_path_;
};

TEST_F(SnapshotTest, MissingSnapshotReturnsNotLoadedAndPreservesMap) {
  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  const SnapshotLoadResult result = snapshot.Load(store);

  EXPECT_FALSE(result.loaded);
  EXPECT_EQ(0U, result.entry_count);
  EXPECT_EQ(0U, result.wal_offset);
  ASSERT_EQ(1U, store.size());
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, SaveAndLoadRoundTripEntriesAndWalOffset) {
  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> original{
      {"alpha", "1"}, {"message", "hello world"}, {"empty", ""}};

  snapshot.Save(original, 12345);

  std::unordered_map<std::string, std::string> loaded;
  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(original.size(), result.entry_count);
  EXPECT_EQ(12345U, result.wal_offset);
  EXPECT_EQ(original, loaded);
}

TEST_F(SnapshotTest, EmptySnapshotFileIsTreatedAsNotLoaded) {
  WriteBinaryFile(snapshot_path_, "");
  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  const SnapshotLoadResult result = snapshot.Load(store);

  EXPECT_FALSE(result.loaded);
  EXPECT_EQ(0U, result.entry_count);
  EXPECT_EQ(0U, result.wal_offset);
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, LegacySnapshotLoadsWithZeroWalOffset) {
  WriteBinaryFile(snapshot_path_,
                  LegacySnapshotBytes({{"legacy", "format"}, {"a", "b"}}));
  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> loaded;

  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(2U, result.entry_count);
  EXPECT_EQ(0U, result.wal_offset);
  EXPECT_EQ("format", loaded.at("legacy"));
  EXPECT_EQ("b", loaded.at("a"));
}

TEST_F(SnapshotTest, EmptyKeyAndValueRoundTrip) {
  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> original{{"", ""}};

  snapshot.Save(original, 9);

  std::unordered_map<std::string, std::string> loaded;
  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(1U, result.entry_count);
  EXPECT_EQ(9U, result.wal_offset);
  ASSERT_NE(loaded.end(), loaded.find(""));
  EXPECT_EQ("", loaded.at(""));
}

TEST_F(SnapshotTest, LargeValueRoundTrip) {
  Snapshot snapshot(snapshot_path_);
  const std::string large_value(512 * 1024, 's');
  std::unordered_map<std::string, std::string> original{{"large", large_value}};

  snapshot.Save(original, 1);

  std::unordered_map<std::string, std::string> loaded;
  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(1U, result.entry_count);
  EXPECT_EQ(large_value, loaded.at("large"));
}

TEST_F(SnapshotTest, SaveReplacesExistingSnapshot) {
  Snapshot snapshot(snapshot_path_);
  snapshot.Save({{"old", "value"}}, 1);
  snapshot.Save({{"new", "value"}}, 2);

  std::unordered_map<std::string, std::string> loaded;
  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(1U, result.entry_count);
  EXPECT_EQ(2U, result.wal_offset);
  EXPECT_EQ("value", loaded.at("new"));
  EXPECT_EQ(loaded.end(), loaded.find("old"));
}

TEST_F(SnapshotTest, ClearRemovesSnapshotAndTempFiles) {
  Snapshot snapshot(snapshot_path_);
  snapshot.Save({{"alpha", "1"}}, 0);
  WriteBinaryFile(snapshot_path_ + ".tmp", "abandoned");

  snapshot.Clear();

  EXPECT_FALSE(FileExists(snapshot_path_));
  EXPECT_FALSE(FileExists(snapshot_path_ + ".tmp"));
}

TEST_F(SnapshotTest, ClearMissingFilesIsNoOp) {
  Snapshot snapshot(snapshot_path_);

  EXPECT_NO_THROW(snapshot.Clear());
  EXPECT_FALSE(FileExists(snapshot_path_));
  EXPECT_FALSE(FileExists(snapshot_path_ + ".tmp"));
}

TEST_F(SnapshotTest, TruncatedSnapshotThrowsAndPreservesExistingMap) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotMagic);
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotVersion);
  AppendPrimitive<std::uint64_t>(bytes, 10U);
  AppendPrimitive<std::uint32_t>(bytes, 1U);
  WriteBinaryFile(snapshot_path_, bytes);

  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  EXPECT_THROW(snapshot.Load(store), std::runtime_error);
  ASSERT_EQ(1U, store.size());
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, CorruptedMetadataThrowsAndPreservesExistingMap) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotMagic);
  AppendPrimitive<std::uint32_t>(bytes, 999U);
  WriteBinaryFile(snapshot_path_, bytes);

  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  EXPECT_THROW(snapshot.Load(store), std::runtime_error);
  ASSERT_EQ(1U, store.size());
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, OversizedKeyLengthThrowsSafelyAndPreservesExistingMap) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotMagic);
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotVersion);
  AppendPrimitive<std::uint64_t>(bytes, 0U);
  AppendPrimitive<std::uint32_t>(bytes, 1U);
  AppendPrimitive<std::uint32_t>(
      bytes, static_cast<std::uint32_t>(kMaxSnapshotFieldLength + 1));
  WriteBinaryFile(snapshot_path_, bytes);

  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  EXPECT_THROW(snapshot.Load(store), std::runtime_error);
  ASSERT_EQ(1U, store.size());
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, OversizedValueLengthThrowsSafelyAndPreservesExistingMap) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotMagic);
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotVersion);
  AppendPrimitive<std::uint64_t>(bytes, 0U);
  AppendPrimitive<std::uint32_t>(bytes, 1U);
  AppendPrimitive<std::uint32_t>(bytes, 3U);
  bytes.append("key");
  AppendPrimitive<std::uint32_t>(
      bytes, static_cast<std::uint32_t>(kMaxSnapshotFieldLength + 1));
  WriteBinaryFile(snapshot_path_, bytes);

  Snapshot snapshot(snapshot_path_);
  std::unordered_map<std::string, std::string> store{{"keep", "value"}};

  EXPECT_THROW(snapshot.Load(store), std::runtime_error);
  ASSERT_EQ(1U, store.size());
  EXPECT_EQ("value", store.at("keep"));
}

TEST_F(SnapshotTest, MissingSnapshotFileAfterClearLoadsAsNotLoaded) {
  Snapshot snapshot(snapshot_path_);
  snapshot.Save({{"alpha", "1"}}, 7);
  snapshot.Clear();
  std::unordered_map<std::string, std::string> loaded;

  const SnapshotLoadResult result = snapshot.Load(loaded);

  EXPECT_FALSE(result.loaded);
  EXPECT_EQ(0U, result.entry_count);
  EXPECT_EQ(0U, result.wal_offset);
  EXPECT_TRUE(loaded.empty());
}

}  // namespace
