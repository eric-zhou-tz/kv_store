#ifndef KV_STORE_PARSER_COMMAND_PARSER_H_
#define KV_STORE_PARSER_COMMAND_PARSER_H_

#include <string>

#include <nlohmann/json.hpp>

namespace kv {
namespace parser {

/**
 * @brief Shared JSON type for agent request parsing.
 */
using Json = nlohmann::json;

/**
 * @brief Parses and validates a raw agent JSON request.
 *
 * Required request shape:
 * {
 *   "action": "...",
 *   "params": { ... }
 * }
 *
 * @param raw Raw JSON request string.
 * @return Validated JSON request object.
 * @throws std::invalid_argument When the request is malformed or invalid.
 */
Json parse_agent_request(const std::string& raw);

}  // namespace parser
}  // namespace kv

#endif  // KV_STORE_PARSER_COMMAND_PARSER_H_
