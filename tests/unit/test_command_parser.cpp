#include "parser/command_parser.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace {

using kv::parser::Json;
using kv::parser::parse_agent_request;

TEST(CommandParserTest, ParsesValidAgentRequest) {
  const Json request = parse_agent_request(
      R"({"action":"put","params":{"key":"alpha","value":"1"}})");

  EXPECT_EQ("put", request.at("action").get<std::string>());
  EXPECT_EQ("alpha", request.at("params").at("key").get<std::string>());
  EXPECT_EQ("1", request.at("params").at("value").get<std::string>());
}

TEST(CommandParserTest, RejectsInvalidJson) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"({"action":)");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request must be valid JSON", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandParserTest, RejectsNonObjectRequest) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"(["put"])");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request must be a JSON object", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandParserTest, RejectsMissingAction) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"({"params":{}})");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request.action is required", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandParserTest, RejectsNonStringAction) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"({"action":1,"params":{}})");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request.action must be a string", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandParserTest, RejectsMissingParams) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"({"action":"put"})");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request.params is required", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

TEST(CommandParserTest, RejectsNonObjectParams) {
  EXPECT_THROW(
      {
        try {
          parse_agent_request(R"({"action":"put","params":"bad"})");
        } catch (const std::invalid_argument& error) {
          EXPECT_STREQ("request.params must be an object", error.what());
          throw;
        }
      },
      std::invalid_argument);
}

}  // namespace
