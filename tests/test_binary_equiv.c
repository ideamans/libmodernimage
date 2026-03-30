/*
 * test_binary_equiv.c - Binary equivalence tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "modernimage.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MI_MKDIR(p) _mkdir(p)
#define MI_UNLINK(p) _unlink(p)
#define DEV_NULL "NUL"
#define CWEBP_BIN "deps/libwebp/build/cwebp.exe"
#define GIF2WEBP_BIN "deps/libwebp/build/gif2webp.exe"
#define AVIFENC_BIN "deps/libavif/build/avifenc.exe"
#else
#include <unistd.h>
#define MI_MKDIR(p) mkdir(p, 0755)
#define MI_UNLINK(p) unlink(p)
#define DEV_NULL "/dev/null"
#define CWEBP_BIN "deps/libwebp/build/cwebp"
#define GIF2WEBP_BIN "deps/libwebp/build/gif2webp"
#define AVIFENC_BIN "deps/libavif/build/avifenc"
#endif

#define FIXTURES "tests/fixtures"
#ifdef _WIN32
#define TMP "modernimage_test_tmp"
#else
#define TMP "/tmp/modernimage_test"
#endif

static int g_pass = 0, g_fail = 0;

/* Strip \r in-place (normalize \r\n → \n for Windows compatibility) */
static size_t strip_cr(char* buf, size_t len) {
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != '\r') buf[j++] = buf[i];
    }
    return j;
}

static void ensure_dir(const char* p) {
    struct stat st;
    if (stat(p, &st) != 0) MI_MKDIR(p);
}

static long fsize(const char* p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static int files_eq(const char* a, const char* b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    fseek(fa, 0, SEEK_END); fseek(fb, 0, SEEK_END);
    long sa = ftell(fa), sb = ftell(fb);
    if (sa != sb) { fclose(fa); fclose(fb); fprintf(stderr, "    size: %ld vs %ld\n", sa, sb); return 0; }
    rewind(fa); rewind(fb);
    unsigned char ba[4096], bb[4096];
    size_t n;
    while ((n = fread(ba, 1, sizeof(ba), fa)) > 0) {
        fread(bb, 1, n, fb);
        if (memcmp(ba, bb, n)) { fclose(fa); fclose(fb); return 0; }
    }
    fclose(fa); fclose(fb);
    return 1;
}

static int cli(const char* cmd) {
#ifdef _WIN32
    /* On Windows, system() uses cmd.exe which doesn't understand Unix paths.
     * Wrap with "bash -c" so MSYS2 bash handles path translation.
     * Use double quotes for the command to avoid issues with pipe/redirect chars. */
    char wrapped[2048];
    snprintf(wrapped, sizeof(wrapped), "bash -c \"%s\"", cmd);
    return system(wrapped);
#else
    int s = system(cmd);
    return WEXITSTATUS(s);
#endif
}

/* ---- cwebp tests ---- */

static void test_cwebp(const char* name, const char* input,
                       int argc, const char* argv[],
                       const char* cli_extra) {
    printf("  [TEST] cwebp: %s ... ", name);
    fflush(stdout);

    char out_cli[256], out_br[256], cmd[512];
    snprintf(out_cli, sizeof(out_cli), TMP "/cwebp_%s_cli.webp", name);
    snprintf(out_br, sizeof(out_br), TMP "/cwebp_%s_br.webp", name);

    snprintf(cmd, sizeof(cmd), CWEBP_BIN " %s %s -o %s 2>" DEV_NULL,
             cli_extra, input, out_cli);
    cli(cmd);

    /* Build bridge argv: tool name + options + input + -o + output */
    const char* full_argv[32];
    int full_argc = 0;
    full_argv[full_argc++] = "cwebp";
    for (int i = 0; i < argc; i++) full_argv[full_argc++] = argv[i];
    full_argv[full_argc++] = input;
    full_argv[full_argc++] = "-o";
    full_argv[full_argc++] = out_br;

    modernimage_context_t* ctx = modernimage_context_new();
    int rc = modernimage_cwebp(ctx, full_argc, full_argv);
    modernimage_context_free(ctx);

    if (rc != 0) { printf("FAIL (exit %d)\n", rc); g_fail++; return; }
    if (!files_eq(out_cli, out_br)) { printf("FAIL (binary mismatch)\n"); g_fail++; return; }
    printf("PASS (%ld bytes)\n", fsize(out_cli));
    g_pass++;
}

/* ---- gif2webp tests ---- */

static void test_gif2webp(const char* name, const char* input,
                          int argc, const char* argv[],
                          const char* cli_extra) {
    printf("  [TEST] gif2webp: %s ... ", name);
    fflush(stdout);

    char out_cli[256], out_br[256], cmd[512];
    snprintf(out_cli, sizeof(out_cli), TMP "/gif_%s_cli.webp", name);
    snprintf(out_br, sizeof(out_br), TMP "/gif_%s_br.webp", name);

    snprintf(cmd, sizeof(cmd), GIF2WEBP_BIN " %s %s -o %s 2>" DEV_NULL,
             cli_extra, input, out_cli);
    cli(cmd);

    const char* full_argv[32];
    int full_argc = 0;
    full_argv[full_argc++] = "gif2webp";
    for (int i = 0; i < argc; i++) full_argv[full_argc++] = argv[i];
    full_argv[full_argc++] = input;
    full_argv[full_argc++] = "-o";
    full_argv[full_argc++] = out_br;

    modernimage_context_t* ctx = modernimage_context_new();
    int rc = modernimage_gif2webp(ctx, full_argc, full_argv);
    modernimage_context_free(ctx);

    if (rc != 0) { printf("FAIL (exit %d)\n", rc); g_fail++; return; }
    if (!files_eq(out_cli, out_br)) { printf("FAIL (binary mismatch)\n"); g_fail++; return; }
    printf("PASS (%ld bytes)\n", fsize(out_cli));
    g_pass++;
}

/* ---- avifenc tests ---- */

static void test_avifenc(const char* name, const char* input,
                         int argc, const char* argv[],
                         const char* cli_extra) {
    printf("  [TEST] avifenc: %s ... ", name);
    fflush(stdout);

    char out_cli[256], out_br[256], cmd[512];
    snprintf(out_cli, sizeof(out_cli), TMP "/avif_%s_cli.avif", name);
    snprintf(out_br, sizeof(out_br), TMP "/avif_%s_br.avif", name);

    snprintf(cmd, sizeof(cmd), AVIFENC_BIN " %s %s %s 2>" DEV_NULL,
             cli_extra, input, out_cli);
    cli(cmd);

    /* avifenc puts output as last arg (no -o flag) */
    const char* full_argv[32];
    int full_argc = 0;
    full_argv[full_argc++] = "avifenc";
    for (int i = 0; i < argc; i++) full_argv[full_argc++] = argv[i];
    full_argv[full_argc++] = input;
    full_argv[full_argc++] = out_br;

    modernimage_context_t* ctx = modernimage_context_new();
    int rc = modernimage_avifenc(ctx, full_argc, full_argv);
    modernimage_context_free(ctx);

    if (rc != 0) { printf("FAIL (exit %d)\n", rc); g_fail++; return; }
    if (!files_eq(out_cli, out_br)) { printf("FAIL (binary mismatch)\n"); g_fail++; return; }
    printf("PASS (%ld bytes)\n", fsize(out_cli));
    g_pass++;
}

/* ---- Repeated execution (idempotency) ---- */

static void test_repeated(void) {
    printf("  [TEST] repeated: cwebp 10x same args ... ");
    fflush(stdout);

    const char* ref = TMP "/repeat_ref.webp";
    const char* cur = TMP "/repeat_cur.webp";

    modernimage_context_t* ctx = modernimage_context_new();
    const char* args0[] = {"cwebp", "-q", "75", FIXTURES "/test_red_64x64.png", "-o", ref};
    modernimage_cwebp(ctx, 6, args0);

    for (int i = 0; i < 10; i++) {
        modernimage_context_reset(ctx);
        const char* args[] = {"cwebp", "-q", "75", FIXTURES "/test_red_64x64.png", "-o", cur};
        modernimage_cwebp(ctx, 6, args);
        if (!files_eq(ref, cur)) {
            printf("FAIL (iter %d differs)\n", i);
            g_fail++;
            modernimage_context_free(ctx);
            return;
        }
    }
    modernimage_context_free(ctx);
    printf("PASS\n");
    g_pass++;
}

/* ---- stderr capture ---- */

static void test_stderr_capture(void) {
    printf("  [TEST] stderr: captured on help ... ");
    fflush(stdout);

    modernimage_context_t* ctx = modernimage_context_new();
    const char* args[] = {"cwebp", "-h"};
    modernimage_cwebp(ctx, 2, args);

    size_t out = modernimage_get_stdout_size(ctx);
    size_t err = modernimage_get_stderr_size(ctx);
    printf("(stdout=%zu, stderr=%zu) ", out, err);

    if (out == 0 && err == 0) {
        printf("FAIL (no output captured)\n");
        g_fail++;
    } else {
        printf("PASS\n");
        g_pass++;
    }
    modernimage_context_free(ctx);
}

static void test_stderr_match(void) {
    printf("  [TEST] stderr: content matches CLI ... ");
    fflush(stdout);

    const char* input = FIXTURES "/test_red_64x64.png";
    const char* out = TMP "/stderr_test.webp";

    /* CLI stderr */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             CWEBP_BIN " -q 80 -short %s -o %s 2>" TMP "/cli_stderr.txt",
             input, out);
    cli(cmd);

    FILE* f = fopen(TMP "/cli_stderr.txt", "rb");
    if (!f) { printf("FAIL (can't read CLI stderr)\n"); g_fail++; return; }
    fseek(f, 0, SEEK_END);
    long csz = ftell(f);
    rewind(f);
    char* cdata = malloc(csz + 1);
    fread(cdata, 1, csz, f);
    cdata[csz] = '\0';
    fclose(f);

    /* Bridge stderr */
    modernimage_context_t* ctx = modernimage_context_new();
    const char* args[] = {"cwebp", "-q", "80", "-short", input, "-o", out};
    modernimage_cwebp(ctx, 7, args);

    size_t bsz = modernimage_get_stderr_size(ctx);
    char* bdata = malloc(bsz + 1);
    modernimage_copy_stderr(ctx, bdata, bsz);
    bdata[bsz] = '\0';

    /* Normalize line endings for cross-platform comparison */
    csz = (long)strip_cr(cdata, csz); cdata[csz] = '\0';
    bsz = strip_cr(bdata, bsz); bdata[bsz] = '\0';

    if (csz == (long)bsz && memcmp(cdata, bdata, csz) == 0) {
        printf("PASS (%zu bytes)\n", bsz);
        g_pass++;
    } else {
        printf("FAIL (cli=%ld, bridge=%zu)\n", csz, bsz);
        if (csz < 200) fprintf(stderr, "    CLI: [%s]\n", cdata);
        if (bsz < 200) fprintf(stderr, "    Bridge: [%s]\n", bdata);
        g_fail++;
    }

    free(cdata);
    free(bdata);
    modernimage_context_free(ctx);
}

/* ---- stdin tests ---- */

/* Helper: read file into malloc'd buffer */
static char* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static void test_cwebp_stdin(void) {
    printf("  [TEST] cwebp: stdin (-) vs file ... ");
    fflush(stdout);

    const char* input = FIXTURES "/test_red_64x64.png";
    const char* out_file = TMP "/cwebp_file.webp";
    const char* out_stdin = TMP "/cwebp_stdin.webp";

    /* File-based */
    modernimage_context_t* ctx = modernimage_context_new();
    const char* a1[] = {"cwebp", "-q", "80", input, "-o", out_file};
    int rc1 = modernimage_cwebp(ctx, 6, a1);

    /* Stdin-based */
    size_t data_size;
    char* data = read_file(input, &data_size);
    if (!data) { printf("FAIL (can't read input)\n"); g_fail++; modernimage_context_free(ctx); return; }

    modernimage_context_reset(ctx);
    modernimage_set_stdin(ctx, data, data_size);
    /* cwebp requires "-- -" to read from stdin (bare "-" is treated as unknown option) */
    const char* a2[] = {"cwebp", "-q", "80", "-o", out_stdin, "--", "-"};
    int rc2 = modernimage_cwebp(ctx, 7, a2);

    /* Debug: show stderr on failure */
    if (rc2 != 0) {
        size_t esz = modernimage_get_stderr_size(ctx);
        if (esz > 0) {
            char ebuf[1024];
            size_t n = modernimage_copy_stderr(ctx, ebuf, sizeof(ebuf)-1);
            ebuf[n] = '\0';
            fprintf(stderr, "    stdin stderr: %s\n", ebuf);
        }
    }

    modernimage_context_free(ctx);
    free(data);

    if (rc1 != 0 || rc2 != 0) {
        printf("FAIL (exit: file=%d, stdin=%d)\n", rc1, rc2);
        g_fail++; return;
    }
    if (!files_eq(out_file, out_stdin)) {
        printf("FAIL (binary mismatch: file=%ld, stdin=%ld)\n", fsize(out_file), fsize(out_stdin));
        g_fail++; return;
    }
    printf("PASS (%ld bytes)\n", fsize(out_file));
    g_pass++;
}

static void test_cwebp_stdin_cli_match(void) {
    printf("  [TEST] cwebp: stdin bridge == stdin CLI ... ");
    fflush(stdout);

    const char* input = FIXTURES "/test_blue_128x128.png";
    const char* out_cli = TMP "/cwebp_stdin_cli.webp";
    const char* out_br = TMP "/cwebp_stdin_br.webp";

    /* CLI: cat file | cwebp -q 75 - -o output */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "cat %s | " CWEBP_BIN " -q 75 -o %s -- - 2>" DEV_NULL, input, out_cli);
    cli(cmd);

    /* Bridge: set_stdin + "-- -" */
    size_t data_size;
    char* data = read_file(input, &data_size);
    if (!data) { printf("FAIL (can't read input)\n"); g_fail++; return; }

    modernimage_context_t* ctx = modernimage_context_new();
    modernimage_set_stdin(ctx, data, data_size);
    const char* args[] = {"cwebp", "-q", "75", "-o", out_br, "--", "-"};
    int rc = modernimage_cwebp(ctx, 7, args);

    if (rc != 0) {
        size_t esz = modernimage_get_stderr_size(ctx);
        if (esz > 0) {
            char ebuf[1024];
            size_t n = modernimage_copy_stderr(ctx, ebuf, sizeof(ebuf)-1);
            ebuf[n] = '\0';
            fprintf(stderr, "    cli_match stderr: %s\n", ebuf);
        }
    }
    modernimage_context_free(ctx);
    free(data);

    if (rc != 0) { printf("FAIL (exit %d)\n", rc); g_fail++; return; }
    if (!files_eq(out_cli, out_br)) {
        printf("FAIL (binary mismatch)\n"); g_fail++; return;
    }
    printf("PASS (%ld bytes)\n", fsize(out_cli));
    g_pass++;
}

static void test_avifenc_stdin(void) {
    printf("  [TEST] avifenc: --stdin vs file ... ");
    fflush(stdout);

    const char* input = FIXTURES "/test_red_64x64.png";
    const char* out_file = TMP "/avifenc_file.avif";
    const char* out_stdin = TMP "/avifenc_stdin.avif";

    /* File-based */
    modernimage_context_t* ctx = modernimage_context_new();
    const char* a1[] = {"avifenc", "-q", "60", "-s", "8", input, out_file};
    int rc1 = modernimage_avifenc(ctx, 7, a1);

    /* Stdin-based */
    size_t data_size;
    char* data = read_file(input, &data_size);
    if (!data) { printf("FAIL (can't read input)\n"); g_fail++; modernimage_context_free(ctx); return; }

    modernimage_context_reset(ctx);
    modernimage_set_stdin(ctx, data, data_size);
    const char* a2[] = {"avifenc", "-q", "60", "-s", "8",
                        "--input-format", "png", "-o", out_stdin, "--stdin"};
    int rc2 = modernimage_avifenc(ctx, 10, a2);

    if (rc2 != 0) {
        size_t esz = modernimage_get_stderr_size(ctx);
        size_t osz = modernimage_get_stdout_size(ctx);
        if (esz > 0) {
            char ebuf[1024];
            size_t n = modernimage_copy_stderr(ctx, ebuf, sizeof(ebuf)-1);
            ebuf[n] = '\0';
            fprintf(stderr, "    avifenc stderr: %s\n", ebuf);
        }
        if (osz > 0) {
            char obuf[1024];
            size_t n = modernimage_copy_stdout(ctx, obuf, sizeof(obuf)-1);
            obuf[n] = '\0';
            fprintf(stderr, "    avifenc stdout: %s\n", obuf);
        }
    }

    modernimage_context_free(ctx);
    free(data);

    if (rc1 != 0 || rc2 != 0) {
        printf("FAIL (exit: file=%d, stdin=%d)\n", rc1, rc2);
        g_fail++; return;
    }
    if (!files_eq(out_file, out_stdin)) {
        printf("FAIL (binary mismatch: file=%ld, stdin=%ld)\n", fsize(out_file), fsize(out_stdin));
        g_fail++; return;
    }
    printf("PASS (%ld bytes)\n", fsize(out_file));
    g_pass++;
}

/* ---- main ---- */

int main(void) {
    ensure_dir(TMP);

    printf("\n=== Binary Equivalence Tests ===\n\n");

    printf("--- cwebp ---\n");
    { const char* a[] = {"-q", "80"}; test_cwebp("q80", FIXTURES "/test_red_64x64.png", 2, a, "-q 80"); }
    { const char* a[] = {"-lossless"}; test_cwebp("lossless", FIXTURES "/test_green_32x32.png", 1, a, "-lossless"); }
    { const char* a[] = {"-q", "10"}; test_cwebp("q10", FIXTURES "/test_blue_128x128.png", 2, a, "-q 10"); }
    { const char* a[] = {"-q", "50"}; test_cwebp("q50", FIXTURES "/test_blue_128x128.png", 2, a, "-q 50"); }
    { const char* a[] = {"-q", "90"}; test_cwebp("q90", FIXTURES "/test_blue_128x128.png", 2, a, "-q 90"); }
    { const char* a[] = {"-q", "100"}; test_cwebp("q100", FIXTURES "/test_blue_128x128.png", 2, a, "-q 100"); }
    { const char* a[] = {"-resize", "64", "64", "-q", "75"}; test_cwebp("resize", FIXTURES "/test_blue_128x128.png", 5, a, "-resize 64 64 -q 75"); }

    printf("\n--- gif2webp ---\n");
    { const char* a[] = {NULL}; test_gif2webp("default", FIXTURES "/test_anim.gif", 0, a, ""); }
    { const char* a[] = {"-lossy", "-q", "50"}; test_gif2webp("lossy_q50", FIXTURES "/test_anim.gif", 3, a, "-lossy -q 50"); }

    printf("\n--- avifenc ---\n");
    { const char* a[] = {"-q", "60", "-s", "8"}; test_avifenc("q60_s8", FIXTURES "/test_red_64x64.png", 4, a, "-q 60 -s 8"); }
    { const char* a[] = {"-l", "-s", "8"}; test_avifenc("lossless", FIXTURES "/test_green_32x32.png", 3, a, "-l -s 8"); }
    { const char* a[] = {"-q", "20", "-s", "8"}; test_avifenc("q20", FIXTURES "/test_blue_128x128.png", 4, a, "-q 20 -s 8"); }
    { const char* a[] = {"-q", "80", "-s", "8"}; test_avifenc("q80", FIXTURES "/test_blue_128x128.png", 4, a, "-q 80 -s 8"); }

    printf("\n--- stdin ---\n");
    test_cwebp_stdin();
    test_cwebp_stdin_cli_match();
    test_avifenc_stdin();

    printf("\n--- Repeated execution ---\n");
    test_repeated();

    printf("\n--- stderr capture ---\n");
    test_stderr_capture();
    test_stderr_match();

    printf("\n=== Results: %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
