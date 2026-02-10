#pragma once

#include <string>

namespace nazg {
namespace blackbox {
class logger;
}
} // namespace nazg

namespace nazg::test::workspace_suite {

bool run_snapshot_creation(nazg::blackbox::logger *log, std::string &error);
bool run_prune_behavior(nazg::blackbox::logger *log, std::string &error);
bool run_env_capture(nazg::blackbox::logger *log, std::string &error);
bool run_restore_full(nazg::blackbox::logger *log, std::string &error);

} // namespace nazg::test::workspace_suite

