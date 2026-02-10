#include "git/gitea_api.hpp"
#include "blackbox/logger.hpp"
#include <curl/curl.h>
#include <sstream>
#include <stdexcept>

namespace nazg::git {

namespace {

// Callback for libcurl to write response data
size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* output) {
  size_t total_size = size * nmemb;
  output->append(static_cast<char*>(contents), total_size);
  return total_size;
}

// Simple JSON string escaping
std::string escape_json_impl(const std::string& str) {
  std::ostringstream oss;
  for (char c : str) {
    switch (c) {
      case '"':  oss << "\\\""; break;
      case '\\': oss << "\\\\"; break;
      case '\b': oss << "\\b";  break;
      case '\f': oss << "\\f";  break;
      case '\n': oss << "\\n";  break;
      case '\r': oss << "\\r";  break;
      case '\t': oss << "\\t";  break;
      default:
        if (c < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          oss << c;
        }
    }
  }
  return oss.str();
}

} // namespace

GiteaAPI::GiteaAPI(const std::string& base_url,
                   const std::string& token,
                   nazg::blackbox::logger* log)
    : base_url_(base_url), token_(token), log_(log) {

  // Remove trailing slash from base_url if present
  if (!base_url_.empty() && base_url_.back() == '/') {
    base_url_.pop_back();
  }

  // Initialize libcurl (global init)
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

GiteaAPI::~GiteaAPI() {
  curl_global_cleanup();
}

std::string GiteaAPI::http_request(const std::string& method,
                                   const std::string& endpoint,
                                   const std::string& body) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL");
  }

  std::string url = base_url_ + endpoint;
  std::string response;

  // Set URL
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  // Set method
  if (method == "GET") {
    // Default
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  } else if (method == "PATCH") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
  }

  // Set headers
  struct curl_slist* headers = nullptr;
  std::string auth_header = "Authorization: token " + token_;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  // Set response callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  // Perform request
  CURLcode res = curl_easy_perform(curl);

  // Cleanup
  curl_slist_free_all(headers);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("CURL error: ") + curl_easy_strerror(res));
    }
    throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
  }

  if (log_) {
    log_->debug("Git/gitea-api", method + " " + endpoint + " -> " + std::to_string(http_code));
  }

  // Check HTTP status
  if (http_code >= 400) {
    if (log_) {
      log_->error("Git/gitea-api", "HTTP error " + std::to_string(http_code) + ": " + response);
    }
    return "";
  }

  return response;
}

std::string GiteaAPI::http_get(const std::string& endpoint) {
  return http_request("GET", endpoint, "");
}

std::string GiteaAPI::http_post(const std::string& endpoint, const std::string& body) {
  return http_request("POST", endpoint, body);
}

std::string GiteaAPI::http_delete(const std::string& endpoint) {
  return http_request("DELETE", endpoint, "");
}

std::string GiteaAPI::http_patch(const std::string& endpoint, const std::string& body) {
  return http_request("PATCH", endpoint, body);
}

std::string GiteaAPI::escape_json(const std::string& str) {
  return escape_json_impl(str);
}

// Simple JSON extraction helpers (manual parsing for simplicity)
std::optional<std::string> GiteaAPI::extract_json_string(const std::string& json,
                                                         const std::string& key) {
  std::string search = "\"" + key + "\":\"";
  auto pos = json.find(search);
  if (pos == std::string::npos) {
    search = "\"" + key + "\": \"";
    pos = json.find(search);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
  }

  pos += search.length();
  auto end = json.find("\"", pos);
  if (end == std::string::npos) {
    return std::nullopt;
  }

  return json.substr(pos, end - pos);
}

std::optional<int64_t> GiteaAPI::extract_json_int(const std::string& json,
                                                  const std::string& key) {
  std::string search = "\"" + key + "\":";
  auto pos = json.find(search);
  if (pos == std::string::npos) {
    search = "\"" + key + "\": ";
    pos = json.find(search);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
  }

  pos += search.length();

  // Skip whitespace
  while (pos < json.length() && std::isspace(json[pos])) {
    ++pos;
  }

  // Extract number
  std::string num_str;
  while (pos < json.length() && (std::isdigit(json[pos]) || json[pos] == '-')) {
    num_str += json[pos++];
  }

  if (num_str.empty()) {
    return std::nullopt;
  }

  return std::stoll(num_str);
}

std::optional<bool> GiteaAPI::extract_json_bool(const std::string& json,
                                               const std::string& key) {
  std::string search = "\"" + key + "\":";
  auto pos = json.find(search);
  if (pos == std::string::npos) {
    search = "\"" + key + "\": ";
    pos = json.find(search);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
  }

  pos += search.length();

  // Skip whitespace
  while (pos < json.length() && std::isspace(json[pos])) {
    ++pos;
  }

  if (json.substr(pos, 4) == "true") {
    return true;
  } else if (json.substr(pos, 5) == "false") {
    return false;
  }

  return std::nullopt;
}

// Repository JSON methods
std::string GiteaAPI::repo_to_json(const Repository& repo) {
  std::ostringstream json;
  json << "{";
  json << "\"name\":\"" << escape_json(repo.name) << "\"";
  if (!repo.description.empty()) {
    json << ",\"description\":\"" << escape_json(repo.description) << "\"";
  }
  json << ",\"private\":" << (repo.is_private ? "true" : "false");
  json << ",\"auto_init\":false";
  json << "}";
  return json.str();
}

Repository GiteaAPI::parse_repo_json(const std::string& json) {
  Repository repo;

  auto id = extract_json_int(json, "id");
  if (id) repo.id = *id;

  auto name = extract_json_string(json, "name");
  if (name) repo.name = *name;

  auto full_name = extract_json_string(json, "full_name");
  if (full_name) repo.full_name = *full_name;

  auto description = extract_json_string(json, "description");
  if (description) repo.description = *description;

  auto is_private = extract_json_bool(json, "private");
  if (is_private) repo.is_private = *is_private;

  auto clone_url = extract_json_string(json, "clone_url");
  if (clone_url) repo.clone_url = *clone_url;

  auto ssh_url = extract_json_string(json, "ssh_url");
  if (ssh_url) repo.ssh_url = *ssh_url;

  auto html_url = extract_json_string(json, "html_url");
  if (html_url) repo.html_url = *html_url;

  return repo;
}

std::vector<Repository> GiteaAPI::parse_repos_json(const std::string& json) {
  std::vector<Repository> repos;

  // Simple array parsing - find each object between { }
  size_t pos = 0;
  while ((pos = json.find("{", pos)) != std::string::npos) {
    size_t end = json.find("}", pos);
    if (end == std::string::npos) break;

    // Find the matching closing brace (handle nested objects)
    int depth = 1;
    size_t search_pos = pos + 1;
    while (depth > 0 && search_pos < json.length()) {
      if (json[search_pos] == '{') {
        depth++;
      } else if (json[search_pos] == '}') {
        depth--;
      }
      search_pos++;
    }

    if (depth == 0) {
      std::string obj = json.substr(pos, search_pos - pos);
      repos.push_back(parse_repo_json(obj));
      pos = search_pos;
    } else {
      break;
    }
  }

  return repos;
}

// User JSON methods
std::string GiteaAPI::user_to_json(const User& user, const std::string& password) {
  std::ostringstream json;
  json << "{";
  json << "\"username\":\"" << escape_json(user.username) << "\"";
  json << ",\"email\":\"" << escape_json(user.email) << "\"";
  json << ",\"password\":\"" << escape_json(password) << "\"";
  if (!user.full_name.empty()) {
    json << ",\"full_name\":\"" << escape_json(user.full_name) << "\"";
  }
  json << ",\"must_change_password\":false";
  json << "}";
  return json.str();
}

User GiteaAPI::parse_user_json(const std::string& json) {
  User user;

  auto id = extract_json_int(json, "id");
  if (id) user.id = *id;

  auto username = extract_json_string(json, "username");
  if (username) user.username = *username;

  auto email = extract_json_string(json, "email");
  if (email) user.email = *email;

  auto full_name = extract_json_string(json, "full_name");
  if (full_name) user.full_name = *full_name;

  auto is_admin = extract_json_bool(json, "is_admin");
  if (is_admin) user.is_admin = *is_admin;

  return user;
}

std::vector<User> GiteaAPI::parse_users_json(const std::string& json) {
  std::vector<User> users;

  size_t pos = 0;
  while ((pos = json.find("{", pos)) != std::string::npos) {
    size_t end = json.find("}", pos);
    if (end == std::string::npos) break;

    int depth = 1;
    size_t search_pos = pos + 1;
    while (depth > 0 && search_pos < json.length()) {
      if (json[search_pos] == '{') {
        depth++;
      } else if (json[search_pos] == '}') {
        depth--;
      }
      search_pos++;
    }

    if (depth == 0) {
      std::string obj = json.substr(pos, search_pos - pos);
      users.push_back(parse_user_json(obj));
      pos = search_pos;
    } else {
      break;
    }
  }

  return users;
}

// Organization JSON methods
std::string GiteaAPI::org_to_json(const Organization& org) {
  std::ostringstream json;
  json << "{";
  json << "\"username\":\"" << escape_json(org.username) << "\"";
  if (!org.full_name.empty()) {
    json << ",\"full_name\":\"" << escape_json(org.full_name) << "\"";
  }
  if (!org.description.empty()) {
    json << ",\"description\":\"" << escape_json(org.description) << "\"";
  }
  if (!org.website.empty()) {
    json << ",\"website\":\"" << escape_json(org.website) << "\"";
  }
  json << "}";
  return json.str();
}

Organization GiteaAPI::parse_org_json(const std::string& json) {
  Organization org;

  auto id = extract_json_int(json, "id");
  if (id) org.id = *id;

  auto username = extract_json_string(json, "username");
  if (username) org.username = *username;

  auto full_name = extract_json_string(json, "full_name");
  if (full_name) org.full_name = *full_name;

  auto description = extract_json_string(json, "description");
  if (description) org.description = *description;

  auto website = extract_json_string(json, "website");
  if (website) org.website = *website;

  return org;
}

std::vector<Organization> GiteaAPI::parse_orgs_json(const std::string& json) {
  std::vector<Organization> orgs;

  size_t pos = 0;
  while ((pos = json.find("{", pos)) != std::string::npos) {
    int depth = 1;
    size_t search_pos = pos + 1;
    while (depth > 0 && search_pos < json.length()) {
      if (json[search_pos] == '{') {
        depth++;
      } else if (json[search_pos] == '}') {
        depth--;
      }
      search_pos++;
    }

    if (depth == 0) {
      std::string obj = json.substr(pos, search_pos - pos);
      orgs.push_back(parse_org_json(obj));
      pos = search_pos;
    } else {
      break;
    }
  }

  return orgs;
}

// Webhook JSON methods
std::string GiteaAPI::webhook_to_json(const Webhook& hook) {
  std::ostringstream json;
  json << "{";
  json << "\"type\":\"" << escape_json(hook.type) << "\"";
  json << ",\"config\":{";
  json << "\"url\":\"" << escape_json(hook.url) << "\"";
  json << ",\"content_type\":\"" << escape_json(hook.content_type) << "\"";
  json << "}";
  json << ",\"events\":[";
  for (size_t i = 0; i < hook.events.size(); ++i) {
    if (i > 0) json << ",";
    json << "\"" << escape_json(hook.events[i]) << "\"";
  }
  json << "]";
  json << ",\"active\":" << (hook.active ? "true" : "false");
  json << "}";
  return json.str();
}

Webhook GiteaAPI::parse_webhook_json(const std::string& json) {
  Webhook hook;

  auto id = extract_json_int(json, "id");
  if (id) hook.id = *id;

  auto type = extract_json_string(json, "type");
  if (type) hook.type = *type;

  // Extract URL from nested config object
  auto config_pos = json.find("\"config\"");
  if (config_pos != std::string::npos) {
    auto url = extract_json_string(json.substr(config_pos), "url");
    if (url) hook.url = *url;

    auto content_type = extract_json_string(json.substr(config_pos), "content_type");
    if (content_type) hook.content_type = *content_type;
  }

  auto active = extract_json_bool(json, "active");
  if (active) hook.active = *active;

  // Parse events array (simplified - just look for quoted strings in events)
  auto events_pos = json.find("\"events\"");
  if (events_pos != std::string::npos) {
    auto array_start = json.find("[", events_pos);
    auto array_end = json.find("]", array_start);
    if (array_start != std::string::npos && array_end != std::string::npos) {
      std::string events_str = json.substr(array_start + 1, array_end - array_start - 1);
      // Extract each quoted string
      size_t pos = 0;
      while ((pos = events_str.find("\"", pos)) != std::string::npos) {
        ++pos;
        auto end = events_str.find("\"", pos);
        if (end != std::string::npos) {
          hook.events.push_back(events_str.substr(pos, end - pos));
          pos = end + 1;
        } else {
          break;
        }
      }
    }
  }

  return hook;
}

std::vector<Webhook> GiteaAPI::parse_webhooks_json(const std::string& json) {
  std::vector<Webhook> hooks;

  size_t pos = 0;
  while ((pos = json.find("{", pos)) != std::string::npos) {
    int depth = 1;
    size_t search_pos = pos + 1;
    while (depth > 0 && search_pos < json.length()) {
      if (json[search_pos] == '{') {
        depth++;
      } else if (json[search_pos] == '}') {
        depth--;
      }
      search_pos++;
    }

    if (depth == 0) {
      std::string obj = json.substr(pos, search_pos - pos);
      hooks.push_back(parse_webhook_json(obj));
      pos = search_pos;
    } else {
      break;
    }
  }

  return hooks;
}

// API methods - Users
std::optional<User> GiteaAPI::get_user(const std::string& username) {
  try {
    std::string response = http_get("/users/" + username);
    if (response.empty()) {
      return std::nullopt;
    }
    return parse_user_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("get_user failed: ") + e.what());
    }
    return std::nullopt;
  }
}

bool GiteaAPI::create_user(const User& user, const std::string& password) {
  try {
    std::string body = user_to_json(user, password);
    std::string response = http_post("/admin/users", body);
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("create_user failed: ") + e.what());
    }
    return false;
  }
}

std::vector<User> GiteaAPI::list_users() {
  try {
    std::string response = http_get("/admin/users");
    if (response.empty()) {
      return {};
    }
    return parse_users_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("list_users failed: ") + e.what());
    }
    return {};
  }
}

// API methods - Organizations
bool GiteaAPI::create_org(const Organization& org) {
  try {
    std::string body = org_to_json(org);
    std::string response = http_post("/orgs", body);
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("create_org failed: ") + e.what());
    }
    return false;
  }
}

std::vector<Organization> GiteaAPI::list_orgs() {
  try {
    std::string response = http_get("/user/orgs");
    if (response.empty()) {
      return {};
    }
    return parse_orgs_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("list_orgs failed: ") + e.what());
    }
    return {};
  }
}

std::optional<Organization> GiteaAPI::get_org(const std::string& orgname) {
  try {
    std::string response = http_get("/orgs/" + orgname);
    if (response.empty()) {
      return std::nullopt;
    }
    return parse_org_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("get_org failed: ") + e.what());
    }
    return std::nullopt;
  }
}

// API methods - Repositories
bool GiteaAPI::create_repo(const std::string& owner, const Repository& repo) {
  try {
    std::string body = repo_to_json(repo);
    std::string endpoint;

    if (owner.empty() || owner == "~" || owner == "admin") {
      endpoint = "/user/repos";
    } else {
      endpoint = "/org/" + owner + "/repos";
    }

    std::string response = http_post(endpoint, body);
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("create_repo failed: ") + e.what());
    }
    return false;
  }
}

bool GiteaAPI::delete_repo(const std::string& owner, const std::string& repo) {
  try {
    std::string endpoint = "/repos/" + owner + "/" + repo;
    std::string response = http_delete(endpoint);
    return true;  // DELETE returns empty on success
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("delete_repo failed: ") + e.what());
    }
    return false;
  }
}

std::optional<Repository> GiteaAPI::get_repo(const std::string& owner,
                                             const std::string& repo) {
  try {
    std::string response = http_get("/repos/" + owner + "/" + repo);
    if (response.empty()) {
      return std::nullopt;
    }
    return parse_repo_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("get_repo failed: ") + e.what());
    }
    return std::nullopt;
  }
}

std::vector<Repository> GiteaAPI::list_repos(const std::string& owner) {
  try {
    std::string endpoint;
    if (owner.empty()) {
      endpoint = "/user/repos";
    } else {
      // Check if it's an org or user
      auto org = get_org(owner);
      if (org) {
        endpoint = "/orgs/" + owner + "/repos";
      } else {
        endpoint = "/users/" + owner + "/repos";
      }
    }

    std::string response = http_get(endpoint);
    if (response.empty()) {
      return {};
    }
    return parse_repos_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("list_repos failed: ") + e.what());
    }
    return {};
  }
}

// API methods - Webhooks
bool GiteaAPI::create_webhook(const std::string& owner,
                              const std::string& repo,
                              const Webhook& hook) {
  try {
    std::string body = webhook_to_json(hook);
    std::string endpoint = "/repos/" + owner + "/" + repo + "/hooks";
    std::string response = http_post(endpoint, body);
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("create_webhook failed: ") + e.what());
    }
    return false;
  }
}

std::vector<Webhook> GiteaAPI::list_webhooks(const std::string& owner,
                                             const std::string& repo) {
  try {
    std::string endpoint = "/repos/" + owner + "/" + repo + "/hooks";
    std::string response = http_get(endpoint);
    if (response.empty()) {
      return {};
    }
    return parse_webhooks_json(response);
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("list_webhooks failed: ") + e.what());
    }
    return {};
  }
}

bool GiteaAPI::delete_webhook(const std::string& owner,
                              const std::string& repo,
                              int64_t hook_id) {
  try {
    std::string endpoint = "/repos/" + owner + "/" + repo + "/hooks/" + std::to_string(hook_id);
    http_delete(endpoint);
    return true;
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("delete_webhook failed: ") + e.what());
    }
    return false;
  }
}

// API methods - Mirroring
bool GiteaAPI::mirror_repo(const std::string& remote_url,
                           const std::string& name,
                           bool is_private) {
  try {
    std::ostringstream json;
    json << "{";
    json << "\"clone_addr\":\"" << escape_json(remote_url) << "\"";
    json << ",\"repo_name\":\"" << escape_json(name) << "\"";
    json << ",\"mirror\":true";
    json << ",\"private\":" << (is_private ? "true" : "false");
    json << "}";

    std::string response = http_post("/repos/migrate", json.str());
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("mirror_repo failed: ") + e.what());
    }
    return false;
  }
}

// API methods - SSH Keys
bool GiteaAPI::add_ssh_key(const std::string& title, const std::string& key) {
  try {
    std::ostringstream json;
    json << "{";
    json << "\"title\":\"" << escape_json(title) << "\"";
    json << ",\"key\":\"" << escape_json(key) << "\"";
    json << "}";

    std::string response = http_post("/user/keys", json.str());
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("add_ssh_key failed: ") + e.what());
    }
    return false;
  }
}

std::vector<GiteaAPI::SSHKey> GiteaAPI::list_ssh_keys() {
  try {
    std::string response = http_get("/user/keys");
    if (response.empty()) {
      return {};
    }

    // Parse SSH keys from response
    std::vector<SSHKey> keys;
    size_t pos = 0;
    while ((pos = response.find("{", pos)) != std::string::npos) {
      int depth = 1;
      size_t search_pos = pos + 1;
      while (depth > 0 && search_pos < response.length()) {
        if (response[search_pos] == '{') {
          depth++;
        } else if (response[search_pos] == '}') {
          depth--;
        }
        search_pos++;
      }

      if (depth == 0) {
        std::string obj = response.substr(pos, search_pos - pos);

        SSHKey key;
        auto id = extract_json_int(obj, "id");
        if (id) key.id = *id;

        auto title = extract_json_string(obj, "title");
        if (title) key.title = *title;

        auto key_str = extract_json_string(obj, "key");
        if (key_str) key.key = *key_str;

        auto fingerprint = extract_json_string(obj, "fingerprint");
        if (fingerprint) key.fingerprint = *fingerprint;

        keys.push_back(key);
        pos = search_pos;
      } else {
        break;
      }
    }

    return keys;
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("list_ssh_keys failed: ") + e.what());
    }
    return {};
  }
}

bool GiteaAPI::delete_ssh_key(int64_t key_id) {
  try {
    std::string endpoint = "/user/keys/" + std::to_string(key_id);
    http_delete(endpoint);
    return true;
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("delete_ssh_key failed: ") + e.what());
    }
    return false;
  }
}

// Health check
bool GiteaAPI::ping() {
  try {
    std::string response = http_get("/version");
    return !response.empty();
  } catch (const std::exception& e) {
    if (log_) {
      log_->error("Git/gitea-api", std::string("ping failed: ") + e.what());
    }
    return false;
  }
}

} // namespace nazg::git
