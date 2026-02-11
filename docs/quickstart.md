# Quickstart Guide

Get up and running with Nazg in under a minute.

---

## 1. Install (pre-built binary)

Download the latest release and drop it into your PATH:

```bash
curl -fsSL https://github.com/purpleneutral/nazg/releases/latest/download/nazg-linux-x86_64 \
  -o ~/.local/bin/nazg
chmod +x ~/.local/bin/nazg
```

Make sure `~/.local/bin` is on your `PATH`. If it is not, add this to your shell profile:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

---

## 2. Install (from source)

**Prerequisites:** GCC 9+ or Clang 10+, CMake 3.16+, libsqlite3-dev, libcurl4-openssl-dev, libssl-dev.

```bash
# Install build dependencies (Debian / Ubuntu)
sudo apt-get update
sudo apt-get install -y build-essential cmake libsqlite3-dev \
  libcurl4-openssl-dev libssl-dev git

# Clone and build
git clone https://github.com/purpleneutral/nazg.git
cd nazg
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)

# Optionally install system-wide
sudo cp build/nazg /usr/local/bin/
```

---

## 3. First run

Point Nazg at any project directory and launch it without arguments to enter assistant mode:

```bash
cd ~/my-project
nazg
```

Example output:

```
Hi! I'm Nazg. How can I help?
Facts
  Current directory  my-project
  Language detected  python
  Git repository     yes
  Changes            2 modified, 0 staged, 1 untracked

Actions
  0. Show all available commands
  1. View project status summary
  2. Build project
  3. Commit changes
  4. Update nazg
  5. Exit
```

Nazg detects your project language, build system, and git state, then offers relevant actions.

---

## 4. Direct commands

Skip the assistant menu by passing a command directly:

| Command | What it does |
|---------|--------------|
| `nazg status` | Print a project summary (language, build system, git info). |
| `nazg build` | Build the project using the detected build system. |
| `nazg git-status` | Show branch, upstream, divergence, and working-tree changes. |
| `nazg tui` | Launch the full terminal user interface. |
| `nazg why` | Explain the rationale behind the last planned action. |
| `nazg info` | Display Nazg version, data paths, and loaded configuration. |

Run `nazg commands` to list every registered command.

---

## 5. Configuration

Nazg works without a config file. When you want to customise behaviour, copy the annotated reference config:

```bash
mkdir -p ~/.config/nazg
cp examples/config.toml ~/.config/nazg/config.toml
```

Edit the file to uncomment and adjust the settings you need. See `docs/config.md` for full documentation of the configuration subsystem.

---

## 6. Updating

Nazg can update itself when built from source:

```bash
nazg update
```

Use `nazg update --help` for rollback and version-pinning options.

---

## 7. What next?

Explore the deeper documentation to understand how Nazg thinks and operates:

- [Engine lifecycle and command dispatch](engine.md)
- [Brain: detector, snapshotter, and planner internals](brain.md)
- [Configuration sources and usage](config.md)
