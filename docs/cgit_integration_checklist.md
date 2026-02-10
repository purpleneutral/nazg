# cgit Integration Implementation Checklist

Quick reference for implementing the cgit integration plan. Check off items as they're completed.

---

## Phase 1: Core Infrastructure ✅/❌

### fcgiwrap Setup
- [x] Add `setup_fcgiwrap()` method to `CgitServer`
- [x] Detect systemd availability
- [x] Enable `fcgiwrap.socket`
- [x] Verify socket at `/run/fcgiwrap.sock`
- [x] Add to installation workflow
- [ ] Test on Arch Linux

### git-http-backend
- [x] Add `/git` location block to nginx config
- [x] Configure `GIT_PROJECT_ROOT=/srv/git`
- [x] Set `GIT_HTTP_EXPORT_ALL`
- [x] Point to `/usr/lib/git-core/git-http-backend`
- [ ] Test HTTP clone: `git clone http://host/git/repo.git`

### Git User Account
- [x] Add `setup_git_user()` method
- [x] Create user: `useradd -r -m -d /srv/git -s /usr/bin/git-shell git`
- [x] Add `deploy_ssh_key()` method
- [x] Create `/srv/git/.ssh/authorized_keys`
- [x] Set permissions: 700/.ssh, 600/authorized_keys
- [ ] Test SSH push: `git push git@host:/srv/git/repo.git`

### Enhanced cgitrc
- [x] Add SSH clone URL: `ssh://git@host:/srv/git/$CGIT_REPO_URL`
- [x] Add HTTP clone URL: `http://host/git/$CGIT_REPO_URL`
- [x] Configure `cache-size=1000`
- [x] Add `enable-http-clone=1`
- [x] Set `css=/cgit-css/cgit.css`
- [x] Set `logo=/cgit-css/cgit.png`
- [x] Add caching configuration (cache-root, cache-*-ttl)
- [x] Add UI enhancements (commit-graph, log-filecount, branch-sort)
- [x] Detect Arch vs Debian nginx paths

### Phase 1 Testing
- [ ] Full install on fresh VM works
- [ ] Web UI accessible
- [ ] HTTP clone works
- [ ] SSH push works
- [ ] Write integration test

---

## Phase 2: Migration & Management ✅/❌

### Bulk Migration Command
- [ ] Create `cmd_git_server_migrate()` in commands.cpp
- [ ] Scan directory for git repos
- [ ] Create bare repos on server
- [ ] Use `git push --mirror` for each repo
- [ ] Add `internal` remote locally
- [ ] Store migration records in Nexus
- [ ] Add CLI help text

### Repository Discovery
- [ ] Create `scan_bare_repos()` function
- [ ] Find all `*.git` directories
- [ ] Read `description` files
- [ ] Extract last commit timestamp
- [ ] Update `bare_repos` table

### Enhanced Status
- [ ] Show all clone URLs in `nazg git status`
- [ ] Display remote divergence
- [ ] Show repository count on server
- [ ] Add `nazg git clone-url` command

### Phase 2 Testing
- [ ] Migrate 5+ repos successfully
- [ ] All branches/tags preserved
- [ ] Remotes configured correctly
- [ ] `nazg git status` shows complete info

---

## Phase 3: Git Server Health Checks ✅/❌

### Git Doctor Script
- [x] Create `modules/bot/scripts/git-doctor.sh`
- [x] Check cgit/gitea version
- [x] Check nginx status
- [x] Check fcgiwrap.socket status
- [x] Count repositories
- [x] Calculate total repo size
- [x] Test git-http-backend accessibility
- [x] Verify git user configuration
- [x] Check disk space in `/srv/git`
- [x] Return JSON report

### Git Doctor Bot
- [x] Create `bot/git_doctor.hpp`
- [x] Create `bot/git_doctor.cpp`
- [x] Implement script execution
- [x] Parse JSON health report
- [x] Store results in Nexus
- [x] Register in bot registry

### Integration
- [x] Create `nazg bot git-doctor` command
- [ ] Add `--health-check` to `git server status`
- [ ] Display health warnings in status output
- [ ] Show recommendations for issues

### Database Schema
- [x] Create `git_servers` table
- [x] Create `repo_migrations` table
- [x] Create `git_server_health` table
- [x] Write migration SQL

### Phase 3 Testing
- [ ] Doctor bot runs successfully
- [ ] Detects healthy system
- [ ] Detects stopped services
- [ ] Detects disk space issues
- [ ] Health history stored correctly

---

## Phase 4: Advanced Features ✅/❌

### Multi-Remote Workflows
- [ ] Implement dual-remote setup
- [ ] Implement multiple push URLs
- [ ] Generate post-receive hooks
- [ ] Add `nazg git remote-strategy` command
- [ ] Add `nazg git pushboth` alias
- [ ] Document workflows

### Pre-Push Guards
- [ ] Create pre-push hook template
- [ ] Add `nazg git hooks install` command
- [ ] Support branch pattern blocking
- [ ] Add configuration options
- [ ] Test with WIP branches

### TLS Support
- [ ] Detect certbot availability
- [ ] Add `nazg git server enable-tls` command
- [ ] Generate Let's Encrypt certificates
- [ ] Configure nginx for HTTPS
- [ ] Add HTTP→HTTPS redirect
- [ ] Update clone URLs

### Cross-Platform Support
- [ ] Detect Debian/Ubuntu
- [ ] Use apt package names
- [ ] Handle sites-available/sites-enabled
- [ ] Install apache2-utils on Debian
- [ ] Test on Ubuntu 22.04
- [ ] Test on Debian 12

### Phase 4 Testing
- [ ] Dual-remote push works
- [ ] Pre-push hooks block correctly
- [ ] TLS certificate generated
- [ ] Works on Arch Linux
- [ ] Works on Debian
- [ ] Works on Ubuntu

---

## Phase 5: Polish & Documentation ✅/❌

### Documentation
- [ ] Update `docs/git.md`
- [ ] Create `docs/git_server_setup.md`
- [ ] Add troubleshooting guide
- [ ] Document health check metrics
- [ ] Add CLI examples
- [ ] Create video/tutorial (optional)

### Testing
- [ ] Full test on Arch VM
- [ ] Full test on Debian VM
- [ ] Test all migration scenarios
- [ ] Test all health checks
- [ ] Test error conditions
- [ ] Write smoke tests

### CLI Improvements
- [ ] Add progress indicators
- [ ] Colorize health output
- [ ] Interactive install wizard
- [ ] Better error messages
- [ ] Add `--verbose` support

### Assistant Integration
- [ ] Add git server setup card
- [ ] Show health warnings in greeting
- [ ] Quick actions for git tasks
- [ ] Repository status display

### Phase 5 Testing
- [ ] All commands have help text
- [ ] Examples work as documented
- [ ] Assistant cards functional
- [ ] Smoke tests pass

---

## Quick Commands Reference

### Phase 1
```bash
nazg git server add --type cgit --host myserver
nazg git server install myserver
nazg git server status myserver
```

### Phase 2
```bash
nazg git server migrate --source ~/projects --server myserver
nazg git status
nazg git clone-url
```

### Phase 3
```bash
nazg bot git-doctor --host myserver
nazg git server status --health-check
nazg bot history --bot git-doctor
```

### Phase 4
```bash
nazg git remote-strategy --type dual-remote
nazg git hooks install --type pre-push-guard
nazg git server enable-tls --domain git.example.com
nazg git pushboth
```

---

## Files to Modify

### Existing Files
- [x] `modules/git/include/git/cgit.hpp` - Add new methods
- [x] `modules/git/src/cgit.cpp` - Implement enhancements
- [x] `modules/git/src/commands.cpp` - Add new commands
- [x] `modules/git/include/git/server.hpp` - Extend interface
- [x] `docs/git.md` - Update documentation

### New Files
- [ ] `modules/bot/include/bot/git_doctor.hpp`
- [ ] `modules/bot/src/git_doctor.cpp`
- [ ] `modules/bot/scripts/git-doctor.sh`
- [ ] `modules/git/include/git/migration.hpp`
- [ ] `modules/git/src/migration.cpp`
- [ ] `modules/git/include/git/remote_strategy.hpp`
- [ ] `modules/git/src/remote_strategy.cpp`
- [ ] `docs/git_server_setup.md`
- [ ] `docs/git_server_troubleshooting.md`

---

## Code Reuse Tracker

### SSH Operations → bot/transport
- [x] Used for remote commands
- [ ] Used for file uploads
- [ ] Used for health checks

### Package Management → system/package
- [ ] Used for cgit installation
- [ ] Used for nginx installation
- [ ] Used for fcgiwrap installation

### Prompts → prompt/prompt
- [ ] Used for install confirmation
- [ ] Used for migration confirmation
- [ ] Used for health report display

### Database → nexus/store
- [ ] git_servers table created
- [ ] repo_migrations table created
- [ ] git_server_health table created

---

## Known Issues / TODOs

- [ ] Handle firewall configuration (ports 80, 443, 22)
- [ ] Support for non-standard SSH ports
- [ ] Handle SELinux contexts (if enabled)
- [ ] IPv6 support
- [ ] Windows Subsystem for Linux compatibility
- [ ] Handle existing cgit installations (upgrade path)

---

## Performance Considerations

- [ ] Parallel repository syncing
- [ ] Incremental health checks
- [ ] Caching of server status
- [ ] Batch operations for migrations
- [ ] Background health monitoring

---

## Security Checklist

- [ ] SSH key generation uses ed25519
- [ ] No passwords stored in database
- [ ] git-shell restriction enforced
- [ ] StrictHostKeyChecking handled appropriately
- [ ] TLS certificates verified
- [ ] File permissions checked (700, 600)
- [ ] User input sanitized with shell_quote
- [ ] Sudo usage minimized and logged

---

## Notes

- Keep backward compatibility with existing `git-server-*` commands
- All new features should have `--help` text
- Error messages should be actionable
- Prompts should show exactly what will be executed
- Always use `shell_quote()` for user input
- Log all remote operations for audit trail
