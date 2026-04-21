#include "persistence/wal.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#include "helpers/file_utils.h"
#include "helpers/temp_dir.h"

namespace {

using kv::persistence::WriteAheadLog;
using kv::tests::AppendBinaryFile;
using kv::tests::AppendPrimitive;
using kv::tests::FileExists;
using kv::tests::FileSize;
using kv::tests::RemoveIfExists;
using kv::tests::TempDir;

constexpr std::uint8_t kSetOp = 1;
constexpr std::uint8_t kDeleteOp = 2;
constexpr std::size_t kMaxWalRecordLength = 64U * 1024U * 1024U;

std::string FrameRecord(const std::string& payload) {
  std::string bytes;
  AppendPrimitive<std::uint32_t>(bytes,
                                 static_cast<std::uint32_t>(payload.size()));
  bytes.append(payload);
  return bytes;
}

std::string SetPayload(const std::string& key, const std::string& value) {
  std::string payload;
  AppendPrimitive<std::uint8_t>(payload, kSetOp);
  AppendPrimitive<std::uint32_t>(payload, static_cast<std::uint32_t>(key.size()));
  payload.append(key);
  AppendPrimitive<std::uint32_t>(payload,
                                 static_cast<std::uint32_t>(value.size()));
  payload.append(value);
  return payload;
}

std::string DeletePayload(const std::string& key) {
  std::string payload;
  AppendPrimitive<std::uint8_t>(payload, kDeleteOp);
  AppendPrimitive<std::uint32_t>(payload, static_cast<std::uint32_t>(key.size()));
  payload.append(key);
  return payload;
}

std::string UnknownPayload(const std::string& key) {
  std::string payload;
  AppendPrimitive<std::uint8_t>(payload, static_cast<std::uint8_t>(99));
  AppendPrimitive<std::uint32_t>(payload, static_cast<std::uint32_t>(key.size()));
  payload.append(key);
  return payload;
}

class WalTest : public ::testing::Test {
 protected:
  WalTest() : wal_path_(temp_dir_.FilePath("wal.log")) {}

  std::unordered_map<std::string, std::string> Replay() const {
    WriteAheadLog wal(wal_path_);
    std::unordered_map<std::string, std::string> recovered;
    wal.Replay(recovered);
    return recovered;
  }

  TempDir temp_dir_;
  std::string wal_path_;
};

TEST_F(WalTest, ConstructorCreatesEmptyWalFile) {
  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_TRUE(FileExists(wal_path_));
  EXPECT_EQ(0U, FileSize(wal_path_));
  EXPECT_EQ(0U, wal.Replay(recovered));
  EXPECT_TRUE(recovered.empty());
}

TEST_F(WalTest, MissingWalPathReplaysAsNoOp) {
  WriteAheadLog wal(wal_path_);
  RemoveIfExists(wal_path_);
  std::unordered_map<std::string, std::string> recovered{{"keep", "value"}};

  EXPECT_EQ(0U, wal.Replay(recovered));
  EXPECT_EQ(1U, recovered.size());
  EXPECT_EQ("value", recovered["keep"]);
}

TEST_F(WalTest, AppendingPutRecordRestoresStateAfterReplay) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("alpha", "1");
  }

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("1", recovered.at("alpha"));
}

TEST_F(WalTest, AppendingDeleteRecordRemovesKeyAfterReplay) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("alpha", "1");
    wal.AppendDelete("alpha");
  }

  const auto recovered = Replay();

  EXPECT_TRUE(recovered.empty());
}

TEST_F(WalTest, MultipleOperationsOnSameKeyReplayFinalState) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("hot", "1");
    wal.AppendSet("hot", "2");
    wal.AppendDelete("hot");
    wal.AppendSet("hot", "3");
  }

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("3", recovered.at("hot"));
}

TEST_F(WalTest, InterleavedOperationsAcrossKeysPreserveFinalState) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("a", "1");
    wal.AppendSet("b", "1");
    wal.AppendDelete("a");
    wal.AppendSet("c", "1");
    wal.AppendSet("b", "2");
  }

  const auto recovered = Replay();

  ASSERT_EQ(2U, recovered.size());
  EXPECT_EQ("2", recovered.at("b"));
  EXPECT_EQ("1", recovered.at("c"));
  EXPECT_EQ(recovered.end(), recovered.find("a"));
}

TEST_F(WalTest, ReplayHandlesManyRecords) {
  {
    WriteAheadLog wal(wal_path_);
    for (int i = 0; i < 5000; ++i) {
      wal.AppendSet("key-" + std::to_string(i), "value-" + std::to_string(i));
    }
  }

  const auto recovered = Replay();

  ASSERT_EQ(5000U, recovered.size());
  EXPECT_EQ("value-0", recovered.at("key-0"));
  EXPECT_EQ("value-4999", recovered.at("key-4999"));
}

TEST_F(WalTest, ZeroLengthKeyAndValueRoundTrip) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("", "");
  }

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  ASSERT_NE(recovered.end(), recovered.find(""));
  EXPECT_EQ("", recovered.at(""));
}

TEST_F(WalTest, LargeRecordReplay) {
  const std::string large_value(512 * 1024, 'v');
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("large", large_value);
  }

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ(large_value, recovered.at("large"));
}

TEST_F(WalTest, ReplayFromOffsetStartsAtRecordBoundary) {
  std::uint64_t offset = 0;
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("old", "skip");
    offset = wal.CurrentOffset();
    wal.AppendSet("new", "apply");
  }

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_EQ(1U, wal.ReplayFrom(offset, recovered));
  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("apply", recovered.at("new"));
  EXPECT_EQ(recovered.end(), recovered.find("old"));
}

TEST_F(WalTest, UnknownOpcodeIsSkippedAndReplayContinues) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("before", "1");
  }
  AppendBinaryFile(wal_path_, FrameRecord(UnknownPayload("ignored")));
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("after", "2");
  }

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_EQ(2U, wal.Replay(recovered));
  EXPECT_EQ("1", recovered.at("before"));
  EXPECT_EQ("2", recovered.at("after"));
  EXPECT_EQ(recovered.end(), recovered.find("ignored"));
}

TEST_F(WalTest, SetRecordWithExtraTrailingBytesIsSkipped) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("before", "1");
  }

  std::string malformed = SetPayload("bad", "value");
  malformed.push_back('x');
  AppendBinaryFile(wal_path_, FrameRecord(malformed));

  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("after", "2");
  }

  const auto recovered = Replay();

  ASSERT_EQ(2U, recovered.size());
  EXPECT_EQ("1", recovered.at("before"));
  EXPECT_EQ("2", recovered.at("after"));
  EXPECT_EQ(recovered.end(), recovered.find("bad"));
}

TEST_F(WalTest, DeleteRecordWithTrailingBytesIsSkipped) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("victim", "value");
  }

  std::string malformed = DeletePayload("victim");
  malformed.push_back('x');
  AppendBinaryFile(wal_path_, FrameRecord(malformed));

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("value", recovered.at("victim"));
}

TEST_F(WalTest, MalformedKeySizeDoesNotCorruptEarlierOrLaterRecords) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("before", "1");
  }

  std::string malformed;
  AppendPrimitive<std::uint8_t>(malformed, kSetOp);
  AppendPrimitive<std::uint32_t>(malformed, 100U);
  AppendBinaryFile(wal_path_, FrameRecord(malformed));

  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("after", "2");
  }

  const auto recovered = Replay();

  ASSERT_EQ(2U, recovered.size());
  EXPECT_EQ("1", recovered.at("before"));
  EXPECT_EQ("2", recovered.at("after"));
}

TEST_F(WalTest, MalformedValueSizeDoesNotCorruptEarlierOrLaterRecords) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("before", "1");
  }

  std::string malformed;
  AppendPrimitive<std::uint8_t>(malformed, kSetOp);
  AppendPrimitive<std::uint32_t>(malformed, 3U);
  malformed.append("bad");
  AppendPrimitive<std::uint32_t>(malformed, 100U);
  AppendBinaryFile(wal_path_, FrameRecord(malformed));

  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("after", "2");
  }

  const auto recovered = Replay();

  ASSERT_EQ(2U, recovered.size());
  EXPECT_EQ("1", recovered.at("before"));
  EXPECT_EQ("2", recovered.at("after"));
  EXPECT_EQ(recovered.end(), recovered.find("bad"));
}

TEST_F(WalTest, PartialTrailingLengthIsIgnoredSafely) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("good", "value");
  }

  std::string partial_length;
  AppendPrimitive<std::uint32_t>(partial_length, 10U);
  partial_length.resize(2);
  AppendBinaryFile(wal_path_, partial_length);

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("value", recovered.at("good"));
}

TEST_F(WalTest, TruncatedTrailingPayloadStopsAfterValidRecords) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("good", "value");
  }

  std::string torn_record;
  AppendPrimitive<std::uint32_t>(torn_record, 32U);
  AppendPrimitive<std::uint8_t>(torn_record, kSetOp);
  AppendBinaryFile(wal_path_, torn_record);

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_EQ(1U, wal.Replay(recovered));
  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("value", recovered.at("good"));
}

TEST_F(WalTest, ImpossibleRecordLengthStopsWithoutAllocatingPayload) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("good", "value");
  }

  std::string impossible_record;
  AppendPrimitive<std::uint32_t>(
      impossible_record, static_cast<std::uint32_t>(kMaxWalRecordLength + 1));
  AppendBinaryFile(wal_path_, impossible_record);
  AppendBinaryFile(wal_path_, FrameRecord(SetPayload("after", "ignored")));

  const auto recovered = Replay();

  ASSERT_EQ(1U, recovered.size());
  EXPECT_EQ("value", recovered.at("good"));
  EXPECT_EQ(recovered.end(), recovered.find("after"));
}

TEST_F(WalTest, OffsetPastEndReplaysNoRecords) {
  {
    WriteAheadLog wal(wal_path_);
    wal.AppendSet("good", "value");
  }

  WriteAheadLog wal(wal_path_);
  std::unordered_map<std::string, std::string> recovered;

  EXPECT_EQ(0U, wal.ReplayFrom(1000000, recovered));
  EXPECT_TRUE(recovered.empty());
}

}  // namespace
