# Gitea Integration Checklist

**Plan Document**: [gitea_integration_plan.md](gitea_integration_plan.md)
**Priority**: Next Major Milestone
**Status**: Planning → Implementation
**Last Updated**: 2025-10-06

---

## Quick Command Reference (Post-Implementation)

```bash
# Server setup
nazg git-server-add gitea 10.0.0.5        # Register Gitea server
nazg git-server-install                    # Install Gitea remotely
nazg git-server-status                     # Check installation status

# Repository management
nazg git-repo-create myproject             # Create repo on Gitea
nazg git-server-sync                       # Push local repo to Gitea
nazg git-repo-list                         # List all repos

# User/org management
nazg git-user-create alice                 # Create Gitea user
nazg git-org-create myteam                 # Create organization

# Advanced
nazg git-webhook-add owner/repo <url>      # Add webhook
nazg git-mirror https://github.com/...     # Mirror external repo
```

---

## Phase 1: Basic Installation & Setup (Week 1)

**Goal**: Automate Gitea installation on remote machine

### 1.1 Research & Planning

- [x] Study Gitea installation methods
  - Binary installation (recommended)
  - Configuration structure (app.ini)
  - Systemd service setup
- [x] Analyze existing cgit pattern in codebase
- [x] Plan SSH execution pattern reuse
- [ ] Set up test machine/VM for development

### 1.2 Header Files

- [ ] Create `modules/git/include/git/gitea.hpp`
  - [ ] Include guard and namespace
  - [ ] Forward declarations (Store, logger)
  - [ ] Define `GiteaServer` class inheriting from `Server`
  - [ ] Declare public interface methods:
    - [ ] `is_installed()` override
    - [ ] `install()` override
    - [ ] `configure()` override
    - [ ] `sync_repos()` override
    - [ ] `get_status()` override
    - [ ] `config()` const override
  - [ ] Declare Gitea-specific methods:
    - [ ] `create_admin_token()` - Generate API token
    - [ ] `get_api_url()` - Return API base URL
  - [ ] Declare private helper methods:
    - [ ] `ssh_exec()` - Execute SSH command
    - [ ] `ssh_test_connection()` - Test connectivity
    - [ ] `upload_file()` - SCP file upload
    - [ ] `download_gitea_binary()` - Get binary from GitHub
    - [ ] `create_gitea_user()` - Create system user
    - [ ] `generate_app_ini()` - Generate config file
    - [ ] `generate_secret_key()` - Generate random secret
    - [ ] `setup_systemd_service()` - Create service
    - [ ] `initialize_database()` - Run migrations
  - [ ] Declare member variables:
    - [ ] `config_` - ServerConfig
    - [ ] `store_` - nexus::Store pointer
    - [ ] `log_` - blackbox::logger pointer
    - [ ] `api_token_` - Admin API token (optional)

### 1.3 Implementation

- [ ] Implement `modules/git/src/gitea.cpp`
  - [ ] Include necessary headers
  - [ ] Constructor implementation
    - [ ] Set default paths (/etc/gitea, /var/lib/gitea)
    - [ ] Set default ports (HTTP: 3000, SSH: 22)
  - [ ] SSH helper methods (reuse cgit pattern)
    - [ ] `ssh_test_connection()` - Test SSH with timeout
    - [ ] `ssh_exec()` - Execute command, optionally capture output
    - [ ] `upload_file()` - SCP file from local to remote
  - [ ] Installation helper methods
    - [ ] `download_gitea_binary()`
      ```cpp
      // Detect architecture (x86_64, aarch64)
      // Download from GitHub releases
      // Example: https://github.com/go-gitea/gitea/releases/download/v1.21.0/gitea-1.21.0-linux-amd64
      ```
    - [ ] `create_gitea_user()`
      ```cpp
      // Create system user: sudo useradd -r -m -d /var/lib/gitea -s /bin/bash git
      // Create directories: /var/lib/gitea/{data,repositories,log}
      // Set permissions
      ```
    - [ ] `generate_secret_key()`
      ```cpp
      // Generate random 64-char hex string
      // Use OpenSSL: RAND_bytes()
      ```
    - [ ] `generate_app_ini()`
      ```cpp
      // Generate app.ini from template
      // Replace {{ GITEA_HOST }}, {{ SECRET_KEY }}, etc.
      // Return as string
      ```
    - [ ] `setup_systemd_service()`
      ```cpp
      // Generate gitea.service file
      // Upload to /etc/systemd/system/gitea.service
      // sudo systemctl daemon-reload
      // sudo systemctl enable gitea
      ```
    - [ ] `initialize_database()`
      ```cpp
      // Run: gitea migrate
      // Create admin user: gitea admin user create --admin --username admin --password ... --email admin@local
      ```
  - [ ] Public interface implementation
    - [ ] `is_installed()`
      ```cpp
      // Check if /usr/local/bin/gitea exists
      // Check if gitea service is active
      ```
    - [ ] `install()`
      ```cpp
      // Orchestrate installation:
      // 1. ssh_test_connection()
      // 2. download_gitea_binary()
      // 3. create_gitea_user()
      // 4. generate_app_ini() and upload
      // 5. setup_systemd_service()
      // 6. initialize_database()
      // 7. create_admin_token()
      // Log each step, return false on failure
      ```
    - [ ] `get_status()`
      ```cpp
      // Return ServerStatus with:
      // - reachable (SSH test)
      // - installed (binary exists)
      // - version (gitea --version)
      // - repo_count (query API if available)
      ```
    - [ ] `configure()`
      ```cpp
      // Regenerate app.ini
      // Restart gitea service
      ```
    - [ ] `sync_repos()` (Phase 1: stub, Phase 2: full implementation)
      ```cpp
      // For now, just log and return true
      // Will implement with API in Phase 2
      ```

### 1.4 Factory & Registration

- [ ] Update `modules/git/src/server.cpp`
  - [ ] Add `#include "git/gitea.hpp"`
  - [ ] Update `create_server()` factory:
    ```cpp
    if (cfg.type == "cgit") {
      return std::make_unique<CgitServer>(cfg, store, log);
    } else if (cfg.type == "gitea") {
      return std::make_unique<GiteaServer>(cfg, store, log);
    }
    ```

### 1.5 CLI Command Updates

- [ ] Update `modules/git/src/commands.cpp`
  - [ ] Modify `cmd_git_server_add()`
    - [ ] Add "gitea" to type help text
    - [ ] Set appropriate defaults for Gitea (port 3000, ssh_port 22)
  - [ ] Modify `cmd_git_server_install()`
    - [ ] Support Gitea installation flow
    - [ ] Show Gitea-specific prompts (admin password, etc.)
  - [ ] Modify `cmd_git_server_status()`
    - [ ] Display Gitea-specific info (API URL, web UI)

### 1.6 CMakeLists & Dependencies

- [ ] Verify `CMakeLists.txt` has required dependencies
  - [ ] Check for libcurl linkage (already used?)
  - [ ] Check for OpenSSL linkage (for crypto module)
  - [ ] Add if missing:
    ```cmake
    find_package(CURL REQUIRED)
    target_link_libraries(git PUBLIC CURL::libcurl)
    ```

### 1.7 Testing Phase 1

- [ ] Prepare test environment
  - [ ] VM or physical machine with SSH access
  - [ ] SSH key authentication set up
  - [ ] Sudo access configured (or NOPASSWD sudoers)
- [ ] Manual testing
  - [ ] `nazg git-server-add gitea 10.0.0.5 --ssh-user myuser`
  - [ ] `nazg git-server-status` (before install, should show not installed)
  - [ ] `nazg git-server-install`
  - [ ] Verify Gitea web UI accessible at http://10.0.0.5:3000
  - [ ] Verify systemd service running: `ssh gitea-host systemctl status gitea`
  - [ ] `nazg git-server-status` (after install, should show installed)
- [ ] Document any issues encountered
- [ ] Fix bugs and re-test

**Phase 1 Status**: ⬜ Not Started

**Phase 1 Complete When**:
- ✅ `nazg git-server-install` successfully installs Gitea on remote machine
- ✅ Gitea web UI is accessible
- ✅ Systemd service is running
- ✅ `nazg git-server-status` shows correct information

---

## Phase 2: Repository Management (Week 2)

**Goal**: Create and manage repositories via Gitea API

### 2.1 API Client Header

- [ ] Create `modules/git/include/git/gitea_api.hpp`
  - [ ] Include guard and namespace
  - [ ] Define API structs:
    ```cpp
    struct User {
      int64_t id;
      std::string username;
      std::string email;
      bool is_admin;
    };

    struct Organization {
      int64_t id;
      std::string username;
      std::string full_name;
      std::string description;
    };

    struct Repository {
      int64_t id;
      std::string name;
      std::string full_name;  // owner/repo
      std::string description;
      bool is_private;
      std::string clone_url;
      std::string ssh_url;
    };

    struct Webhook {
      int64_t id;
      std::string url;
      std::string content_type;
      std::vector<std::string> events;
      bool active;
    };
    ```
  - [ ] Define `GiteaAPI` class:
    - [ ] Constructor: `GiteaAPI(base_url, token)`
    - [ ] Repository methods:
      - [ ] `create_repo(owner, repo)` → bool
      - [ ] `delete_repo(owner, repo)` → bool
      - [ ] `get_repo(owner, repo)` → optional<Repository>
      - [ ] `list_repos(owner)` → vector<Repository>
    - [ ] User methods (Phase 3):
      - [ ] `create_user(user, password)` → bool
      - [ ] `get_user(username)` → optional<User>
      - [ ] `list_users()` → vector<User>
    - [ ] Organization methods (Phase 3):
      - [ ] `create_org(org)` → bool
      - [ ] `list_orgs()` → vector<Organization>
    - [ ] Webhook methods (Phase 4):
      - [ ] `create_webhook(owner, repo, webhook)` → bool
      - [ ] `list_webhooks(owner, repo)` → vector<Webhook>
    - [ ] Private HTTP helpers:
      - [ ] `http_get(endpoint)` → string
      - [ ] `http_post(endpoint, body)` → string
      - [ ] `http_delete(endpoint)` → string
      - [ ] `http_patch(endpoint, body)` → string
    - [ ] Private JSON helpers:
      - [ ] `repo_to_json(repo)` → string
      - [ ] `parse_repo_json(json)` → Repository
      - [ ] `parse_repos_json(json)` → vector<Repository>

### 2.2 API Client Implementation

- [ ] Implement `modules/git/src/gitea_api.cpp`
  - [ ] Include libcurl headers
  - [ ] Constructor implementation
    - [ ] Store base_url and token
    - [ ] Validate URL format
  - [ ] HTTP helper implementations
    - [ ] `http_get(endpoint)`
      ```cpp
      // CURL setup
      // Set URL: base_url_ + endpoint
      // Set headers: Authorization: token <token>
      // Perform request
      // Return response body
      ```
    - [ ] `http_post(endpoint, body)`
      ```cpp
      // Similar to GET but:
      // Set POST method
      // Set body data
      // Set Content-Type: application/json
      ```
    - [ ] `http_delete(endpoint)`
    - [ ] `http_patch(endpoint, body)`
  - [ ] JSON helper implementations
    - [ ] `repo_to_json(repo)`
      ```cpp
      // Manual JSON construction (or use library if available)
      std::ostringstream json;
      json << "{";
      json << "\"name\":\"" << escape_json(repo.name) << "\",";
      json << "\"description\":\"" << escape_json(repo.description) << "\",";
      json << "\"private\":" << (repo.is_private ? "true" : "false");
      json << "}";
      return json.str();
      ```
    - [ ] `parse_repo_json(json)`
      ```cpp
      // Simple manual parsing or use JSON library
      // Extract: id, name, full_name, clone_url, ssh_url
      // Return Repository struct
      ```
  - [ ] Repository API implementations
    - [ ] `create_repo(owner, repo)`
      ```cpp
      // Endpoint: /api/v1/user/repos (for user)
      //        or /api/v1/org/{org}/repos (for org)
      // POST with repo JSON
      // Check response for success (status 201)
      ```
    - [ ] `get_repo(owner, repo)`
      ```cpp
      // GET /api/v1/repos/{owner}/{repo}
      // Parse JSON response
      // Return optional<Repository>
      ```
    - [ ] `list_repos(owner)`
      ```cpp
      // GET /api/v1/user/repos (for authenticated user)
      //  or /api/v1/users/{username}/repos (for specific user)
      //  or /api/v1/orgs/{org}/repos (for org)
      // Parse JSON array
      // Return vector<Repository>
      ```
    - [ ] `delete_repo(owner, repo)`
      ```cpp
      // DELETE /api/v1/repos/{owner}/{repo}
      // Check response (status 204)
      ```

### 2.3 Integrate API into GiteaServer

- [ ] Update `modules/git/include/git/gitea.hpp`
  - [ ] Add `#include "git/gitea_api.hpp"`
  - [ ] Add member: `std::unique_ptr<GiteaAPI> api_`
  - [ ] Add public methods:
    - [ ] `create_repo(name, owner, is_private)` → bool
    - [ ] `delete_repo(owner, repo)` → bool
    - [ ] `list_repos(owner)` → vector<Repository>
  - [ ] Add method: `api()` → GiteaAPI* (accessor)

- [ ] Update `modules/git/src/gitea.cpp`
  - [ ] In constructor, initialize `api_` (after token is available)
  - [ ] Implement `create_repo()`
    ```cpp
    if (!api_) return false;
    Repository repo;
    repo.name = name;
    repo.is_private = is_private;
    return api_->create_repo(owner, repo);
    ```
  - [ ] Implement `sync_repos()` (full version now)
    ```cpp
    // For each local bare repo:
    // 1. Extract repo name
    // 2. Create repo on Gitea via API
    // 3. Add Gitea as remote
    // 4. Push to Gitea
    ```

### 2.4 CLI Commands for Repositories

- [ ] Add new commands to `modules/git/src/commands.cpp`
  - [ ] `cmd_git_repo_create()`
    ```cpp
    // Parse args: name, --owner, --description, --private
    // Get GiteaServer from stored config
    // Call create_repo()
    // Display clone URLs
    ```
  - [ ] `cmd_git_repo_list()`
    ```cpp
    // Parse args: --owner (optional)
    // Get GiteaServer, call list_repos()
    // Display table of repos
    ```
  - [ ] `cmd_git_repo_delete()`
    ```cpp
    // Parse args: owner/repo
    // Prompt for confirmation
    // Call delete_repo()
    ```
  - [ ] Update `cmd_git_server_sync()`
    - [ ] Now uses API to create repos before syncing

- [ ] Register new commands in `register_commands()`
  ```cpp
  reg.add("git-repo-create", "Create repository on Gitea", cmd_git_repo_create);
  reg.add("git-repo-list", "List repositories", cmd_git_repo_list);
  reg.add("git-repo-delete", "Delete repository", cmd_git_repo_delete);
  ```

### 2.5 Database Updates

- [ ] Create or update migration `migrations/007_git_platforms.sql`
  - [ ] Add columns for Gitea:
    ```sql
    ALTER TABLE git_platforms ADD COLUMN api_token TEXT;
    ALTER TABLE git_platforms ADD COLUMN http_port INTEGER DEFAULT 3000;
    ```

- [ ] Update `nexus::Store` if needed
  - [ ] `set_platform_token(platform_id, token)`
  - [ ] `get_platform_token(platform_id)` → optional<string>

### 2.6 Testing Phase 2

- [ ] Manual testing
  - [ ] Create repository: `nazg git-repo-create test-repo --private`
  - [ ] Verify repo exists in Gitea web UI
  - [ ] List repos: `nazg git-repo-list`
  - [ ] Clone repo locally: `git clone git@gitea-host:admin/test-repo.git`
  - [ ] Push changes
  - [ ] Sync local bare repo: `nazg git-server-sync`
  - [ ] Delete repo: `nazg git-repo-delete admin/test-repo`
- [ ] Error handling tests
  - [ ] Invalid auth token
  - [ ] Duplicate repo name
  - [ ] Network failure
- [ ] Document issues and fix

**Phase 2 Status**: ⬜ Not Started

**Phase 2 Complete When**:
- ✅ Can create repositories via Gitea API from CLI
- ✅ Can list repositories
- ✅ Can delete repositories
- ✅ Can sync local bare repos to Gitea

---

## Phase 3: User & Organization Management (Week 3)

**Goal**: Manage users and organizations via API

### 3.1 User Management Implementation

- [ ] Implement user methods in `gitea_api.cpp`
  - [ ] `create_user(user, password)`
    ```cpp
    // POST /api/v1/admin/users
    // Requires admin token
    // Body: {"username": "...", "email": "...", "password": "..."}
    ```
  - [ ] `get_user(username)`
    ```cpp
    // GET /api/v1/users/{username}
    ```
  - [ ] `list_users()`
    ```cpp
    // GET /api/v1/admin/users (requires admin)
    ```

### 3.2 Organization Management Implementation

- [ ] Implement org methods in `gitea_api.cpp`
  - [ ] `create_org(org)`
    ```cpp
    // POST /api/v1/orgs
    // Body: {"username": "...", "full_name": "...", "description": "..."}
    ```
  - [ ] `list_orgs()`
    ```cpp
    // GET /api/v1/user/orgs (for authenticated user)
    ```

### 3.3 CLI Commands for Users/Orgs

- [ ] Add commands to `modules/git/src/commands.cpp`
  - [ ] `cmd_git_user_create()`
    ```cpp
    // Parse args: username, --email, --admin
    // Prompt for password (use prompt module)
    // Call API create_user()
    ```
  - [ ] `cmd_git_user_list()`
    ```cpp
    // Call API list_users()
    // Display table
    ```
  - [ ] `cmd_git_org_create()`
    ```cpp
    // Parse args: name, --description
    // Call API create_org()
    ```
  - [ ] `cmd_git_org_list()`
    ```cpp
    // Call API list_orgs()
    // Display table
    ```

- [ ] Register commands
  ```cpp
  reg.add("git-user-create", "Create Gitea user", cmd_git_user_create);
  reg.add("git-user-list", "List Gitea users", cmd_git_user_list);
  reg.add("git-org-create", "Create organization", cmd_git_org_create);
  reg.add("git-org-list", "List organizations", cmd_git_org_list);
  ```

### 3.4 Database Schema for Users/Orgs

- [ ] Update migration `007_git_platforms.sql`
  - [ ] Add tables:
    ```sql
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
    ```

- [ ] Update `nexus::Store` with methods:
  - [ ] `add_gitea_user(platform_id, user)` → int64_t
  - [ ] `get_gitea_users(platform_id)` → vector<User>
  - [ ] `add_gitea_org(platform_id, org)` → int64_t
  - [ ] `get_gitea_orgs(platform_id)` → vector<Org>

### 3.5 Testing Phase 3

- [ ] Manual testing
  - [ ] Create user: `nazg git-user-create alice --email alice@example.com`
  - [ ] Verify user in Gitea web UI
  - [ ] List users: `nazg git-user-list`
  - [ ] Create org: `nazg git-org-create myteam --description "My Team"`
  - [ ] Verify org in Gitea web UI
  - [ ] Create repo in org: `nazg git-repo-create backend --owner myteam`
- [ ] Error tests
  - [ ] Duplicate username
  - [ ] Invalid email
  - [ ] Permission errors (non-admin token)

**Phase 3 Status**: ⬜ Not Started

**Phase 3 Complete When**:
- ✅ Can create/list users via CLI
- ✅ Can create/list organizations via CLI
- ✅ Can create repositories in organizations

---

## Phase 4: Advanced Features (Week 4)

**Goal**: Webhooks, mirroring, SSH keys, Gitea Actions

### 4.1 Webhook Management

- [ ] Implement webhook methods in `gitea_api.cpp`
  - [ ] `create_webhook(owner, repo, webhook)`
    ```cpp
    // POST /api/v1/repos/{owner}/{repo}/hooks
    // Body: {"type": "gitea", "config": {"url": "...", "content_type": "json"}, "events": [...]}
    ```
  - [ ] `list_webhooks(owner, repo)`
    ```cpp
    // GET /api/v1/repos/{owner}/{repo}/hooks
    ```
  - [ ] `delete_webhook(owner, repo, hook_id)`
    ```cpp
    // DELETE /api/v1/repos/{owner}/{repo}/hooks/{id}
    ```

- [ ] Add CLI command
  - [ ] `cmd_git_webhook_add()`
    ```cpp
    // Parse args: owner/repo, url, --events
    // Create webhook via API
    ```
  - [ ] `cmd_git_webhook_list()`
  - [ ] `cmd_git_webhook_delete()`

### 4.2 Repository Mirroring

- [ ] Implement mirroring in `gitea_api.cpp`
  - [ ] `mirror_repo(remote_url, name, is_private)`
    ```cpp
    // POST /api/v1/repos/migrate
    // Body: {"clone_addr": "...", "repo_name": "...", "mirror": true, "private": ...}
    ```

- [ ] Add CLI command
  - [ ] `cmd_git_mirror()`
    ```cpp
    // Parse args: remote-url, --name, --private
    // Call API mirror_repo()
    // Display result
    ```

### 4.3 SSH Key Management

- [ ] Implement SSH key methods in `gitea_api.cpp`
  - [ ] `add_ssh_key(title, key)`
    ```cpp
    // POST /api/v1/user/keys
    // Body: {"title": "...", "key": "ssh-rsa ..."}
    ```
  - [ ] `list_ssh_keys()`
    ```cpp
    // GET /api/v1/user/keys
    ```
  - [ ] `delete_ssh_key(key_id)`
    ```cpp
    // DELETE /api/v1/user/keys/{id}
    ```

- [ ] Add CLI command
  - [ ] `cmd_git_key_add()`
    ```cpp
    // Read ~/.ssh/id_rsa.pub or provided path
    // Upload to Gitea via API
    ```
  - [ ] `cmd_git_key_list()`

### 4.4 Gitea Actions Integration

- [ ] Add helper to generate workflow files
  - [ ] `generate_test_workflow()` - Generate .gitea/workflows/test.yml
  - [ ] Integrate with test module to get test command

- [ ] Add CLI command
  - [ ] `cmd_git_actions_setup()`
    ```cpp
    // Detect project type (via brain module)
    // Generate appropriate workflow file
    // Commit and push
    ```

### 4.5 Testing Phase 4

- [ ] Test webhooks
  - [ ] Add webhook to test repo
  - [ ] Trigger push event
  - [ ] Verify webhook delivery in Gitea UI
- [ ] Test mirroring
  - [ ] Mirror a GitHub repo
  - [ ] Verify mirror sync
- [ ] Test SSH keys
  - [ ] Add local SSH key to Gitea
  - [ ] Clone repo via SSH
- [ ] Test Gitea Actions
  - [ ] Setup workflow for test project
  - [ ] Push commit and verify action runs

**Phase 4 Status**: ⬜ Not Started

**Phase 4 Complete When**:
- ✅ Can manage webhooks via CLI
- ✅ Can mirror external repos
- ✅ Can manage SSH keys
- ✅ Can generate Gitea Actions workflows

---

## Documentation

### During Implementation

- [ ] Add inline code documentation (doxygen-style comments)
- [ ] Document public API methods
- [ ] Add usage examples in comments

### Final Documentation

- [ ] Update `docs/git.md` with Gitea sections
  - [ ] Quick reference for Gitea commands
  - [ ] Installation guide
  - [ ] API usage examples
- [ ] Create `docs/gitea.md` (detailed guide)
  - [ ] Server setup walkthrough
  - [ ] Repository management
  - [ ] User/org management
  - [ ] Advanced features
  - [ ] Troubleshooting
- [ ] Update README with Gitea features
- [ ] Add examples to plan document

---

## Testing Checklist

### Unit Tests (Optional but Recommended)

- [ ] Create `tests/git_gitea_api_test.cpp`
  - [ ] Mock HTTP responses
  - [ ] Test JSON parsing
  - [ ] Test error handling

### Integration Tests

- [ ] Full installation workflow
- [ ] Repository CRUD operations
- [ ] User management
- [ ] Organization management
- [ ] Webhook creation
- [ ] Mirroring

### Manual Test Scenarios

1. **First-time setup**
   - [ ] Fresh machine, no Gitea
   - [ ] Run full install
   - [ ] Verify all services running

2. **Repository workflow**
   - [ ] Create repo
   - [ ] Push code
   - [ ] Create PR (via web UI for now)
   - [ ] Merge PR

3. **Multi-user scenario**
   - [ ] Create multiple users
   - [ ] Create organization
   - [ ] Add users to org
   - [ ] Create org repos
   - [ ] Test permissions

4. **Migration scenario**
   - [ ] Mirror external repos
   - [ ] Sync local bare repos
   - [ ] Verify all repos accessible

---

## Dependencies & Requirements

### Build Dependencies

- [ ] Verify CMakeLists.txt includes:
  - [ ] libcurl (for API calls)
  - [ ] OpenSSL (for token encryption, secret generation)

### Runtime Dependencies (on Gitea server)

- [ ] git (usually pre-installed)
- [ ] systemd (for service management)

### Development/Test Requirements

- [ ] Test machine/VM with:
  - [ ] Ubuntu 22.04 or later (or Arch, Debian)
  - [ ] SSH access with key auth
  - [ ] Sudo privileges
  - [ ] At least 1GB RAM
  - [ ] At least 2GB free disk space

---

## Rollout Timeline

| Week | Phase | Key Deliverables |
|------|-------|------------------|
| 1 | Phase 1 | Remote Gitea installation working |
| 2 | Phase 2 | Repository creation/management via API |
| 3 | Phase 3 | User and organization management |
| 4 | Phase 4 | Advanced features (webhooks, mirroring, etc.) |

**Target**: 4 weeks to fully functional Gitea integration

---

## Success Criteria

### Minimum Viable Product (MVP)

- ✅ Can install Gitea on remote machine with one command
- ✅ Can create repositories via CLI
- ✅ Can sync local repos to Gitea
- ✅ Can access repos via web UI

### Full Feature Set

- ✅ All MVP features
- ✅ User and organization management
- ✅ Webhook configuration
- ✅ Repository mirroring
- ✅ SSH key management
- ✅ Documentation complete

---

## Risk Mitigation

### Potential Issues

1. **SSH key authentication**
   - Risk: User doesn't have SSH key on remote
   - Mitigation: Add check and helpful error message, guide user to ssh-copy-id

2. **Sudo permissions**
   - Risk: User doesn't have sudo on remote
   - Mitigation: Document requirements clearly, provide sudoers config example

3. **Network/firewall issues**
   - Risk: Can't download Gitea binary
   - Mitigation: Provide alternative (manual download, local copy)

4. **API compatibility**
   - Risk: Gitea API changes between versions
   - Mitigation: Document tested version, add version check

5. **JSON parsing**
   - Risk: Manual parsing is fragile
   - Mitigation: Consider adding nlohmann/json library or keep parsing simple

---

## Notes

- Follow existing cgit pattern closely for consistency
- Reuse SSH helpers where possible
- Log everything for debugging
- Prompt user for confirmation on destructive actions
- Store API tokens securely (encrypted or env vars)
- Test incrementally - don't wait until end of phase

---

**Overall Status**: Planning (0% Complete)
**Next Action**: Begin Phase 1 - Create gitea.hpp
**Test Machine**: [Configure your machine details here]
**Last Updated**: 2025-10-06
