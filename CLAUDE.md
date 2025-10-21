# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C99 application template demonstrating async I/O patterns using libuv and libcurl. Contains two example programs:
- **main.c**: Advanced async multi-download manager integrating libuv event loop with libcurl multi interface
- **main2.c**: Simple HTTP client with libuv timer demonstration

## Build System

### Prerequisites
```bash
brew install libuv curl cmake
```

### Build Commands
```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run executable
./build/bin/spdtest [urls...]
```

### Build Artifacts
- Binary output: `build/bin/spdtest`
- CMake uses out-of-source builds in `build/` directory
- Never commit `build/` directory

## Code Architecture

### Async I/O Integration Pattern (main.c)

The core architecture integrates two event-driven systems:

**libuv ↔ libcurl Integration Flow:**
1. `curl_multi_setopt()` registers callbacks for socket events and timeouts
2. `handle_socket()` creates `curl_context_t` wrappers and maps curl sockets to uv_poll_t handles
3. Socket activity triggers `curl_perform()` → `curl_multi_socket_action()` → processes transfers
4. Completed transfers detected in `check_multi_info()` via `curl_multi_info_read()`

**Key Components:**
- `curl_context_t`: Bridges curl socket descriptors with libuv poll handles
- `handle_socket()`: CURLMOPT_SOCKETFUNCTION callback - creates/destroys poll contexts
- `start_timeout()`: CURLMOPT_TIMERFUNCTION callback - manages transfer timeouts
- `curl_perform()`: uv_poll callback - drives curl transfer progress
- `check_multi_info()`: Polls for completed transfers and cleanup

**Memory Management:**
- Socket contexts created in `create_curl_context()`, destroyed via `curl_close_cb()`
- File handles opened in `add_download()` (note: current implementation doesn't close files)
- curl_multi handles assigned socket data via `curl_multi_assign()`

### Code Style

Project uses `.clang-format` configuration:
- LLVM-based style with Allman braces
- 2-space indentation, 100 char line limit
- No short functions/statements on single lines
- Format code before committing: `clang-format -i *.c`

## Development Notes

### Adding New Downloads
The multi-download pattern expects URLs as command-line arguments. Each URL creates a numbered file (`N.download`). To modify download behavior, focus on `add_download()` function.

### Event Loop Integration
When extending functionality, maintain the separation between:
- libuv event loop management (UV_READABLE/UV_WRITABLE events)
- libcurl multi interface socket operations (CURL_POLL_IN/OUT/REMOVE)
- Callback chains: socket events → poll handlers → curl actions → completion checks

### Platform Considerations
CMake searches for libuv in `/usr/local` and `/opt/homebrew` paths (macOS-focused). Adjust `CMakeLists.txt` paths for Linux/other platforms.
