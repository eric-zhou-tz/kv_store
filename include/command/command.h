#ifndef KV_STORE_COMMAND_COMMAND_H_
#define KV_STORE_COMMAND_COMMAND_H_

#include "parser/command_parser.h"
#include "store/kv_store.h"

namespace kv {
namespace command {

using Json = parser::Json;

/**
 * @brief Executes a validated agent request against the KV store.
 *
 * The request must contain an "action" string and a "params" object.
 * This function maps high-level agent actions into concrete KV operations.
 *
 * @param request Validated JSON request object.
 * @param kv KV store instance used for storage operations.
 * @return JSON response describing success or failure.
 * @throws std::invalid_argument When action-specific parameters are invalid.
 */
Json execute_command(const Json& request, store::KVStore& kv);

}  // namespace command
}  // namespace kv

#endif  // KV_STORE_COMMAND_COMMAND_H_
