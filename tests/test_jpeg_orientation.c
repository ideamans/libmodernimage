/*
 * test_jpeg_orientation.c — unit tests for modernimage_jpeg_orientation()
 *
 * The orientation parser is a pure binary scanner over JPEG markers and
 * the EXIF APP1 TIFF block. We test it with hand-crafted minimal JPEGs
 * (SOI + APP1(Exif/IFD0/Orientation) + EOI) so the test has no fixture
 * file dependencies and exercises both little- and big-endian TIFF.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "modernimage.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(label, cond) do { \
    printf("  [TEST] %s ... ", label); fflush(stdout); \
    if (cond) { printf("PASS\n"); g_pass++; } \
    else      { printf("FAIL\n"); g_fail++; } \
} while (0)

/*
 * Layout of the little-endian template (offsets are 0-based):
 *
 *   00 FF D8                    SOI
 *   02 FF E1                    APP1 marker
 *   04 00 22                    APP1 length = 34 bytes (incl. length field)
 *   06 'E' 'x' 'i' 'f' 00 00    "Exif\0\0"
 *   12 'I' 'I'                  TIFF byte order = little-endian
 *   14 2A 00                    TIFF magic = 42
 *   16 08 00 00 00              IFD0 offset (from TIFF start) = 8
 *   20 01 00                    IFD0 entry count = 1
 *   22 12 01                    tag = 0x0112 (Orientation)
 *   24 03 00                    type = 3 (SHORT)
 *   26 01 00 00 00              count = 1
 *   30 ?? 00 00 00              value (orientation, low byte)  ← byte 30
 *   34 00 00 00 00              next IFD offset = 0
 *   38 FF D9                    EOI
 *
 * Total length = 40 bytes.
 */
static uint8_t le_template[] = {
    0xFF, 0xD8,                                /*  0: SOI                   */
    0xFF, 0xE1, 0x00, 0x22,                    /*  2: APP1, len=34          */
    'E',  'x',  'i',  'f',  0x00, 0x00,        /*  6: "Exif\0\0"            */
    'I',  'I',                                  /* 12: little-endian        */
    0x2A, 0x00,                                /* 14: TIFF magic 42         */
    0x08, 0x00, 0x00, 0x00,                    /* 16: IFD0 offset = 8       */
    0x01, 0x00,                                /* 20: 1 IFD entry           */
    0x12, 0x01,                                /* 22: tag 0x0112            */
    0x03, 0x00,                                /* 24: type SHORT            */
    0x01, 0x00, 0x00, 0x00,                    /* 26: count = 1             */
    0x01, 0x00, 0x00, 0x00,                    /* 30: orientation value     */
    0x00, 0x00, 0x00, 0x00,                    /* 34: next IFD offset       */
    0xFF, 0xD9                                 /* 38: EOI                   */
};
#define LE_VALUE_OFFSET 30

/* Big-endian variant: same structure but "MM" + byte order swapped */
static uint8_t be_template[] = {
    0xFF, 0xD8,                                /*  0: SOI                   */
    0xFF, 0xE1, 0x00, 0x22,                    /*  2: APP1, len=34          */
    'E',  'x',  'i',  'f',  0x00, 0x00,        /*  6: "Exif\0\0"            */
    'M',  'M',                                  /* 12: big-endian           */
    0x00, 0x2A,                                /* 14: TIFF magic 42         */
    0x00, 0x00, 0x00, 0x08,                    /* 16: IFD0 offset = 8       */
    0x00, 0x01,                                /* 20: 1 IFD entry           */
    0x01, 0x12,                                /* 22: tag 0x0112            */
    0x00, 0x03,                                /* 24: type SHORT            */
    0x00, 0x00, 0x00, 0x01,                    /* 26: count = 1             */
    0x00, 0x01, 0x00, 0x00,                    /* 30: orientation value     */
    0x00, 0x00, 0x00, 0x00,                    /* 34: next IFD offset       */
    0xFF, 0xD9                                 /* 38: EOI                   */
};
#define BE_VALUE_OFFSET 30

static void test_le_orientations(void) {
    for (int o = 1; o <= 8; o++) {
        char label[64];
        snprintf(label, sizeof(label), "LE orientation = %d", o);
        le_template[LE_VALUE_OFFSET] = (uint8_t)o;
        int got = modernimage_jpeg_orientation(le_template, sizeof(le_template));
        printf("  [TEST] %s ... ", label); fflush(stdout);
        if (got == o) { printf("PASS\n"); g_pass++; }
        else          { printf("FAIL (got %d)\n", got); g_fail++; }
    }
}

static void test_be_orientations(void) {
    for (int o = 1; o <= 8; o++) {
        char label[64];
        snprintf(label, sizeof(label), "BE orientation = %d", o);
        /* Big-endian SHORT: high byte first. Values 1..8 fit in low byte. */
        be_template[BE_VALUE_OFFSET]     = 0x00;
        be_template[BE_VALUE_OFFSET + 1] = (uint8_t)o;
        int got = modernimage_jpeg_orientation(be_template, sizeof(be_template));
        printf("  [TEST] %s ... ", label); fflush(stdout);
        if (got == o) { printf("PASS\n"); g_pass++; }
        else          { printf("FAIL (got %d)\n", got); g_fail++; }
    }
}

static void test_invalid_orientation_values(void) {
    /* Out-of-range orientation values must yield 0 (not the value itself). */
    le_template[LE_VALUE_OFFSET] = 0;
    CHECK("LE orientation = 0 → 0",
          modernimage_jpeg_orientation(le_template, sizeof(le_template)) == 0);

    le_template[LE_VALUE_OFFSET] = 9;
    CHECK("LE orientation = 9 → 0",
          modernimage_jpeg_orientation(le_template, sizeof(le_template)) == 0);

    le_template[LE_VALUE_OFFSET] = 255;
    CHECK("LE orientation = 255 → 0",
          modernimage_jpeg_orientation(le_template, sizeof(le_template)) == 0);

    /* Restore to a valid value for any later tests that share the buffer. */
    le_template[LE_VALUE_OFFSET] = 1;
}

static void test_null_and_short_inputs(void) {
    CHECK("NULL pointer → 0",
          modernimage_jpeg_orientation(NULL, 0) == 0);
    CHECK("NULL with size → 0",
          modernimage_jpeg_orientation(NULL, 100) == 0);

    /* Anything shorter than the minimum (12) must short-circuit. */
    static const uint8_t tiny[] = {0xFF, 0xD8, 0xFF, 0xE1};
    CHECK("size < 12 → 0",
          modernimage_jpeg_orientation(tiny, sizeof(tiny)) == 0);
}

static void test_non_jpeg(void) {
    /* PNG signature */
    static const uint8_t png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52
    };
    CHECK("PNG header → 0",
          modernimage_jpeg_orientation(png, sizeof(png)) == 0);

    /* Random garbage that happens to be 16 bytes */
    static const uint8_t junk[16] = {
        'h','e','l','l','o',' ','w','o','r','l','d','!','!','!','!','!'
    };
    CHECK("garbage → 0",
          modernimage_jpeg_orientation(junk, sizeof(junk)) == 0);
}

static void test_no_app1(void) {
    /* JPEG with only SOI + SOS + EOI, no APP1 anywhere. The parser hits
     * SOS (0xDA) and stops, returning 0. */
    static const uint8_t no_app1[] = {
        0xFF, 0xD8,                  /* SOI */
        0xFF, 0xDA, 0x00, 0x02,      /* SOS, len=2 (empty) */
        0xFF, 0xD9                   /* EOI */
    };
    CHECK("JPEG without APP1 → 0",
          modernimage_jpeg_orientation(no_app1, sizeof(no_app1)) == 0);
}

static void test_app1_without_exif(void) {
    /* APP1 segment that does not start with "Exif\0\0" — e.g. an XMP packet.
     * The parser must skip over it and continue. With no Exif APP1
     * downstream, the function returns 0. */
    static const uint8_t app1_xmp[] = {
        0xFF, 0xD8,                                  /* SOI */
        0xFF, 0xE1, 0x00, 0x10,                      /* APP1, len=16 */
        'h','t','t','p',':','/','/','n','s','.','x','m','p','/',  /* 14 bytes */
        0xFF, 0xD9                                   /* EOI */
    };
    CHECK("APP1 (XMP) without Exif → 0",
          modernimage_jpeg_orientation(app1_xmp, sizeof(app1_xmp)) == 0);
}

static void test_exif_without_orientation_tag(void) {
    /* Exif APP1 with a single non-orientation tag (ImageWidth=0x0100).
     * The parser should iterate the IFD, fail to find 0x0112, and return 0. */
    static uint8_t exif_no_orient[] = {
        0xFF, 0xD8,                                /*  0: SOI               */
        0xFF, 0xE1, 0x00, 0x22,                    /*  2: APP1, len=34      */
        'E',  'x',  'i',  'f',  0x00, 0x00,        /*  6: "Exif\0\0"        */
        'I',  'I',                                  /* 12: little-endian    */
        0x2A, 0x00,                                /* 14: TIFF magic        */
        0x08, 0x00, 0x00, 0x00,                    /* 16: IFD0 offset       */
        0x01, 0x00,                                /* 20: 1 entry           */
        0x00, 0x01,                                /* 22: tag 0x0100 (not orientation) */
        0x03, 0x00,                                /* 24: type SHORT        */
        0x01, 0x00, 0x00, 0x00,                    /* 26: count             */
        0x80, 0x00, 0x00, 0x00,                    /* 30: value             */
        0x00, 0x00, 0x00, 0x00,                    /* 34: next IFD          */
        0xFF, 0xD9                                 /* 38: EOI               */
    };
    CHECK("Exif APP1 without orientation tag → 0",
          modernimage_jpeg_orientation(exif_no_orient, sizeof(exif_no_orient)) == 0);
}

static void test_real_jpeg_fixture(void) {
    /* Sanity check against the actual JPEG used by the bridge tests.
     * The fixture was generated by sips, which strips orientation, so
     * the expected result is 0 (no orientation tag → 0). The point of
     * this test is to confirm that a real-world JPEG flows through the
     * marker scanner without crashing or returning garbage. */
    FILE* f = fopen("tests/fixtures/test_photo.jpg", "rb");
    if (!f) {
        printf("  [TEST] real fixture (test_photo.jpg) ... SKIP (not found)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    int o = modernimage_jpeg_orientation(buf, (size_t)sz);
    free(buf);

    printf("  [TEST] real fixture parses without crash (got=%d) ... ", o);
    if (o >= 0 && o <= 8) { printf("PASS\n"); g_pass++; }
    else                  { printf("FAIL\n"); g_fail++; }
}

int main(void) {
    printf("\n=== JPEG Orientation Parser Tests ===\n\n");

    printf("--- Little-endian (II) ---\n");
    test_le_orientations();

    printf("\n--- Big-endian (MM) ---\n");
    test_be_orientations();

    printf("\n--- Invalid orientation values ---\n");
    test_invalid_orientation_values();

    printf("\n--- NULL / short inputs ---\n");
    test_null_and_short_inputs();

    printf("\n--- Non-JPEG ---\n");
    test_non_jpeg();

    printf("\n--- JPEG without orientation ---\n");
    test_no_app1();
    test_app1_without_exif();
    test_exif_without_orientation_tag();

    printf("\n--- Real fixture ---\n");
    test_real_jpeg_fixture();

    printf("\n=== Results: %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
