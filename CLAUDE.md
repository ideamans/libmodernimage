# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Full build (deps + library + tests)
./scripts/build-deps.sh        # libwebp, libaom, libavif
./scripts/build.sh             # libmodernimage (.a, .so/.dylib) + tests

# Run tests
./build/test_binary_equiv      # CLI vs bridge byte-for-byte comparison (19 tests)
./build/test_thread_safety     # concurrency + memory (7 tests)

# Generate metadata
./scripts/generate-cli-compat.sh   # build/cli-compat.json
./scripts/package.sh               # build/dist/libmodernimage-{platform}.tar.gz

# Clean rebuild
rm -rf build && ./scripts/build.sh
```

Deps only need rebuilding when submodules change. `build.sh` calls `cmake --install` to create the fat `.a`.

## Architecture

### Bridge Pattern (zero upstream modification)

Each tool's `main()` is compiled into the library via `#define main` + `#include`:

```
src/bridge_cwebp.c:
  1. Include all headers the original needs (with real stdio)
  2. Block re-inclusion via include guards (#define WEBP_EXAMPLES_UNICODE_H_ etc.)
  3. Provide stub macros (unicode.h, stopwatch.h)
  4. #define main modernimage_cwebp_main
  5. #include "../deps/libwebp/examples/cwebp.c"
```

The bridges must pre-include all headers and provide compatible macro stubs because the `#define` applies to everything in the included source. `gifdec.c` is compiled as a separate source file (not in any libwebp `.a`).

### fd-level Capture (dup2/pipe)

All tool execution goes through `modernimage.c`:

```
context_new → [set_stdin] → cwebp/gif2webp/avifenc → get output → reset/free
                                    │
                          g_io_mutex lock
                          dup2(pipe → stdout/stderr)
                          [spawn stdin writer thread]
                          call tool_main(argc, argv)
                          restore fds
                          drain pipes → context buffers
                          mutex unlock
```

A **single global mutex** serializes all calls because `dup2` is process-wide. Separate contexts own separate buffers, so output never leaks between callers.

### Fat Static Library

`cmake --install` merges all `.a` files into one `libmodernimage.a`:
- macOS: `libtool -static`
- Linux/Windows: `ar -M` with MRI script (generated via CMake `file()` commands)

Tests link against the thin `.a` + individual dep `.a` files (fat is created at install time only).

### Link Order (Linux)

Linux `ld` is order-sensitive. In `CMakeLists.txt`, libraries that USE symbols come before those that DEFINE them:

```
imagedec → imageenc → imageioutil → exampleutil → webpmux → webpdemux → webp → webpdecoder → sharpyuv
avif_apps → avif_internal → aom
```

## Cross-Platform (Windows)

- **MSYS2 UCRT64** toolchain for Windows builds
- `WEBP_UNICODE=OFF` in `build-deps.sh` — prevents wchar_t/UTF-16 corruption in pipe capture
- `MI_PIPE_TEXT` for stdout/stderr pipes, `MI_PIPE_BIN` for stdin (binary data)
- `MI_DUP/MI_DUP2/MI_CLOSE/MI_READ/MI_WRITE` macros abstract POSIX vs `_dup/_pipe/_close` etc.
- Tests use `MI_MKDIR/_mkdir`, `MI_UNLINK/_unlink`, platform-specific tmp dirs, `NUL` instead of `/dev/null`
- `system()` calls wrapped with `bash -c` on Windows (cmd.exe can't handle Unix paths)
- `\r\n` → `\n` normalization via `strip_cr()` in stderr comparison tests

## CI/CD

**test.yml** (push to develop/main): builds all 4 platforms, runs tests, caches `build/dist/` keyed by `{platform}-{SHA}`.

**release.yml** (v* tags): restores cache from test.yml → skips full rebuild on hit → uploads archives → creates GitHub Release. Cache miss triggers full build as fallback.

**Platforms**: linux-x86_64, linux-aarch64, darwin-arm64, windows-x86_64

## Key Constraints

- `avifenc` takes `char* argv[]` (non-const) — `mi_run_tool_avif` creates a mutable `strdup` copy
- `cwebp` stdin requires `-- -` (bare `-` is parsed as unknown option)
- `gif2webp` has no stdin support (file path only)
- `clearerr(stdin) + fseek(stdin, 0, SEEK_SET)` needed after dup2 to reset C stdio buffer state
- `-DBUILD_SHARED_LIBS=OFF` required for libavif (otherwise Windows produces DLL import symbols)
