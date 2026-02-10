#include "directive/agent_commands.hpp"

#include "directive/agent_utils.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"

#include "config/config.hpp"
#include "system/fs.hpp"
#include "system/process.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace nazg::directive {
namespace {

namespace fs = std::filesystem;

void print_agent_help() {
  std::cout << "Usage: nazg agent <command> [options]\n"
            << "Commands:\n"
            << "  package [--base-image IMAGE] [--output PATH] [--overwrite]\n"
            << "          Build the nazg-agent container and export it as a tarball.\n";
}

int cmd_agent_package(const command_context &ctx, const context &ectx) {
  std::string base_image = "ubuntu:22.04";
  bool base_image_override = false;
  fs::path output_path = default_agent_tarball_path();
  bool output_override = false;
  bool overwrite = false;

  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--base-image" || arg == "-b") && i + 1 < ctx.argc) {
      base_image = ctx.argv[++i];
      base_image_override = true;
    } else if ((arg == "--output" || arg == "-o") && i + 1 < ctx.argc) {
      output_path = fs::path(::nazg::system::expand_tilde(ctx.argv[++i]));
      output_override = true;
    } else if (arg == "--overwrite" || arg == "--force" || arg == "-f") {
      overwrite = true;
    } else {
      std::cerr << "Unknown option for agent package: " << arg << "\n";
      print_agent_help();
      return 1;
    }
  }

  if (!base_image_override && ectx.cfg &&
      ectx.cfg->has("containers", "nazg_agent_base_image")) {
    base_image = ectx.cfg->get_string("containers", "nazg_agent_base_image");
  }

  if (!output_override && ectx.cfg &&
      ectx.cfg->has("containers", "nazg_agent_tar")) {
    auto cfg_path = ectx.cfg->get_string("containers", "nazg_agent_tar");
    if (!cfg_path.empty())
      output_path = fs::path(::nazg::system::expand_tilde(cfg_path));
  }

  if (output_path.empty()) {
    std::cerr << "Output path cannot be empty." << std::endl;
    return 1;
  }

  std::string image_tag;
  fs::path temp_tar;
  fs::path temp_dir;
  if (!build_agent_container_image(ectx, base_image, image_tag, temp_tar,
                                   temp_dir)) {
    return 1;
  }

  auto cleanup = [&](bool remove_tar) {
    std::error_code ec;
    if (remove_tar && !temp_tar.empty())
      fs::remove(temp_tar, ec);
    if (!temp_dir.empty())
      fs::remove_all(temp_dir, ec);
  };

  std::error_code ec;
  fs::create_directories(output_path.parent_path(), ec);
  if (ec) {
    std::cerr << "Failed to create directory for " << output_path << ": "
              << ec.message() << std::endl;
    cleanup(true);
    return 1;
  }

  if (!overwrite && fs::exists(output_path)) {
    std::cerr << "File already exists at " << output_path
              << " (use --overwrite to replace it)." << std::endl;
    cleanup(true);
    return 1;
  }

  ec.clear();
  fs::rename(temp_tar, output_path, ec);
  bool keep_temp_tar = false;
  if (ec) {
    auto copy_opts = overwrite ? fs::copy_options::overwrite_existing
                               : fs::copy_options::none;
    std::error_code copy_ec;
    fs::copy_file(temp_tar, output_path, copy_opts, copy_ec);
    if (copy_ec) {
      bool permission_denied =
          copy_ec == std::make_error_code(std::errc::permission_denied);
      if (permission_denied) {
        fs::path fallback = fs::current_path() / "build-self" / "nazg-agent.tar";

        std::error_code fb_ec;
        fs::create_directories(fallback.parent_path(), fb_ec);
        if (fb_ec) {
          std::cerr << "Failed to create directory for fallback path "
                    << fallback << ": " << fb_ec.message() << std::endl;
          cleanup(true);
          return 1;
        }

        fs::rename(temp_tar, fallback, fb_ec);
        if (fb_ec) {
          auto fb_opts = overwrite ? fs::copy_options::overwrite_existing
                                   : fs::copy_options::none;
          std::error_code fb_copy_ec;
          fs::copy_file(temp_tar, fallback, fb_opts, fb_copy_ec);
          if (fb_copy_ec) {
            std::cerr << "Failed to write tarball to " << output_path
                      << ": " << copy_ec.message() << std::endl;
            std::cerr << "Fallback path " << fallback
                      << " also failed: " << fb_copy_ec.message() << std::endl;
            cleanup(true);
            return 1;
          }
          keep_temp_tar = true;
        } else {
          keep_temp_tar = false;
        }
        output_path = fallback;
      } else {
        std::cerr << "Failed to write tarball to " << output_path
                  << ": " << copy_ec.message() << std::endl;
        cleanup(true);
        return 1;
      }
    } else {
      keep_temp_tar = true;
    }
  }

  cleanup(keep_temp_tar);

  std::cout << "✓ nazg-agent container saved to " << output_path << "\n"
            << "  Install with: nazg server install-agent <label> --container-tar "
            << output_path << "\n";
  return 0;
}

int cmd_agent(const command_context &ctx, const context &ectx) {
  if (ctx.argc < 3) {
    print_agent_help();
    return 1;
  }

  std::string sub = ctx.argv[2];
  if (sub == "package" || sub == "build" || sub == "build-container")
    return cmd_agent_package(ctx, ectx);

  std::cerr << "Unknown agent subcommand: " << sub << "\n";
  print_agent_help();
  return 1;
}

} // namespace

void register_agent_commands(registry &reg, const context &ctx) {
  (void)ctx;
  command_spec agent_spec{};
  agent_spec.name = "agent";
  agent_spec.summary = "Build and manage nazg-agent deployment artifacts";
  agent_spec.run = &cmd_agent;
  reg.add(agent_spec);
}

} // namespace nazg::directive
