/*
 * libmodernimage - avifenc engine
 *
 * Caches avifenc's parsed settings for reuse across calls
 * with different input/output files.
 */

#ifndef MI_ENGINE_AVIFENC_H_
#define MI_ENGINE_AVIFENC_H_

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parsed avifenc parameters (cacheable).
 * Mirrors the local variables in avifenc main() that are set during
 * argv parsing and used during encoding.
 */
typedef struct {
    /* The main settings struct from avifenc */
    int jobs;
    avifCodecChoice codecChoice;
    int speed;
    int quality;
    int qualityAlpha;
    avifBool lossless;
    avifBool premultiplyAlpha;
    avifRange requestedRange;
    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avifMatrixCoefficients matrixCoefficients;
    avifBool cicpExplicitlySet;
    avifPixelFormat requestedFormat;
    int requestedDepth;
    avifHeaderFormat headerFormat;
    avifBool ignoreExif;
    avifBool ignoreXMP;
    avifBool ignoreColorProfile;
    avifBool ignoreGainMap;
    avifBool noOverwrite;
} mi_avifenc_params_t;

/*
 * Parse avifenc argv into params. Returns 0 on success.
 * Extracts input/output paths.
 */
int mi_avifenc_parse(mi_avifenc_params_t* params, int argc, char* argv[],
                     const char** out_in_file, const char** out_out_file);

/*
 * Encode using pre-parsed params.
 * Returns 0 on success, 1 on failure.
 */
int mi_avifenc_encode(const mi_avifenc_params_t* params,
                      const char* in_file, const char* out_file);

/*
 * Identify argv indices that are input/output paths.
 */
int mi_avifenc_io_indices(int argc, const char* argv[],
                          int* indices, int max_indices);

#ifdef __cplusplus
}
#endif

#endif /* MI_ENGINE_AVIFENC_H_ */
