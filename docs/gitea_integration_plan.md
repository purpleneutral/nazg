# Gitea Integration Plan

**Version:** 1.0
**Created:** 2025-10-06
**Priority:** Next Major Milestone
**Status:** Planning Phase

---

## Executive Summary

Automate the installation and management of a Gitea server for self-hosted git repository management. This builds on Nazg's existing `git::Server` infrastructure (currently supporting cgit) to provide a modern, full-featured Git hosting platform similar to GitHub but self-hosted.

**Goal**: One command to install Gitea on a remote machine and manage all your repositories from Nazg.

---

## Table of Contents

1. [Why Gitea?](#1-why-gitea)
2. [Current Infrastructure](#2-current-infrastructure)
3. [Architecture Overview](#3-architecture-overview)
4. [Implementation Phases](#4-implementation-phases)
5. [API Integration](#5-api-integration)
6. [Database Schema](#6-database-schema)
7. [Security Considerations](#7-security-considerations)
8. [Testing Strategy](#8-testing-strategy)

---

## 1. Why Gitea?

### Advantages Over cgit

| Feature | cgit | Gitea |
|---------|------|-------|
| **Web UI** | Read-only browser | Full featured (browse, edit, PR, issues) |
| **Repository Management** | Manual file sync | API-driven (create, delete, transfer) |
| **Users/Organizations** | None | Full user system with teams |
| **Pull Requests** | ❌ | ✅ |
| **Issues/Wikis** | ❌ | ✅ |
| **CI/CD** | ❌ | Gitea Actions (GitHub Actions compatible) |
| **Webhooks** | ❌ | ✅ |
| **API** | None | RESTful API |
| **Installation** | Package manager | Single binary or Docker |
| **Database** | None | SQLite (default), MySQL, PostgreSQL |

### Use Cases

1. **Self-hosted GitHub alternative** - Full control, no cloud dependency
2. **Private organization repos** - Teams, permissions, code review
3. **CI/CD integration** - Gitea Actions for automated testing/deployment
4. **Issue tracking** - Built-in issue management
5. **Wiki documentation** - Per-repo wikis
6. **Mirror external repos** - Mirror GitHub/GitLab repos locally

---

## 2. Current Infrastructure

### Existing Components

**`git::Server` Interface** (`modules/git/include/git/server.hpp`):
```cpp
class Server {
  virtual bool is_installed() = 0;
  virtual bool install() = 0;
  virtual bool configure() = 0;
  virtual bool sync_repos(const std::vector<std::string>& local_paths) = 0;
  virtual ServerStatus get_status() = 0;
};
```

**`CgitServer` Implementation** (`modules/git/src/cgit.cpp`):
- SSH helpers: `ssh_exec()`, `ssh_test_connection()`, `upload_file()`
- Installation: Detect package manager, install deps, configure nginx
- Config generation: Generate cgitrc, upload via SCP
- Repo syncing: rsync bare repos over SSH

**CLI Commands**:
- `nazg git-server-add <type> <host>` - Register server
- `nazg git-server-install` - Install server software
- `nazg git-server-sync` - Sync local bare repos
- `nazg git-server-status` - Check server status

### What's Missing for Gitea

1. **Gitea binary management** - Download, install, systemd service
2. **app.ini configuration** - Database, HTTP server, SSH settings
3. **API client** - Create repos, manage users, webhooks
4. **User/organization management** - Via API
5. **Token management** - API authentication
6. **Repository creation via API** - Not just sync
7. **Pull request workflow** - Create, review, merge via API

---

## 3. Architecture Overview

### 3.1 New Components

```
modules/git/
├── include/git/
│   ├── server.hpp          # EXISTING - Base interface
│   ├── cgit.hpp            # EXISTING - cgit implementation
│   ├── gitea.hpp           # NEW - Gitea implementation
│   └── gitea_api.hpp       # NEW - Gitea API client
├── src/
│   ├── server.cpp          # EXISTING - Factory
│   ├── cgit.cpp            # EXISTING - cgit implementation
│   ├── gitea.cpp           # NEW - Gitea server management
│   └── gitea_api.cpp       # NEW - API client implementation
```

### 3.2 Gitea Server Class

```cpp
class GiteaServer : public Server {
public:
  GiteaServer(const ServerConfig& cfg, Store* store, logger* log);

  // Server interface
  bool is_installed() override;
  bool install() override;
  bool configure() override;
  bool sync_repos(const std::vector<std::string>& local_paths) override;
  ServerStatus get_status() override;

  // Gitea-specific methods
  bool create_user(const std::string& username, const std::string& email,
                   const std::string& password);
  bool create_organization(const std::string& name);
  bool create_repo(const std::string& name, const std::string& owner,
                   bool is_private = true);
  bool delete_repo(const std::string& owner, const std::string& repo);
  bool setup_webhook(const std::string& owner, const std::string& repo,
                     const std::string& url);

  // API client access
  GiteaAPI* api() { return api_.get(); }

private:
  std::unique_ptr<GiteaAPI> api_;

  // Installation helpers
  bool download_gitea_binary();
  bool create_gitea_user();
  bool generate_app_ini();
  bool setup_systemd_service();
  bool initialize_database();

  // SSH helpers (inherited pattern from cgit)
  bool ssh_exec(const std::string& cmd, std::string* output = nullptr);
  bool ssh_test_connection();
  bool upload_file(const std::string& local, const std::string& remote);
};
```

### 3.3 Gitea API Client

```cpp
class GiteaAPI {
public:
  GiteaAPI(const std::string& base_url, const std::string& token);

  // User management
  struct User {
    int64_t id;
    std::string username;
    std::string email;
    bool is_admin;
  };

  std::optional<User> get_user(const std::string& username);
  bool create_user(const User& user, const std::string& password);
  std::vector<User> list_users();

  // Organization management
  struct Organization {
    int64_t id;
    std::string username;
    std::string full_name;
    std::string description;
  };

  bool create_org(const Organization& org);
  std::vector<Organization> list_orgs();

  // Repository management
  struct Repository {
    int64_t id;
    std::string name;
    std::string full_name;  // owner/repo
    std::string description;
    bool is_private;
    std::string clone_url;
    std::string ssh_url;
  };

  bool create_repo(const std::string& owner, const Repository& repo);
  bool delete_repo(const std::string& owner, const std::string& repo);
  std::optional<Repository> get_repo(const std::string& owner,
                                     const std::string& repo);
  std::vector<Repository> list_repos(const std::string& owner);

  // Webhook management
  struct Webhook {
    int64_t id;
    std::string url;
    std::string content_type;  // json or form
    std::vector<std::string> events;  // push, pull_request, etc.
    bool active;
  };

  bool create_webhook(const std::string& owner, const std::string& repo,
                     const Webhook& hook);
  std::vector<Webhook> list_webhooks(const std::string& owner,
                                     const std::string& repo);

  // Mirror management
  bool mirror_repo(const std::string& remote_url, const std::string& name,
                   bool is_private = true);

private:
  std::string base_url_;
  std::string token_;

  // HTTP helpers
  std::string http_get(const std::string& endpoint);
  std::string http_post(const std::string& endpoint, const std::string& body);
  std::string http_delete(const std::string& endpoint);
  std::string http_patch(const std::string& endpoint, const std::string& body);

  // JSON parsing (use simple parser or nlohmann/json if available)
  std::string to_json(const Repository& repo);
  Repository parse_repo_json(const std::string& json);
};
```

---

## 4. Implementation Phases

### Phase 1: Basic Installation & Setup (Week 1)

**Goal**: Automate Gitea installation on remote machine

#### Tasks

- [x] Research - Understand Gitea installation methods
  - Binary installation (single executable)
  - Docker installation (alternative)
  - Configuration file structure (app.ini)
  - Systemd service setup

- [ ] Create `modules/git/include/git/gitea.hpp`
  - [ ] Define `GiteaServer` class
  - [ ] Define installation constants (download URL, paths)

- [ ] Implement `modules/git/src/gitea.cpp`
  - [ ] Constructor - Initialize config
  - [ ] `ssh_test_connection()` - Reuse pattern from cgit
  - [ ] `ssh_exec()` - SSH command execution
  - [ ] `upload_file()` - SCP file upload
  - [ ] `is_installed()` - Check if Gitea binary exists
  - [ ] `download_gitea_binary()` - Download from GitHub releases
  - [ ] `create_gitea_user()` - Create system user for Gitea
  - [ ] `generate_app_ini()` - Generate Gitea config
  - [ ] `setup_systemd_service()` - Create and enable service
  - [ ] `initialize_database()` - Run Gitea migrate
  - [ ] `install()` - Orchestrate full installation
  - [ ] `get_status()` - Check if Gitea is running

- [ ] Update `modules/git/src/server.cpp` factory
  - [ ] Add Gitea case to `create_server()`

- [ ] Update CLI commands
  - [ ] Modify `cmd_git_server_add()` to support "gitea" type
  - [ ] Modify `cmd_git_server_install()` for Gitea installation flow
  - [ ] Modify `cmd_git_server_status()` to query Gitea API

- [ ] Testing
  - [ ] Manual test: Install on test VM
  - [ ] Verify Gitea web UI accessible
  - [ ] Verify systemd service running

**Deliverable**: `nazg git-server-add gitea 10.0.0.5` and `nazg git-server-install` successfully installs Gitea

---

### Phase 2: Repository Management (Week 2)

**Goal**: Create and sync repositories via Gitea API

#### Tasks

- [ ] Create `modules/git/include/git/gitea_api.hpp`
  - [ ] Define `GiteaAPI` class
  - [ ] Define API structs (Repository, User, Organization)
  - [ ] Define HTTP method signatures

- [ ] Implement `modules/git/src/gitea_api.cpp`
  - [ ] HTTP helpers using libcurl
    - [ ] `http_get()` - GET request with auth header
    - [ ] `http_post()` - POST request with JSON body
    - [ ] `http_delete()` - DELETE request
    - [ ] `http_patch()` - PATCH request
  - [ ] JSON helpers (manual or library)
    - [ ] `to_json()` for Repository, User, etc.
    - [ ] `parse_json()` for responses
  - [ ] Repository API methods
    - [ ] `create_repo()` - POST /api/v1/user/repos
    - [ ] `delete_repo()` - DELETE /api/v1/repos/{owner}/{repo}
    - [ ] `get_repo()` - GET /api/v1/repos/{owner}/{repo}
    - [ ] `list_repos()` - GET /api/v1/user/repos

- [ ] Update `GiteaServer` class
  - [ ] Add `api_` member (GiteaAPI instance)
  - [ ] Implement `create_repo()`
  - [ ] Implement `sync_repos()` - Create repo via API, then push
  - [ ] Implement `configure()` - Generate admin token

- [ ] Database schema
  - [ ] Add `api_token` to `git_platforms` table
  - [ ] Store Gitea-specific settings (admin credentials)

- [ ] Update CLI commands
  - [ ] Add `cmd_git_repo_create()` - Create repo on Gitea
  - [ ] Update `cmd_git_server_sync()` - Create via API, then push
  - [ ] Add `cmd_git_repo_list()` - List repos on Gitea

- [ ] Testing
  - [ ] Test repo creation via API
  - [ ] Test repo listing
  - [ ] Test sync workflow (create + push)

**Deliverable**: `nazg git-repo-create myproject` creates repo on Gitea and pushes local changes

---

### Phase 3: User & Organization Management (Week 3)

**Goal**: Manage users and organizations via API

#### Tasks

- [ ] Extend `GiteaAPI` class
  - [ ] `create_user()` - POST /api/v1/admin/users
  - [ ] `get_user()` - GET /api/v1/users/{username}
  - [ ] `list_users()` - GET /api/v1/admin/users
  - [ ] `create_org()` - POST /api/v1/orgs
  - [ ] `list_orgs()` - GET /api/v1/user/orgs

- [ ] Add CLI commands
  - [ ] `cmd_git_user_create()` - Create Gitea user
  - [ ] `cmd_git_user_list()` - List users
  - [ ] `cmd_git_org_create()` - Create organization
  - [ ] `cmd_git_org_list()` - List organizations

- [ ] Interactive workflows
  - [ ] User creation wizard (prompt for username, email, password)
  - [ ] Org creation wizard (name, description)

- [ ] Testing
  - [ ] Create test users
  - [ ] Create test organization
  - [ ] Verify via Gitea web UI

**Deliverable**: `nazg git-user-create alice` creates user, `nazg git-org-create myteam` creates org

---

### Phase 4: Advanced Features (Week 4)

**Goal**: Webhooks, mirroring, SSH key management

#### Tasks

- [ ] Webhook Management
  - [ ] Extend `GiteaAPI` with webhook methods
  - [ ] `create_webhook()` - POST /api/v1/repos/{owner}/{repo}/hooks
  - [ ] `list_webhooks()` - GET webhooks
  - [ ] Add `cmd_git_webhook_add()` command

- [ ] Repository Mirroring
  - [ ] `mirror_repo()` - Mirror external repo to Gitea
  - [ ] Add `cmd_git_mirror()` command
  - [ ] Schedule periodic sync (via Gitea cron)

- [ ] SSH Key Management
  - [ ] `add_ssh_key()` - POST /api/v1/user/keys
  - [ ] `list_ssh_keys()` - GET user keys
  - [ ] Auto-upload local SSH key to Gitea

- [ ] Gitea Actions (CI/CD)
  - [ ] Generate `.gitea/workflows/test.yml` for projects
  - [ ] Integrate with test runner module

- [ ] Advanced CLI
  - [ ] `nazg git-mirror https://github.com/owner/repo` - Mirror repo
  - [ ] `nazg git-webhook-add https://ci.example.com/hook` - Add webhook
  - [ ] `nazg git-actions-setup` - Generate workflow files

**Deliverable**: Full-featured Gitea management from Nazg CLI

---

## 5. API Integration

### 5.1 Gitea API Reference

**Base URL**: `http://GITEA_HOST:3000/api/v1`

**Authentication**: Bearer token in `Authorization` header

### 5.2 Key Endpoints

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/api/v1/admin/users` | Create user (admin only) |
| GET | `/api/v1/users/{username}` | Get user info |
| POST | `/api/v1/user/repos` | Create repo for authenticated user |
| POST | `/api/v1/org/{org}/repos` | Create repo in organization |
| GET | `/api/v1/repos/{owner}/{repo}` | Get repo info |
| DELETE | `/api/v1/repos/{owner}/{repo}` | Delete repo |
| POST | `/api/v1/repos/{owner}/{repo}/hooks` | Create webhook |
| POST | `/api/v1/orgs` | Create organization |
| POST | `/api/v1/user/keys` | Add SSH key |
| POST | `/api/v1/repos/migrate` | Mirror external repo |

### 5.3 Example: Create Repository

**Request**:
```bash
curl -X POST "http://gitea.local:3000/api/v1/user/repos" \
  -H "Authorization: token abc123..." \
  -H "Content-Type: application/json" \
  -d '{
    "name": "myproject",
    "description": "My awesome project",
    "private": true,
    "auto_init": false
  }'
```

**Response**:
```json
{
  "id": 1,
  "name": "myproject",
  "full_name": "alice/myproject",
  "private": true,
  "clone_url": "http://gitea.local:3000/alice/myproject.git",
  "ssh_url": "git@gitea.local:alice/myproject.git"
}
```

### 5.4 Implementation Pattern

```cpp
bool GiteaAPI::create_repo(const std::string& owner, const Repository& repo) {
  // Build JSON body
  std::ostringstream json;
  json << "{";
  json << "\"name\":\"" << repo.name << "\",";
  json << "\"description\":\"" << repo.description << "\",";
  json << "\"private\":" << (repo.is_private ? "true" : "false") << ",";
  json << "\"auto_init\":false";
  json << "}";

  // Determine endpoint (user or org)
  std::string endpoint;
  if (owner.empty() || owner == "~") {
    endpoint = "/api/v1/user/repos";
  } else {
    endpoint = "/api/v1/org/" + owner + "/repos";
  }

  // Make request
  std::string response = http_post(endpoint, json.str());

  // Parse response to verify success
  return !response.empty() && response.find("\"id\"") != std::string::npos;
}
```

---

## 6. Database Schema

### 6.1 Update Migration 007

```sql
-- Add Gitea-specific columns to git_platforms table
ALTER TABLE git_platforms ADD COLUMN admin_username TEXT;
ALTER TABLE git_platforms ADD COLUMN admin_email TEXT;
ALTER TABLE git_platforms ADD COLUMN http_port INTEGER DEFAULT 3000;
ALTER TABLE git_platforms ADD COLUMN ssh_port INTEGER DEFAULT 22;
ALTER TABLE git_platforms ADD COLUMN database_type TEXT DEFAULT 'sqlite3';
ALTER TABLE git_platforms ADD COLUMN secret_key TEXT;  -- For app.ini

-- Table for tracking Gitea users (if we create them via API)
CREATE TABLE gitea_users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform_id INTEGER NOT NULL,
  gitea_user_id INTEGER,
  username TEXT NOT NULL,
  email TEXT,
  is_admin INTEGER DEFAULT 0,
  created_at INTEGER,
  FOREIGN KEY (platform_id) REFERENCES git_platforms(id)
);

-- Table for tracking Gitea organizations
CREATE TABLE gitea_organizations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform_id INTEGER NOT NULL,
  gitea_org_id INTEGER,
  name TEXT NOT NULL,
  full_name TEXT,
  description TEXT,
  created_at INTEGER,
  FOREIGN KEY (platform_id) REFERENCES git_platforms(id)
);

-- Extend git_pull_requests to support Gitea PRs
ALTER TABLE git_pull_requests ADD COLUMN pr_type TEXT DEFAULT 'github';
```

---

## 7. Security Considerations

### 7.1 API Token Storage

**Problem**: Gitea API tokens must be stored securely

**Solution**:
```cpp
// Encrypt token before storing in database
std::string encrypt_token(const std::string& token, const std::string& key);
std::string decrypt_token(const std::string& encrypted, const std::string& key);

// Or use environment variable
std::string token = std::getenv("GITEA_TOKEN");
```

### 7.2 SSH Connection Security

**Current cgit approach**: Password-less SSH with key authentication

**Requirements**:
1. User must have SSH key on remote machine: `ssh-copy-id git@gitea-host`
2. Nazg never handles passwords directly
3. All sudo commands use NOPASSWD for specific commands in sudoers

**Sudoers config on remote**:
```
git ALL=(ALL) NOPASSWD: /usr/bin/systemctl start gitea
git ALL=(ALL) NOPASSWD: /usr/bin/systemctl stop gitea
git ALL=(ALL) NOPASSWD: /usr/bin/systemctl restart gitea
git ALL=(ALL) NOPASSWD: /usr/bin/mv /tmp/app.ini /etc/gitea/app.ini
```

### 7.3 Admin Token Generation

**During installation**, generate admin token:
```bash
# On Gitea server, generate token for admin user
gitea admin user generate-access-token \
  --username admin \
  --token-name nazg-management \
  --scopes write:admin,write:repository,write:organization
```

Store token in nexus database (encrypted).

---

## 8. Testing Strategy

### 8.1 Test Environment

**Requirements**:
- Test VM or machine for Gitea installation
- SSH access with key authentication
- Sudo privileges for installation

**Recommended**:
- VirtualBox/VMware VM running Ubuntu/Debian
- Or use the machine you already have ready

### 8.2 Test Phases

#### Phase 1 Tests (Installation)

```bash
# Test server registration
$ nazg git-server-add gitea 10.0.0.5 --ssh-user director

# Test status check (before install)
$ nazg git-server-status
Type: gitea
Host: 10.0.0.5
Reachable: yes
Installed: no

# Test installation
$ nazg git-server-install
Installing Gitea on 10.0.0.5...
[✓] Downloaded Gitea binary
[✓] Created gitea system user
[✓] Generated configuration (app.ini)
[✓] Created systemd service
[✓] Initialized database
[✓] Started Gitea service
Web UI: http://10.0.0.5:3000

# Test status after install
$ nazg git-server-status
Type: gitea
Host: 10.0.0.5
Reachable: yes
Installed: yes
Version: Gitea 1.21.0
Repos: 0
Web UI: http://10.0.0.5:3000
```

#### Phase 2 Tests (Repository Management)

```bash
# Test repo creation
$ nazg git-repo-create myproject --description "Test project" --private
Creating repository on Gitea...
[✓] Repository created: alice/myproject
Clone URL: http://10.0.0.5:3000/alice/myproject.git
SSH URL: git@10.0.0.5:alice/myproject.git

# Test repo sync (push local repo to Gitea)
$ cd ~/projects/myapp
$ nazg git-server-sync
[✓] Created remote repository: myapp
[✓] Configured remote 'origin'
[✓] Pushed to origin/main

# Test repo listing
$ nazg git-repo-list
alice/myproject - Test project
alice/myapp - My application
```

#### Phase 3 Tests (Users/Organizations)

```bash
# Create user
$ nazg git-user-create bob --email bob@example.com
Password: ********
[✓] User created: bob

# Create organization
$ nazg git-org-create myteam --description "My Team"
[✓] Organization created: myteam

# Create repo in organization
$ nazg git-repo-create backend --owner myteam
[✓] Repository created: myteam/backend
```

---

## 9. Configuration

### 9.1 Gitea app.ini Template

Generated by `GiteaServer::generate_app_ini()`:

```ini
APP_NAME = Nazg Git Server
RUN_MODE = prod

[server]
PROTOCOL = http
DOMAIN = {{ GITEA_HOST }}
HTTP_PORT = 3000
ROOT_URL = http://{{ GITEA_HOST }}:3000/
DISABLE_SSH = false
SSH_PORT = 22
START_SSH_SERVER = true
LFS_START_SERVER = true

[database]
DB_TYPE = sqlite3
PATH = /var/lib/gitea/data/gitea.db

[repository]
ROOT = /var/lib/gitea/repositories

[security]
INSTALL_LOCK = true
SECRET_KEY = {{ GENERATED_SECRET }}
INTERNAL_TOKEN = {{ GENERATED_TOKEN }}

[service]
DISABLE_REGISTRATION = true
REQUIRE_SIGNIN_VIEW = false
DEFAULT_KEEP_EMAIL_PRIVATE = true

[log]
MODE = file
LEVEL = Info
ROOT_PATH = /var/lib/gitea/log

[actions]
ENABLED = true
```

### 9.2 Systemd Service Template

```ini
[Unit]
Description=Gitea (Git with a cup of tea)
After=network.target

[Service]
Type=simple
User=git
Group=git
WorkingDirectory=/var/lib/gitea
ExecStart=/usr/local/bin/gitea web --config /etc/gitea/app.ini
Restart=always
Environment=USER=git HOME=/home/git GITEA_WORK_DIR=/var/lib/gitea

[Install]
WantedBy=multi-user.target
```

---

## 10. Command Reference (After Implementation)

### Server Management

```bash
# Register Gitea server
nazg git-server-add gitea <host> [--ssh-user USER] [--ssh-port PORT]

# Install Gitea on remote server
nazg git-server-install [--version VERSION] [--http-port 3000]

# Check server status
nazg git-server-status

# Uninstall Gitea (future)
nazg git-server-uninstall
```

### Repository Management

```bash
# Create repository
nazg git-repo-create <name> [--owner ORG] [--description DESC] [--private]

# List repositories
nazg git-repo-list [--owner USER_OR_ORG]

# Delete repository
nazg git-repo-delete <owner>/<repo>

# Sync local repo to Gitea
nazg git-server-sync [--force]

# Clone from Gitea
nazg git-repo-clone <owner>/<repo> [path]
```

### User & Organization Management

```bash
# Create user
nazg git-user-create <username> --email <email> [--admin]

# List users
nazg git-user-list

# Create organization
nazg git-org-create <name> [--description DESC]

# List organizations
nazg git-org-list
```

### Advanced Features

```bash
# Add webhook
nazg git-webhook-add <owner>/<repo> <url> [--events push,pull_request]

# Mirror external repository
nazg git-mirror <remote-url> [--name NAME] [--private]

# Add SSH key
nazg git-key-add [--title TITLE] [path/to/key.pub]

# Setup Gitea Actions
nazg git-actions-setup
```

---

## 11. Implementation Timeline

| Week | Phase | Deliverable |
|------|-------|-------------|
| 1 | Phase 1 | Gitea installation automated |
| 2 | Phase 2 | Repository creation and sync via API |
| 3 | Phase 3 | User and organization management |
| 4 | Phase 4 | Webhooks, mirroring, advanced features |

**Total**: 4 weeks to full-featured Gitea integration

---

## 12. Dependencies

### Required Packages

**On local machine (where Nazg runs)**:
- libcurl (for HTTP API calls)
- OpenSSL (for token encryption)

**On remote server (Gitea host)**:
- None initially (Nazg will install everything)
- After installation: Gitea binary, SQLite (or MySQL/PostgreSQL if preferred)

### Installation

```bash
# Local machine (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev libssl-dev

# Local machine (Arch)
sudo pacman -S curl openssl
```

---

## 13. Next Steps

1. **Review this plan** - Confirm approach and priorities
2. **Prepare test machine** - Ensure SSH access and sudo privileges
3. **Begin Phase 1** - Create gitea.hpp and start implementation
4. **Test incrementally** - Verify each phase before moving to next

---

## 14. Future Enhancements (Post-Initial Implementation)

1. **Gitea Actions Integration** - Auto-generate CI/CD workflows
2. **Pull Request Management** - Create, review, merge PRs via CLI
3. **Issue Management** - Create/update issues from CLI
4. **Wiki Management** - Edit repo wikis from CLI
5. **Statistics & Insights** - Commit graphs, contributor stats
6. **Backup/Restore** - Automated Gitea backup to local/remote
7. **Multi-server Support** - Manage multiple Gitea instances
8. **Docker Installation Option** - Alternative to binary install

---

**Document Status**: Draft v1.0 - Ready for Implementation
**Last Updated**: 2025-10-06
**Next Action**: Begin Phase 1 - Create gitea.hpp and gitea_api.hpp
