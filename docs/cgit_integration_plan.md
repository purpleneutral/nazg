# cgit Integration Plan - Phased Implementation

## Overview

Integrate the comprehensive cgit setup from `cgitPlan.txt` into Nazg's existing git module using a phased approach. Each phase builds upon the previous, adding features incrementally while reusing existing code.

---

## Phase 1: Core Infrastructure (Week 1)

**Goal:** Complete the essential cgit installation flow with proper service setup.

### 1.1 fcgiwrap Setup
**Reuse:** Existing `CgitServer::ssh_exec()`, `install_deps()`
**New:** Methods to enable/configure fcgiwrap

```cpp
// git/cgit.hpp
bool setup_fcgiwrap();
bool verify_fcgiwrap_socket();
```

**Tasks:**
- Detect systemd vs SysV init
- Enable `fcgiwrap.socket` (Arch/Debian)
- Verify socket at `/run/fcgiwrap.sock`
- Add to `CgitServer::install()` workflow

### 1.2 git-http-backend Configuration
**Reuse:** Existing `setup_web_server()`, nginx config generation
**Extend:** Add git HTTP clone/push support

**Tasks:**
- Add `/git` location block to nginx config
- Configure `GIT_PROJECT_ROOT`, `GIT_HTTP_EXPORT_ALL`
- Point to `/usr/lib/git-core/git-http-backend`
- Update `generate_cgitrc()` to include HTTP clone URLs

### 1.3 Git User Account Setup
**Reuse:** Existing SSH transport from bot module
**New:** Automated git user provisioning

```cpp
// git/cgit.hpp
bool setup_git_user();
bool deploy_ssh_key(const std::string& public_key);
```

**Tasks:**
- Create `git` user with `git-shell`
- Set up `/srv/git` directory structure
- Deploy SSH public key to `/srv/git/.ssh/authorized_keys`
- Set proper permissions (700/.ssh, 600/authorized_keys)

### 1.4 Enhanced cgitrc Generation
**Reuse:** Existing `generate_cgitrc()`
**Extend:** Full feature set from plan

**Tasks:**
- Add multiple clone URLs (SSH + HTTP)
- Configure caching (cache-size, cache-root)
- Set scan-path correctly
- Add static asset paths (`/cgit-css/`)
- Support custom root-title/root-desc

### Deliverables
- `nazg git server install` works end-to-end
- Web UI accessible at `http://host/cgit`
- HTTP clone works: `git clone http://host/git/repo.git`
- SSH push works: `git push git@host:/srv/git/repo.git`

---

## Phase 2: Repository Migration & Management (Week 2)

**Goal:** Add bulk repository migration and improved bare repo management.

### 2.1 Bulk Migration Command
**Reuse:** Existing `BareRepoManager`, `sync_repos()`
**New:** `nazg git server migrate` command

```cpp
// git/commands.cpp
static int cmd_git_server_migrate(const directive::command_context& ctx,
                                   const directive::context& ectx);
```

**Tasks:**
- Scan local directory for git repos
- Create matching bare repos on server
- Push all branches/tags with `--mirror`
- Add `internal` remote to each local repo
- Store migration metadata in Nexus

**CLI:**
```bash
nazg git server migrate --source ~/projects --server myserver
```

### 2.2 Repository Discovery
**New:** Automated bare repo scanning

```cpp
// git/bare.hpp
std::vector<BareRepoInfo> scan_bare_repos(const std::string& base_path);
```

**Tasks:**
- Find all `*.git` directories
- Read descriptions from `description` file
- Extract last commit info
- Update Nexus `bare_repos` table

### 2.3 Clone URL Management
**Extend:** Current remote handling

**Tasks:**
- Show all clone URLs in `nazg git status`
- Support custom clone URL templates
- Display both SSH and HTTP options
- Add `nazg git clone-url` command

### Deliverables
- `nazg git server migrate ~/projects` migrates all repos at once
- `nazg git status` shows comprehensive remote info
- Migration script from cgitPlan.txt integrated

---

## Phase 3: Doctor Bot Health Checks (Week 2-3)

**Goal:** Extend doctor bot to monitor git server infrastructure health.

### 3.1 Git Server Health Script
**Reuse:** Doctor bot framework, script embedding
**New:** `modules/bot/scripts/git-doctor.sh`

**Check Items:**
- cgit/gitea binary installed and version
- nginx running and responding
- fcgiwrap.socket active
- Repository count and total size
- git-http-backend accessible
- SSH git user configured correctly
- Disk space in repo directory
- Recent push/clone activity

**JSON Output:**
```json
{
  "git_server": {
    "type": "cgit",
    "version": "1.2.3",
    "web_ui_reachable": true,
    "http_clone_works": true,
    "ssh_push_works": true
  },
  "services": {
    "nginx": "running",
    "fcgiwrap": "running"
  },
  "repositories": {
    "count": 15,
    "total_size_gb": 4.2,
    "last_push": 1704634800
  },
  "disk": {
    "path": "/srv/git",
    "used_pct": 45,
    "free_gb": 120
  },
  "status": "ok",
  "notes": []
}
```

### 3.2 Git Doctor Bot Class
**Reuse:** `DoctorBot` pattern
**New:** Specialized git server bot

```cpp
// bot/git_doctor.hpp
class GitDoctorBot : public BotBase {
  std::string get_script_content() const override;
  ReportData parse_git_report(const std::string& json) const;
};
```

### 3.3 Integration with git server commands
**Extend:** `cmd_git_server_status()`

**Tasks:**
- Add `--health-check` flag to run doctor bot
- Combine git status with health metrics
- Display warnings for misconfigurations

**CLI:**
```bash
nazg git server status --health-check
nazg bot git-doctor --host myserver
```

### 3.4 Automated Health Monitoring
**New:** Scheduled checks and alerts

**Tasks:**
- Store health check history in Nexus
- Detect degraded states (services down, disk full)
- Generate actionable recommendations
- Support for email/webhook notifications (future)

### Deliverables
- `nazg bot git-doctor` performs comprehensive checks
- Health data integrated into `nazg git server status`
- Warnings for common misconfigurations
- Historical tracking of git server health

---

## Phase 4: Advanced Features (Week 3-4)

**Goal:** Multi-remote workflows, TLS, and production hardening.

### 4.1 Multi-Remote Strategies
**Reuse:** Existing remote management
**Extend:** Support multiple push targets

**Features from cgitPlan.txt Section 14:**
- Two remotes per repo (internal + github)
- Multiple push URLs (push to both at once)
- Server-side mirroring via post-receive hooks
- `pushDefault` configuration

**Commands:**
```bash
nazg git remote-strategy --type dual-remote
nazg git remote-strategy --type mirror-to-github
nazg git pushboth  # alias for pushing to all remotes
```

### 4.2 Pre-Push Guards
**New:** Hook installation for branch protection

**Tasks:**
- Install `.git/hooks/pre-push` locally
- Block WIP branches from pushing to public remotes
- Configurable branch patterns
- Per-repo hook management

**CLI:**
```bash
nazg git hooks install --type pre-push-guard
nazg git hooks config --block-pattern "wip/*"
```

### 4.3 TLS/HTTPS Support
**Extend:** Nginx configuration
**New:** Certificate management

**Tasks:**
- Detect certbot availability
- Generate Let's Encrypt certificates
- Configure nginx for HTTPS
- Automatic HTTP→HTTPS redirect
- Update clone URLs to use https://

**CLI:**
```bash
nazg git server enable-tls --domain git.example.com
```

### 4.4 Debian/Ubuntu Support
**Extend:** Package detection and paths
**New:** Distribution-specific logic

**Tasks:**
- Detect apt vs pacman
- Use correct package names (apache2-utils vs apache)
- Handle different nginx paths (sites-available vs conf.d)
- Support different service managers

### Deliverables
- Multi-remote workflows documented and automated
- TLS support for production deployments
- Cross-platform compatibility (Arch + Debian)
- Pre-push hooks for workflow protection

---

## Phase 5: Polish & Documentation (Week 4)

### 5.1 Comprehensive Testing
- Test on fresh Arch Linux VM
- Test on fresh Debian/Ubuntu VM
- Test migration from GitHub to internal
- Test dual-push workflows
- Test doctor bot health checks

### 5.2 Documentation
- Update `docs/git.md` with new features
- Create `docs/git_server_setup.md` quickstart
- Add troubleshooting section
- Document health check metrics

### 5.3 CLI Improvements
- Better progress indicators during install
- Colorized output for health checks
- Interactive wizards for complex setups
- Shell completion helpers

### 5.4 Assistant Integration
- Add git server setup to assistant menu
- Show health warnings in assistant greeting
- Quick actions for common git tasks
- Repository status cards

---

## Code Reuse Strategy

### Existing Components to Leverage

1. **SSH Transport** (`bot/transport.hpp`)
   - Reuse for all remote operations
   - Already handles authentication, error handling
   - Supports agent fallback

2. **Package Management** (`system/package.hpp`)
   - Reuse `detect_package_manager()`
   - Reuse `is_package_installed()`
   - Reuse `install_package()` with prompts

3. **Process Execution** (`system/process.hpp`)
   - Reuse `run_command()`, `run_capture()`
   - Reuse `shell_quote()` for safety

4. **Nexus Persistence** (`nexus/store.hpp`)
   - Extend existing `bare_repos` table
   - Add `git_server_health` table for checks
   - Track migration history

5. **Prompt Module** (`prompt/prompt.hpp`)
   - Reuse for all confirmations
   - Show installation progress
   - Display health check results

6. **Git Client** (`git/client.hpp`)
   - Reuse for local operations
   - Status checking
   - Remote management

### New Components Needed

1. **git/cgit_installer.hpp** - Orchestrates installation
2. **git/git_user_provisioner.hpp** - Sets up git user account
3. **bot/git_doctor.hpp** - Git server health checks
4. **git/migration.hpp** - Bulk repo migration logic
5. **git/remote_strategy.hpp** - Multi-remote workflows

---

## Database Schema Additions

```sql
-- Track git server installations
CREATE TABLE git_servers (
  id INTEGER PRIMARY KEY,
  label TEXT NOT NULL UNIQUE,
  host TEXT NOT NULL,
  type TEXT NOT NULL,  -- 'cgit', 'gitea'
  ssh_port INTEGER DEFAULT 22,
  web_url TEXT,
  repo_base_path TEXT,
  installed_at INTEGER,
  last_health_check INTEGER
);

-- Track repository migrations
CREATE TABLE repo_migrations (
  id INTEGER PRIMARY KEY,
  repo_name TEXT NOT NULL,
  source_path TEXT,
  server_id INTEGER REFERENCES git_servers(id),
  migrated_at INTEGER,
  branch_count INTEGER,
  tag_count INTEGER,
  size_bytes INTEGER
);

-- Track git server health check results
CREATE TABLE git_server_health (
  id INTEGER PRIMARY KEY,
  server_id INTEGER REFERENCES git_servers(id),
  timestamp INTEGER,
  status TEXT,  -- 'ok', 'warning', 'critical'
  web_ui_reachable BOOLEAN,
  http_clone_works BOOLEAN,
  ssh_push_works BOOLEAN,
  service_status TEXT,  -- JSON
  repo_count INTEGER,
  disk_used_pct INTEGER,
  notes TEXT  -- JSON array
);
```

---

## Testing Checklist

### Phase 1
- [ ] Fresh Arch Linux VM: Install cgit successfully
- [ ] fcgiwrap.socket is active and responding
- [ ] nginx serves cgit UI correctly
- [ ] HTTP clone works
- [ ] SSH push works
- [ ] Git user account configured correctly

### Phase 2
- [ ] Migrate 5+ repos from local directory
- [ ] All branches and tags preserved
- [ ] Remote URLs configured correctly
- [ ] Nexus records migration history

### Phase 3
- [ ] Doctor bot detects cgit installation
- [ ] All health checks pass on healthy system
- [ ] Warnings detected when service stopped
- [ ] Health history tracked in Nexus

### Phase 4
- [ ] Dual-remote push works
- [ ] Pre-push hooks block WIP branches
- [ ] TLS certificate generated and installed
- [ ] Works on both Arch and Debian

### Phase 5
- [ ] All documentation complete
- [ ] Examples tested and verified
- [ ] Assistant cards working
- [ ] CLI help text accurate

---

## Migration from Current State

The current `CgitServer` class already has:
- ✅ SSH connection methods
- ✅ Basic package installation
- ✅ Nginx config generation (needs enhancement)
- ✅ Repository syncing via rsync

We'll enhance, not replace:
1. Keep existing `CgitServer` class structure
2. Add missing methods for fcgiwrap, git user, etc.
3. Improve `generate_cgitrc()` with full features
4. Extend `setup_web_server()` for HTTP backend
5. Add new commands without breaking existing ones

---

## Success Metrics

After full implementation:
1. ✅ Install cgit on fresh server in < 5 minutes
2. ✅ Migrate all local repos with one command
3. ✅ Health checks provide actionable insights
4. ✅ Zero manual config file editing needed
5. ✅ Works on Arch Linux and Debian/Ubuntu
6. ✅ TLS setup is automated
7. ✅ Multi-remote workflows documented and working

---

## Next Steps

**Immediate Actions:**
1. Create feature branch: `feature/cgit-phase1`
2. Implement fcgiwrap setup in `CgitServer`
3. Add git user provisioning methods
4. Enhance nginx config generation
5. Write unit tests for new methods

**After Phase 1 Complete:**
1. Create migration command
2. Implement bulk repo scanning
3. Update CLI help and docs

**After Phase 3 Complete:**
1. Announce git server health monitoring
2. Gather user feedback
3. Prioritize Phase 4 features based on usage
