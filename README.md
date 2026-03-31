# libmodernimage

[日本語](README_ja.md)

Thread-safe FFI bridge library that exposes **cwebp**, **gif2webp**, and **avifenc** as callable C functions. Link a single `.a` or `.so` to get full image format conversion without shipping separate CLI binaries.

## Bundled tools

| Tool | Upstream | Format |
|------|----------|--------|
| cwebp | libwebp 1.6.0 | PNG/JPEG → WebP |
| gif2webp | libwebp 1.6.0 | GIF → Animated WebP |
| avifenc | libavif 1.4.1 + aom 3.13.2 | PNG/JPEG → AVIF |

## Quick start

### Build

```bash
git clone --recursive https://github.com/user/libmodernimage.git
cd libmodernimage

./scripts/build-deps.sh   # libwebp, libaom, libavif
./scripts/build.sh         # libmodernimage.a, .so/.dylib
```

Requires: cmake, ninja, libpng-dev, libjpeg-dev, libgif-dev

### Link

```bash
# Static (all deps bundled — only system libs needed)
cc -o myapp myapp.c -Lbuild -lmodernimage -lpthread -lm

# Dynamic
cc -o myapp myapp.c -Lbuild -lmodernimage -lpthread
```

## API

```c
#include "modernimage.h"

// 1. Create context
modernimage_context_t* ctx = modernimage_context_new();

// 2. (Optional) Feed input via stdin instead of file
modernimage_set_stdin(ctx, png_data, png_size);

// 3. Execute — same args as CLI
const char* argv[] = {"cwebp", "-q", "80", "-o", "out.webp", "--", "-"};
int rc = modernimage_cwebp(ctx, 7, argv);

// 4. Read captured output
size_t err_size = modernimage_get_stderr_size(ctx);
char* err = malloc(err_size + 1);
modernimage_copy_stderr(ctx, err, err_size);
err[err_size] = '\0';

// 5. Reuse or free
modernimage_context_reset(ctx);  // reuse
modernimage_context_free(ctx);   // done
```

### Functions

| Category | Function | Description |
|----------|----------|-------------|
| Lifecycle | `modernimage_context_new()` | Create context |
| | `modernimage_context_free(ctx)` | Destroy context |
| | `modernimage_context_reset(ctx)` | Clear for reuse |
| Input | `modernimage_set_stdin(ctx, data, size)` | Set in-memory stdin data |
| Execution | `modernimage_cwebp(ctx, argc, argv)` | Run cwebp |
| | `modernimage_gif2webp(ctx, argc, argv)` | Run gif2webp |
| | `modernimage_avifenc(ctx, argc, argv)` | Run avifenc |
| Output | `modernimage_get_stdout_size(ctx)` | Captured stdout size |
| | `modernimage_get_stderr_size(ctx)` | Captured stderr size |
| | `modernimage_copy_stdout(ctx, buf, size)` | Copy stdout to caller buffer |
| | `modernimage_copy_stderr(ctx, buf, size)` | Copy stderr to caller buffer |
| | `modernimage_get_exit_code(ctx)` | Last exit code |
| Info | `modernimage_version()` | Library version string |

### Stdin support

| Tool | How | argv example |
|------|-----|-------------|
| cwebp | `set_stdin` + `"-- -"` | `cwebp -q 80 -o out.webp -- -` |
| avifenc | `set_stdin` + `"--stdin"` | `avifenc -q 60 --input-format png -o out.avif --stdin` |
| gif2webp | Not supported | Use file path |

## Thread safety

- All calls are serialized by a global IO mutex (dup2 is process-wide)
- Each context owns its own output buffers — no cross-context interference
- Safe to call from multiple threads with separate contexts
- Tested: 8 threads x 15 mixed operations with zero failures

## Testing

```bash
./build/test_binary_equiv    # Output matches original CLI byte-for-byte
./build/test_thread_safety   # Concurrency, memory leaks, output isolation
```

## Release artifacts

Each [GitHub Release](https://github.com/user/libmodernimage/releases) provides per-platform archives:

```
libmodernimage-linux-x86_64.tar.gz
libmodernimage-linux-aarch64.tar.gz
libmodernimage-darwin-arm64.tar.gz
libmodernimage-windows-x86_64.tar.gz   # MSYS2 UCRT64
```

Contents:

| File | Description |
|------|-------------|
| `libmodernimage.a` | Fat static library (all deps included) |
| `libmodernimage.so` / `.dylib` / `.dll` | Shared library |
| `modernimage.h` | Public C header |
| `cli-compat.json` | Machine-readable tool versions and CLI spec |

## cli-compat.json

Describes which tool versions are bundled and what arguments they support. Intended for downstream binding libraries to programmatically build argv arrays.

```json
{
  "libmodernimage_version": "0.2.0",
  "tools": {
    "cwebp": {
      "upstream_version": "1.6.0",
      "stdin_support": { "arg": "-- -" },
      "key_options": { "-q": { "type": "int", "range": [0, 100] }, ... }
    },
    ...
  }
}
```

## Architecture

This library compiles each tool's `main()` function into a shared library by:

1. `#define main modernimage_xxx_main` + `#include "original_source.c"`
2. At runtime, `dup2()` redirects stdin/stdout/stderr to pipes
3. A global mutex ensures fd-level redirection is safe across threads
4. Captured output is stored in per-context buffers

No upstream source code is modified — bridge files include originals with macro renaming.

## License

MIT. Bundled libraries retain their original licenses (BSD for libwebp/libavif/libaom).
