/*
 * libmodernimage - cwebp engine
 *
 * Splits cwebp main() into parse (config construction) and encode phases.
 * The parse result (mi_cwebp_params_t) can be cached and reused.
 */

#ifndef MI_ENGINE_CWEBP_H_
#define MI_ENGINE_CWEBP_H_

#include "webp/encode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed parameters from cwebp argv (cacheable) */
typedef struct {
    WebPConfig config;
    int short_output;
    int quiet;
    int keep_alpha;
    int blend_alpha;
    uint32_t background_color;
    int crop, crop_x, crop_y, crop_w, crop_h;
    int resize_w, resize_h, resize_mode;
    int lossless_preset;
    int use_lossless_preset;
    int show_progress;
    int keep_metadata;
    int print_distortion;
    int verbose;
    int extra_info_type;  /* picture.extra_info_type from -map */
} mi_cwebp_params_t;

/*
 * Parse cwebp argv into params. Faithfully replicates cwebp main()'s
 * argument parsing and validation logic.
 *
 * Returns 0 on success. Extracts input/output paths into the pointers.
 * On failure, returns non-zero (e.g. invalid args).
 */
int mi_cwebp_parse(mi_cwebp_params_t* params, int argc, const char* argv[],
                   const char** out_in_file, const char** out_out_file);

/*
 * Encode a single image using pre-parsed params.
 * Returns EXIT_SUCCESS or EXIT_FAILURE.
 */
int mi_cwebp_encode(const mi_cwebp_params_t* params,
                    const char* in_file, const char* out_file);

/*
 * Identify argv indices that are input/output paths (for cache key generation).
 * Returns the number of indices written.
 */
int mi_cwebp_io_indices(int argc, const char* argv[],
                        int* indices, int max_indices);

#ifdef __cplusplus
}
#endif

#endif /* MI_ENGINE_CWEBP_H_ */
