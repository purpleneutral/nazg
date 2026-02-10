# Docker Monitoring System - Implementation Progress

## Executive Summary

Foundation for docker monitoring via phone-home agents has been established. The system enables nazg to monitor docker containers across remote servers without repeated SSH overhead.

## What's Been Done

### ✅ Database Schema (nexus)
- **File**: `modules/nexus/migrations/009_docker_monitoring.sql`
- **Tables Added**:
  - `servers` - Track remote hosts running docker
  - `containers` - Current state of all containers
  - `compose_files` - Docker Compose file tracking
  - `docker_images` - Images on each server
  - `docker_networks` - Networks on each server
  - `docker_volumes` - Volumes on each server
  - `docker_status_history` - Timeline of container state changes
  - `agent_updates` - Audit trail of agent communications
  - `docker_commands` - Command history (for future control operations)

### ✅ Database API (nexus::Store)
- **File**: `modules/nexus/include/nexus/store.hpp`
- **Methods Added**: ~30 methods for:
  - Server management (add, get, list, update heartbeat, delete)
  - Container tracking (upsert, list, get by name/id, cleanup)
  - Compose file tracking
  - Image/network/volume tracking
  - Status history logging
  - Agent update logging
  - Command logging (phase 2)

**Note**: Implementation stubs need to be added to `store.cpp` (reference implementation provided in `modules/nexus/src/docker_monitoring.cpp`)

### ✅ Agent Protocol Extensions
- **File**: `modules/agent/include/agent/protocol.hpp`
- **New Message Types**:
  - `Register` / `RegisterAck` - Agent registration with control center
  - `DockerFullScan` - Complete docker environment snapshot
  - `DockerEvent` - Single container event (start, stop, restart, etc.)
  - `DockerIncrementalUpdate` - Incremental state updates
  - `DockerCommand` / `DockerCommandResult` - For phase 2 (control operations)

- **JSON Payload Schemas** documented in protocol.hpp comments

### ✅ Docker Scanner Interface
- **File**: `modules/agent/include/agent/docker_scanner.hpp`
- **Capabilities**:
  - List containers with full metadata
  - Inspect individual containers
  - List images, networks, volumes
  - Discover compose files in common locations
  - Generate full scan JSON payload
  - Extract system information (hostname, OS, docker version)

### ✅ Documentation
- **File**: `docs/docker-monitoring.md` - Complete system design
- Protocol message examples with JSON schemas
- Security considerations
- Configuration examples for agent and nazg

## What's Left to Do

### 🔧 Implementation Tasks

#### 1. Docker Scanner Implementation
**File**: `modules/agent/src/docker_scanner.cpp`
**Effort**: 4-6 hours

Need to implement:
- `exec_command()` - Run docker CLI commands, capture output
- `list_containers()` - Parse `docker ps --format json` or `docker ps` output
- `inspect_container()` - Parse `docker inspect` JSON output
- `list_images/networks/volumes()` - Similar parsing
- `discover_compose_files()` - Find and hash compose files
- `generate_full_scan_json()` - Assemble complete payload

**Key Commands**:
```bash
docker ps --no-trunc --format '{{json .}}'
docker inspect <container>
docker images --format '{{json .}}'
docker network ls --format '{{json .}}'
docker volume ls --format '{{json .}}'
```

#### 2. Agent Phone-Home Mode
**Files**:
- `modules/agent/src/runtime.cpp` (modify)
- `modules/agent/src/phone_home.cpp` (new)

**Effort**: 3-4 hours

Changes needed:
- Add config for control center host/port
- Reverse connection: agent connects to nazg (not listen mode)
- Send `Register` message on startup
- Periodic `DockerFullScan` every N minutes (configurable)
- Event monitoring loop (watch `docker events`)

#### 3. Control Center Receiver
**Files**:
- `modules/controlCenter/src/receiver.cpp` (enhance existing or create new)
- `modules/controlCenter/src/message_handler.cpp` (new)

**Effort**: 4-5 hours

Need to:
- Listen for incoming agent connections
- Handle `Register` messages (create/update server in nexus)
- Handle `DockerFullScan` messages (upsert containers, compose files, etc.)
- Handle `DockerEvent` messages (log to docker_status_history)
- Mark agent_updates as processed
- Update server heartbeat timestamps

#### 4. Store Method Implementations
**File**: `modules/nexus/src/store.cpp`

**Effort**: 2-3 hours

Append implementations from `docker_monitoring.cpp` to `store.cpp` before the log helper methods. Follow existing patterns.

#### 5. CLI Commands
**Files**:
- `modules/directive/src/docker_commands.cpp` (new)
- Register in `modules/engine/src/engine.cpp`

**Effort**: 2-3 hours

Commands to add:
```bash
nazg server add <label> <host> [--ssh-key PATH]
nazg server list
nazg server status <label>

nazg docker list [--server LABEL]
nazg docker status <container> [--server LABEL]
nazg docker history <container> --server LABEL
nazg docker events [--server LABEL] [--follow]
```

### 🧪 Testing & Integration

#### Phase 1: Local Testing
1. Build nazg with new migrations
2. Run nazg, verify `servers` table exists
3. Manually insert test server
4. Query with new store methods

#### Phase 2: Agent Testing
1. Build `nazg-agent` with docker scanner
2. Run scanner standalone, verify JSON output
3. Test agent phone-home to local nazg instance

#### Phase 3: End-to-End
1. Deploy agent to media server
2. Configure control center host
3. Start agent, verify registration in nazg
4. Wait for full scan, verify containers in database
5. Test CLI: `nazg docker list --server media-server`
6. Restart a container on media server, verify event captured

## Next Steps (Recommended Order)

1. **Implement Docker Scanner** - Core data collection
   - Can be tested independently with unit tests
   - Critical for all other work

2. **Implement Store Methods** - Database persistence
   - Needed before control center can store data
   - Can be tested with direct nexus calls

3. **Implement Control Center Receiver** - Message handling
   - Receives and processes agent messages
   - Uses store methods to persist data

4. **Modify Agent for Phone-Home** - Connection initiation
   - Connects to control center
   - Uses scanner to gather data
   - Sends via protocol

5. **Add CLI Commands** - User interface
   - Query and display monitoring data
   - User-facing feature

6. **End-to-End Testing** - Validation
   - Deploy to real environment
   - Verify full workflow

## Configuration Files

### Agent Config (`/etc/nazg/agent.toml` on remote server)
```toml
[agent]
server_label = "media-server"
control_center_host = "nazg-controller.local"  # Your nazg machine
control_center_port = 7070
heartbeat_interval_sec = 30
scan_interval_sec = 300

[docker]
enabled = true
scan_compose_paths = ["/opt", "/srv", "/home/*/docker"]
include_env_vars = false
```

### Nazg Config (`~/.config/nazg/config.toml` on control machine)
```toml
[controlCenter]
enabled = true
bind_address = "0.0.0.0"
port = 7070
require_auth = true
auth_token = "your-secret-token-here"

[docker]
retention_history_days = 90
```

## Timeline Estimate

- **Scanner + Store**: 6-9 hours
- **Agent + Control Center**: 7-9 hours
- **CLI + Testing**: 4-5 hours
- **Total**: 17-23 hours of focused work

## Current Status

**Phase 1: Monitor Mode** - 40% Complete

Foundation is solid. The hard design work (schema, protocol, interfaces) is done.
Remaining work is straightforward implementation following established patterns.

## Usage Example (Once Complete)

```bash
# On nazg controller machine
$ nazg server add media-server tank@10.0.0.4 --ssh-key ~/.ssh/id_ed25519

# On media server
$ sudo systemctl start nazg-agent

# Back on controller
$ nazg server list
┌───────────────┬────────────────┬────────┬──────────────┐
│ Label         │ Host           │ Status │ Last Seen    │
├───────────────┼────────────────┼────────┼──────────────┤
│ media-server  │ tank@10.0.0.4  │ online │ 5 seconds ago│
└───────────────┴────────────────┴────────┴──────────────┘

$ nazg docker list --server media-server
┌──────────┬────────────────────────┬─────────┬────────────┐
│ Name     │ Image                  │ State   │ Status     │
├──────────┼────────────────────────┼─────────┼────────────┤
│ plex     │ linuxserver/plex:latest│ running │ Up 3 days  │
│ sonarr   │ linuxserver/sonarr     │ running │ Up 3 days  │
│ radarr   │ linuxserver/radarr     │ running │ Up 3 days  │
└──────────┴────────────────────────┴─────────┴────────────┘

$ nazg docker status plex --server media-server
Container: plex (abc123def456)
Image:     linuxserver/plex:latest
State:     running
Status:    Up 3 days (healthy)
Restart:   unless-stopped
Ports:     32400:32400/tcp
Volumes:   /tank/media:/data:ro
Compose:   /opt/media/docker-compose.yml (service: plex)
```
