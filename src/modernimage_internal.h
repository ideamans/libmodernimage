/*
 * libmodernimage - Internal header
 */

#ifndef MODERNIMAGE_INTERNAL_H_
#define MODERNIMAGE_INTERNAL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic memory buffer for capturing output */
typedef struct {
    char*  data;
    size_t size;
    size_t capacity;
} mi_buffer_t;

void mi_buffer_init(mi_buffer_t* buf);
void mi_buffer_free(mi_buffer_t* buf);
void mi_buffer_reset(mi_buffer_t* buf);
void mi_buffer_write(mi_buffer_t* buf, const char* data, size_t len);

/* Context structure */
struct modernimage_context {
    mi_buffer_t  out_buf;
    mi_buffer_t  err_buf;
    int          exit_code;
    /* Stdin injection (borrowed pointer, caller owns the data) */
    const void*  stdin_data;
    size_t       stdin_size;
};

/* Internal main functions (defined in bridge_*.c) */
int modernimage_cwebp_main(int argc, const char* argv[]);
int modernimage_gif2webp_main(int argc, const char* argv[]);
int modernimage_avifenc_main(int argc, char* argv[]);
int modernimage_jpegtran_main(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif /* MODERNIMAGE_INTERNAL_H_ */
