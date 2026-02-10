#include "engine/updater.hpp"
#include "blackbox/logger.hpp"
#include "config/parser.hpp"
#include "system/fs.hpp"
#include "system/process.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;
using nazg::system::expand_tilde;
using nazg::system::run_capture;
using nazg::system::run_command;
using nazg::system::trim;
using nazg::system::shell_quote;

namespace {

static std::time_t mtime(const fs::path &p) {
  std::error_code ec;
  auto ft = fs::last_write_time(p, ec);
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  return ec ? std::time_t{} : decltype(ft)::clock::to_time_t(ft);
#else
  (void)ft;
  (void)ec;
  return std::time(nullptr);
#endif
}

bool ensure_dir(const fs::path &p) {
  std::error_code ec;
  if (fs::exists(p, ec))
    return fs::is_directory(p, ec);
  return fs::create_directories(p, ec);
}

bool is_writable(const fs::path &p) {
  std::error_code ec;
  auto st = fs::status(p, ec);
  if (ec)
    return false;
  if (!fs::exists(p))
    return is_writable(p.parent_path());
  std::ofstream test(p / ".wcheck.tmp");
  if (!test.good())
    return false;
  test.close();
  fs::remove(p / ".wcheck.tmp", ec);
  return true;
}

std::string current_exe() {
#if defined(__linux__)
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    return std::string(buf);
  }
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0)
    return std::string(buf);
#endif
  return {};
}

std::string git_short(const std::string &dir) {
  auto s = run_capture("git -C " + shell_quote(dir) +
                       " rev-parse --short HEAD 2>/dev/null");
  return trim(s);
}

std::string git_full(const std::string &dir) {
  auto s = run_capture("git -C " + shell_quote(dir) +
                       " rev-parse HEAD 2>/dev/null");
  return trim(s);
}

std::string detect_version_tag(const std::string &dir) {
  auto tag = trim(run_capture("git -C " + shell_quote(dir) +
                              " describe --tags --abbrev=0 2>/dev/null"));
  if (!tag.empty())
    return tag;
  return git_short(dir);
}

void log_info(const nazg::update::Config &cfg, const std::string &tag,
              const std::string &msg) {
  if (cfg.log)
    cfg.log->info(tag, msg);
  else if (cfg.verbose)
    std::cout << "[" << tag << "] " << msg << "\n";
}

void log_error(const nazg::update::Config &cfg, const std::string &tag,
               const std::string &msg) {
  if (cfg.log)
    cfg.log->error(tag, msg);
  else
    std::cerr << "[" << tag << "] ERROR: " << msg << "\n";
}

} // namespace

namespace nazg::update {

Config default_config() {
  Config c{};
  // Use XDG-compliant directories
  c.prefix = nazg::config::default_data_dir() + "/versions";
  c.bin_dir = nazg::config::default_bin_dir();
  const char *ev = std::getenv("NAZG_REPO");
  c.repo_url = ev ? ev : "https://github.com/yourorg/nazg.git";
  c.ref = "";
  return c;
}

CheckResult check(const Config &cfg) {
  CheckResult r{};
  r.current_version = "unknown";
  r.latest_version = "unknown";
  r.latest_ref = cfg.ref.empty() ? "auto" : cfg.ref;
  return r;
}

static fs::path active_symlink_path(const Config &cfg) {
  return fs::path(expand_tilde(cfg.bin_dir)) / "nazg";
}

static std::optional<std::string> active_version(const Config &cfg) {
  std::error_code ec;
  auto link = active_symlink_path(cfg);
  if (!fs::exists(link, ec))
    return std::nullopt;
  auto target = fs::read_symlink(link, ec);
  if (ec)
    return std::nullopt;
  auto p = target;
  if (p.has_parent_path())
    p = p.parent_path().parent_path().filename();
  return p.string();
}

static fs::path versions_root(const Config &cfg) {
  return fs::path(expand_tilde(cfg.prefix));
}

static fs::path version_dir(const Config &cfg, const std::string &ver) {
  return versions_root(cfg) / ver;
}

static bool write_meta(const fs::path &dir, const BuildInfo &bi) {
  std::ofstream out(dir / "META.json");
  if (!out.good())
    return false;
  out << "{\n"
      << "  \"version\": \"" << bi.version << "\",\n"
      << "  \"commit\": \"" << bi.commit << "\",\n"
      << "  \"src\": \"" << bi.src_dir << "\"\n"
      << "}\n";
  return true;
}

static bool activate(const Config &cfg, const std::string &ver) {
  auto link = active_symlink_path(cfg);
  auto target = version_dir(cfg, ver) / "bin" / "nazg";
  std::error_code ec;
  fs::create_directories(link.parent_path(), ec);
  if (fs::exists(link, ec))
    fs::remove(link, ec);
  fs::create_symlink(target, link, ec);
  return !ec;
}

static void prune_old(const Config &cfg, int keep) {
  std::vector<std::pair<fs::path, std::time_t>> list;
  std::error_code ec;
  for (auto &d : fs::directory_iterator(versions_root(cfg), ec)) {
    if (!d.is_directory())
      continue;
    auto name = d.path().filename().string();
    if (name.size() < 2 || name[0] != 'v')
      continue;
    list.push_back({d.path(), mtime(d.path())});
  }
  std::sort(list.begin(), list.end(),
            [](auto &a, auto &b) { return a.second > b.second; });
  for (size_t i = keep; i < list.size(); ++i) {
    std::error_code delc;
    log_info(cfg, "update",
             "Pruning old version: " + list[i].first.filename().string());
    fs::remove_all(list[i].first, delc);
  }
}

UpdateResult update_from_source(const Config &cfg_in) {
  UpdateResult ur{};
  Config cfg = cfg_in;

  auto root = versions_root(cfg);
  if (!ensure_dir(root)) {
    ur.message = "cannot create versions root: " + root.string();
    log_error(cfg, "update", ur.message);
    return ur;
  }
  if (!is_writable(root)) {
    ur.message = "no write permission for " + root.string();
    log_error(cfg, "update", ur.message);
    return ur;
  }

  // Choose source directory
  std::string src_dir;

  if (!cfg.local_src_hint.empty()) {
    src_dir = expand_tilde(cfg.local_src_hint);
    log_info(cfg, "update", "Using source hint: " + src_dir);
  }

  if (src_dir.empty()) {
    if (const char *ev = std::getenv("NAZG_SRC"); ev && *ev) {
      src_dir = ev;
      log_info(cfg, "update", "Using NAZG_SRC: " + src_dir);
    }
  }

  if (src_dir.empty()) {
    const char *home = std::getenv("HOME");
    if (home) {
      fs::path p = fs::path(home) / "projects" / "cpp" / "nazg";
      if (fs::exists(p / "CMakeLists.txt")) {
        src_dir = p.string();
        log_info(cfg, "update", "Found source at: " + src_dir);
      }
    }
  }

  if (src_dir.empty()) {
    auto exe = current_exe();
    if (!exe.empty()) {
      fs::path p = exe;
      p = p.parent_path().parent_path();
      if (fs::exists(p / "CMakeLists.txt")) {
        src_dir = p.string();
        log_info(cfg, "update", "Found source near exe: " + src_dir);
      }
    }
  }

  bool have_local =
      !src_dir.empty() && fs::exists(fs::path(src_dir) / "CMakeLists.txt");

  if (!have_local) {
    auto tmp = (root / "tmp-src").string();
    log_info(cfg, "update", "Cloning repository: " + cfg.repo_url);
    fs::remove_all(tmp);
    if (run_command("git clone " + shell_quote(cfg.repo_url) + " " +
                    shell_quote(tmp)) != 0) {
      ur.message = "git clone failed";
      log_error(cfg, "update", ur.message);
      return ur;
    }
    src_dir = tmp;
    have_local = true;
    cfg.prefer_local = false;
  }

  const bool local_mode = cfg.prefer_local;
  std::string commit;
  std::string version;

  if (!local_mode) {
    std::string ref = cfg.ref;
    if (ref.empty()) {
      auto latest_tag = trim(run_capture(
          "git -C " + shell_quote(src_dir) +
          " tag --list --sort=-v:refname | head -n1"));
      ref = latest_tag.empty() ? "main" : latest_tag;
    }
    log_info(cfg, "update", "Fetching updates for ref: " + ref);
    if (run_command("git -C " + shell_quote(src_dir) +
                    " fetch --all --tags --prune") != 0) {
      ur.message = "git fetch failed";
      log_error(cfg, "update", ur.message);
      return ur;
    }
    if (run_command("git -C " + shell_quote(src_dir) + " checkout " +
                    shell_quote(ref)) != 0) {
      ur.message = "git checkout " + ref + " failed";
      log_error(cfg, "update", ur.message);
      return ur;
    }
    if (ref == "main" || ref == "master" || ref == "develop" ||
        ref.rfind("v", 0) != 0) {
      (void)run_command("git -C " + shell_quote(src_dir) + " pull --ff-only");
    }

    commit = git_full(src_dir);
    version = detect_version_tag(src_dir);
    if (version.empty())
      version = git_short(src_dir);
    if (version.rfind('v', 0) != 0)
      version = "v" + version;
  } else {
    std::string tag = trim(run_capture(
        "git -C " + shell_quote(src_dir) +
        " describe --tags --abbrev=0 2>/dev/null"));
    std::string shortsha = git_short(src_dir);
    commit = git_full(src_dir);
    if (!tag.empty())
      version = tag;
    else if (!shortsha.empty())
      version = "v" + shortsha;
    else
      version = "v-local";
    log_info(cfg, "update", "Building local source as: " + version);
  }

  // Build
  fs::path build_dir = fs::path(src_dir) / "build-self";
  fs::remove_all(build_dir);
  if (!ensure_dir(build_dir)) {
    ur.message = "cannot create build dir";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  const std::string src_dir_quoted = shell_quote(src_dir);
  const std::string build_dir_quoted = shell_quote(build_dir.string());

  std::string configure_cmd = "cmake -S " + src_dir_quoted + " -B " +
                              build_dir_quoted +
                              " -DCMAKE_BUILD_TYPE=RelWithDebInfo";

  auto add_fetchcontent_hint = [&](const fs::path &candidate) {
    if (!candidate.empty() && fs::exists(candidate)) {
      configure_cmd +=
          " -DFETCHCONTENT_SOURCE_DIR_FTXUI=" + shell_quote(candidate.string());
      log_info(cfg, "update",
               "Using local FTXUI cache from " + candidate.string());
      return true;
    }
    return false;
  };

  // Prefer a top-level build cache if one exists; fall back to relative cache
  // roots commonly produced by local development builds.
  fs::path ftxui_cache = fs::path(src_dir) / "build" / "_deps" / "ftxui-src";
  if (!add_fetchcontent_hint(ftxui_cache)) {
    ftxui_cache = fs::path(src_dir) / "_deps" / "ftxui-src";
    if (!add_fetchcontent_hint(ftxui_cache)) {
      ftxui_cache = fs::path(src_dir) / "external" / "ftxui";
      add_fetchcontent_hint(ftxui_cache);
    }
  }

  log_info(cfg, "update", "Configuring build...");
  if (run_command(configure_cmd) != 0) {
    ur.message = "cmake configure failed";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  log_info(cfg, "update", "Building nazg...");
  if (run_command("cmake --build " + build_dir_quoted + " -j") != 0) {
    ur.message = "cmake build failed";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  // Stage
  fs::path staged = version_dir(cfg, version);
  if (fs::exists(staged))
    fs::remove_all(staged);
  fs::path staged_bin = staged / "bin";
  if (!ensure_dir(staged_bin)) {
    ur.message = "cannot create staged bin dir";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  fs::path built = fs::path(build_dir) / "nazg";
  if (!fs::exists(built))
    built = fs::path(build_dir) / "bin" / "nazg";
  if (!fs::exists(built)) {
    ur.message = "built binary not found";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  std::error_code ec;
  fs::copy_file(built, staged_bin / "nazg",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    ur.message = "copy binary failed: " + ec.message();
    log_error(cfg, "update", ur.message);
    return ur;
  }

  // Copy migrations
  fs::path staged_migrations = staged / "migrations";
  if (!ensure_dir(staged_migrations)) {
    ur.message = "cannot create migrations dir";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  fs::path migrations_src = fs::path(src_dir) / "modules" / "nexus" / "migrations";
  if (fs::exists(migrations_src) && fs::is_directory(migrations_src)) {
    int migration_count = 0;
    for (const auto &entry : fs::directory_iterator(migrations_src)) {
      if (entry.is_regular_file() && entry.path().extension() == ".sql") {
        std::error_code copy_ec;
        fs::copy_file(entry.path(), staged_migrations / entry.path().filename(),
                      fs::copy_options::overwrite_existing, copy_ec);
        if (!copy_ec) {
          migration_count++;
        } else {
          log_error(cfg, "update", "Failed to copy migration " +
                    entry.path().filename().string() + ": " + copy_ec.message());
        }
      }
    }
    log_info(cfg, "update", "Copied " + std::to_string(migration_count) +
             " migration(s) to " + staged_migrations.string());
  } else {
    log_error(cfg, "update", "Warning: No migrations found in " + migrations_src.string());
  }

  (void)run_capture(shell_quote((staged_bin / "nazg").string()) +
                    " --version 2>/dev/null");

  BuildInfo bi{version, commit, src_dir, build_dir.string(), staged.string()};
  write_meta(staged, bi);

  if (cfg.dry_run) {
    ur.ok = true;
    ur.activated_version = version;
    ur.message = "built to " + staged.string() + " (dry-run; not activated)";
    log_info(cfg, "update", ur.message);
    return ur;
  }

  log_info(cfg, "update", "Activating version: " + version);
  if (!activate(cfg, version)) {
    ur.message = "activate (symlink) failed";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  prune_old(cfg, cfg.keep);

  ur.ok = true;
  ur.activated_version = version;
  ur.message = "updated to " + version +
               (commit.empty() ? "" : " (" + commit.substr(0, 7) + ")");
  log_info(cfg, "update", ur.message);

  if (cfg.reexec_after) {
    auto new_path = (version_dir(cfg, version) / "bin" / "nazg").string();
    ::execl(new_path.c_str(), new_path.c_str(), "--version", (char *)nullptr);
  }

  return ur;
}

UpdateResult rollback(const Config &cfg,
                      std::optional<std::string> to_version) {
  UpdateResult ur{};

  auto root = versions_root(cfg);

  std::vector<fs::path> vers;
  std::error_code ec;
  for (auto &d : fs::directory_iterator(root, ec)) {
    if (!d.is_directory())
      continue;
    auto name = d.path().filename().string();
    if (name.size() >= 2 && name[0] == 'v')
      vers.push_back(d.path());
  }
  if (vers.empty()) {
    ur.message = "no installed versions found";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  std::vector<std::pair<fs::path, std::time_t>> list;
  for (auto &p : vers) {
    list.push_back({p, mtime(p)});
  }
  std::sort(list.begin(), list.end(),
            [](auto &a, auto &b) { return a.second > b.second; });

  auto current = active_version(cfg);
  std::string target;

  if (to_version && !to_version->empty()) {
    std::string want = *to_version;
    std::string want_norm =
        (want.size() && want[0] == 'v') ? want : ("v" + want);
    for (auto &it : list) {
      if (it.first.filename() == want_norm) {
        target = want_norm;
        break;
      }
    }
    if (target.empty()) {
      ur.message = "version not found: " + want;
      log_error(cfg, "update", ur.message);
      return ur;
    }
    if (current && *current == target) {
      ur.message = "already at " + target;
      ur.ok = true;
      ur.activated_version = target;
      log_info(cfg, "update", ur.message);
      return ur;
    }
  } else {
    if (list.size() < 2) {
      ur.message = "no previous version to roll back to";
      log_error(cfg, "update", ur.message);
      return ur;
    }
    if (current && list[0].first.filename() == *current) {
      target = list[1].first.filename().string();
    } else {
      target = list[0].first.filename().string();
    }
  }

  log_info(cfg, "update", "Rolling back to: " + target);
  if (!activate(cfg, target)) {
    ur.message = "rollback activation failed";
    log_error(cfg, "update", ur.message);
    return ur;
  }

  ur.ok = true;
  ur.activated_version = target;
  ur.message = "rolled back to " + target;
  log_info(cfg, "update", ur.message);
  return ur;
}

} // namespace nazg::update
