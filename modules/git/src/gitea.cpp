#include "git/gitea.hpp"
#include "git/gitea_api.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include "system/process.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <random>
#include <iomanip>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace nazg::git {

namespace {

// Execute command and capture output
std::string exec_output(const std::string& cmd) {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

std::string default_app_ini_path() {
  return "/etc/gitea/app.ini";
}

std::string normalize_config_path(const std::string& configured) {
  if (configured.empty()) {
    return default_app_ini_path();
  }
  fs::path path(configured);
  if (!path.has_extension()) {
    path /= "app.ini";
  }
  return path.string();
}

std::string config_directory_from_path(const std::string& path) {
  fs::path p(path);
  auto parent = p.has_parent_path() ? p.parent_path().string() : std::string{};
  if (parent.empty()) {
    return "/etc/gitea";
  }
  return parent;
}

std::string generate_random_password(std::size_t length = 24) {
  static constexpr char charset[] =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789";
  static constexpr std::size_t charset_size = sizeof(charset) - 1;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<std::size_t> dist(0, charset_size - 1);

  std::string result;
  result.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    result.push_back(charset[dist(gen)]);
  }
  return result;
}

} // namespace

GiteaServer::GiteaServer(const ServerConfig& cfg,
                         nazg::nexus::Store* store,
                         nazg::blackbox::logger* log)
    : config_(cfg), store_(store), log_(log) {

  // Set defaults for Gitea
  config_.config_path = normalize_config_path(config_.config_path);
  if (config_.repo_base_path.empty()) {
    config_.repo_base_path = "/var/lib/gitea/repositories";
  }
  if (config_.port == 80) {
    config_.port = 3000;  // Default Gitea HTTP port
  }
  if (config_.web_url.empty()) {
    config_.web_url = "http://" + config_.host + ":" + std::to_string(config_.port);
  }

  if (!config_.admin_token.empty()) {
    admin_token_ = config_.admin_token;
    api_ = std::make_unique<GiteaAPI>(get_api_url(), admin_token_, log_);
  }
}

GiteaServer::~GiteaServer() = default;

std::string GiteaServer::ssh_connection() const {
  return config_.ssh_user + "@" + config_.host;
}

bool GiteaServer::ssh_test_connection() {
  std::string cmd = "ssh -o ConnectTimeout=5 -o BatchMode=yes -p " +
                    std::to_string(config_.ssh_port) + " " +
                    ssh_connection() +
                    " 'echo connected' 2>/dev/null";

  std::string output = exec_output(cmd);
  return trim(output) == "connected";
}

bool GiteaServer::ssh_exec(const std::string& cmd, std::string* output) {
  std::string full_cmd = "ssh -p " + std::to_string(config_.ssh_port) + " " +
                         ssh_connection() +
                         " '" + cmd + "' 2>&1";

  if (log_) {
    log_->debug("Git/gitea", "SSH exec: " + cmd);
  }

  if (output) {
    *output = exec_output(full_cmd);
    return true;
  } else {
    int ret = std::system(full_cmd.c_str());
    return ret == 0;
  }
}

bool GiteaServer::upload_file(const std::string& local, const std::string& remote) {
  std::string cmd = "scp -P " + std::to_string(config_.ssh_port) + " " +
                    local + " " + ssh_connection() + ":" + remote + " 2>&1";

  if (log_) {
    log_->debug("Git/gitea", "Uploading: " + local + " -> " + remote);
  }

  int ret = std::system(cmd.c_str());
  return ret == 0;
}

std::string GiteaServer::generate_secret_key() {
  // Generate a random 64-character hex string for SECRET_KEY
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;

  std::ostringstream oss;
  for (int i = 0; i < 4; ++i) {
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
  }
  return oss.str();
}

std::string GiteaServer::generate_app_ini() {
  std::ostringstream ini;

  ini << "APP_NAME = Nazg Git Server\n";
  ini << "RUN_MODE = prod\n\n";

  ini << "[server]\n";
  ini << "PROTOCOL = http\n";
  ini << "DOMAIN = " << config_.host << "\n";
  ini << "HTTP_PORT = " << config_.port << "\n";
  ini << "ROOT_URL = " << config_.web_url << "/\n";
  ini << "DISABLE_SSH = false\n";
  ini << "SSH_PORT = " << config_.ssh_port << "\n";
  ini << "START_SSH_SERVER = true\n";
  ini << "LFS_START_SERVER = true\n\n";

  ini << "[database]\n";
  ini << "DB_TYPE = sqlite3\n";
  ini << "PATH = /var/lib/gitea/data/gitea.db\n\n";

  ini << "[repository]\n";
  ini << "ROOT = " << config_.repo_base_path << "\n\n";

  ini << "[security]\n";
  ini << "INSTALL_LOCK = true\n";
  ini << "SECRET_KEY = " << generate_secret_key() << "\n";
  ini << "INTERNAL_TOKEN = " << generate_secret_key() << "\n\n";

  ini << "[service]\n";
  ini << "DISABLE_REGISTRATION = true\n";
  ini << "REQUIRE_SIGNIN_VIEW = false\n";
  ini << "DEFAULT_KEEP_EMAIL_PRIVATE = true\n\n";

  ini << "[log]\n";
  ini << "MODE = file\n";
  ini << "LEVEL = Info\n";
  ini << "ROOT_PATH = /var/lib/gitea/log\n\n";

  ini << "[actions]\n";
  ini << "ENABLED = true\n\n";

  return ini.str();
}

bool GiteaServer::is_installed() {
  if (!ssh_test_connection()) {
    if (log_) {
      log_->warn("Git/gitea", "Cannot connect to " + config_.host);
    }
    return false;
  }

  std::string output;
  ssh_exec("test -f /usr/local/bin/gitea && echo 'found' || echo 'not found'", &output);

  bool installed = trim(output) == "found";

  if (log_) {
    if (installed) {
      log_->info("Git/gitea", "Gitea is installed on " + config_.host);
    } else {
      log_->info("Git/gitea", "Gitea not found on " + config_.host);
    }
  }

  return installed;
}

ServerStatus GiteaServer::get_status() {
  ServerStatus status;
  status.type = "gitea";

  // Test SSH connection
  status.reachable = ssh_test_connection();
  if (!status.reachable) {
    status.error_message = "Cannot SSH to " + config_.host;
    return status;
  }

  // Check if Gitea binary exists
  std::string output;
  ssh_exec("test -f /usr/local/bin/gitea && echo 'found' || echo 'not found'", &output);
  status.installed = (trim(output) == "found");

  if (status.installed) {
    // Get version
    ssh_exec("/usr/local/bin/gitea --version | head -1", &output);
    status.version = trim(output);

    // Check if service is running
    ssh_exec("systemctl is-active gitea 2>/dev/null || echo 'inactive'", &output);
    if (trim(output) != "active") {
      status.error_message = "Gitea service not running";
    }

    // Try to get repo count via API if available
    if (!admin_token_.empty() && api_) {
      // Query repos via API
      auto repos = api_->list_repos();
      status.repo_count = static_cast<int>(repos.size());
    }
  }

  return status;
}

bool GiteaServer::download_gitea_binary() {
  if (log_) {
    log_->info("Git/gitea", "Downloading Gitea binary...");
  }

  // Detect architecture on remote system
  std::string arch_output;
  ssh_exec("uname -m", &arch_output);
  std::string arch = trim(arch_output);

  // Map to Gitea architecture names
  std::string gitea_arch;
  if (arch == "x86_64" || arch == "amd64") {
    gitea_arch = "linux-amd64";
  } else if (arch == "aarch64" || arch == "arm64") {
    gitea_arch = "linux-arm64";
  } else {
    if (log_) {
      log_->error("Git/gitea", "Unsupported architecture: " + arch);
    }
    return false;
  }

  // Download latest Gitea binary (you may want to make version configurable)
  std::string version = "1.21.0";  // TODO: Make this configurable or fetch latest
  std::string download_url = "https://github.com/go-gitea/gitea/releases/download/v" +
                             version + "/gitea-" + version + "-" + gitea_arch;

  std::string cmd = "wget -q -O /tmp/gitea " + download_url + " && "
                    "sudo mv /tmp/gitea /usr/local/bin/gitea && "
                    "sudo chmod +x /usr/local/bin/gitea";

  if (!ssh_exec(cmd)) {
    if (log_) {
      log_->error("Git/gitea", "Failed to download Gitea binary");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/gitea", "Gitea binary downloaded successfully");
  }
  return true;
}

bool GiteaServer::create_gitea_user() {
  if (log_) {
    log_->info("Git/gitea", "Creating gitea system user...");
  }

  const std::string config_dir = config_directory_from_path(config_.config_path);

  // Check if user already exists
  std::string output;
  ssh_exec("id -u git >/dev/null 2>&1 && echo 'exists' || echo 'not found'", &output);
  if (trim(output) == "exists") {
    if (log_) {
      log_->info("Git/gitea", "User 'git' already exists");
    }
    return true;
  }

  // Create system user
  std::string cmd = "sudo useradd -r -m -d /var/lib/gitea -s /bin/bash git";
  if (!ssh_exec(cmd)) {
    if (log_) {
      log_->error("Git/gitea", "Failed to create git user");
    }
    return false;
  }

  // Create necessary directories
  cmd = "sudo mkdir -p /var/lib/gitea/{data,repositories,log} && "
        "sudo mkdir -p " + nazg::system::shell_quote(config_dir) +
        " && sudo chown -R git:git /var/lib/gitea " +
        nazg::system::shell_quote(config_dir);

  if (!ssh_exec(cmd)) {
    if (log_) {
      log_->error("Git/gitea", "Failed to create Gitea directories");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/gitea", "Gitea user and directories created");
  }
  return true;
}

bool GiteaServer::setup_systemd_service() {
  if (log_) {
    log_->info("Git/gitea", "Setting up systemd service...");
  }

  const std::string config_path = config_.config_path;

  // Generate systemd service file
  std::ostringstream service;
  service << "[Unit]\n";
  service << "Description=Gitea (Git with a cup of tea)\n";
  service << "After=network.target\n\n";

  service << "[Service]\n";
  service << "Type=simple\n";
  service << "User=git\n";
  service << "Group=git\n";
  service << "WorkingDirectory=/var/lib/gitea\n";
  service << "ExecStart=/usr/local/bin/gitea web --config=" << config_path << "\n";
  service << "Restart=always\n";
  service << "Environment=USER=git HOME=/home/git GITEA_WORK_DIR=/var/lib/gitea\n\n";

  service << "[Install]\n";
  service << "WantedBy=multi-user.target\n";

  // Write to temporary file
  std::string temp_file = "/tmp/gitea_service_" + std::to_string(std::time(nullptr));
  std::ofstream out(temp_file);
  if (!out) {
    if (log_) {
      log_->error("Git/gitea", "Failed to create temporary service file");
    }
    return false;
  }
  out << service.str();
  out.close();

  // Upload and install service file
  if (!upload_file(temp_file, "/tmp/gitea.service")) {
    fs::remove(temp_file);
    return false;
  }

  // Move to systemd directory and enable
  std::string cmd = "sudo mv /tmp/gitea.service /etc/systemd/system/gitea.service && "
                    "sudo systemctl daemon-reload && "
                    "sudo systemctl enable gitea";

  bool success = ssh_exec(cmd);
  fs::remove(temp_file);

  if (!success) {
    if (log_) {
      log_->error("Git/gitea", "Failed to install systemd service");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/gitea", "Systemd service created and enabled");
  }
  return true;
}

bool GiteaServer::initialize_database() {
  if (log_) {
    log_->info("Git/gitea", "Initializing Gitea database...");
  }

  // Run database migrations
  std::string cmd = "cd /var/lib/gitea && sudo -u git /usr/local/bin/gitea migrate";
  if (!ssh_exec(cmd)) {
    if (log_) {
      log_->error("Git/gitea", "Failed to run database migrations");
    }
    return false;
  }

  // Create admin user
  admin_password_ = generate_random_password();
  cmd = "cd /var/lib/gitea && sudo -u git /usr/local/bin/gitea admin user create "
        "--admin --username admin --password " +
        nazg::system::shell_quote(admin_password_) + " "
        "--email admin@" + config_.host +
        " --must-change-password=false 2>&1";

  std::string output;
  ssh_exec(cmd, &output);

  bool created = output.find("created!") != std::string::npos;
  bool already_exists = output.find("already exists") != std::string::npos;

  if (!created && !already_exists) {
    if (log_) {
      log_->warn("Git/gitea", "Admin user creation uncertain: " + output);
    }
  }

  if (already_exists) {
    admin_password_.clear();
  }

  if (log_) {
    log_->info("Git/gitea", "Database initialized and admin user created");
  }

  return true;
}

std::optional<std::string> GiteaServer::create_admin_token() {
  if (log_) {
    log_->info("Git/gitea", "Creating admin API token...");
  }

  std::string cmd = "cd /var/lib/gitea && sudo -u git /usr/local/bin/gitea admin user generate-access-token "
                    "--username admin --token-name nazg-management "
                    "--scopes write:admin,write:repository,write:organization,write:user 2>&1";

  std::string output;
  if (!ssh_exec(cmd, &output)) {
    if (log_) {
      log_->error("Git/gitea", "Failed to generate admin token");
    }
    return std::nullopt;
  }

  // Extract token from output (format: "Access token was successfully created: <TOKEN>")
  auto pos = output.find("created:");
  if (pos == std::string::npos) {
    pos = output.find(":");  // Alternative format
  }

  if (pos != std::string::npos) {
    std::string token = trim(output.substr(pos + 1));
    admin_token_ = token;
    config_.admin_token = token;

    // Initialize API client
    api_ = std::make_unique<GiteaAPI>(get_api_url(), token, log_);

    if (log_) {
      log_->info("Git/gitea", "Admin API token created successfully");
    }

    return token;
  }

  if (log_) {
    log_->error("Git/gitea", "Failed to parse admin token from output");
  }
  return std::nullopt;
}

std::string GiteaServer::get_api_url() const {
  return config_.web_url + "/api/v1";
}

bool GiteaServer::install() {
  if (log_) {
    log_->info("Git/gitea", "Starting Gitea installation on " + config_.host);
  }

  // 1. Test SSH connection
  if (!ssh_test_connection()) {
    if (log_) {
      log_->error("Git/gitea", "Cannot establish SSH connection");
    }
    return false;
  }

  // 2. Download Gitea binary
  if (!download_gitea_binary()) {
    return false;
  }

  // 3. Create gitea user and directories
  if (!create_gitea_user()) {
    return false;
  }

  // 4. Generate and upload app.ini
  std::string app_ini_content = generate_app_ini();
  std::string temp_ini = "/tmp/gitea_app_ini_" + std::to_string(std::time(nullptr));
  const std::string remote_app_ini = config_.config_path;

  std::ofstream out(temp_ini);
  if (!out) {
    if (log_) {
      log_->error("Git/gitea", "Failed to create temporary app.ini");
    }
    return false;
  }
  out << app_ini_content;
  out.close();

  if (!upload_file(temp_ini, "/tmp/app.ini")) {
    fs::remove(temp_ini);
    return false;
  }

  std::string cmd = "sudo mv /tmp/app.ini " +
                    nazg::system::shell_quote(remote_app_ini) +
                    " && sudo chown git:git " +
                    nazg::system::shell_quote(remote_app_ini);
  if (!ssh_exec(cmd)) {
    fs::remove(temp_ini);
    if (log_) {
      log_->error("Git/gitea", "Failed to install app.ini");
    }
    return false;
  }
  fs::remove(temp_ini);

  if (log_) {
    log_->info("Git/gitea", "Configuration file installed");
  }

  // 5. Setup systemd service
  if (!setup_systemd_service()) {
    return false;
  }

  // 6. Initialize database
  if (!initialize_database()) {
    return false;
  }

  // 7. Start Gitea service
  if (log_) {
    log_->info("Git/gitea", "Starting Gitea service...");
  }

  if (!ssh_exec("sudo systemctl start gitea")) {
    if (log_) {
      log_->error("Git/gitea", "Failed to start Gitea service");
    }
    return false;
  }

  // Wait a bit for service to start
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // 8. Create admin API token
  auto token = create_admin_token();
  if (!token) {
    if (log_) {
      log_->warn("Git/gitea", "Failed to create admin token, but installation succeeded");
      log_->warn("Git/gitea", "You can create a token manually via the web UI");
    }
  }

  if (log_) {
    log_->info("Git/gitea", "Gitea installation completed successfully!");
    log_->info("Git/gitea", "Web UI: " + config_.web_url);
  }

  return true;
}

bool GiteaServer::configure() {
  if (log_) {
    log_->info("Git/gitea", "Reconfiguring Gitea...");
  }

  // Regenerate and upload app.ini
  std::string app_ini_content = generate_app_ini();
  std::string temp_ini = "/tmp/gitea_app_ini_" + std::to_string(std::time(nullptr));
  const std::string remote_app_ini = config_.config_path;

  std::ofstream out(temp_ini);
  if (!out) {
    if (log_) {
      log_->error("Git/gitea", "Failed to create temporary app.ini");
    }
    return false;
  }
  out << app_ini_content;
  out.close();

  if (!upload_file(temp_ini, "/tmp/app.ini")) {
    fs::remove(temp_ini);
    return false;
  }

  std::string cmd = "sudo mv /tmp/app.ini " +
                    nazg::system::shell_quote(remote_app_ini) +
                    " && sudo chown git:git " +
                    nazg::system::shell_quote(remote_app_ini) +
                    " && sudo systemctl restart gitea";

  bool success = ssh_exec(cmd);
  fs::remove(temp_ini);

  if (!success) {
    if (log_) {
      log_->error("Git/gitea", "Failed to reconfigure Gitea");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/gitea", "Gitea reconfigured successfully");
  }

  return true;
}

bool GiteaServer::sync_repos(const std::vector<std::string>& local_paths) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized, cannot sync repos");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/gitea", "Syncing " + std::to_string(local_paths.size()) + " repos to Gitea");
  }

  bool all_ok = true;

  for (const auto &local_path : local_paths) {
    fs::path repo_path(local_path);

    // Extract repo name from path (strip .git suffix if present)
    std::string repo_name = repo_path.filename().string();
    if (repo_name.size() > 4 && repo_name.substr(repo_name.size() - 4) == ".git") {
      repo_name = repo_name.substr(0, repo_name.size() - 4);
    }
    if (repo_name.empty()) {
      repo_name = repo_path.parent_path().filename().string();
    }

    if (log_) {
      log_->info("Git/gitea", "Syncing repo: " + repo_name + " from " + local_path);
    }

    // Check if repo already exists on Gitea
    auto existing = api_->get_repo("admin", repo_name);
    if (!existing) {
      // Create the repo on Gitea
      Repository repo;
      repo.name = repo_name;
      repo.is_private = true;

      if (!api_->create_repo("admin", repo)) {
        if (log_) {
          log_->error("Git/gitea", "Failed to create repo: " + repo_name);
        }
        all_ok = false;
        continue;
      }

      if (log_) {
        log_->info("Git/gitea", "Created repo on Gitea: " + repo_name);
      }
    }

    // Add gitea remote to local repo (ignore error if already exists)
    std::string gitea_url = config_.web_url + "/admin/" + repo_name + ".git";
    std::string add_remote_cmd = "git -C " + nazg::system::shell_quote(local_path) +
                                 " remote add gitea " + nazg::system::shell_quote(gitea_url) +
                                 " 2>/dev/null; true";
    nazg::system::run_command(add_remote_cmd);

    // Update remote URL in case it changed
    std::string set_url_cmd = "git -C " + nazg::system::shell_quote(local_path) +
                              " remote set-url gitea " + nazg::system::shell_quote(gitea_url);
    nazg::system::run_command(set_url_cmd);

    // Push all refs to Gitea
    std::string push_cmd = "git -C " + nazg::system::shell_quote(local_path) +
                           " push --mirror gitea 2>&1";
    auto push_result = nazg::system::run_command_capture(push_cmd);

    if (push_result.exit_code != 0) {
      if (log_) {
        log_->error("Git/gitea", "Failed to push " + repo_name + ": " + push_result.output);
      }
      all_ok = false;
    } else {
      if (log_) {
        log_->info("Git/gitea", "Pushed " + repo_name + " to Gitea");
      }
    }
  }

  return all_ok;
}

bool GiteaServer::create_repo(const std::string& name,
                              const std::string& owner,
                              bool is_private) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized");
    }
    return false;
  }

  Repository repo;
  repo.name = name;
  repo.is_private = is_private;

  return api_->create_repo(owner.empty() ? "admin" : owner, repo);
}

bool GiteaServer::delete_repo(const std::string& owner, const std::string& repo) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized");
    }
    return false;
  }

  return api_->delete_repo(owner, repo);
}

bool GiteaServer::create_user(const std::string& username,
                              const std::string& email,
                              const std::string& password) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized");
    }
    return false;
  }

  User user;
  user.username = username;
  user.email = email;

  return api_->create_user(user, password);
}

bool GiteaServer::create_organization(const std::string& name,
                                     const std::string& description) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized");
    }
    return false;
  }

  Organization org;
  org.username = name;
  org.full_name = name;
  org.description = description;

  return api_->create_org(org);
}

bool GiteaServer::setup_webhook(const std::string& owner,
                               const std::string& repo,
                               const std::string& url) {
  if (!api_) {
    if (log_) {
      log_->error("Git/gitea", "API client not initialized");
    }
    return false;
  }

  Webhook hook;
  hook.type = "gitea";
  hook.url = url;
  hook.content_type = "json";
  hook.events = {"push", "pull_request"};
  hook.active = true;

  return api_->create_webhook(owner, repo, hook);
}

} // namespace nazg::git
