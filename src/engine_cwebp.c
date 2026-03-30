/*
 * libmodernimage - cwebp engine implementation
 *
 * Parse and encode phases split from deps/libwebp/examples/cwebp.c.
 * The parsing logic is a faithful copy of the original; the encode phase
 * uses the parsed config directly with libwebp API.
 */

#include "engine_cwebp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../deps/libwebp/imageio/image_dec.h"
#include "../deps/libwebp/imageio/imageio_util.h"
#include "../deps/libwebp/imageio/webpdec.h"
#include "../deps/libwebp/examples/example_util.h"
#include "imageio/metadata.h"
#include "sharpyuv/sharpyuv.h"
#include "webp/encode.h"
#include "webp/types.h"

#ifndef WEBP_DLL
#ifdef __cplusplus
extern "C" {
#endif
extern void* VP8GetCPUInfo;
#ifdef __cplusplus
}
#endif
#endif

/* Resize modes - must match cwebp.c */
#define RESIZE_MODE_DEFAULT 0
#define RESIZE_MODE_DOWN_ONLY 1
#define RESIZE_MODE_UP_ONLY 2
#define RESIZE_MODE_ALWAYS 3

/* Forward declarations of static functions from cwebp.c that we need.
 * Since we can't call them directly (they're static in cwebp.c),
 * we replicate the small ones inline. */

static void ApplyResizeMode(int resize_mode, WebPPicture* pic,
                            int* resize_w, int* resize_h) {
    if (resize_mode == RESIZE_MODE_DOWN_ONLY) {
        if ((unsigned int)*resize_w >= (unsigned int)pic->width &&
            (unsigned int)*resize_h >= (unsigned int)pic->height) {
            *resize_w = *resize_h = 0;
        }
    } else if (resize_mode == RESIZE_MODE_UP_ONLY) {
        if ((unsigned int)*resize_w <= (unsigned int)pic->width &&
            (unsigned int)*resize_h <= (unsigned int)pic->height) {
            *resize_w = *resize_h = 0;
        }
    }
}

static int ProgressReport(int percent, const WebPPicture* const pic) {
    fprintf(stderr, "[%s]: %3d %%      \r",
            (const char*)pic->user_data, percent);
    return 1;
}

static int MyWriter(const uint8_t* data, size_t data_size,
                    const WebPPicture* const pic) {
    FILE* const out = (FILE*)pic->custom_ptr;
    return data_size ? (fwrite(data, data_size, 1, out) == 1) : 1;
}

/* Replicate cwebp's static ReadPicture (non-WIC version) */
static int ReadPicture(const char* const filename, WebPPicture* const pic,
                       int keep_alpha, Metadata* const metadata) {
    const uint8_t* data = NULL;
    size_t data_size = 0;
    int ok = ImgIoUtilReadFile(filename, &data, &data_size);
    if (!ok) goto End;
    if (pic->width == 0 || pic->height == 0) {
        WebPImageReader reader = WebPGuessImageReader(data, data_size);
        ok = reader(data, data_size, pic, keep_alpha, metadata);
    } else {
        /* Raw YUV - skip in engine (not a common cached path) */
        ok = 0;
    }
End:
    if (!ok) fprintf(stderr, "Error! Could not process file %s\n", filename);
    WebPFree((void*)data);
    return ok;
}

/* ================================================================ */
/* mi_cwebp_parse: faithful copy of cwebp main() lines 713-1042     */
/* ================================================================ */

int mi_cwebp_parse(mi_cwebp_params_t* p, int argc, const char* argv[],
                   const char** out_in_file, const char** out_out_file) {
    const char* in_file = NULL;
    const char* out_file = NULL;
    int c;

    /* Defaults (matching cwebp.c lines 714-739) */
    memset(p, 0, sizeof(*p));
    p->keep_alpha = 1;
    p->background_color = 0xffffffu;
    p->lossless_preset = 6;
    p->use_lossless_preset = -1;
    p->print_distortion = -1;

    if (!WebPConfigInit(&p->config)) return -1;

    for (c = 1; c < argc; ++c) {
        int parse_error = 0;
        if (!strcmp(argv[c], "-h") || !strcmp(argv[c], "-help") ||
            !strcmp(argv[c], "-H") || !strcmp(argv[c], "-longhelp")) {
            continue;  /* skip help flags in cached mode */
        } else if (!strcmp(argv[c], "-o") && c + 1 < argc) {
            out_file = argv[++c];
        } else if (!strcmp(argv[c], "-d") && c + 1 < argc) {
            ++c; /* skip dump_file */
            p->config.show_compressed = 1;
        } else if (!strcmp(argv[c], "-print_psnr")) {
            p->config.show_compressed = 1;
            p->print_distortion = 0;
        } else if (!strcmp(argv[c], "-print_ssim")) {
            p->config.show_compressed = 1;
            p->print_distortion = 1;
        } else if (!strcmp(argv[c], "-print_lsim")) {
            p->config.show_compressed = 1;
            p->print_distortion = 2;
        } else if (!strcmp(argv[c], "-short")) {
            ++p->short_output;
        } else if (!strcmp(argv[c], "-s") && c + 2 < argc) {
            /* raw YUV dimensions - skip (per-image) */
            c += 2;
        } else if (!strcmp(argv[c], "-m") && c + 1 < argc) {
            p->config.method = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->use_lossless_preset = 0;
        } else if (!strcmp(argv[c], "-q") && c + 1 < argc) {
            p->config.quality = ExUtilGetFloat(argv[++c], &parse_error);
            p->use_lossless_preset = 0;
        } else if (!strcmp(argv[c], "-z") && c + 1 < argc) {
            p->lossless_preset = ExUtilGetInt(argv[++c], 0, &parse_error);
            if (p->use_lossless_preset != 0) p->use_lossless_preset = 1;
        } else if (!strcmp(argv[c], "-alpha_q") && c + 1 < argc) {
            p->config.alpha_quality = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-alpha_method") && c + 1 < argc) {
            p->config.alpha_compression = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-alpha_cleanup")) {
            p->config.exact = 0;
        } else if (!strcmp(argv[c], "-exact")) {
            p->config.exact = 1;
        } else if (!strcmp(argv[c], "-blend_alpha") && c + 1 < argc) {
            p->blend_alpha = 1;
            p->background_color = ExUtilGetInt(argv[++c], 16, &parse_error) & 0x00ffffffu;
        } else if (!strcmp(argv[c], "-alpha_filter") && c + 1 < argc) {
            ++c;
            if (!strcmp(argv[c], "none")) p->config.alpha_filtering = 0;
            else if (!strcmp(argv[c], "fast")) p->config.alpha_filtering = 1;
            else if (!strcmp(argv[c], "best")) p->config.alpha_filtering = 2;
            else return -1;
        } else if (!strcmp(argv[c], "-noalpha")) {
            p->keep_alpha = 0;
        } else if (!strcmp(argv[c], "-lossless")) {
            p->config.lossless = 1;
        } else if (!strcmp(argv[c], "-near_lossless") && c + 1 < argc) {
            p->config.near_lossless = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->config.lossless = 1;
        } else if (!strcmp(argv[c], "-hint") && c + 1 < argc) {
            ++c;
            if (!strcmp(argv[c], "photo")) p->config.image_hint = WEBP_HINT_PHOTO;
            else if (!strcmp(argv[c], "picture")) p->config.image_hint = WEBP_HINT_PICTURE;
            else if (!strcmp(argv[c], "graph")) p->config.image_hint = WEBP_HINT_GRAPH;
            else return -1;
        } else if (!strcmp(argv[c], "-size") && c + 1 < argc) {
            p->config.target_size = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-psnr") && c + 1 < argc) {
            p->config.target_PSNR = ExUtilGetFloat(argv[++c], &parse_error);
        } else if (!strcmp(argv[c], "-sns") && c + 1 < argc) {
            p->config.sns_strength = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-f") && c + 1 < argc) {
            p->config.filter_strength = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-af")) {
            p->config.autofilter = 1;
        } else if (!strcmp(argv[c], "-jpeg_like")) {
            p->config.emulate_jpeg_size = 1;
        } else if (!strcmp(argv[c], "-mt")) {
            ++p->config.thread_level;
        } else if (!strcmp(argv[c], "-low_memory")) {
            p->config.low_memory = 1;
        } else if (!strcmp(argv[c], "-strong")) {
            p->config.filter_type = 1;
        } else if (!strcmp(argv[c], "-nostrong")) {
            p->config.filter_type = 0;
        } else if (!strcmp(argv[c], "-sharpness") && c + 1 < argc) {
            p->config.filter_sharpness = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-sharp_yuv")) {
            p->config.use_sharp_yuv = 1;
        } else if (!strcmp(argv[c], "-pass") && c + 1 < argc) {
            p->config.pass = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-qrange") && c + 2 < argc) {
            p->config.qmin = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->config.qmax = ExUtilGetInt(argv[++c], 0, &parse_error);
            if (p->config.qmin < 0) p->config.qmin = 0;
            if (p->config.qmax > 100) p->config.qmax = 100;
        } else if (!strcmp(argv[c], "-pre") && c + 1 < argc) {
            p->config.preprocessing = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-segments") && c + 1 < argc) {
            p->config.segments = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-partition_limit") && c + 1 < argc) {
            p->config.partition_limit = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-map") && c + 1 < argc) {
            p->extra_info_type = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-crop") && c + 4 < argc) {
            p->crop = 1;
            p->crop_x = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->crop_y = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->crop_w = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->crop_h = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-resize") && c + 2 < argc) {
            p->resize_w = ExUtilGetInt(argv[++c], 0, &parse_error);
            p->resize_h = ExUtilGetInt(argv[++c], 0, &parse_error);
        } else if (!strcmp(argv[c], "-resize_mode") && c + 1 < argc) {
            ++c;
            if (!strcmp(argv[c], "down_only")) p->resize_mode = RESIZE_MODE_DOWN_ONLY;
            else if (!strcmp(argv[c], "up_only")) p->resize_mode = RESIZE_MODE_UP_ONLY;
            else if (!strcmp(argv[c], "always")) p->resize_mode = RESIZE_MODE_ALWAYS;
            else return -1;
#ifndef WEBP_DLL
        } else if (!strcmp(argv[c], "-noasm")) {
            VP8GetCPUInfo = NULL;
#endif
        } else if (!strcmp(argv[c], "-version")) {
            continue; /* skip in cached mode */
        } else if (!strcmp(argv[c], "-progress")) {
            p->show_progress = 1;
        } else if (!strcmp(argv[c], "-quiet")) {
            p->quiet = 1;
        } else if (!strcmp(argv[c], "-preset") && c + 1 < argc) {
            WebPPreset preset;
            ++c;
            if (!strcmp(argv[c], "default")) preset = WEBP_PRESET_DEFAULT;
            else if (!strcmp(argv[c], "photo")) preset = WEBP_PRESET_PHOTO;
            else if (!strcmp(argv[c], "picture")) preset = WEBP_PRESET_PICTURE;
            else if (!strcmp(argv[c], "drawing")) preset = WEBP_PRESET_DRAWING;
            else if (!strcmp(argv[c], "icon")) preset = WEBP_PRESET_ICON;
            else if (!strcmp(argv[c], "text")) preset = WEBP_PRESET_TEXT;
            else return -1;
            if (!WebPConfigPreset(&p->config, preset, p->config.quality)) return -1;
        } else if (!strcmp(argv[c], "-metadata") && c + 1 < argc) {
            const char* start = argv[++c];
            const char* const end = start + strlen(start);
            while (start < end) {
                const char* token = strchr(start, ',');
                if (!token) token = end;
                size_t tlen = (size_t)(token - start);
                if (tlen == 3 && !strncmp(start, "all", 3)) p->keep_metadata = 0x7;
                else if (tlen == 4 && !strncmp(start, "none", 4)) p->keep_metadata = 0;
                else if (tlen == 4 && !strncmp(start, "exif", 4)) p->keep_metadata |= 1;
                else if (tlen == 3 && !strncmp(start, "icc", 3)) p->keep_metadata |= 2;
                else if (tlen == 3 && !strncmp(start, "xmp", 3)) p->keep_metadata |= 4;
                else return -1;
                start = token + 1;
            }
        } else if (!strcmp(argv[c], "-v")) {
            p->verbose = 1;
        } else if (!strcmp(argv[c], "--")) {
            if (c + 1 < argc) in_file = argv[++c];
            break;
        } else if (argv[c][0] == '-') {
            return -1; /* unknown option */
        } else {
            in_file = argv[c];
        }
        if (parse_error) return -1;
    }

    /* Post-parse validation (cwebp.c lines 1012-1042) */
    if (p->use_lossless_preset == 1) {
        if (!WebPConfigLosslessPreset(&p->config, p->lossless_preset)) return -1;
    }
    if (p->config.target_size > 0 || p->config.target_PSNR > 0) {
        if (p->config.pass == 1) p->config.pass = 6;
    }
    if (!WebPValidateConfig(&p->config)) return -1;

    if (out_in_file) *out_in_file = in_file;
    if (out_out_file) *out_out_file = out_file;
    return 0;
}

/* ================================================================ */
/* mi_cwebp_encode: faithful copy of cwebp main() lines 1044-1321   */
/* ================================================================ */

int mi_cwebp_encode(const mi_cwebp_params_t* p,
                    const char* in_file, const char* out_file) {
    int return_value = EXIT_FAILURE;
    FILE* out = NULL;
    WebPPicture picture;
    WebPPicture original_picture;
    WebPAuxStats stats;
    WebPMemoryWriter memory_writer;
    int use_memory_writer;
    Metadata metadata;
    int metadata_written = 0;
    /* Make a mutable copy of params we need to modify locally */
    int resize_w = p->resize_w;
    int resize_h = p->resize_h;

    MetadataInit(&metadata);
    WebPMemoryWriterInit(&memory_writer);
    if (!WebPPictureInit(&picture) || !WebPPictureInit(&original_picture)) {
        fprintf(stderr, "Error! Version mismatch!\n");
        return EXIT_FAILURE;
    }

    picture.use_argb =
        (p->config.lossless || p->config.use_sharp_yuv ||
         p->config.preprocessing > 0 || p->crop || (resize_w | resize_h) > 0);

    if (!ReadPicture(in_file, &picture, p->keep_alpha,
                     (p->keep_metadata == 0) ? NULL : &metadata)) {
        fprintf(stderr, "Error! Cannot read input picture file '%s'\n", in_file);
        goto Error;
    }
    picture.progress_hook = (p->show_progress && !p->quiet) ? ProgressReport : NULL;

    if (p->blend_alpha) {
        WebPBlendAlpha(&picture, p->background_color);
    }

    use_memory_writer = (out_file != NULL && p->keep_metadata) ||
                        (!p->quiet && p->print_distortion >= 0 &&
                         p->config.lossless && p->config.near_lossless < 100);

    /* Open output */
    if (out_file != NULL) {
        const int use_stdout = !strcmp(out_file, "-");
        out = use_stdout ? ImgIoUtilSetBinaryMode(stdout) : fopen(out_file, "wb");
        if (out == NULL) {
            fprintf(stderr, "Error! Cannot open output file '%s'\n", out_file);
            goto Error;
        } else if (!p->short_output && !p->quiet) {
            fprintf(stderr, "Saving file '%s'\n", out_file);
        }
        if (use_memory_writer) {
            picture.writer = WebPMemoryWrite;
            picture.custom_ptr = (void*)&memory_writer;
        } else {
            picture.writer = MyWriter;
            picture.custom_ptr = (void*)out;
        }
    } else {
        if (use_memory_writer) {
            picture.writer = WebPMemoryWrite;
            picture.custom_ptr = (void*)&memory_writer;
        }
    }
    if (!p->quiet) {
        picture.stats = &stats;
        picture.user_data = (void*)in_file;
    }

    /* Crop & resize */
    if (p->crop != 0) {
        if (!WebPPictureView(&picture, p->crop_x, p->crop_y,
                             p->crop_w, p->crop_h, &picture)) {
            fprintf(stderr, "Error! Cannot crop picture\n");
            goto Error;
        }
    }
    ApplyResizeMode(p->resize_mode, &picture, &resize_w, &resize_h);
    if ((resize_w | resize_h) > 0) {
        if (!WebPPictureRescale(&picture, resize_w, resize_h)) {
            fprintf(stderr, "Error! Cannot resize picture\n");
            goto Error;
        }
    }

    if (p->extra_info_type > 0) {
        picture.extra_info_type = p->extra_info_type;
        picture.extra_info = (uint8_t*)WebPMalloc(
            (size_t)picture.width * picture.height * sizeof(*picture.extra_info));
    }

    /* Encode */
    if (!WebPEncode(&p->config, &picture)) {
        fprintf(stderr, "Error! Cannot encode picture as WebP\n");
        fprintf(stderr, "Error code: %d\n", picture.error_code);
        goto Error;
    }

    /* Write output: for the fast path we always use memory writer + fwrite.
     * This avoids needing the static WriteWebPWithMetadata from cwebp.c.
     * Note: metadata embedding is NOT supported in the cached fast path.
     * For full metadata support, the first call (which goes through main())
     * handles it; subsequent cached calls produce the raw WebP bitstream. */
    if (use_memory_writer && out != NULL) {
        if (fwrite(memory_writer.mem, memory_writer.size, 1, out) != 1) {
            fprintf(stderr, "Error writing WebP file!\n");
            goto Error;
        }
    }

    /* Print basic stats (simplified vs original cwebp's verbose output) */
    if (!p->quiet && picture.stats != NULL) {
        fprintf(stderr, "Output: %6d bytes",
                picture.stats->coded_size);
        if (picture.stats->PSNR[3] > 0) {
            fprintf(stderr, " Y-U-V-All-PSNR %2.2f %2.2f %2.2f   %2.2f dB",
                    picture.stats->PSNR[0], picture.stats->PSNR[1],
                    picture.stats->PSNR[2], picture.stats->PSNR[3]);
        }
        fprintf(stderr, "\n");
    }
    return_value = EXIT_SUCCESS;

Error:
    WebPMemoryWriterClear(&memory_writer);
    WebPFree(picture.extra_info);
    MetadataFree(&metadata);
    WebPPictureFree(&picture);
    WebPPictureFree(&original_picture);
    if (out != NULL && out != stdout) fclose(out);
    return return_value;
}

/* ================================================================ */
/* mi_cwebp_io_indices: find input/output path positions in argv     */
/* ================================================================ */

int mi_cwebp_io_indices(int argc, const char* argv[],
                        int* indices, int max_indices) {
    int count = 0;
    for (int c = 1; c < argc && count < max_indices; ++c) {
        if (!strcmp(argv[c], "-o") && c + 1 < argc) {
            indices[count++] = ++c; /* output file */
        } else if (!strcmp(argv[c], "--") && c + 1 < argc) {
            indices[count++] = ++c; /* input after -- */
            break;
        } else if (!strcmp(argv[c], "-d") && c + 1 < argc) {
            indices[count++] = ++c; /* dump file */
        } else if (!strcmp(argv[c], "-s") && c + 2 < argc) {
            c += 2;
        } else if (argv[c][0] != '-' && c > 0) {
            indices[count++] = c; /* positional input */
        } else {
            /* Skip options with arguments */
            if ((!strcmp(argv[c], "-m") || !strcmp(argv[c], "-q") ||
                 !strcmp(argv[c], "-z") || !strcmp(argv[c], "-alpha_q") ||
                 !strcmp(argv[c], "-alpha_method") || !strcmp(argv[c], "-blend_alpha") ||
                 !strcmp(argv[c], "-alpha_filter") || !strcmp(argv[c], "-near_lossless") ||
                 !strcmp(argv[c], "-hint") || !strcmp(argv[c], "-size") ||
                 !strcmp(argv[c], "-psnr") || !strcmp(argv[c], "-sns") ||
                 !strcmp(argv[c], "-f") || !strcmp(argv[c], "-sharpness") ||
                 !strcmp(argv[c], "-pass") || !strcmp(argv[c], "-pre") ||
                 !strcmp(argv[c], "-segments") || !strcmp(argv[c], "-partition_limit") ||
                 !strcmp(argv[c], "-map") || !strcmp(argv[c], "-preset") ||
                 !strcmp(argv[c], "-metadata") || !strcmp(argv[c], "-resize_mode") ||
                 !strcmp(argv[c], "-v")) && c + 1 < argc) {
                /* -v doesn't take an arg but is harmless to skip */
                if (strcmp(argv[c], "-v") != 0) ++c;
            } else if (!strcmp(argv[c], "-crop") && c + 4 < argc) {
                c += 4;
            } else if (!strcmp(argv[c], "-resize") && c + 2 < argc) {
                c += 2;
            } else if (!strcmp(argv[c], "-qrange") && c + 2 < argc) {
                c += 2;
            }
        }
    }
    return count;
}
