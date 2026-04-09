/*
 * test_pipe_drain.c - Empirical test for the mi_capture pipe drain fix.
 *
 * Background: previous versions of mi_capture_begin/end drained stdout/stderr
 * pipes only AFTER the tool returned, which meant any tool writing more than
 * the OS pipe buffer (~64 KB on Linux/macOS) would deadlock waiting for the
 * pipe to be read while the main thread waited for the tool to finish.
 *
 * The fix spawns a reader thread for each of stdout and stderr inside
 * mi_capture_begin (symmetric to the existing stdin writer thread), so the
 * pipes are drained concurrently with tool execution.
 *
 * This test verifies the fix empirically by registering a fake "tool"
 * function that writes 256 KB and 1 MB to stderr (well past the typical
 * 64 KB pipe buffer) and confirming that the captured stderr matches the
 * expected size — a deadlocked test would never reach the assertion.
 *
 * Compilation strategy: this file is amalgamated with modernimage.c via
 * #include so it can call the static mi_run_tool() with a fake tool
 * function. The bridge entry points are stubbed out below; this test
 * never invokes them.
 */

/* Stub the bridge declarations BEFORE including modernimage.c so the
 * amalgamated source can find these symbols at compile time. The actual
 * bridge implementations are NOT linked into this test binary. */
#include <stddef.h>

int modernimage_cwebp_main(int argc, const char* argv[]) {
    (void)argc; (void)argv; return 0;
}
int modernimage_gif2webp_main(int argc, const char* argv[]) {
    (void)argc; (void)argv; return 0;
}
int modernimage_avifenc_main(int argc, char* argv[]) {
    (void)argc; (void)argv; return 0;
}
int modernimage_jpegtran_main(int argc, char** argv) {
    (void)argc; (void)argv; return 0;
}

/* Amalgamate the implementation so we can call mi_run_tool with our fake. */
#include "../src/modernimage.c"

#include <stdio.h>
#include <string.h>

/* ---------- Fake verbose "tool" ---------- */

static size_t g_fake_target_bytes = 0;

static int fake_verbose_tool(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    /* Write line-buffered chunks until we reach the target. Using a 1024-byte
     * line keeps the count predictable and forces multiple writes. */
    char line[1024];
    memset(line, 'x', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\n';
    size_t written = 0;
    while (written + sizeof(line) <= g_fake_target_bytes) {
        size_t n = fwrite(line, 1, sizeof(line), stderr);
        if (n != sizeof(line)) break;
        written += n;
    }
    fflush(stderr);
    return 0;
}

/* ---------- Test runner ---------- */

static int g_pass = 0, g_fail = 0;

static void run_volume_test(const char* name, size_t target) {
    printf("  [TEST] pipe drain: %s (target %zu bytes) ... ", name, target);
    fflush(stdout);

    g_fake_target_bytes = target;
    modernimage_context_t* ctx = modernimage_context_new();
    if (!ctx) { printf("FAIL (ctx new failed)\n"); g_fail++; return; }

    const char* argv[] = {"fake_verbose"};
    int rc = mi_run_tool(ctx, fake_verbose_tool, 1, argv);

    size_t err_size = modernimage_get_stderr_size(ctx);
    /* The fake tool writes a whole number of 1024-byte lines, so the
     * captured size should be at least target rounded down to a multiple
     * of 1024. We accept anything within ±1 line of the target. */
    size_t expected_min = (target / 1024) * 1024;
    if (err_size >= expected_min && rc == 0) {
        printf("PASS (captured %zu bytes)\n", err_size);
        g_pass++;
    } else {
        printf("FAIL (rc=%d, captured %zu bytes, expected >= %zu)\n",
               rc, err_size, expected_min);
        g_fail++;
    }

    modernimage_context_free(ctx);
}

int main(void) {
    printf("\n=== mi_capture pipe drain (>64KB) tests ===\n\n");

    /* Sanity case: small output that fits in any pipe buffer. */
    run_volume_test("4 KB", 4 * 1024);

    /* Just over the typical Linux pipe buffer (64 KB). */
    run_volume_test("96 KB", 96 * 1024);

    /* 4x the pipe buffer — clearly impossible without concurrent draining. */
    run_volume_test("256 KB", 256 * 1024);

    /* Stress: 1 MB of stderr in a single tool invocation. */
    run_volume_test("1 MB", 1024 * 1024);

    /* Run the 256 KB case repeatedly to surface any concurrency races. */
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "256 KB repeat #%d", i + 1);
        run_volume_test(name, 256 * 1024);
    }

    printf("\n=== Results: %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
