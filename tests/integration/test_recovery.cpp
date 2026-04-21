#include "persistence/snapshot.h"
#include "persistence/wal.h"
#include "store/kv_store.h"

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
using kv::persistence::WriteAheadLog;
using kv::store::KVStore;
using kv::tests::AppendPrimitive;
using kv::tests::FileExists;
using kv::tests::TempDir;
using kv::tests::WriteBinaryFile;

constexpr std::uint32_t kSnapshotMagic = 0x3153564BU;
constexpr std::uint32_t kSnapshotVersion = 1;

class RecoveryTest : public ::testing::Test {
 protected:
  RecoveryTest()
      : wal_path_(temp_dir_.FilePath("wal.log")),
        snapshot_path_(temp_dir_.FilePath("store.snapshot")) {}

  TempDir temp_dir_;
  std::string wal_path_;
  std::string snapshot_path_;
};

TEST_F(RecoveryTest, WritesThenReconstructsStoreFromWal) {
  {
    WriteAheadLog wal(wal_path_);
    KVStore store(&wal);
    store.Set("alpha", "1");
    store.Set("message", "hello world");
    ASSERT_TRUE(store.Delete("alpha"));
  }

  WriteAheadLog wal(wal_path_);
  KVStore recovered(&wal);
  const std::size_t recovered_operations = recovered.ReplayFromWal(wal);

  EXPECT_EQ(3U, recovered_operations);
  EXPECT_EQ(1U, recovered.Size());
  EXPECT_FALSE(recovered.Contains("alpha"));
  ASSERT_TRUE(recovered.Get("message").has_value());
  EXPECT_EQ("hello world", recovered.Get("message").value());
}

TEST_F(RecoveryTest, PutDeleteOverwriteSequenceRecoversOnlyFinalState) {
  {
    WriteAheadLog wal(wal_path_);
    KVStore store(&wal);
    store.Set("a", "1");
    store.Set("b", "1");
    store.Set("a", "2");
    store.Delete("b");
    store.Set("c", "3");
    store.Delete("missing");
  }

  WriteAheadLog wal(wal_path_);
  KVStore recovered(&wal);

  EXPECT_EQ(6U, recovered.ReplayFromWal(wal));
  EXPECT_EQ(2U, recovered.Size());
  EXPECT_EQ("2", recovered.Get("a").value());
  EXPECT_EQ("3", recovered.Get("c").value());
  EXPECT_FALSE(recovered.Contains("b"));
  EXPECT_FALSE(recovered.Contains("missing"));
}

TEST_F(RecoveryTest, SnapshotOnlyRecoveryLoadsMaterializedState) {
  Snapshot snapshot(snapshot_path_);
  snapshot.Save({{"alpha", "1"}, {"empty", ""}}, 77);

  KVStore recovered;
  const SnapshotLoadResult result = recovered.LoadSnapshot(snapshot);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(2U, result.entry_count);
  EXPECT_EQ(77U, result.wal_offset);
  EXPECT_EQ(2U, recovered.Size());
  EXPECT_EQ("1", recovered.Get("alpha").value());
  EXPECT_EQ("", recovered.Get("empty").value());
}

TEST_F(RecoveryTest, StoreCanSaveSnapshotThroughPublicApi) {
  std::uint64_t wal_offset = 0;
  {
    WriteAheadLog wal(wal_path_);
    Snapshot snapshot(snapshot_path_);
    KVStore store(&wal, &snapshot);
    store.Set("alpha", "1");
    store.Set("beta", "2");

    EXPECT_TRUE(store.SaveSnapshot());
    wal_offset = wal.CurrentOffset();
  }

  Snapshot snapshot(snapshot_path_);
  KVStore recovered;
  const SnapshotLoadResult result = recovered.LoadSnapshot(snapshot);

  EXPECT_TRUE(result.loaded);
  EXPECT_EQ(2U, result.entry_count);
  EXPECT_EQ(wal_offset, result.wal_offset);
  EXPECT_EQ("1", recovered.Get("alpha").value());
  EXPECT_EQ("2", recovered.Get("beta").value());
}

TEST_F(RecoveryTest, SnapshotPlusWalTailRecoversFromCoveredOffset) {
  std::uint64_t snapshot_offset = 0;
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("old-wal", "skip");
    snapshot_offset = wal.CurrentOffset();

    Snapshot snapshot(snapshot_path_);
    snapshot.Save({{"old", "snapshot"}, {"keep", "snapshot"}},
                  snapshot_offset);

    wal.AppendDelete("old");
    wal.AppendSet("keep", "wal");
    wal.AppendSet("new", "wal");
  }

  WriteAheadLog wal(wal_path_);
  Snapshot snapshot(snapshot_path_);
  KVStore recovered(&wal, &snapshot);
  const SnapshotLoadResult snapshot_result = recovered.LoadSnapshot(snapshot);
  const std::size_t wal_operations =
      recovered.ReplayFromWal(wal, snapshot_result.wal_offset);

  EXPECT_EQ(snapshot_offset, snapshot_result.wal_offset);
  EXPECT_EQ(3U, wal_operations);
  EXPECT_FALSE(recovered.Contains("old-wal"));
  EXPECT_FALSE(recovered.Contains("old"));
  EXPECT_EQ("wal", recovered.Get("keep").value());
  EXPECT_EQ("wal", recovered.Get("new").value());
}

TEST_F(RecoveryTest, WalTailAfterSnapshotOverridesSnapshotValues) {
  std::uint64_t snapshot_offset = 0;
  {
    WriteAheadLog wal(wal_path_);
    Snapshot snapshot(snapshot_path_);
    snapshot_offset = wal.CurrentOffset();
    snapshot.Save({{"key", "snapshot"}, {"delete-me", "snapshot"}},
                  snapshot_offset);
    wal.AppendSet("key", "wal");
    wal.AppendDelete("delete-me");
  }

  WriteAheadLog wal(wal_path_);
  Snapshot snapshot(snapshot_path_);
  KVStore recovered(&wal, &snapshot);
  const SnapshotLoadResult result = recovered.LoadSnapshot(snapshot);

  EXPECT_EQ(2U, recovered.ReplayFromWal(wal, result.wal_offset));
  EXPECT_EQ(1U, recovered.Size());
  EXPECT_EQ("wal", recovered.Get("key").value());
  EXPECT_FALSE(recovered.Contains("delete-me"));
}

TEST_F(RecoveryTest, MissingSnapshotFallsBackToFullWalReplay) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("alpha", "1");
    wal.AppendSet("beta", "2");
  }

  WriteAheadLog wal(wal_path_);
  Snapshot snapshot(snapshot_path_);
  KVStore recovered(&wal, &snapshot);
  const SnapshotLoadResult result = recovered.LoadSnapshot(snapshot);
  const std::size_t wal_operations =
      recovered.ReplayFromWal(wal, result.wal_offset);

  EXPECT_FALSE(result.loaded);
  EXPECT_EQ(0U, result.wal_offset);
  EXPECT_EQ(2U, wal_operations);
  EXPECT_EQ("1", recovered.Get("alpha").value());
  EXPECT_EQ("2", recovered.Get("beta").value());
}

TEST_F(RecoveryTest, CorruptedSnapshotThrowsWithoutReplacingLiveStore) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotMagic);
  AppendPrimitive<std::uint32_t>(bytes, kSnapshotVersion);
  AppendPrimitive<std::uint64_t>(bytes, 0U);
  AppendPrimitive<std::uint32_t>(bytes, 1U);
  WriteBinaryFile(snapshot_path_, bytes);

  Snapshot snapshot(snapshot_path_);
  KVStore recovered;
  recovered.Set("keep", "value");

  EXPECT_THROW(recovered.LoadSnapshot(snapshot), std::runtime_error);
  ASSERT_EQ(1U, recovered.Size());
  EXPECT_EQ("value", recovered.Get("keep").value());
}

TEST_F(RecoveryTest, StartupRecoveryIsIdempotentAcrossFreshStores) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("a", "1");
    wal.AppendSet("a", "2");
    wal.AppendSet("b", "3");
    wal.AppendDelete("b");
  }

  WriteAheadLog wal(wal_path_);
  KVStore first(&wal);
  KVStore second(&wal);

  EXPECT_EQ(4U, first.ReplayFromWal(wal));
  EXPECT_EQ(4U, second.ReplayFromWal(wal));
  EXPECT_EQ(first.Size(), second.Size());
  EXPECT_EQ(first.Get("a"), second.Get("a"));
  EXPECT_EQ(first.Get("b"), second.Get("b"));
}

TEST_F(RecoveryTest, ClearPersistenceKeepsMemoryButResetsDurableState) {
  Snapshot snapshot(snapshot_path_);
  snapshot.Save({{"from-snapshot", "old"}}, 0);

  {
    WriteAheadLog wal(wal_path_);
    KVStore store(&wal, &snapshot);
    store.Set("before-clear", "old");

    store.ClearPersistence();

    EXPECT_EQ("old", store.Get("before-clear").value());
    EXPECT_FALSE(FileExists(snapshot_path_));

    store.Set("after-clear", "new");
  }

  std::unordered_map<std::string, std::string> loaded_snapshot;
  const SnapshotLoadResult snapshot_result = snapshot.Load(loaded_snapshot);
  EXPECT_FALSE(snapshot_result.loaded);

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> replayed;
  EXPECT_EQ(1U, wal.Replay(replayed));
  ASSERT_EQ(1U, replayed.size());
  EXPECT_EQ("new", replayed.at("after-clear"));
  EXPECT_EQ(replayed.end(), replayed.find("before-clear"));
}

TEST_F(RecoveryTest, RepeatedOpenCloseCyclesAppendAndReplayAllRecords) {
  for (int i = 0; i < 100; ++i) {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("key-" + std::to_string(i), "value-" + std::to_string(i));
  }

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_EQ(100U, wal.Replay(recovered));
  ASSERT_EQ(100U, recovered.size());
  EXPECT_EQ("value-0", recovered.at("key-0"));
  EXPECT_EQ("value-99", recovered.at("key-99"));
}

}  // namespace
