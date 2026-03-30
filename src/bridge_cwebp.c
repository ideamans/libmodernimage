/*
 * libmodernimage - cwebp bridge
 *
 * Renames main() to modernimage_cwebp_main() and resets global state.
 * stdout/stderr redirection is handled at the fd level by the caller.
 */

/* Include all headers the original source needs */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "webp/config.h"
#endif

#include "../deps/libwebp/imageio/image_dec.h"
#include "../deps/libwebp/imageio/imageio_util.h"
#include "../deps/libwebp/imageio/webpdec.h"
#include "../deps/libwebp/examples/example_util.h"
#include "../deps/libwebp/imageio/metadata.h"
#include "sharpyuv/sharpyuv.h"
#include "webp/encode.h"
#include "webp/types.h"

/* Prevent re-inclusion of headers the original source #includes */
#define WEBP_EXAMPLES_UNICODE_H_
#define WEBP_EXAMPLES_STOPWATCH_H_
#define WEBP_IMAGEIO_IMAGE_DEC_H_
#define WEBP_IMAGEIO_IMAGEIO_UTIL_H_
#define WEBP_IMAGEIO_WEBPDEC_H_
#define WEBP_EXAMPLES_EXAMPLE_UTIL_H_
#define WEBP_IMAGEIO_METADATA_H_

/* Provide unicode.h macros (non-Windows) */
#define W_CHAR char
#define INIT_WARGV(ARGC, ARGV)
#define GET_WARGV(ARGV, C) (ARGV)[C]
#define GET_WARGV_SHIFTED(ARGV, C) (ARGV)[C]
#define GET_WARGV_OR_NULL() NULL
#define FREE_WARGV()
#define LOCAL_FREE(WARGV)
#define TO_W_CHAR(STR) (STR)
#define WFOPEN(ARG, OPT) fopen(ARG, OPT)
#define WPRINTF(STR, ...) printf(STR, __VA_ARGS__)
#define WFPRINTF(STREAM, STR, ...) fprintf(STREAM, STR, __VA_ARGS__)
#define WSTRLEN(FILENAME) strlen(FILENAME)
#define WSTRCMP(FILENAME, STR) strcmp(FILENAME, STR)
#define WSTRRCHR(FILENAME, STR) strrchr(FILENAME, STR)
#define WSNPRINTF(A, B, STR, ...) snprintf(A, B, STR, __VA_ARGS__)
#define FREE_WARGV_AND_RETURN(VALUE) do { return (VALUE); } while (0)

/* Provide stopwatch stubs */
typedef struct { int dummy; } Stopwatch;
static inline void StopwatchReset(Stopwatch* s) { (void)s; }
static inline double StopwatchReadAndReset(Stopwatch* s) { (void)s; return 0.0; }

/* Rename main */
#define main modernimage_cwebp_main
#include "../deps/libwebp/examples/cwebp.c"
#undef main
