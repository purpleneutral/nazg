#include "test/adapters.hpp"
#include "blackbox/logger.hpp"

namespace nazg::test {

std::unique_ptr<Adapter> create_adapter(Framework framework, nazg::blackbox::logger *log) {
  switch (framework) {
  case Framework::GTEST:
  case Framework::CTEST:
    return std::make_unique<GTestAdapter>(log);

  case Framework::PYTEST:
  case Framework::UNITTEST:
    return std::make_unique<PytestAdapter>(log);

  // Placeholder for future frameworks
  case Framework::CARGO:
  case Framework::JEST:
  case Framework::VITEST:
  case Framework::GO_TEST:
  case Framework::CATCH2:
  case Framework::UNKNOWN:
  default:
    if (log) {
      log->warn("TestAdapter", "No adapter implemented for framework " +
                                    std::to_string(static_cast<int>(framework)));
    }
    return nullptr;
  }
}

}  // namespace nazg::test
