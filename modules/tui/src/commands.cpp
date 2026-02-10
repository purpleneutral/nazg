#include "tui/commands.hpp"
#include "tui/tui.hpp"
#include "tui/menu_registry.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include <iostream>

namespace nazg::tui {

namespace {

int cmd_tui(const directive::command_context& cctx, const directive::context& ectx) {
  // Parse options
  for (int i = 2; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg tui [options]\n\n";
      std::cout << "Start the nazg TUI multiplexer.\n\n";
      std::cout << "The TUI provides a terminal multiplexer similar to tmux,\n";
      std::cout << "with vim-like modal editing and keyboard navigation.\n\n";
      std::cout << "Options:\n";
      std::cout << "  -h, --help    Show this help message\n\n";
      std::cout << "Keyboard shortcuts:\n";
      std::cout << "  Ctrl-D        Exit TUI\n";
      std::cout << "  Ctrl-C        Send interrupt to shell\n\n";
      std::cout << "Built-in menus:\n";
      std::cout << "  :load docker  Open Docker dashboard\n\n";
      std::cout << "Note: Phase 1 implementation - single pane only.\n";
      std::cout << "Future phases will add window management, prefix keys,\n";
      std::cout << "modal system, and more.\n";
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      std::cerr << "Run 'nazg tui --help' for usage.\n";
      return 1;
    }
  }

  // Create and run TUI app
  TUIApp app(ectx.log, ectx.cfg);

  // Register built-in menus (docker, etc.)
  if (ectx.store) {
    app.register_menus(ectx.store);
  }

  return app.run();
}

} // namespace

void register_commands(directive::registry& reg, const directive::context& ctx) {
  (void)ctx; // Not used in Phase 1

  reg.add("tui",
          "Start the nazg TUI multiplexer (tmux-like terminal UI)",
          cmd_tui);
}

} // namespace nazg::tui
