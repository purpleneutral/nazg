#pragma once
#include <string>

namespace nazg::scaffold::templates {

// ====== C++ Templates ======

inline std::string cmake_cpp(const std::string &name) {
  return R"(cmake_minimum_required(VERSION 3.16)
project()" + name + R"( LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable()" + name + R"( src/main.cpp)
)";
}

inline std::string main_cpp() {
  return R"(#include <iostream>

int main() {
    std::cout << "Hello World!" << std::endl;
    return 0;
}
)";
}

// ====== C Templates ======

inline std::string cmake_c(const std::string &name) {
  return R"(cmake_minimum_required(VERSION 3.16)
project()" + name + R"( LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable()" + name + R"( src/main.c)
)";
}

inline std::string main_c() {
  return R"(#include <stdio.h>

int main(void) {
    printf("Hello World!\n");
    return 0;
}
)";
}

// ====== Python Templates ======

inline std::string python_main(const std::string &pkg) {
  return R"(#!/usr/bin/env python3
"""Main entry point for )" + pkg + R"(."""

def main():
    print("Hello World!")

if __name__ == "__main__":
    main()
)";
}

inline std::string python_requirements() {
  return R"(# Add your dependencies here
# Example:
# requests>=2.28.0
# numpy>=1.24.0
)";
}

inline std::string python_envrc() {
  return R"(# direnv configuration
# Automatically activates virtual environment when entering directory
layout python python3
)";
}

inline std::string python_gitignore() {
  return R"(# Python
__pycache__/
*.py[cod]
*$py.class
*.so
.Python
env/
venv/
.venv/
ENV/
*.egg-info/
dist/
build/
.pytest_cache/
.mypy_cache/
.coverage
htmlcov/

# Direnv
.direnv/

# IDE
.vscode/
.idea/
*.swp
*.swo
*~
)";
}

// ====== C/C++ Templates ======

inline std::string cpp_gitignore() {
  return R"(# Build artifacts
build/
build-*/
*.o
*.a
*.so
*.dylib
*.exe
*.out

# CMake
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
compile_commands.json

# IDE
.vscode/
.idea/
*.swp
*.swo
*~
)";
}

inline std::string generic_readme(const std::string &name, const std::string &lang) {
  return "# " + name + "\n\n"
         "A " + lang + " project scaffolded by Nazg.\n\n"
         "## Build\n\n"
         "```bash\n"
         "nazg build\n"
         "```\n\n"
         "## Run\n\n"
         "```bash\n"
         "./build/" + name + "\n"
         "```\n";
}

inline std::string python_readme(const std::string &name) {
  return "# " + name + "\n\n"
         "A Python project scaffolded by Nazg.\n\n"
         "## Setup\n\n"
         "### With direnv (recommended)\n\n"
         "```bash\n"
         "direnv allow\n"
         "pip install -r requirements.txt\n"
         "```\n\n"
         "### Manual setup\n\n"
         "```bash\n"
         "python3 -m venv .venv\n"
         "source .venv/bin/activate\n"
         "pip install -r requirements.txt\n"
         "```\n\n"
         "## Run\n\n"
         "```bash\n"
         "python src/main.py\n"
         "```\n";
}

} // namespace nazg::scaffold::templates
