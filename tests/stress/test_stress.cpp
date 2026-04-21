#include "persistence/snapshot.h"
#include "persistence/wal.h"
#include "store/kv_store.h"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <unordered_map>

#include "helpers/temp_dir.h"

namespace {

using kv::persistence::Snapshot;
using kv::persistence::SnapshotLoadResult;
using kv::persistence::WriteAheadLog;
using kv::store::KVStore;
using kv::tests::TempDir;

class StressTest : public ::testing::Test {
 protected:
  StressTest()
      : wal_path_(temp_dir_.FilePath("stress.wal")),
        snapshot_path_(temp_dir_.FilePath("stress.snapshot")) {}

  void ExpectRecoveredMapMatches(
      const std::unordered_map<std::string, std::string>& expected) const {
    WriteAheadLog wal(wal_path_);
    std::unordered_map<std::string, std::string> recovered;

    wal.Replay(recovered);

    ASSERT_EQ(expected.size(), recovered.size());
    for (const auto& entry : expected) {
      ASSERT_NE(recovered.end(), recovered.find(entry.first));
      EXPECT_EQ(entry.second, recovered.at(entry.first));
    }
  }

  void ExpectStoreMatchesReference(
      const KVStore& store,
      const std::unordered_map<std::string, std::string>& expected) const {
    ASSERT_EQ(expected.size(), store.Size());
    for (const auto& entry : expected) {
      ASSERT_TRUE(store.Get(entry.first).has_value());
      EXPECT_EQ(entry.second, store.Get(entry.first).value());
    }
  }

  TempDir temp_dir_;
  std::string wal_path_;
  std::string snapshot_path_;
};

TEST_F(StressTest, DeterministicMixedWorkloadReplaysReferenceModel) {
  constexpr int kOperations = 12000;
  constexpr int kKeySpace = 500;
  std::mt19937 rng(123456);
  std::uniform_int_distribution<int> key_dist(0, kKeySpace - 1);
  std::uniform_int_distribution<int> op_dist(0, 99);

  std::unordered_map<std::string, std::string> expected;
  {
    WriteAheadLog wal(wal_path_);
    for (int i = 0; i < kOperations; ++i) {
      const std::string key = "key-" + std::to_string(key_dist(rng));
      if (op_dist(rng) < 65) {
        const std::string value =
            "value-" + std::to_string(i) + "-" + std::to_string(rng());
        wal.AppendSet(key, value);
        expected[key] = value;
      } else {
        wal.AppendDelete(key);
        expected.erase(key);
      }
    }
  }

  ExpectRecoveredMapMatches(expected);
}

TEST_F(StressTest, ManyOverwritesOfHotKeyRecoverLatestValue) {
  constexpr int kWrites = 10000;
  {
    WriteAheadLog wal(wal_path_);
    KVStore store(&wal);
    for (int i = 0; i < kWrites; ++i) {
      store.Set("hot", "value-" + std::to_string(i));
    }
  }

  WriteAheadLog wal(wal_path_);
  KVStore recovered(&wal);

  EXPECT_EQ(static_cast<std::size_t>(kWrites), recovered.ReplayFromWal(wal));
  EXPECT_EQ(1U, recovered.Size());
  EXPECT_EQ("value-9999", recovered.Get("hot").value());
}

TEST_F(StressTest, ManyDistinctKeysRecoverAllValues) {
  constexpr int kKeys = 15000;
  std::unordered_map<std::string, std::string> expected;
  {
    WriteAheadLog wal(wal_path_);
    for (int i = 0; i < kKeys; ++i) {
      const std::string key = "key-" + std::to_string(i);
      const std::string value = "value-" + std::to_string(i);
      wal.AppendSet(key, value);
      expected[key] = value;
    }
  }

  ExpectRecoveredMapMatches(expected);
}

TEST_F(StressTest, LargeValuesRemainRecoverableWithinPracticalLimits) {
  constexpr int kRecords = 64;
  const std::string value(128 * 1024, 'L');
  std::unordered_map<std::string, std::string> expected;
  {
    WriteAheadLog wal(wal_path_);
    for (int i = 0; i < kRecords; ++i) {
      const std::string key = "large-" + std::to_string(i);
      const std::string versioned_value = value + std::to_string(i);
      wal.AppendSet(key, versioned_value);
      expected[key] = versioned_value;
    }
  }

  ExpectRecoveredMapMatches(expected);
}

TEST_F(StressTest, SnapshotPlusWalTailRecoversLargeState) {
  constexpr int kInitialWrites = 1500;
  std::unordered_map<std::string, std::string> expected;
  {
    WriteAheadLog wal(wal_path_);
    Snapshot snapshot(snapshot_path_);
    KVStore store(&wal, &snapshot);

    for (int i = 0; i < kInitialWrites; ++i) {
      const std::string key = "key-" + std::to_string(i);
      const std::string value = "value-" + std::to_string(i);
      store.Set(key, value);
      expected[key] = value;
    }

    store.Set("key-10", "tail-update");
    expected["key-10"] = "tail-update";
    store.Delete("key-20");
    expected.erase("key-20");
    store.Set("tail-only", "present");
    expected["tail-only"] = "present";
  }

  WriteAheadLog wal(wal_path_);
  Snapshot snapshot(snapshot_path_);
  KVStore recovered(&wal, &snapshot);
  const SnapshotLoadResult snapshot_result = recovered.LoadSnapshot(snapshot);
  ASSERT_TRUE(snapshot_result.loaded);

  recovered.ReplayFromWal(wal, snapshot_result.wal_offset);

  ExpectStoreMatchesReference(recovered, expected);
}

}  // namespace
