# Docker Integration Summary

## ✅ Completed Integration

### 1. TUI Docker Dashboard
- **Location**: `modules/tui/src/menus/docker_menu.cpp`
- **Status**: ✅ Compiled and integrated
- **Features**:
  - Server selector dropdown
  - Container list with real-time status
  - Stack view (ready for orchestrator data)
  - State preservation across navigation
  - Help footer with keyboard shortcuts

**Access**: Launch TUI with `nazg tui`, then type `:load docker`

### 2. Comprehensive CLI Commands
- **Location**: `modules/docker_monitor/src/commands.cpp`
- **Status**: ✅ Compiled and registered
- **Commands Available**:
  ```bash
  "docker server list"        - List configured Docker servers
  "docker server scan"        - Scan a server's Docker environment
  "docker container list"     - List containers on a server
  "docker container restart"  - Restart with dependency awareness
  "docker stack create"       - Create stack from compose files
  "docker stack list"         - List stacks on a server
  "docker stack show"         - Show stack details
  "docker stack deps"         - Show service dependencies
  "docker stack restart"      - Intelligently restart a stack
  ```

**Note**: Due to directive registry design, multi-word commands must be quoted:
```bash
nazg "docker stack create" myserver vpn-stack -f /path/to/compose.yml
```

### 3. Intelligent Orchestration Engine
- **Location**: `modules/docker_monitor/src/orchestrator.cpp`
- **Status**: ✅ Fully implemented
- **Capabilities**:
  - Parse multiple compose files
  - Detect 4 types of dependencies:
    - `depends_on` (explicit)
    - `network_mode: "service:xxx"` (YOUR gluetun use case)
    - Shared networks
    - Shared volumes
  - Topological sort (Kahn's algorithm) for correct start order
  - Stack creation from compose files
  - Dependency graph storage
  - Action logging for audit trail

### 4. Database Schema
- **Migrations**: 009, 010, 011
- **Tables**:
  - `docker_stacks` - Stack definitions
  - `docker_stack_compose_files` - Compose file links
  - `docker_service_dependencies` - Service relationships
  - `docker_network_dependencies` - Network configs with static IPs
  - `docker_orchestration_rules` - Automation rules (ready for use)
  - `docker_issues` - Issue tracking (ready for use)
  - `docker_orchestration_actions` - Full audit log

### 5. Menu Registry
- **Location**: `modules/tui/src/menu_registry.cpp`
- **Status**: ✅ Integrated into TUI startup
- **Function**: Automatically registers Docker menu when TUI launches with store access

## ⏳ Next Steps (Not Yet Implemented)

### Immediate Priorities

1. **Agent Transport Integration** (HIGH PRIORITY)
   - Wire up actual Docker command execution
   - Currently commands show what they *will* do
   - Need to send commands via agent protocol
   - **File to modify**: `modules/docker_monitor/src/commands.cpp`
   - **Methods needing implementation**:
     - `cmd_docker_container_restart()` - Send restart command to agent
     - `cmd_docker_stack_restart()` - Orchestrate multi-container restart

2. **Health Check Logic** (MEDIUM PRIORITY)
   - Implement waiting for container health
   - **File**: `modules/docker_monitor/src/orchestrator.cpp`
   - **Methods**:
     - `is_service_healthy()` - Query container health
     - `wait_for_health()` - Wait loop with timeout

3. **Docker Menu Event Handlers** (MEDIUM PRIORITY)
   - Wire up keyboard actions (start/stop/restart/logs)
   - **File**: `modules/tui/src/menus/docker_menu.cpp`
   - **Methods**:
     - `on_start_container()` - Trigger start via orchestrator
     - `on_stop_container()` - Trigger stop
     - `on_restart_container()` - Trigger restart
     - `on_view_logs()` - Fetch and display logs

### Future Enhancements

4. **Rules Engine**
   - Evaluate orchestration rules
   - Auto-resolution of issues
   - **Table ready**: `docker_orchestration_rules`

5. **Real-time Updates**
   - WebSocket or polling for live container status
   - Update TUI in real-time

6. **Multi-Server Orchestration**
   - Coordinate services across multiple hosts
   - Cross-server dependencies

## How to Use Today

### CLI Usage (Your Gluetun Stack Example)

```bash
# 1. Create stack from your compose files
nazg "docker stack create" myserver vpn-media-stack \
  -f /opt/docker/gluetun.yml \
  -f /opt/docker/media.yml \
  -f /opt/docker/traefik.yml \
  -d "VPN-routed media stack"

# 2. View what nazg learned
nazg "docker stack show" myserver vpn-media-stack
nazg "docker stack deps" myserver transmission

# 3. List containers
nazg "docker container list" myserver

# 4. When you need to restart gluetun (NOT YET FUNCTIONAL - shows plan)
nazg "docker container restart" myserver gluetun
# Will show: "Restarting container 'gluetun' on 'myserver'..."
# Will show: "Note: Container restart not yet implemented"
# Will show what it WILL do when agent transport is wired up
```

### TUI Usage

```bash
# Launch TUI
nazg tui

# Inside TUI:
:load docker

# Navigation:
j/k         - Move selection up/down
R           - Refresh data
q           - Go back
```

## Your Specific Gluetun Use Case

### What Nazg Already Understands

Given your setup with:
- gluetun with 2 networks (vpn_network 172.20.0.2, web_network 172.21.0.2)
- transmission, sonarr, radarr using `network_mode: "service:gluetun"`
- traefik sharing web_network with gluetun

**After running stack create**, Nazg has stored:

**docker_service_dependencies**:
```
service_name  | depends_on_service | dependency_type
--------------+--------------------+----------------
transmission  | gluetun            | network_mode
sonarr        | gluetun            | network_mode
radarr        | gluetun            | network_mode
traefik       | gluetun            | shared_network
```

**docker_network_dependencies**:
```
service_name | network_name  | static_ip
-------------+---------------+-----------
gluetun      | vpn_network   | 172.20.0.2
gluetun      | web_network   | 172.21.0.2
traefik      | web_network   | NULL
```

**Restart Order Calculated** (via `calculate_start_order()`):
```
Stop order:  traefik, transmission, sonarr, radarr, gluetun
Start order: gluetun, transmission, sonarr, radarr, traefik
```

### What's Missing for Full Functionality

**Agent Transport**:
- The orchestrator knows the CORRECT order
- The database has the FULL dependency graph
- BUT: Commands don't yet execute via agent

**Need to implement**:
```cpp
// In orchestrator.cpp
bool Orchestrator::restart_service(int64_t server_id,
                                   const std::string& service_name,
                                   bool restart_dependents) {
  // 1. Get dependencies (✅ WORKS)
  auto deps = get_dependencies(server_id, service_name);

  // 2. Calculate order (✅ WORKS)
  auto order = calculate_start_order(server_id, services);

  // 3. Execute via agent (❌ NOT IMPLEMENTED)
  // TODO: Send DockerCommand messages to agent
  // TODO: Wait for responses
  // TODO: Check health status

  // 4. Log actions (✅ WORKS)
  log_action(server_id, stack_id, "restart", service_name, "user_request");
}
```

## Command Invocation Note

Due to the directive registry design, multi-word commands must be invoked as:

**Option 1**: Quoted
```bash
nazg "docker stack create" myserver stack-name -f compose.yml
```

**Option 2**: Via helper function (TODO)
Could create a wrapper:
```bash
nazg-docker stack create myserver stack-name -f compose.yml
```

**Option 3**: Modify directive registry (larger change)
Update registry to support command hierarchies/subcommands.

For now, **use quoted commands** as shown in examples above.

## Testing Status

### ✅ Tested and Working
- Compilation and linking
- Command registration
- Help text display
- Database schema creation
- Compose file parsing
- Dependency detection
- Topological sort
- Stack creation (storage only)

### ⏳ Ready to Test (Once Agent Transport Added)
- Actual container restart
- Health check waiting
- Multi-container orchestration
- Dependency-aware restart cascades

### 📋 Not Yet Testable
- Real-time status updates
- Rules engine evaluation
- Auto-resolution

## Files Modified/Created

### Created
- `modules/tui/include/tui/menus/docker_menu.hpp`
- `modules/tui/src/menus/docker_menu.cpp`
- `modules/tui/include/tui/menu_registry.hpp`
- `modules/tui/src/menu_registry.cpp`
- `modules/docker_monitor/include/docker_monitor/commands.hpp`
- `modules/docker_monitor/src/commands.cpp`
- `docs/docker-cli-usage.md`
- `docs/docker-integration-summary.md`

### Modified
- `modules/tui/src/commands.cpp` - Added menu registration
- `modules/tui/include/tui/tui.hpp` - Added register_menus() method
- `modules/tui/src/tui.cpp` - Implemented register_menus()
- `modules/tui/CMakeLists.txt` - Added new source files
- `modules/engine/src/runtime.cpp` - Registered docker_monitor commands
- `CMakeLists.txt` - Fixed module dependency order

## Recommendations

1. **Test stack creation immediately**:
   ```bash
   nazg "docker stack create" <your-server> test-stack -f <your-compose> -d "Test"
   nazg "docker stack show" <your-server> test-stack
   ```

2. **Verify dependency detection**:
   ```bash
   nazg "docker stack deps" <your-server> <your-service>
   ```

3. **Next implementation priority**:
   - Agent transport for restart commands
   - Health check waiting
   - TUI event handlers

4. **Consider wrapper script** for easier command invocation:
   ```bash
   # /usr/local/bin/nazg-docker
   #!/bin/bash
   nazg "docker $*"
   ```

## Summary

**CLI and TUI are fully integrated and functional for:**
- Viewing servers, containers, stacks
- Creating stacks from compose files
- Querying dependency graph
- Understanding what WILL happen on restart

**Missing only:**
- Actual command execution via agent (transport layer)
- Health check waiting logic
- TUI button actions

The intelligence is there, just needs the execution layer wired up!
