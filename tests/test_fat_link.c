/*
 * test_fat_link.c - Verify that the fat static library (libmodernimage.a)
 * links correctly as a standalone unit, catching missing symbols that would
 * only appear when consumers link against the installed .a file.
 *
 * This test links ONLY against the fat libmodernimage.a (+ system libs),
 * NOT against individual dependency archives. If internal cross-references
 * (e.g. bridge_avifenc.c → libavif) are broken, this test will fail to LINK.
 *
 * The runtime tests (encoding) are optional — they run only when fixture
 * files are found. The primary value is the successful compile+link itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modernimage.h"

#ifdef _WIN32
#include <io.h>
#define MI_UNLINK(p) _unlink(p)
#else
#include <unistd.h>
#define MI_UNLINK(p) unlink(p)
#endif

static int g_pass = 0, g_fail = 0, g_skip = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        g_fail++; \
    } else { \
        printf("PASS: %s\n", msg); \
        g_pass++; \
    } \
} while(0)

/* Read file into malloc'd buffer, returns NULL if not found */
static unsigned char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(len);
    *out_len = fread(buf, 1, len, f);
    fclose(f);
    return buf;
}

int main(void) {
    printf("=== Fat static library link test ===\n\n");

    /* 1. Version check (basic API access) */
    const char* ver = modernimage_version();
    ASSERT(ver != NULL && strlen(ver) > 0, "modernimage_version() returns non-empty string");
    printf("  version: %s\n", ver);

    /* 2. Context lifecycle */
    modernimage_context_t* ctx = modernimage_context_new();
    ASSERT(ctx != NULL, "modernimage_context_new() succeeds");
    modernimage_context_free(ctx);

    /* 3. cwebp (exercises libwebp symbols) - optional, needs fixtures */
    {
        size_t jpeg_len;
        unsigned char* jpeg = read_file("tests/fixtures/photo.jpg", &jpeg_len);
        if (jpeg) {
            modernimage_context_t* c = modernimage_context_new();
            modernimage_set_stdin(c, jpeg, jpeg_len);
            const char* argv[] = {"cwebp", "-q", "80", "-o", "/tmp/fat_link_test.webp", "--", "-"};
            int rc = modernimage_cwebp(c, 7, argv);
            ASSERT(rc == 0, "cwebp encodes JPEG to WebP via fat .a");
            MI_UNLINK("/tmp/fat_link_test.webp");
            modernimage_context_free(c);
            free(jpeg);
        } else {
            printf("SKIP: cwebp test (tests/fixtures/photo.jpg not found)\n");
            g_skip++;
        }
    }

    /* 4. avifenc (exercises libavif + libaom symbols - the regression case) */
    {
        size_t jpeg_len;
        unsigned char* jpeg = read_file("tests/fixtures/photo.jpg", &jpeg_len);
        if (jpeg) {
            modernimage_context_t* c = modernimage_context_new();
            modernimage_set_stdin(c, jpeg, jpeg_len);
            const char* argv[] = {"avifenc", "-q", "80", "-s", "9",
                "--input-format", "jpeg", "-o", "/tmp/fat_link_test.avif", "--stdin"};
            int rc = modernimage_avifenc(c, 10, argv);
            ASSERT(rc == 0, "avifenc encodes JPEG to AVIF via fat .a");
            MI_UNLINK("/tmp/fat_link_test.avif");
            modernimage_context_free(c);
            free(jpeg);
        } else {
            printf("SKIP: avifenc test (tests/fixtures/photo.jpg not found)\n");
            g_skip++;
        }
    }

    /* 5. gif2webp (exercises giflib symbols) */
    {
        size_t gif_len;
        unsigned char* gif = read_file("tests/fixtures/animation.gif", &gif_len);
        if (gif) {
            modernimage_context_t* c = modernimage_context_new();
            const char* argv[] = {"gif2webp", "tests/fixtures/animation.gif",
                "-o", "/tmp/fat_link_test_gif.webp"};
            int rc = modernimage_gif2webp(c, 4, argv);
            ASSERT(rc == 0, "gif2webp encodes GIF to WebP via fat .a");
            MI_UNLINK("/tmp/fat_link_test_gif.webp");
            modernimage_context_free(c);
            free(gif);
        } else {
            printf("SKIP: gif2webp test (tests/fixtures/animation.gif not found)\n");
            g_skip++;
        }
    }

    printf("\n=== Results: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail > 0 ? 1 : 0;
}
