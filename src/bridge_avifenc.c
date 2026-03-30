/*
 * libmodernimage - avifenc bridge
 *
 * Renames main() to modernimage_avifenc_main().
 * stdout/stderr redirection is handled at the fd level by the caller.
 */

/* Include all headers the original source needs */
#include "avif/avif.h"
#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prevent re-inclusion in the original source */
#define AVIF_AVIF_H
#define LIBAVIF_APPS_SHARED_AVIFJPEG_H
#define LIBAVIF_APPS_SHARED_AVIFPNG_H
#define LIBAVIF_APPS_SHARED_AVIFUTIL_H
#define LIBAVIF_APPS_SHARED_Y4M_H

/* Rename main */
#define main modernimage_avifenc_main
#include "../deps/libavif/apps/avifenc.c"
#undef main
