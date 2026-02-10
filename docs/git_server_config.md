# Git Server Configuration

## Overview

Nazg supports a hybrid approach for managing git servers:
- **config.toml** - Source of truth for server configuration (version controlled, easy to edit)
- **Nexus database** - Runtime state tracking (installation status, health checks, last used)

When configuration differs between sources, Nazg prompts you to sync changes.

---

## Config Format

Add servers to your `~/.config/nazg/config.toml`:

```toml
[git.servers.myserver]
type = "cgit"
host = "10.0.0.4"
ssh_user = "youruser"
ssh_port = 22
repo_base_path = "/srv/git"

[git.servers.production]
type = "cgit"
host = "git.example.com"
ssh_user = "git"

[git.servers.dev]
type = "gitea"
host = "10.0.0.5"
ssh_user = "youruser"
port = 3000
```

### Configuration Keys

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `type` | Yes | - | Server type: `cgit`, `gitea`, `gitlab` |
| `host` | Yes | - | Hostname or IP address |
| `ssh_user` | No | `git` | SSH username for remote operations |
| `ssh_port` | No | `22` | SSH port |
| `port` | No | `80` (cgit)<br>`3000` (gitea) | HTTP/web port |
| `repo_base_path` | No | `/srv/git` | Repository directory on server |
| `config_path` | No | `/etc/cgitrc` (cgit)<br>`/etc/gitea/app.ini` (gitea) | Server config file path |
| `web_url` | No | Auto-generated | Web UI URL |

---

## Usage Examples

### Add and Install a Server

```bash
# Define in config.toml first:
[git.servers.myserver]
type = "cgit"
host = "10.0.0.4"
ssh_user = "youruser"

# Then install
nazg git server install myserver
```

### Check Server Status

```bash
nazg git server status myserver
```

### Configuration Changes

If you edit config.toml after a server is already registered:

```bash
$ nazg git server status myserver

┌─────────────────────────────────┐
│ Configuration Changed           │
├─────────────────────────────────┤
│ Sync updated config to database?│
│                                 │
│ Facts                           │
│   Server:  myserver             │
│   Host:    10.0.0.5 (was 10.0.0.4) │
│   Type:    cgit                 │
│                                 │
│ Actions                         │
│   • Update database with new    │
│     configuration from          │
│     config.toml                 │
│                                 │
│ Continue? [Y/n]                 │
└─────────────────────────────────┘
```

---

## Runtime State (Database)

Nazg tracks additional information in its database:

| Field | Description |
|-------|-------------|
| `status` | `not_installed`, `installed`, `online`, `offline` |
| `installed_at` | Timestamp of installation |
| `last_check` | Last health check timestamp |
| `config_hash` | Hash for detecting config changes |

View runtime state:

```bash
nazg git server status myserver --verbose
```

---

## Conflict Resolution

### Scenario 1: Config Added, Not in Database
- **Action:** Server auto-registered on first use
- **Status:** `not_installed`

### Scenario 2: Config Changed
- **Action:** Prompt to sync database
- **User Choice:** Accept or keep old config

### Scenario 3: Removed from Config, Still in Database
- **Action:** Warning displayed, database entry retained
- **Manual Cleanup:** `nazg git server remove myserver`

---

## Multiple Servers

Manage multiple git servers easily:

```toml
[git.servers.lan]
type = "cgit"
host = "10.0.0.4"
ssh_user = "youruser"

[git.servers.backup]
type = "cgit"
host = "backup.local"
ssh_user = "git"

[git.servers.cloud]
type = "gitea"
host = "git.example.com"
ssh_user = "git"
```

List all servers:

```bash
nazg git server list
```

---

## Security Notes

- **SSH Keys:** Store in `~/.ssh/`, never in config
- **Passwords:** Never store in config.toml or database
- **sudo Access:** Required for installation (ssh_user needs sudo)
- **git User:** Created automatically during install for push access

---

## Migration from Hardcoded Values

Old commands like `nazg git-server-add` used positional arguments:

```bash
# Old style (still works)
nazg git-server-add cgit 10.0.0.4

# New style (preferred)
# 1. Add to config.toml
# 2. Use named reference
nazg git server install myserver
```

---

## Troubleshooting

### "Server not found: myserver"
- Check config.toml has `[git.servers.myserver]` section
- Ensure config.toml is loaded: `nazg config show`

### "Configuration conflict detected"
- Config.toml was edited after initial registration
- Accept sync prompt to update database
- Or manually edit database: `nazg git server remove myserver` and re-add

### "Cannot connect to server"
- Verify SSH access: `ssh user@host`
- Check ssh_user has sudo access
- Ensure SSH keys are deployed

---

## API for Modules

Other modules can access the server registry:

```cpp
#include "git/server_registry.hpp"

git::ServerRegistry registry(cfg, store, log);

// Get server by label
auto server_opt = registry.get_server("myserver");
if (server_opt) {
  auto& entry = *server_opt;

  // Check for config changes
  if (entry.has_config_changes) {
    prompt::Prompt p(log);
    registry.sync_config_to_database("myserver", &p);
  }

  // Use server config
  auto server = git::create_server(entry.config, store, log);
  server->install();

  // Update runtime state
  registry.mark_installed("myserver");
}
```

---

## Example Workflow

Complete setup from scratch:

1. **Create config:**
```bash
cat >> ~/.config/nazg/config.toml <<'EOF'
[git.servers.devbox]
type = "cgit"
host = "10.0.0.4"
ssh_user = "director"
EOF
```

2. **Install:**
```bash
nazg git server install youruser@devbox
```

3. **Check status:**
```bash
nazg git server status youruser@devbox
```

4. **Use for repos:**
```bash
nazg git create-bare myproject
nazg git server sync youruser@devbox
```

5. **Monitor health:**
```bash
nazg bot git-doctor --host youruser@devbox
```
