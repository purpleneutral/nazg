# Docker CLI Usage Guide

## Overview

Nazg provides comprehensive Docker management via both CLI and TUI. Every operation available in the TUI has a corresponding CLI command, and vice versa.

## Quick Reference

```bash
# Server Management
nazg docker server list                          # List all servers
nazg docker server scan <server>                 # Trigger agent scan

# Container Management
nazg docker container list <server>              # List running containers
nazg docker container list -a <server>           # List all containers
nazg docker container restart <server> <name>    # Restart with dependencies

# Stack Management (Compose File Intelligence)
nazg docker stack create <server> <stack-name> -f <compose>...   # Create from compose files
nazg docker stack list <server>                                   # List stacks
nazg docker stack show <server> <stack-name>                      # Show stack details
nazg docker stack deps <server> <service>                         # Show dependencies
nazg docker stack restart <server> <stack-name>                   # Intelligent restart

# TUI Dashboard
nazg tui                                         # Launch TUI
  :load docker                                   # Open Docker dashboard
```

## Managing Complex Stacks (Your Gluetun Use Case)

### Scenario

You have a complex setup:
- **gluetun** container with 2 networks (vpn_network, web_network) and static IPs
- **transmission**, **sonarr**, **radarr** using `network_mode: "service:gluetun"`
- **traefik** accessing services via gluetun's web_network
- Multiple compose files that need to work together

### Solution: Create a Stack

Nazg automatically parses your compose files and understands the full dependency graph:

```bash
# One-time setup: Create stack from your compose files
nazg docker stack create myserver vpn-media-stack \
  -f /opt/docker/gluetun.yml \
  -f /opt/docker/media.yml \
  -f /opt/docker/traefik.yml \
  -d "VPN-routed media stack with Traefik"

# Nazg automatically:
# ✓ Parses all 3 compose files
# ✓ Detects gluetun's 2 networks with static IPs
# ✓ Identifies network_mode: "service:gluetun" dependencies
# ✓ Maps shared network relationships (traefik ↔ gluetun)
# ✓ Stores complete dependency graph in database
```

### View Dependencies

```bash
# See what transmission depends on
nazg docker stack deps myserver transmission
# Output:
# Dependencies for 'transmission':
# DEPENDS ON          TYPE
# gluetun            network_mode

# See what depends on gluetun (reverse lookup via query)
# Query: SELECT service_name FROM docker_service_dependencies
#        WHERE depends_on_service = 'gluetun'
```

### Intelligent Restart

```bash
# Restart gluetun (Nazg handles all dependents automatically)
nazg docker container restart myserver gluetun

# What Nazg does:
# 1. Queries database: "What depends on gluetun?"
#    → transmission, sonarr, radarr (network_mode dependencies)
# 2. Calculates stop order (reverse):
#    → transmission, sonarr, radarr → gluetun
# 3. Calculates start order:
#    → gluetun → wait for health → transmission, sonarr, radarr
# 4. Executes restart sequence via agent
# 5. Waits for health checks
# 6. Logs all actions to docker_orchestration_actions table

# Restart entire stack
nazg docker stack restart myserver vpn-media-stack

# What Nazg does:
# 1. Loads all services in stack
# 2. Builds full dependency graph
# 3. Topological sort for correct ordering
# 4. Stops all in reverse dependency order
# 5. Starts all in dependency order (gluetun → media → traefik)
# 6. Waits for each service to be healthy before starting next
```

### List Your Containers

```bash
nazg docker container list myserver

# Output:
# Containers on 'myserver':
# ================================================================================
# NAME                  STATE       IMAGE                        HEALTH         STATUS
# gluetun              running     qmcgaw/gluetun              healthy        Up 2 days
# transmission         running     linuxserver/transmission    n/a            Up 2 days
# sonarr               running     linuxserver/sonarr          n/a            Up 2 days
# radarr               running     linuxserver/radarr          n/a            Up 2 days
# traefik              running     traefik:latest              healthy        Up 2 days
```

### View Stack Details

```bash
nazg docker stack show myserver vpn-media-stack

# Output:
# Stack: vpn-media-stack
# ================================================================================
# ID:          1
# Server ID:   123
# Description: VPN-routed media stack with Traefik
# Priority:    0
# Auto-restart: no
# Health timeout: 30s
#
# Compose Files:
#   1. /opt/docker/gluetun.yml
#   2. /opt/docker/media.yml
#   3. /opt/docker/traefik.yml
```

## CLI vs TUI Parity

| Operation | CLI | TUI |
|-----------|-----|-----|
| List servers | `nazg docker server list` | `:load docker` → Server dropdown |
| List containers | `nazg docker container list <server>` | `:load docker` → Select server → View containers |
| Show dependencies | `nazg docker stack deps <server> <service>` | `:load docker` → Select service → Dependencies panel |
| Restart container | `nazg docker container restart <server> <name>` | `:load docker` → Select container → Press `r` |
| Restart stack | `nazg docker stack restart <server> <stack>` | `:load docker` → Select stack → Press `R` |
| Create stack | `nazg docker stack create ...` | `:load docker` → `:stack-create` command |

## Database Schema

Nazg stores everything in SQLite for intelligent decision-making:

### Dependency Storage

**docker_service_dependencies** - Service-to-service relationships:
```
service_name  | depends_on_service | dependency_type
--------------+--------------------+----------------
transmission  | gluetun            | network_mode
sonarr        | gluetun            | network_mode
radarr        | gluetun            | network_mode
traefik       | gluetun            | shared_network
```

**docker_network_dependencies** - Network configurations:
```
service_name | network_name  | static_ip
-------------+---------------+-----------
gluetun      | vpn_network   | 172.20.0.2
gluetun      | web_network   | 172.21.0.2
traefik      | web_network   | NULL
```

**docker_stacks** - Stack definitions:
```
id | name              | server_id | priority | auto_restart
---+-------------------+-----------+----------+-------------
1  | vpn-media-stack   | 123       | 0        | false
```

**docker_stack_compose_files** - Compose file linkage:
```
stack_id | compose_file_id | execution_order
---------+-----------------+----------------
1        | 15              | 0
1        | 16              | 1
1        | 17              | 2
```

### Action Auditing

**docker_orchestration_actions** - Full audit log:
```sql
-- Every restart/start/stop is logged with:
-- • What was done (action_type: restart/start/stop)
-- • What service/stack (target)
-- • Why (reason: user_request/auto-heal/scheduled)
-- • When (triggered_at timestamp)
-- • Result (status: success/failed, exit_code, output)
```

This allows:
- Troubleshooting: "Why did this restart?"
- Trend analysis: "How often does gluetun restart?"
- Compliance: Full audit trail

## Example Workflow

```bash
# 1. Add your server to nazg
nazg agent add myserver user@192.168.1.100

# 2. Create stack from your existing compose files
nazg docker stack create myserver vpn-stack \
  -f /opt/docker/gluetun.yml \
  -f /opt/docker/media.yml \
  -d "VPN and media services"

# 3. View what nazg learned
nazg docker stack deps myserver transmission
nazg docker stack show myserver vpn-stack

# 4. When you need to restart gluetun
nazg docker container restart myserver gluetun
# Nazg automatically restarts transmission/sonarr/radarr too

# 5. Or restart the whole stack intelligently
nazg docker stack restart myserver vpn-stack

# 6. View containers
nazg docker container list myserver

# 7. Launch TUI for interactive management
nazg tui
  :load docker
  # Navigate with j/k
  # Select server
  # View containers and dependencies
  # Press 'r' to restart selected container
```

## What's Implemented vs Coming Soon

### ✅ Implemented (Working Now)

- Stack creation from compose files
- Dependency detection (4 types: depends_on, network_mode, shared networks, volumes)
- Database storage of full dependency graph
- Topological sort for correct start order
- CLI commands for all operations
- TUI Docker dashboard with server/container/stack views
- Menu registry integration

### ⏳ Coming Soon (Next Steps)

- Actual command execution via agent transport
  - Currently: Commands show what they *will* do
  - Next: Wire up agent protocol to actually execute docker commands
- Health check waiting logic
- Real-time container status updates in TUI
- Stack orchestration rules engine
- Auto-resolution of common issues

## Advanced: Dependency Types

Nazg detects 4 types of dependencies:

1. **depends_on** (explicit in compose file)
   ```yaml
   depends_on:
     - database
   ```

2. **network_mode** (hard dependency - shares network stack)
   ```yaml
   network_mode: "service:gluetun"  # THIS is your use case
   ```

3. **shared_network** (soft dependency - on same network)
   ```yaml
   networks:
     - web_network  # Both services on web_network
   ```

4. **shared_volume** (soft dependency - share storage)
   ```yaml
   volumes:
     - shared_data:/data
   ```

## Tips

1. **Always use stack create for complex setups** - It's easier than manual dependency configuration

2. **Check dependencies before major changes** - `nazg docker stack deps` shows what will be affected

3. **Use TUI for exploration, CLI for automation** - TUI is great for understanding your setup, CLI is perfect for scripts

4. **Let nazg handle restart order** - Don't manually restart containers in sequence, use `nazg docker container restart` to handle dependencies automatically

5. **Audit logs are your friend** - Query `docker_orchestration_actions` to understand what happened and when

## Next Steps

See `docs/docker-orchestration-status.md` for implementation status.
See `docs/docker-orchestration-design.md` for architecture details.
