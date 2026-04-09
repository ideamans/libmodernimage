/*
 * libmodernimage - jpegtran bridge
 *
 * Custom implementation using libjpeg-turbo's transupp API.
 * Unlike other bridges, we cannot #include jpegtran.c directly because
 * cdjpeg.h and transupp.h lack include guards. Instead, we implement
 * a jpegtran-compatible CLI interface using the same underlying API.
 *
 * Supports the subset of jpegtran options needed by modernimage:
 *   -rotate 90|180|270
 *   -flip horizontal|vertical
 *   -transpose
 *   -transverse
 *   -trim
 *   -copy none|comments|icc|all
 *   -outfile <path>
 *   [inputfile]  (or stdin if no file specified)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JPEG_INTERNAL_OPTIONS
#define JPEG_INTERNAL_OPTIONS
#endif
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"
#include "transupp.h"

static int keymatch_simple(const char* arg, const char* keyword) {
    while (*arg && *keyword) {
        if (*arg != *keyword) return 0;
        arg++;
        keyword++;
    }
    return (*arg == '\0');
}

int modernimage_jpegtran_main(int argc, char** argv) {
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct jpeg_error_mgr jsrcerr, jdsterr;
    jvirt_barray_ptr *src_coef_arrays;
    jvirt_barray_ptr *dst_coef_arrays;
    FILE *input_file = NULL;
    FILE *output_file = NULL;
    const char *infilename = NULL;
    const char *outfilename = NULL;
    JCOPY_OPTION copyoption = JCOPYOPT_DEFAULT;
    jpeg_transform_info transformoption;
    int retval = 1;

    memset(&transformoption, 0, sizeof(transformoption));
    transformoption.transform = JXFORM_NONE;

    /* Parse arguments (skip argv[0] = program name) */
    int argn = 1;
    while (argn < argc) {
        const char *arg = argv[argn];
        if (arg[0] != '-') {
            /* Not a switch - treat as input filename */
            infilename = arg;
            argn++;
            continue;
        }
        arg++;  /* skip '-' */

        if (keymatch_simple(arg, "rotate")) {
            if (++argn >= argc) goto usage_err;
            if (keymatch_simple(argv[argn], "90"))
                transformoption.transform = JXFORM_ROT_90;
            else if (keymatch_simple(argv[argn], "180"))
                transformoption.transform = JXFORM_ROT_180;
            else if (keymatch_simple(argv[argn], "270"))
                transformoption.transform = JXFORM_ROT_270;
            else goto usage_err;
        } else if (keymatch_simple(arg, "flip")) {
            if (++argn >= argc) goto usage_err;
            if (keymatch_simple(argv[argn], "horizontal"))
                transformoption.transform = JXFORM_FLIP_H;
            else if (keymatch_simple(argv[argn], "vertical"))
                transformoption.transform = JXFORM_FLIP_V;
            else goto usage_err;
        } else if (keymatch_simple(arg, "transpose")) {
            transformoption.transform = JXFORM_TRANSPOSE;
        } else if (keymatch_simple(arg, "transverse")) {
            transformoption.transform = JXFORM_TRANSVERSE;
        } else if (keymatch_simple(arg, "trim")) {
            transformoption.trim = TRUE;
        } else if (keymatch_simple(arg, "copy")) {
            if (++argn >= argc) goto usage_err;
            if (keymatch_simple(argv[argn], "none"))
                copyoption = JCOPYOPT_NONE;
            else if (keymatch_simple(argv[argn], "comments"))
                copyoption = JCOPYOPT_COMMENTS;
            else if (keymatch_simple(argv[argn], "icc"))
                copyoption = JCOPYOPT_ICC;
            else if (keymatch_simple(argv[argn], "all"))
                copyoption = JCOPYOPT_ALL;
            else goto usage_err;
        } else if (keymatch_simple(arg, "outfile")) {
            if (++argn >= argc) goto usage_err;
            outfilename = argv[argn];
        } else {
            goto usage_err;
        }
        argn++;
    }

    /* Initialize JPEG objects */
    srcinfo.err = jpeg_std_error(&jsrcerr);
    jpeg_create_decompress(&srcinfo);
    dstinfo.err = jpeg_std_error(&jdsterr);
    jpeg_create_compress(&dstinfo);

    /* Open input */
    if (infilename != NULL) {
        input_file = fopen(infilename, "rb");
        if (input_file == NULL) {
            fprintf(stderr, "jpegtran: can't open %s for reading\n", infilename);
            goto cleanup;
        }
    } else {
        input_file = stdin;
    }

    /* Open output */
    if (outfilename != NULL) {
        output_file = fopen(outfilename, "wb");
        if (output_file == NULL) {
            fprintf(stderr, "jpegtran: can't open %s for writing\n", outfilename);
            goto cleanup;
        }
    } else {
        output_file = stdout;
    }

    /* Set up source */
    jpeg_stdio_src(&srcinfo, input_file);

    /* Enable saving of extra markers */
    jcopy_markers_setup(&srcinfo, copyoption);

    /* Read header */
    jpeg_read_header(&srcinfo, TRUE);

    /* Request transform workspace */
#if TRANSFORMS_SUPPORTED
    if (!jtransform_request_workspace(&srcinfo, &transformoption)) {
        fprintf(stderr, "jpegtran: transformation is not perfect\n");
        goto cleanup;
    }
#endif

    /* Read coefficients */
    src_coef_arrays = jpeg_read_coefficients(&srcinfo);

    /* Copy critical parameters */
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    /* Adjust for transform */
#if TRANSFORMS_SUPPORTED
    dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
                                                    src_coef_arrays,
                                                    &transformoption);
#else
    dst_coef_arrays = src_coef_arrays;
#endif

    /* Close input if it was a file */
    if (input_file != stdin) {
        fclose(input_file);
        input_file = NULL;
    }

    /* Set up destination */
    jpeg_stdio_dest(&dstinfo, output_file);

    /* Write coefficients */
    jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

    /* Copy markers */
    jcopy_markers_execute(&srcinfo, &dstinfo, copyoption);

    /* Execute transform */
#if TRANSFORMS_SUPPORTED
    jtransform_execute_transformation(&srcinfo, &dstinfo, src_coef_arrays,
                                      &transformoption);
#endif

    /* Finish */
    jpeg_finish_compress(&dstinfo);
    jpeg_finish_decompress(&srcinfo);

    retval = 0;  /* success */
    goto cleanup;

usage_err:
    fprintf(stderr, "jpegtran: invalid arguments\n");
    retval = 1;

cleanup:
    jpeg_destroy_compress(&dstinfo);
    jpeg_destroy_decompress(&srcinfo);
    if (input_file != NULL && input_file != stdin)
        fclose(input_file);
    if (output_file != NULL && output_file != stdout)
        fclose(output_file);

    return retval;
}
