# Roundtable

Editor-agnostic debugging suite in your terminal with reusable debug profiles.

Roundtable is a terminal debugger UI built around the Debug Adapter Protocol. The current focus is CodeLLDB-backed native debugging, that can be launched Standalone, from Neovim, and from VS Code.

> Status: early alpha. The Linux path is the primary development target right now. Windows and macOS support is intended, and some path/config code is already cross-platform, but it needs real testing & validation.

## Highlights

- Standalone terminal UI with locals, watches, memory, disassembly, breakpoints, threads, and stack panes.
- Reusable launch profiles in `roundtable.toml`.
- CodeLLDB auto-detection for common Mason and VS Code extension installs, plus explicit config overrides.
- Neovim integration through [`roundtable.nvim`](https://github.com/Saladin1812/roundtable.nvim).
- Mock mode for contributors and UI smoke testing without a debugger session.

## Install

Requirements:

- `curl` or `wget`
- CodeLLDB for real debug sessions

Install the current alpha release:

```bash
curl -sS https://raw.githubusercontent.com/Saladin1812/Roundtable/main/install.sh | sh -s -- --version v0.1.0-alpha.2
```

Install the latest stable release, once stable releases are available:

```bash
curl -sS https://raw.githubusercontent.com/Saladin1812/Roundtable/main/install.sh | sh
```

## Build From Source

Requirements:

- CMake 3.20+
- C++20 compiler
- Git (for CMake `FetchContent` dependencies)
- CodeLLDB for real debug sessions

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target roundtable
```

Run your local build:

```bash
./build/roundtable
```

Optional test build:

```bash
cmake --build build --target roundtable_tests
./build/roundtable_tests
```

## Quick Start

Use Roundtable from your editor:

- Neovim: install [`roundtable.nvim`](https://github.com/Saladin1812/roundtable.nvim) to integrate with `nvim-dap`, generate Roundtable configs from your Neovim debugging context, and launch Roundtable easily.
- VS Code: extension support is WIP; the intended flow is to generate a temporary Roundtable config from VS Code launch/breakpoint context.

Launch Roundtable dashboard:

```bash
roundtable
```

Launch a binary directly:

```bash
roundtable ./path/to/mybinary
```

Create a user config:

```bash
roundtable --init-config
```

Show CLI help:

```bash
roundtable --help
```

Use the demo/mock UI mode:

```bash
roundtable --mock
```

## Configuration

Roundtable looks for config in this order:

- `./roundtable.toml`
- platform user config paths such as `~/.config/roundtable/roundtable.toml`
- an explicit `--config path`, when provided

Generate a clean user config with:

```bash
roundtable --init-config
```

Use `roundtable.toml.example` as a demo/sample reference. It intentionally contains mock/demo settings and sample profiles; it is not the same thing as the generated user config.

## Launch Profiles

Profiles let you switch targets without rewriting your config.

Example:

```toml
[profiles.app]
program = "./build/my_app"
arguments = []
working_directory = "."
stop_on_entry = true
continue_once = false
watches = ["argc", "argv"]
breakpoints = ["src/main.cpp:42"]
```

Run a profile:

```bash
roundtable --profile app
```

Inside the TUI, use the dashboard profile picker or `Space P`.

## CodeLLDB

Roundtable uses CodeLLDB through DAP. You can either let Roundtable auto-detect it or configure paths explicitly.

```toml
[dap_launch]
command = ""
liblldb_path = ""

[codelldb.auto_detect]
enabled = true
candidate_roots = []
```

If auto-detection fails, set:

```toml
[dap_launch]
command = "/path/to/codelldb"
liblldb_path = "/path/to/liblldb.so"
```

The exact library filename differs by platform.

## Contributor Notes

Mock mode exists for UI development and contributor smoke testing:

```bash
roundtable --mock
```

Sample targets:

```bash
cmake --build build --target roundtable_dap_sample
roundtable --config roundtable.toml.example --profile sample
```

The DAP probe target is mainly for focused DAP/CodeLLDB experiments:

```bash
cmake --build build --target roundtable_dap_probe
```
