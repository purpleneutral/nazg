// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

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
