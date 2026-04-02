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
#include <direct.h>
#define MI_UNLINK(p) _unlink(p)
#define TMP_DIR "fat_link_tmp"
#else
#include <unistd.h>
#define MI_UNLINK(p) unlink(p)
#define TMP_DIR "/tmp"
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

#ifdef _WIN32
    _mkdir(TMP_DIR);
#endif

    /* 1. Version check (basic API access) */
    const char* ver = modernimage_version();
    ASSERT(ver != NULL && strlen(ver) > 0, "modernimage_version() returns non-empty string");
    printf("  version: %s\n", ver);

    /* 2. Context lifecycle */
    modernimage_context_t* ctx = modernimage_context_new();
    ASSERT(ctx != NULL, "modernimage_context_new() succeeds");
    modernimage_context_free(ctx);

    /* 3. cwebp (exercises libwebp symbols) */
    {
        size_t png_len;
        unsigned char* png = read_file("tests/fixtures/test_red_64x64.png", &png_len);
        ASSERT(png != NULL, "load test_red_64x64.png");
        if (png) {
            modernimage_context_t* c = modernimage_context_new();
            modernimage_set_stdin(c, png, png_len);
            const char* argv[] = {"cwebp", "-q", "80", "-o", TMP_DIR "/fat_link_test.webp", "--", "-"};
            int rc = modernimage_cwebp(c, 7, argv);
            ASSERT(rc == 0, "cwebp encodes PNG to WebP via fat .a");
            MI_UNLINK(TMP_DIR "/fat_link_test.webp");
            modernimage_context_free(c);
            free(png);
        }
    }

    /* 4. avifenc (exercises libavif + libaom symbols - the regression case) */
    {
        size_t png_len;
        unsigned char* png = read_file("tests/fixtures/test_red_64x64.png", &png_len);
        ASSERT(png != NULL, "load test_red_64x64.png for avifenc");
        if (png) {
            modernimage_context_t* c = modernimage_context_new();
            modernimage_set_stdin(c, png, png_len);
            const char* argv[] = {"avifenc", "-q", "80", "-s", "9",
                "--input-format", "png", "-o", TMP_DIR "/fat_link_test.avif", "--stdin"};
            int rc = modernimage_avifenc(c, 10, argv);
            ASSERT(rc == 0, "avifenc encodes PNG to AVIF via fat .a");
            MI_UNLINK(TMP_DIR "/fat_link_test.avif");
            modernimage_context_free(c);
            free(png);
        }
    }

    /* 5. gif2webp (exercises giflib symbols) */
    {
        size_t gif_len;
        unsigned char* gif = read_file("tests/fixtures/test_anim.gif", &gif_len);
        ASSERT(gif != NULL, "load test_anim.gif");
        if (gif) {
            modernimage_context_t* c = modernimage_context_new();
            const char* argv[] = {"gif2webp", "tests/fixtures/test_anim.gif",
                "-o", TMP_DIR "/fat_link_test_gif.webp"};
            int rc = modernimage_gif2webp(c, 4, argv);
            ASSERT(rc == 0, "gif2webp encodes GIF to WebP via fat .a");
            MI_UNLINK(TMP_DIR "/fat_link_test_gif.webp");
            modernimage_context_free(c);
            free(gif);
        }
    }

    printf("\n=== Results: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail > 0 ? 1 : 0;
}
