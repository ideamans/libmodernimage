/*
 * test_fat_link.c - Verify that the fat static library (libmodernimage.a)
 * links correctly as a standalone unit, catching missing symbols that would
 * only appear when consumers link against the installed .a file.
 *
 * This test links ONLY against the fat libmodernimage.a (+ system libs),
 * NOT against individual dependency archives. If internal cross-references
 * (e.g. bridge_avifenc.c → libavif) are broken, this test will fail to link.
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

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        g_fail++; \
    } else { \
        printf("PASS: %s\n", msg); \
        g_pass++; \
    } \
} while(0)

/* Read file into malloc'd buffer */
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
    ModernImageContext* ctx = modernimage_context_new();
    ASSERT(ctx != NULL, "modernimage_context_new() succeeds");
    if (!ctx) {
        fprintf(stderr, "Cannot continue without context\n");
        return 1;
    }

    /* 3. cwebp (exercises libwebp symbols) */
    {
        size_t jpeg_len;
        unsigned char* jpeg = read_file("tests/fixtures/photo.jpg", &jpeg_len);
        ASSERT(jpeg != NULL, "load tests/fixtures/photo.jpg");
        if (jpeg) {
            ModernImageContext* c = modernimage_context_new();
            modernimage_set_stdin(c, jpeg, jpeg_len);
            const char* argv[] = {"cwebp", "-q", "80", "-o", "/tmp/fat_link_test.webp", "--", "-"};
            int rc = modernimage_cwebp(c, 7, (char**)argv);
            ASSERT(rc == 0, "cwebp encodes JPEG to WebP via fat .a");
            MI_UNLINK("/tmp/fat_link_test.webp");
            modernimage_context_free(c);
            free(jpeg);
        }
    }

    /* 4. avifenc (exercises libavif + libaom symbols - the regression case) */
    {
        size_t jpeg_len;
        unsigned char* jpeg = read_file("tests/fixtures/photo.jpg", &jpeg_len);
        ASSERT(jpeg != NULL, "load tests/fixtures/photo.jpg for avifenc");
        if (jpeg) {
            ModernImageContext* c = modernimage_context_new();
            modernimage_set_stdin(c, jpeg, jpeg_len);
            const char* argv[] = {"avifenc", "-q", "80", "-s", "9",
                "--input-format", "jpeg", "-o", "/tmp/fat_link_test.avif", "--stdin"};
            int rc = modernimage_avifenc(c, 10, (char**)argv);
            ASSERT(rc == 0, "avifenc encodes JPEG to AVIF via fat .a");
            MI_UNLINK("/tmp/fat_link_test.avif");
            modernimage_context_free(c);
            free(jpeg);
        }
    }

    /* 5. gif2webp (exercises giflib symbols) */
    {
        size_t gif_len;
        unsigned char* gif = read_file("tests/fixtures/animation.gif", &gif_len);
        ASSERT(gif != NULL, "load tests/fixtures/animation.gif");
        if (gif) {
            ModernImageContext* c = modernimage_context_new();
            const char* argv[] = {"gif2webp", "tests/fixtures/animation.gif",
                "-o", "/tmp/fat_link_test_gif.webp"};
            int rc = modernimage_gif2webp(c, 4, (char**)argv);
            ASSERT(rc == 0, "gif2webp encodes GIF to WebP via fat .a");
            MI_UNLINK("/tmp/fat_link_test_gif.webp");
            modernimage_context_free(c);
            free(gif);
        }
    }

    modernimage_context_free(ctx);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
