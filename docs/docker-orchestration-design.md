# Nazg Docker Orchestration System

## Overview

Intelligent Docker stack management system that understands complex multi-compose deployments, dependency graphs, and automates orchestration based on rules and context.

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Master Control Center                     │
│                                                              │
│  ┌────────────────┐  ┌──────────────────┐  ┌─────────────┐ │
│  │ Orchestrator   │  │ Docker Monitor   │  │   Nexus DB  │ │
│  │  - Stack Mgmt  │  │  - Agent Client  │  │  - Stacks   │ │
│  │  - Dep Graph   │  │  - Scan Parsing  │  │  - Rules    │ │
│  │  - Rules Engine│  │                  │  │  - Issues   │ │
│  └────────────────┘  └──────────────────┘  └─────────────┘ │
│           │                   │                     │        │
└───────────┼───────────────────┼─────────────────────┼────────┘
            │                   │                     │
            │             ┌─────▼─────────────────────▼──┐
            │             │   Agent Transport (TCP)      │
            │             └──────────────────────────────┘
            │                         │
            │                         │
┌───────────▼─────────────────────────▼──────────────────────┐
│                    Remote Agent                             │
│                                                             │
│  ┌───────────────┐  ┌────────────────┐  ┌───────────────┐ │
│  │ Runtime       │  │ Docker Scanner │  │  Local Store  │ │
│  │ - Protocol    │  │ - Compose Parse│  │  - Cache      │ │
│  │ - Background  │  │ - Issue Detect │  │  - Scan Data  │ │
│  │   Scanning    │  │                │  │               │ │
│  └───────────────┘  └────────────────┘  └───────────────┘ │
│           │                  │                   │         │
└───────────┼──────────────────┼───────────────────┼─────────┘
            │                  │                   │
            └──────────────────▼───────────────────┘
                          Docker Engine
```

## Key Features

### 1. Stack Profiles

**Problem:** Multiple compose files need to be managed together
**Solution:** Group compose files into logical stacks

```sql
docker_stacks:
  - name: "vpn-stack"
  - priority: 100
  - auto_restart: true
  - compose_files: [vpn.yml, routing.yml]
```

**Example:**
```bash
nazg docker stack create vpn-stack --server myserver --priority 100
nazg docker stack add-compose vpn-stack /path/to/vpn.yml --order 1
nazg docker stack add-compose vpn-stack /path/to/routing.yml --order 2
```

### 2. Dependency Graph

**Problem:** Services have complex dependencies that must be respected
**Solution:** Parse compose files and build dependency graph

```
Service Dependencies:
  transmission → vpn (network dependency)
  sonarr → vpn (network dependency)
  radarr → vpn (network dependency)
  nginx → transmission (reverse proxy)
  nginx → sonarr (reverse proxy)
```

**Stored in:**
- `docker_service_dependencies` - Service-to-service dependencies
- `docker_network_dependencies` - Network usage with static IPs

**Start Order Calculation:**
```
1. vpn (no dependencies)
2. transmission, sonarr, radarr (depend on vpn)
3. nginx (depends on services)
```

### 3. Orchestration Rules

**Problem:** Manual knowledge of restart order, health checks, etc.
**Solution:** Store rules that define orchestration behavior

**Rule Types:**
- `restart_order`: Define restart sequence
- `health_check`: Custom health validation
- `dependency`: Service relationships
- `custom`: User-defined logic

**Example Rules:**
```json
{
  "rule_type": "restart_order",
  "condition": {
    "service": "transmission",
    "action": "restart"
  },
  "action": {
    "restart_first": ["vpn"],
    "wait_for_health": true,
    "then_restart": ["transmission"]
  }
}
```

```json
{
  "rule_type": "health_check",
  "condition": {
    "service": "vpn",
    "health_check_fails": 3
  },
  "action": {
    "restart_service": "vpn",
    "notify": true
  }
}
```

### 4. Issue Detection & Phone-Home

**Problem:** Manual monitoring and problem detection
**Solution:** Agent detects issues and reports to master

**Issue Types:**
- `unhealthy`: Container health check failing
- `restart_loop`: Container restarting repeatedly
- `oom`: Out of memory killer
- `network_error`: Network connectivity issues
- `dependency_failed`: Dependent service down

**Flow:**
```
1. Agent detects unhealthy container
2. Agent phones home with issue details
3. Master logs issue in docker_issues
4. Master evaluates orchestration rules
5. Master decides action:
   a. Auto-fix if rule exists
   b. Notify user for manual action
   c. Log for trend analysis
```

### 5. Smart Actions

**Problem:** Complex multi-step operations done manually
**Solution:** High-level commands that handle complexity

#### Restart Stack
```bash
nazg docker stack restart vpn-stack
```

**What Nazg Does:**
1. Queries dependency graph for vpn-stack services
2. Calculates proper shutdown order (reverse dependency)
3. Stops services: nginx → transmission/sonarr/radarr → vpn
4. Starts services: vpn → wait for health → transmission/sonarr/radarr → nginx
5. Validates each service health before proceeding
6. Logs all actions to docker_orchestration_actions

#### Fix Issue
```bash
nazg docker issue fix <issue-id>
```

**What Nazg Does:**
1. Loads issue details
2. Evaluates applicable rules
3. Determines fix action (restart, recreate, dependency fix)
4. Executes fix with proper orchestration
5. Validates fix
6. Marks issue as resolved

### 6. Context-Aware Intelligence

**Uses Nexus DB Knowledge:**
- Historical restart patterns
- Service health trends
- Network usage patterns
- Resource consumption
- Issue frequency

**Example Smart Decisions:**
- "VPN restarts frequently after 7 days uptime" → Schedule preemptive restart
- "Transmission always fails when VPN is unhealthy" → Auto-restart VPN first
- "Nginx needs 10s to be healthy after restart" → Adjust health check timeout

## Database Schema

### Stack Management
```sql
docker_stacks                    -- Stack definitions
docker_stack_compose_files       -- Compose files in stacks
docker_service_dependencies      -- Service→Service dependencies
docker_network_dependencies      -- Service→Network mappings
```

### Intelligence
```sql
docker_orchestration_rules       -- User-defined rules
docker_issues                    -- Detected issues
docker_orchestration_actions     -- Action history/audit log
```

## Usage Examples

### Setup a Complex Stack (Auto-Discovery Method)

The easiest way - let Nazg parse your compose files and figure out dependencies automatically:

```bash
# Feed nazg your compose files, it handles the rest
nazg docker stack create-from-compose vpn-media-stack --server myserver \
  --compose /opt/docker/gluetun.yml \
  --compose /opt/docker/media.yml \
  --compose /opt/docker/traefik.yml \
  --description "VPN-routed media stack with Traefik"

# That's it! Nazg has:
# ✓ Parsed all compose files
# ✓ Detected that transmission/sonarr/radarr use gluetun's network
# ✓ Detected Traefik accesses services via web network
# ✓ Stored all dependencies in the database
# ✓ Built the dependency graph
# ✓ Ready for intelligent orchestration
```

### Example: Gluetun Multi-Network Setup

Your actual setup with gluetun having two networks:

**gluetun.yml:**
```yaml
version: "3.8"
services:
  gluetun:
    image: qmcgaw/gluetun
    container_name: gluetun
    cap_add:
      - NET_ADMIN
    networks:
      vpn_network:
        ipv4_address: 172.20.0.2
      web_network:
        ipv4_address: 172.21.0.2
    restart: unless-stopped
```

**media.yml:**
```yaml
version: "3.8"
services:
  transmission:
    image: linuxserver/transmission
    container_name: transmission
    network_mode: "service:gluetun"  # Uses gluetun's network!
    restart: unless-stopped

  sonarr:
    image: linuxserver/sonarr
    container_name: sonarr
    network_mode: "service:gluetun"
    restart: unless-stopped

  radarr:
    image: linuxserver/radarr
    container_name: radarr
    network_mode: "service:gluetun"
    restart: unless-stopped
```

**traefik.yml:**
```yaml
version: "3.8"
services:
  traefik:
    image: traefik:latest
    container_name: traefik
    networks:
      - web_network  # Connects to gluetun's web network
    restart: unless-stopped

networks:
  vpn_network:
    external: true
  web_network:
    external: true
```

**What Nazg Detects:**
1. `transmission` → `gluetun` (network_mode dependency) **HARD**
2. `sonarr` → `gluetun` (network_mode dependency) **HARD**
3. `radarr` → `gluetun` (network_mode dependency) **HARD**
4. `traefik` → `gluetun` (shared web_network) **SOFT**
5. `gluetun` networks: vpn_network (172.20.0.2), web_network (172.21.0.2)

**Restart Order Calculated by Nazg:**
```
Stop order (reverse):
  traefik → transmission/sonarr/radarr → gluetun

Start order (dependencies first):
  gluetun → wait for health → transmission/sonarr/radarr → traefik
```

### Setup a Complex Stack (Manual Method - Old Way)

```bash
# Create stack
nazg docker stack create media-stack --server myserver \
  --description "Media stack with VPN routing"

# Add compose files in order
nazg docker stack add-compose media-stack \
  /opt/docker/vpn.yml --order 1 --env /opt/docker/vpn.env

nazg docker stack add-compose media-stack \
  /opt/docker/media.yml --order 2 --env /opt/docker/media.env

nazg docker stack add-compose media-stack \
  /opt/docker/reverse-proxy.yml --order 3

# Add dependency rules
nazg docker rule add myserver \
  --type restart_order \
  --service transmission \
  --depends-on vpn \
  --wait-for-health

# Enable auto-restart on failure
nazg docker stack update media-stack --auto-restart true
```

### Daily Operations

```bash
# View stack status
nazg docker stack status vpn-media-stack

# Restart entire stack intelligently
nazg docker stack restart vpn-media-stack

# Restart just one service (respects dependencies)
nazg docker service restart transmission

# View active issues
nazg docker issues list --server myserver

# Auto-fix issues based on rules
nazg docker issues auto-fix --server myserver

# View action history
nazg docker actions list --server myserver --limit 50
```

### What Happens When You Restart VPN Containers

**You run:** `nazg docker service restart gluetun`

**Nazg's intelligent behavior:**

1. **Query Dependencies** (from database)
   ```
   Found containers using gluetun's network:
     - transmission (network_mode: "service:gluetun")
     - sonarr (network_mode: "service:gluetun")
     - radarr (network_mode: "service:gluetun")
   Found containers on shared network:
     - traefik (web_network)
   ```

2. **Calculate Impact**
   ```
   Hard dependencies (must restart): transmission, sonarr, radarr
   Soft dependencies (may need restart): traefik
   ```

3. **Prompt User** (or auto-execute if rule exists)
   ```
   Restarting gluetun will affect 3 dependent services:
     • transmission (uses gluetun network)
     • sonarr (uses gluetun network)
     • radarr (uses gluetun network)

   Restart strategy:
     1. Stop dependent services
     2. Stop gluetun
     3. Start gluetun, wait for health
     4. Start transmission
     5. Start sonarr
     6. Start radarr

   Proceed? [Y/n]:
   ```

4. **Execute with Logging**
   ```
   [13:45:01] Stopping transmission...               ✓
   [13:45:02] Stopping sonarr...                     ✓
   [13:45:03] Stopping radarr...                     ✓
   [13:45:04] Stopping gluetun...                    ✓
   [13:45:06] Starting gluetun...                    ✓
   [13:45:07] Waiting for gluetun health...          ✓ (10s)
   [13:45:17] Starting transmission...               ✓
   [13:45:18] Starting sonarr...                     ✓
   [13:45:19] Starting radarr...                     ✓
   [13:45:20] Verifying all services healthy...      ✓

   Stack restart completed successfully!
   ```

5. **Log to Database**
   ```sql
   INSERT INTO docker_orchestration_actions
     (action_type='restart', target='gluetun',
      reason='user_request', triggered_by='manual',
      status='success', ...);
   ```

**Contrast with manual approach:**
```bash
# What you'd have to do manually:
docker stop transmission sonarr radarr  # Hope you remember all of them!
docker stop gluetun
docker start gluetun
# Wait... is it healthy? How long?
sleep 10  # Guess?
docker start transmission
docker start sonarr
docker start radarr
# Did they all start correctly? Check each one!
docker ps --filter name=transmission
docker ps --filter name=sonarr
docker ps --filter name=radarr
```

### Advanced: Rule-Based Automation

```bash
# Add rule: If VPN unhealthy, restart it and wait before dependent services
nazg docker rule add myserver \
  --type health_check \
  --condition '{"service":"vpn","health":"unhealthy","duration":60}' \
  --action '{"restart":"vpn","wait_seconds":30,"then_restart_dependents":true}'

# Add rule: Preemptive restart based on uptime
nazg docker rule add myserver \
  --type maintenance \
  --condition '{"service":"vpn","uptime_days":7}' \
  --action '{"restart":"vpn","schedule":"2am"}'
```

## Implementation Status

### ✅ Completed (Phase 1 & 2)
- Agent-side Docker scanning
- Local SQLite cache
- Background scanning
- Protocol messaging (DockerFullScan, Register)
- Control center client
- Master database storage
- JSON parsing and storage

### 🔨 In Progress (Phase 3 & 4)
- Stack profile management
- Dependency graph parser
- Orchestration engine
- Rules system
- Issue detection
- Smart restart logic

### 📋 Next Steps
1. Finish orchestrator implementation
2. Add CLI commands for stack management
3. Implement compose file parser
4. Add topological sort for dependency ordering
5. Build rules evaluation engine
6. Add agent-side issue detection
7. Create phone-home mechanism for issues

## Benefits

1. **Simplicity:** `nazg docker stack restart vpn-stack` vs complex compose commands
2. **Safety:** Respects dependencies, waits for health, handles failures
3. **Intelligence:** Learns patterns, auto-fixes issues, suggests improvements
4. **Visibility:** Full audit log, issue tracking, trend analysis
5. **Automation:** Rules-based orchestration, scheduled maintenance, auto-recovery

## Future Enhancements

- Web UI for visual dependency graphs
- Slack/Discord notifications for issues
- Predictive issue detection using ML on historical data
- Cross-server orchestration (coordinate services across multiple hosts)
- Resource optimization suggestions
- Automatic rollback on failed updates
