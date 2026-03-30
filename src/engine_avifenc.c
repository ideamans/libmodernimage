/*
 * libmodernimage - avifenc engine implementation
 */

#include "engine_avifenc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "avif/avif.h"
#include "avifpng.h"
#include "avifjpeg.h"
#include "avifutil.h"

/* ================================================================ */
/* mi_avifenc_parse                                                  */
/* ================================================================ */

int mi_avifenc_parse(mi_avifenc_params_t* p, int argc, char* argv[],
                     const char** out_in_file, const char** out_out_file) {
    const char* in_file = NULL;
    const char* out_file = NULL;

    memset(p, 0, sizeof(*p));
    p->codecChoice = AVIF_CODEC_CHOICE_AUTO;
    p->speed = -1;
    p->quality = -1;
    p->qualityAlpha = -1;
    p->requestedRange = AVIF_RANGE_FULL;
    p->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
    p->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
    p->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
    p->requestedFormat = AVIF_PIXEL_FORMAT_NONE;
    p->headerFormat = AVIF_HEADER_FULL;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help") ||
            !strcmp(arg, "-V") || !strcmp(arg, "--version")) {
            continue;
        } else if ((!strcmp(arg, "-q") || !strcmp(arg, "--qcolor")) && i + 1 < argc) {
            p->quality = atoi(argv[++i]);
        } else if (!strcmp(arg, "--qalpha") && i + 1 < argc) {
            p->qualityAlpha = atoi(argv[++i]);
        } else if ((!strcmp(arg, "-s") || !strcmp(arg, "--speed")) && i + 1 < argc) {
            p->speed = atoi(argv[++i]);
        } else if ((!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) && i + 1 < argc) {
            p->jobs = atoi(argv[++i]);
        } else if (!strcmp(arg, "-l") || !strcmp(arg, "--lossless")) {
            p->lossless = AVIF_TRUE;
        } else if ((!strcmp(arg, "-o") || !strcmp(arg, "--output")) && i + 1 < argc) {
            out_file = argv[++i];
        } else if (!strcmp(arg, "--no-overwrite")) {
            p->noOverwrite = AVIF_TRUE;
        } else if (!strcmp(arg, "--premultiply")) {
            p->premultiplyAlpha = AVIF_TRUE;
        } else if ((!strcmp(arg, "-r") || !strcmp(arg, "--range")) && i + 1 < argc) {
            ++i;
            if (!strcmp(argv[i], "limited") || !strcmp(argv[i], "l"))
                p->requestedRange = AVIF_RANGE_LIMITED;
        } else if (!strcmp(arg, "--cicp") && i + 3 < argc) {
            p->colorPrimaries = (avifColorPrimaries)atoi(argv[++i]);
            p->transferCharacteristics = (avifTransferCharacteristics)atoi(argv[++i]);
            p->matrixCoefficients = (avifMatrixCoefficients)atoi(argv[++i]);
            p->cicpExplicitlySet = AVIF_TRUE;
        } else if ((!strcmp(arg, "-y") || !strcmp(arg, "--yuv")) && i + 1 < argc) {
            ++i;
            if (!strcmp(argv[i], "444")) p->requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
            else if (!strcmp(argv[i], "422")) p->requestedFormat = AVIF_PIXEL_FORMAT_YUV422;
            else if (!strcmp(argv[i], "420")) p->requestedFormat = AVIF_PIXEL_FORMAT_YUV420;
            else if (!strcmp(argv[i], "400")) p->requestedFormat = AVIF_PIXEL_FORMAT_YUV400;
        } else if ((!strcmp(arg, "-d") || !strcmp(arg, "--depth")) && i + 1 < argc) {
            p->requestedDepth = atoi(argv[++i]);
        } else if (!strcmp(arg, "--min") || !strcmp(arg, "--mini")) {
            p->headerFormat = (avifHeaderFormat)0x1; /* AVIF_HEADER_MINI */
        } else if (!strcmp(arg, "--ignore-exif")) {
            p->ignoreExif = AVIF_TRUE;
        } else if (!strcmp(arg, "--ignore-xmp")) {
            p->ignoreXMP = AVIF_TRUE;
        } else if (!strcmp(arg, "--ignore-icc") || !strcmp(arg, "--ignore-color-profile")) {
            p->ignoreColorProfile = AVIF_TRUE;
        } else if (!strcmp(arg, "--ignore-gain-map")) {
            p->ignoreGainMap = AVIF_TRUE;
        } else if (!strcmp(arg, "--stdin")) {
            in_file = "(stdin)";
        } else if (!strcmp(arg, "--input-format") && i + 1 < argc) {
            ++i;
        } else if ((!strcmp(arg, "-c") || !strcmp(arg, "--codec")) && i + 1 < argc) {
            p->codecChoice = avifCodecChoiceFromName(argv[++i]);
        } else if (arg[0] == '-') {
            if (i + 1 < argc && argv[i+1][0] != '-') ++i;
        } else {
            if (!in_file) in_file = arg;
            else if (!out_file) out_file = arg;
        }
    }

    if (p->jobs == 0) p->jobs = avifQueryCPUCount();
    if (p->quality < 0) p->quality = p->lossless ? AVIF_QUALITY_LOSSLESS : 60;
    if (p->qualityAlpha < 0) p->qualityAlpha = p->quality;
    if (p->speed < 0) p->speed = 6;

    if (p->lossless) {
        p->quality = AVIF_QUALITY_LOSSLESS;
        p->qualityAlpha = AVIF_QUALITY_LOSSLESS;
        if (!p->cicpExplicitlySet)
            p->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        if (p->requestedFormat == AVIF_PIXEL_FORMAT_NONE)
            p->requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
        p->requestedRange = AVIF_RANGE_FULL;
    }

    if (out_in_file) *out_in_file = in_file;
    if (out_out_file) *out_out_file = out_file;
    return 0;
}

/* ================================================================ */
/* mi_avifenc_encode                                                 */
/* ================================================================ */

int mi_avifenc_encode(const mi_avifenc_params_t* p,
                      const char* in_file, const char* out_file) {
    int returnCode = 1;
    avifImage* image = NULL;
    avifRWData raw = AVIF_DATA_EMPTY;
    avifEncoder* encoder = NULL;

    image = avifImageCreateEmpty();
    if (!image) { fprintf(stderr, "ERROR: Out of memory\n"); goto cleanup; }

    image->colorPrimaries = p->colorPrimaries;
    image->transferCharacteristics = p->transferCharacteristics;
    image->matrixCoefficients = p->matrixCoefficients;
    image->yuvRange = p->requestedRange;
    image->alphaPremultiplied = p->premultiplyAlpha;

    /* Read input */
    avifBool isStdin = (in_file && strcmp(in_file, "(stdin)") == 0);
    if (isStdin) {
        /* Read all of stdin into memory, then try PNG/JPEG */
        avifRWData stdinData = AVIF_DATA_EMPTY;
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
            size_t old = stdinData.size;
            avifRWDataRealloc(&stdinData, old + n);
            memcpy(stdinData.data + old, buf, n);
        }
        uint32_t pngDepth = 0;
        avifBool readOk = avifPNGRead(NULL, image,
                                      p->requestedFormat, p->requestedDepth,
                                      AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                                      p->ignoreColorProfile, p->ignoreExif, p->ignoreXMP,
                                      AVIF_DEFAULT_IMAGE_SIZE_LIMIT, &pngDepth);
        /* PNG read from filename won't work for stdin; use avifReadImage workaround:
         * write to temp file and read back. This is pragmatic for the cached path. */
        if (!readOk) {
            /* Fallback: write stdin data to temp file */
            const char* tmp_path = "/tmp/mi_avifenc_stdin_tmp";
            FILE* tmp = fopen(tmp_path, "wb");
            if (tmp) {
                fwrite(stdinData.data, 1, stdinData.size, tmp);
                fclose(tmp);
                avifReadImage(tmp_path, AVIF_APP_FILE_FORMAT_UNKNOWN,
                              p->requestedFormat, p->requestedDepth,
                              AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                              p->ignoreColorProfile, p->ignoreExif, p->ignoreXMP,
                              p->ignoreGainMap, AVIF_DEFAULT_IMAGE_SIZE_LIMIT,
                              image, NULL, NULL, NULL);
                readOk = (image->width > 0 && image->height > 0);
                remove(tmp_path);
            }
        }
        avifRWDataFree(&stdinData);
        if (!readOk) {
            fprintf(stderr, "ERROR: Failed to read from stdin\n");
            goto cleanup;
        }
        printf("Successfully loaded: (stdin)\n");
    } else {
        avifAppFileFormat fmt = avifReadImage(
            in_file, AVIF_APP_FILE_FORMAT_UNKNOWN,
            p->requestedFormat, p->requestedDepth,
            AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
            p->ignoreColorProfile, p->ignoreExif, p->ignoreXMP,
            p->ignoreGainMap, AVIF_DEFAULT_IMAGE_SIZE_LIMIT,
            image, NULL, NULL, NULL);
        if (fmt == AVIF_APP_FILE_FORMAT_UNKNOWN) {
            fprintf(stderr, "ERROR: Failed to read: %s\n", in_file);
            goto cleanup;
        }
        printf("Successfully loaded: %s\n", in_file);
    }

    /* Fix unspecified CICP */
    if (!image->icc.size && !p->cicpExplicitlySet &&
        image->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED &&
        image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_SRGB;
        image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    }
    if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
        image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
    }

    /* Create and configure encoder */
    encoder = avifEncoderCreate();
    if (!encoder) { fprintf(stderr, "ERROR: Out of memory\n"); goto cleanup; }

    encoder->maxThreads = p->jobs;
    encoder->codecChoice = p->codecChoice;
    encoder->speed = p->speed;
    encoder->quality = p->quality;
    encoder->qualityAlpha = p->qualityAlpha;
    encoder->headerFormat = p->headerFormat;

    printf("Encoding with codec '%s' speed [%d], "
           "color quality [%d (%s)], alpha quality [%d (%s)], "
           "automatic tiling, %d worker thread(s), please wait...\n",
           avifCodecName(p->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE),
           p->speed,
           p->quality, (p->quality == AVIF_QUALITY_LOSSLESS) ? "Lossless" :
                        (p->quality >= 80) ? "High" :
                        (p->quality >= 50) ? "Medium" : "Low",
           p->qualityAlpha, (p->qualityAlpha == AVIF_QUALITY_LOSSLESS) ? "Lossless" :
                            (p->qualityAlpha >= 80) ? "High" :
                            (p->qualityAlpha >= 50) ? "Medium" : "Low",
           p->jobs);

    avifResult res = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
    if (res != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to encode: %s\n", avifResultToString(res));
        goto cleanup;
    }
    res = avifEncoderFinish(encoder, &raw);
    if (res != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to finish: %s\n", avifResultToString(res));
        goto cleanup;
    }

    printf("Encoded successfully.\n");
    printf(" * Color total size: %" AVIF_FMT_ZU " bytes\n", encoder->ioStats.colorOBUSize);
    printf(" * Alpha total size: %" AVIF_FMT_ZU " bytes\n", encoder->ioStats.alphaOBUSize);

    /* Write output */
    FILE* f = fopen(out_file, "wb");
    if (!f) { fprintf(stderr, "Failed to open: %s\n", out_file); goto cleanup; }
    if (fwrite(raw.data, 1, raw.size, f) != raw.size) {
        fprintf(stderr, "Failed to write: %s\n", out_file);
        fclose(f); goto cleanup;
    }
    fclose(f);
    printf("Wrote AVIF: %s\n", out_file);
    returnCode = 0;

cleanup:
    if (image) avifImageDestroy(image);
    avifRWDataFree(&raw);
    if (encoder) avifEncoderDestroy(encoder);
    return returnCode;
}

/* ================================================================ */
/* mi_avifenc_io_indices                                             */
/* ================================================================ */

int mi_avifenc_io_indices(int argc, const char* argv[],
                          int* indices, int max_indices) {
    int count = 0;
    for (int i = 1; i < argc && count < max_indices; ++i) {
        if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc) {
            indices[count++] = ++i;
        } else if (!strcmp(argv[i], "--stdin")) {
            /* not a path */
        } else if (argv[i][0] == '-') {
            if (i + 1 < argc && argv[i+1][0] != '-') ++i;
        } else {
            indices[count++] = i;
        }
    }
    return count;
}
