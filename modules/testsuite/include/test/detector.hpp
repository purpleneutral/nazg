#pragma once

#include "test/types.hpp"

#include <string>

namespace nazg {
namespace blackbox {
class logger;
}
namespace nexus {
class Store;
}
}  // namespace nazg

namespace nazg::test {

// Test framework detection utilities
class Detector {
 public:
  Detector(nazg::nexus::Store *store, nazg::blackbox::logger *log);

  // Detect test framework in the given directory
  TestFrameworkInfo detect(const std::string &root_path);

 private:
  bool file_contains(const std::string &path, const std::string &pattern);
  bool file_exists_in_tree(const std::string &root, const std::string &filename);
  bool has_files_matching(const std::string &root, const std::string &pattern);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

}  // namespace nazg::test
