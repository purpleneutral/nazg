#include "prompt/prompt.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include <iostream>

namespace nazg::prompt {

static int cmd_prompt_demo(const directive::command_context& ctx,
                           const directive::context& ectx) {
  std::cout << "=== Prompt Module Demo ===\n\n";

  // Demo 1: Simple confirmation
  std::cout << "1. Simple confirmation:\n";
  if (confirm("Do you want to continue?")) {
    std::cout << "  → You chose yes\n\n";
  } else {
    std::cout << "  → You chose no\n\n";
  }

  // Demo 2: Rich prompt with context
  std::cout << "2. Rich prompt with context:\n";
  Prompt p(ectx.log);
  p.title("git-init")
   .project("myproject", "~/projects/myproject")
   .fact("Language", "C++")
   .fact("Build System", "CMake")
   .status("not a git repository")
   .question("Initialize git repository?")
   .action("Create .git/ directory")
   .action("Generate .gitignore for C++")
   .action("Set up git identity")
   .action("Create initial commit")
   .warning("This will modify your filesystem")
   .info("You can configure git later with 'git config'");

  if (p.confirm()) {
    std::cout << "\n  → Would initialize git here\n\n";
  } else {
    std::cout << "\n  → Skipped\n\n";
  }

  // Demo 3: Multiple choice
  std::cout << "3. Multiple choice:\n";
  Prompt p2;
  p2.title("scaffold")
   .question("Choose project language:");

  int choice = p2.choice({
    "C (with CMake)",
    "C++ (with CMake)",
    "Python (with venv)",
    "Rust (with Cargo)",
    "Go (with modules)"
  }, 1);  // Default to C++

  std::cout << "\n  → You chose option " << (choice + 1) << "\n\n";

  // Demo 4: Text input
  std::cout << "4. Text input:\n";
  Prompt p3;
  p3.title("git-commit")
   .fact("Modified files", "3")
   .question("Enter commit message:");

  std::string msg = p3.input("Brief description of changes");
  if (!msg.empty()) {
    std::cout << "\n  → Commit message: \"" << msg << "\"\n\n";
  } else {
    std::cout << "\n  → No message entered\n\n";
  }

  // Demo 5: Minimal style
  std::cout << "5. Minimal style:\n";
  Prompt p4;
  p4.style(Style::MINIMAL)
   .question("Use minimal style?")
   .action("More compact output")
   .action("Less visual clutter");

  if (p4.confirm()) {
    std::cout << "  → Minimal style selected\n\n";
  }

  std::cout << "Demo complete!\n";
  return 0;
}

void register_demo_command(directive::registry& reg) {
  reg.add("prompt-demo", "Demonstrate prompt module features", cmd_prompt_demo);
}

} // namespace nazg::prompt
