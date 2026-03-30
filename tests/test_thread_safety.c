/*
 * test_thread_safety.c - Thread safety and memory tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "modernimage.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MI_MKDIR(p) _mkdir(p)
#define MI_UNLINK(p) _unlink(p)
#else
#include <unistd.h>
#include <sys/resource.h>
#define MI_MKDIR(p) mkdir(p, 0755)
#define MI_UNLINK(p) unlink(p)
#endif

#define FIXTURES "tests/fixtures"
#ifdef _WIN32
#define TMP "modernimage_test_mt_tmp"
#else
#define TMP "/tmp/modernimage_test_mt"
#endif

static int g_pass = 0, g_fail = 0;

static void ensure_dir(const char* p) {
    struct stat st;
    if (stat(p, &st) != 0) MI_MKDIR(p);
}

typedef struct {
    int id;
    int iters;
    int ok;
    int fail;
} tresult_t;

/* ---- Workers ---- */

static void* w_cwebp(void* arg) {
    tresult_t* r = (tresult_t*)arg;
    r->ok = r->fail = 0;
    for (int i = 0; i < r->iters; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        snprintf(out, sizeof(out), TMP "/mt_cwebp_%d_%d.webp", r->id, i);
        const char* a[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png", "-o", out};
        int rc = modernimage_cwebp(ctx, 6, a);
        (rc == 0) ? r->ok++ : r->fail++;
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }
    return NULL;
}

static void* w_gif2webp(void* arg) {
    tresult_t* r = (tresult_t*)arg;
    r->ok = r->fail = 0;
    for (int i = 0; i < r->iters; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        snprintf(out, sizeof(out), TMP "/mt_gif_%d_%d.webp", r->id, i);
        const char* a[] = {"gif2webp", FIXTURES "/test_anim.gif", "-o", out};
        int rc = modernimage_gif2webp(ctx, 4, a);
        (rc == 0) ? r->ok++ : r->fail++;
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }
    return NULL;
}

static void* w_avifenc(void* arg) {
    tresult_t* r = (tresult_t*)arg;
    r->ok = r->fail = 0;
    for (int i = 0; i < r->iters; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        snprintf(out, sizeof(out), TMP "/mt_avif_%d_%d.avif", r->id, i);
        const char* a[] = {"avifenc", "-q", "60", "-s", "8",
                           FIXTURES "/test_red_64x64.png", out};
        int rc = modernimage_avifenc(ctx, 7, a);
        (rc == 0) ? r->ok++ : r->fail++;
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }
    return NULL;
}

static void* w_mixed(void* arg) {
    tresult_t* r = (tresult_t*)arg;
    r->ok = r->fail = 0;
    for (int i = 0; i < r->iters; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        int rc;
        switch (i % 3) {
        case 0:
            snprintf(out, sizeof(out), TMP "/stress_%d_%d.webp", r->id, i);
            { const char* a[] = {"cwebp", "-q", "75", FIXTURES "/test_red_64x64.png", "-o", out};
              rc = modernimage_cwebp(ctx, 6, a); }
            break;
        case 1:
            snprintf(out, sizeof(out), TMP "/stress_%d_%d.webp", r->id, i);
            { const char* a[] = {"gif2webp", FIXTURES "/test_anim.gif", "-o", out};
              rc = modernimage_gif2webp(ctx, 4, a); }
            break;
        default:
            snprintf(out, sizeof(out), TMP "/stress_%d_%d.avif", r->id, i);
            { const char* a[] = {"avifenc", "-q", "60", "-s", "8",
                                 FIXTURES "/test_red_64x64.png", out};
              rc = modernimage_avifenc(ctx, 7, a); }
            break;
        }
        (rc == 0) ? r->ok++ : r->fail++;
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }
    return NULL;
}

/* ---- Tests ---- */

static void test_repeated_alloc_free(void) {
    printf("  [TEST] memory: 100x new/execute/free ... ");
    fflush(stdout);
    for (int i = 0; i < 100; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        if (!ctx) { printf("FAIL (alloc)\n"); g_fail++; return; }
        char out[256];
        snprintf(out, sizeof(out), TMP "/leak_%d.webp", i);
        const char* a[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png", "-o", out};
        modernimage_cwebp(ctx, 6, a);
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }
    printf("PASS\n"); g_pass++;
}

static void test_context_reuse(void) {
    printf("  [TEST] memory: 100x context reuse ... ");
    fflush(stdout);
    modernimage_context_t* ctx = modernimage_context_new();
    for (int i = 0; i < 100; i++) {
        modernimage_context_reset(ctx);
        char out[256];
        snprintf(out, sizeof(out), TMP "/reuse_%d.webp", i);
        const char* a[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png", "-o", out};
        int rc = modernimage_cwebp(ctx, 6, a);
        if (rc != 0) {
            modernimage_context_free(ctx);
            printf("FAIL (iter %d)\n", i); g_fail++; return;
        }
        MI_UNLINK(out);
    }
    modernimage_context_free(ctx);
    printf("PASS\n"); g_pass++;
}

static void test_memory_growth(void) {
    printf("  [TEST] memory: RSS growth over 200 iters ... ");
    fflush(stdout);

    /* Warm up */
    for (int i = 0; i < 5; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        snprintf(out, sizeof(out), TMP "/warmup_%d.webp", i);
        const char* a[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png", "-o", out};
        modernimage_cwebp(ctx, 6, a);
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }

    long rss0 = 0;
#ifndef _WIN32
    { struct rusage u; getrusage(RUSAGE_SELF, &u); rss0 = u.ru_maxrss / 1024; }
#endif

    for (int i = 0; i < 200; i++) {
        modernimage_context_t* ctx = modernimage_context_new();
        char out[256];
        snprintf(out, sizeof(out), TMP "/memleak_%d.webp", i);
        const char* a[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png", "-o", out};
        modernimage_cwebp(ctx, 6, a);
        modernimage_context_free(ctx);
        MI_UNLINK(out);
    }

    long rss1 = 0;
#ifndef _WIN32
    { struct rusage u; getrusage(RUSAGE_SELF, &u); rss1 = u.ru_maxrss / 1024; }
#endif
    long growth = rss1 - rss0;
    printf("(%ldKB -> %ldKB, +%ldKB) ", rss0, rss1, growth);

    if (growth > 10240) { printf("FAIL\n"); g_fail++; }
    else { printf("PASS\n"); g_pass++; }
}

static void test_concurrent_diff(void) {
    printf("  [TEST] thread: concurrent cwebp+gif2webp+avifenc ... ");
    fflush(stdout);

    pthread_t t[3];
    tresult_t r[3] = {{.id=0,.iters=5},{.id=1,.iters=5},{.id=2,.iters=5}};
    pthread_create(&t[0], NULL, w_cwebp, &r[0]);
    pthread_create(&t[1], NULL, w_gif2webp, &r[1]);
    pthread_create(&t[2], NULL, w_avifenc, &r[2]);
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);

    int fail = r[0].fail + r[1].fail + r[2].fail;
    int ok = r[0].ok + r[1].ok + r[2].ok;
    printf("(%d ok, %d fail) ", ok, fail);
    if (fail) { printf("FAIL\n"); g_fail++; } else { printf("PASS\n"); g_pass++; }
}

static void test_concurrent_same(void) {
    printf("  [TEST] thread: 4x concurrent cwebp ... ");
    fflush(stdout);

    pthread_t t[4];
    tresult_t r[4];
    for (int i = 0; i < 4; i++) { r[i].id = i; r[i].iters = 5; }
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, w_cwebp, &r[i]);
    int fail = 0, ok = 0;
    for (int i = 0; i < 4; i++) { pthread_join(t[i], NULL); fail += r[i].fail; ok += r[i].ok; }
    printf("(%d ok, %d fail) ", ok, fail);
    if (fail) { printf("FAIL\n"); g_fail++; } else { printf("PASS\n"); g_pass++; }
}

static void test_output_isolation(void) {
    printf("  [TEST] isolation: output doesn't leak between calls ... ");
    fflush(stdout);

    modernimage_context_t* ctx = modernimage_context_new();

    /* Call 1: help text (writes to stderr in cwebp) */
    const char* a1[] = {"cwebp", "-h"};
    modernimage_cwebp(ctx, 2, a1);
    size_t sz1 = modernimage_get_stdout_size(ctx) + modernimage_get_stderr_size(ctx);
    char* content1 = malloc(sz1 + 1);
    size_t off = 0;
    off += modernimage_copy_stdout(ctx, content1, modernimage_get_stdout_size(ctx));
    off += modernimage_copy_stderr(ctx, content1 + off, modernimage_get_stderr_size(ctx));
    content1[off] = '\0';

    /* Call 2: encoding */
    modernimage_context_reset(ctx);
    const char* a2[] = {"cwebp", "-q", "80", FIXTURES "/test_red_64x64.png",
                        "-o", TMP "/isolation.webp"};
    modernimage_cwebp(ctx, 6, a2);
    size_t sz2_out = modernimage_get_stdout_size(ctx);
    size_t sz2_err = modernimage_get_stderr_size(ctx);

    /* Verify: call 2's stderr should NOT contain help text from call 1 */
    int leaked = 0;
    if (sz2_err > 0 && sz1 > 0) {
        char* content2 = malloc(sz2_err + 1);
        modernimage_copy_stderr(ctx, content2, sz2_err);
        content2[sz2_err] = '\0';
        /* Check if call 1's help keywords appear in call 2's output */
        if (strstr(content2, "Usage:") || strstr(content2, "-help")) {
            leaked = 1;
        }
        free(content2);
    }

    printf("(call1: %zu bytes, call2: out=%zu err=%zu) ", sz1, sz2_out, sz2_err);
    free(content1);
    modernimage_context_free(ctx);
    MI_UNLINK(TMP "/isolation.webp");

    if (leaked) { printf("FAIL (leaked)\n"); g_fail++; }
    else { printf("PASS\n"); g_pass++; }
}

static void test_stress(void) {
    printf("  [TEST] stress: 8 threads x 15 mixed ops ... ");
    fflush(stdout);

    pthread_t t[8];
    tresult_t r[8];
    for (int i = 0; i < 8; i++) { r[i].id = i; r[i].iters = 15; }
    for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, w_mixed, &r[i]);

    int fail = 0, ok = 0;
    for (int i = 0; i < 8; i++) { pthread_join(t[i], NULL); fail += r[i].fail; ok += r[i].ok; }
    printf("(%d/%d ok) ", ok, 120);
    if (fail) {
        for (int i = 0; i < 8; i++) if (r[i].fail) fprintf(stderr, "    thread %d: %d fail\n", i, r[i].fail);
        printf("FAIL\n"); g_fail++;
    } else {
        printf("PASS\n"); g_pass++;
    }
}

int main(void) {
    ensure_dir(TMP);
    printf("\n=== Thread Safety & Memory Tests ===\n\n");

    printf("--- Memory ---\n");
    test_repeated_alloc_free();
    test_context_reuse();
    test_memory_growth();

    printf("\n--- Threads ---\n");
    test_concurrent_diff();
    test_concurrent_same();

    printf("\n--- Isolation ---\n");
    test_output_isolation();

    printf("\n--- Stress ---\n");
    test_stress();

    printf("\n=== Results: %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
