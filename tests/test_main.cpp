#include "agent/protocol.hpp"
#include "agent/runtime.hpp"
#include "bot/types.hpp"
#include "bot/transport.hpp"
#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"
#include "test/workspace_suite.hpp"
#include "tui/layout_utils.hpp"
#include "tui/tui_context.hpp"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TestCase {
  const char *name;
  std::function<void()> func;
};

std::vector<std::string> failures;

#define EXPECT_TRUE(expr)                                                                    \
  do {                                                                                       \
    if (!(expr)) {                                                                           \
      failures.emplace_back(std::string(__FILE__) + ":" + std::to_string(__LINE__) +        \
                             " EXPECT_TRUE failed: " #expr);                                 \
      return;                                                                                \
    }                                                                                        \
  } while (0)

#define EXPECT_EQ(lhs, rhs)                                                                  \
  do {                                                                                       \
    auto _lhs = (lhs);                                                                       \
    auto _rhs = (rhs);                                                                       \
    if (!((_lhs) == (_rhs))) {                                                               \
      std::ostringstream _oss;                                                               \
      _oss << __FILE__ << ":" << __LINE__ << " EXPECT_EQ failed: " #lhs " != " #rhs         \
           << " (" << _lhs << " vs " << _rhs << ")";                                        \
      failures.emplace_back(_oss.str());                                                    \
      return;                                                                                \
    }                                                                                        \
  } while (0)

void test_protocol_roundtrip() {
  using namespace nazg::agent::protocol;

  Header header{MessageType::RunCommand, 0};
  std::string payload = "echo hi";
  auto buffer = encode(header, payload);

  Header decoded{};
  std::string decoded_payload;
  EXPECT_TRUE(decode(buffer, decoded, decoded_payload));
  EXPECT_EQ(static_cast<int>(decoded.type), static_cast<int>(header.type));
  EXPECT_EQ(decoded_payload, payload);
}

void test_agent_runtime_execute() {
  using namespace nazg;

  agent::Options opts;
  opts.bind_address = "127.0.0.1";
  opts.port = 0; // choose ephemeral port

  nazg::blackbox::options log_opts;
  log_opts.console_enabled = true;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  agent::Runtime runtime(opts, logger.get());
  if (!runtime.start()) {
    std::cout << "[SKIP] agent runtime unavailable in current environment" << std::endl;
    return;
  }

  // allow listener to come up
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  bot::HostConfig host;
  host.address = "127.0.0.1";
  host.agent_port = runtime.port();
  host.extra_config["agent_available"] = "true";

  bot::AgentTransport transport(host);
  EXPECT_TRUE(transport.hello());

  int exit_code = -1;
  std::string stdout_output;
  std::string stderr_output;
  std::string script = "#!/usr/bin/env bash\necho hello-agent";
  EXPECT_TRUE(transport.execute_script(script, exit_code, stdout_output, stderr_output));
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(stdout_output.find("hello-agent") != std::string::npos);
  EXPECT_TRUE(stderr_output.empty());

  // Failure path
  exit_code = -1;
  stdout_output.clear();
  script = "#!/usr/bin/env bash\nexit 5";
  EXPECT_TRUE(transport.execute_script(script, exit_code, stdout_output, stderr_output));
  EXPECT_EQ(exit_code, 5);

  runtime.stop();
}

void test_workspace_snapshot_creation() {
  nazg::blackbox::options log_opts;
  log_opts.console_enabled = false;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  std::string error;
  bool ok =
      nazg::test::workspace_suite::run_snapshot_creation(logger.get(), error);
  if (!ok && !error.empty()) {
    std::cerr << error << std::endl;
  }
  EXPECT_TRUE(ok);
}

void test_workspace_prune_behavior() {
  nazg::blackbox::options log_opts;
  log_opts.console_enabled = false;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  std::string error;
  bool ok = nazg::test::workspace_suite::run_prune_behavior(logger.get(), error);
  if (!ok && !error.empty()) {
    std::cerr << error << std::endl;
  }
  EXPECT_TRUE(ok);
}

void test_workspace_env_capture() {
  nazg::blackbox::options log_opts;
  log_opts.console_enabled = false;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  std::string error;
  bool ok = nazg::test::workspace_suite::run_env_capture(logger.get(), error);
  if (!ok && !error.empty()) {
    std::cerr << error << std::endl;
  }
  EXPECT_TRUE(ok);
}

void test_workspace_restore_full() {
  nazg::blackbox::options log_opts;
  log_opts.console_enabled = false;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  std::string error;
  bool ok = nazg::test::workspace_suite::run_restore_full(logger.get(), error);
  if (!ok && !error.empty()) {
    std::cerr << error << std::endl;
  }
  EXPECT_TRUE(ok);
}

void test_tui_menu_mode_switch() {
  using namespace nazg::tui;

  nazg::blackbox::options log_opts;
  log_opts.console_enabled = false;
  auto logger = std::make_unique<nazg::blackbox::logger>(log_opts);

  TUIContext ctx(logger.get(), nullptr);
  ctx.initialize();

  class DummyMenu : public Menu {
  public:
    std::string id() const override { return "dummy"; }
    std::string title() const override { return "Dummy"; }
    void build(TUIContext& ctx) override { (void)ctx; }
  };

  ctx.menus().register_menu("dummy", []() {
    return std::make_unique<DummyMenu>();
  });

  EXPECT_EQ(static_cast<int>(ctx.modes().current()), static_cast<int>(Mode::INSERT));
  EXPECT_TRUE(ctx.menus().load("dummy"));
  EXPECT_EQ(static_cast<int>(ctx.modes().current()), static_cast<int>(Mode::NORMAL));

  ctx.menus().clear_stack();
  EXPECT_EQ(static_cast<int>(ctx.modes().current()), static_cast<int>(Mode::INSERT));
}

void test_wrap_text_helper() {
  using namespace nazg::tui;

  auto simple = wrap_text("hello world", 5);
  EXPECT_EQ(simple.size(), 2u);
  EXPECT_EQ(simple[0], "hello");
  EXPECT_EQ(simple[1], "world");

  auto long_word = wrap_text("supercalifragilistic", 4);
  EXPECT_EQ(long_word.size(), 5u);
  EXPECT_EQ(long_word[0], "supe");
  EXPECT_EQ(long_word[1], "rcal");

  auto multiline = wrap_text("line1\nline2 words", 10);
  EXPECT_EQ(multiline.size(), 3u);
  EXPECT_EQ(multiline[0], "line1");
  EXPECT_EQ(multiline[1], "line2");
  EXPECT_EQ(multiline[2], "words");
}

} // namespace

int main() {
  std::vector<TestCase> tests = {
      {"protocol_roundtrip", test_protocol_roundtrip},
      {"agent_runtime_execute", test_agent_runtime_execute},
      {"workspace_snapshot_creation", test_workspace_snapshot_creation},
      {"workspace_prune_behavior", test_workspace_prune_behavior},
      {"workspace_env_capture", test_workspace_env_capture},
      {"workspace_restore_full", test_workspace_restore_full},
      {"tui_menu_mode_switch", test_tui_menu_mode_switch},
      {"wrap_text_helper", test_wrap_text_helper},
  };

  for (const auto &test : tests) {
    try {
      test.func();
      if (!failures.empty()) {
        std::cerr << "[FAIL] " << test.name << std::endl;
        for (const auto &msg : failures) {
          std::cerr << "  " << msg << std::endl;
        }
        return 1;
      }
      std::cout << "[PASS] " << test.name << std::endl;
    } catch (const std::exception &ex) {
      std::cerr << "[FAIL] " << test.name << ": exception: " << ex.what() << std::endl;
      return 1;
    } catch (...) {
      std::cerr << "[FAIL] " << test.name << ": unknown exception" << std::endl;
      return 1;
    }
  }

  std::cout << "All tests passed" << std::endl;
  return 0;
}
