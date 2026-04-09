/*
 * libmodernimage - EXIF orientation detection
 *
 * Pure binary parsing of JPEG APP1 (Exif) marker to extract
 * the orientation tag (0x0112).  No libjpeg dependency.
 * Thread-safe: read-only, no shared state.
 */

#include "modernimage.h"
#include <stdint.h>
#include <string.h>

/* Read a 16-bit value with the given byte order */
static uint16_t read16(const uint8_t* p, int big_endian) {
    if (big_endian)
        return (uint16_t)((p[0] << 8) | p[1]);
    return (uint16_t)((p[1] << 8) | p[0]);
}

/* Read a 32-bit value with the given byte order */
static uint32_t read32(const uint8_t* p, int big_endian) {
    if (big_endian)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

int modernimage_jpeg_orientation(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;

    if (!p || size < 12)
        return 0;

    /* Check JPEG SOI marker */
    if (p[0] != 0xFF || p[1] != 0xD8)
        return 0;

    size_t pos = 2;

    /* Scan for APP1 (Exif) marker */
    while (pos + 4 <= size) {
        if (p[pos] != 0xFF)
            return 0;  /* Invalid marker */

        uint8_t marker = p[pos + 1];

        /* Skip padding bytes (0xFF) */
        if (marker == 0xFF) {
            pos++;
            continue;
        }

        /* SOI or standalone marker */
        if (marker == 0xD8 || marker == 0x00) {
            pos += 2;
            continue;
        }

        /* SOS or EOI — stop scanning */
        if (marker == 0xDA || marker == 0xD9)
            return 0;

        /* Read segment length */
        if (pos + 4 > size)
            return 0;

        uint16_t seg_len = (uint16_t)((p[pos + 2] << 8) | p[pos + 3]);
        if (seg_len < 2 || pos + 2 + seg_len > size)
            return 0;

        /* APP1 = 0xE1 */
        if (marker == 0xE1) {
            const uint8_t* seg = p + pos + 4;  /* past marker + length */
            size_t seg_data_len = seg_len - 2;

            /* Check "Exif\0\0" header (6 bytes) */
            if (seg_data_len < 14 ||
                memcmp(seg, "Exif\0\0", 6) != 0) {
                pos += 2 + seg_len;
                continue;  /* Not Exif APP1, try next */
            }

            /* TIFF header starts after "Exif\0\0" */
            const uint8_t* tiff = seg + 6;
            size_t tiff_len = seg_data_len - 6;

            /* Byte order: "II" = little-endian, "MM" = big-endian */
            int big_endian;
            if (tiff[0] == 'I' && tiff[1] == 'I')
                big_endian = 0;
            else if (tiff[0] == 'M' && tiff[1] == 'M')
                big_endian = 1;
            else
                return 0;

            /* TIFF magic number (42) */
            if (read16(tiff + 2, big_endian) != 42)
                return 0;

            /* Offset to IFD0 */
            uint32_t ifd_offset = read32(tiff + 4, big_endian);
            if (ifd_offset + 2 > tiff_len)
                return 0;

            /* Number of IFD entries */
            uint16_t num_entries = read16(tiff + ifd_offset, big_endian);
            size_t entries_start = ifd_offset + 2;

            for (uint16_t i = 0; i < num_entries; i++) {
                size_t entry_pos = entries_start + (size_t)i * 12;
                if (entry_pos + 12 > tiff_len)
                    break;

                uint16_t tag = read16(tiff + entry_pos, big_endian);

                if (tag == 0x0112) {  /* Orientation */
                    uint16_t type = read16(tiff + entry_pos + 2, big_endian);
                    uint32_t count = read32(tiff + entry_pos + 4, big_endian);

                    /* Orientation is a SHORT (type=3), count=1 */
                    if (type != 3 || count != 1)
                        return 0;

                    uint16_t orientation = read16(tiff + entry_pos + 8, big_endian);
                    if (orientation >= 1 && orientation <= 8)
                        return (int)orientation;
                    return 0;
                }
            }
            return 0;  /* Exif found but no orientation tag */
        }

        pos += 2 + seg_len;
    }

    return 0;
}
