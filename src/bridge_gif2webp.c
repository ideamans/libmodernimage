/*
 * libmodernimage - gif2webp bridge
 *
 * Renames main() to modernimage_gif2webp_main() and resets global state.
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

#if defined(HAVE_UNISTD_H) && HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gif_lib.h>

#include "../deps/libwebp/imageio/imageio_util.h"
#include "../deps/libwebp/examples/example_util.h"
#include "../deps/libwebp/examples/gifdec.h"
#include "sharpyuv/sharpyuv.h"
#include "webp/encode.h"
#include "webp/mux.h"

/* Prevent re-inclusion of headers we already included */
#define WEBP_EXAMPLES_UNICODE_H_    /* block unicode.h - we provide macros below */
#define WEBP_IMAGEIO_IMAGEIO_UTIL_H_
#define WEBP_EXAMPLES_EXAMPLE_UTIL_H_
#define WEBP_EXAMPLES_GIFDEC_H_

/* unicode.h macros (non-Windows) */
#define W_CHAR char
#define INIT_WARGV(ARGC, ARGV)
#define GET_WARGV(ARGV, C) (ARGV)[C]
#define GET_WARGV_SHIFTED(ARGV, C) (ARGV)[C]
#define GET_WARGV_OR_NULL() NULL
#define FREE_WARGV()
#define LOCAL_FREE(WARGV)
#define TO_W_CHAR(STR) (STR)
#define WFOPEN(ARG, OPT) fopen(ARG, OPT)
#define WSTRLEN(FILENAME) strlen(FILENAME)
#define WSTRCMP(FILENAME, STR) strcmp(FILENAME, STR)
#define WSTRRCHR(FILENAME, STR) strrchr(FILENAME, STR)
#define WSNPRINTF(A, B, STR, ...) snprintf(A, B, STR, __VA_ARGS__)
#define FREE_WARGV_AND_RETURN(VALUE) do { return (VALUE); } while (0)
#define WPRINTF(STR, ...) printf(STR, __VA_ARGS__)
#define WFPRINTF(STREAM, STR, ...) fprintf(STREAM, STR, __VA_ARGS__)

/* DO NOT block unicode_gif.h - it defines DGifOpenFileUnicode which is used */
/* unicode_gif.h will try to include unicode.h (blocked above) and gifdec.h (blocked above) */
/* but that's fine since we already provided all the macros it needs */

/* Rename main */
#define main modernimage_gif2webp_main
#include "../deps/libwebp/examples/gif2webp.c"
#undef main
