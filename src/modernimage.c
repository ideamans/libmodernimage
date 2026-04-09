/*
 * libmodernimage - Core implementation
 *
 * Uses fd-level (dup2) redirection of stdin/stdout/stderr.
 * A global IO mutex serializes all tool calls to ensure dup2 safety.
 */

#include "modernimage.h"
#include "modernimage_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define MI_PIPE_TEXT(fds) _pipe(fds, 65536, _O_TEXT)
#define MI_PIPE_BIN(fds) _pipe(fds, 65536, _O_BINARY)
#define MI_DUP(fd) _dup(fd)
#define MI_DUP2(fd1, fd2) _dup2(fd1, fd2)
#define MI_CLOSE(fd) _close(fd)
#define MI_READ(fd, buf, n) _read(fd, buf, (unsigned int)(n))
#define MI_WRITE(fd, buf, n) _write(fd, buf, (unsigned int)(n))
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#else
#include <unistd.h>
#define MI_PIPE_TEXT(fds) pipe(fds)
#define MI_PIPE_BIN(fds) pipe(fds)
#define MI_DUP(fd) dup(fd)
#define MI_DUP2(fd1, fd2) dup2(fd1, fd2)
#define MI_CLOSE(fd) close(fd)
#define MI_READ(fd, buf, n) read(fd, buf, n)
#define MI_WRITE(fd, buf, n) write(fd, buf, n)
#endif

#define MODERNIMAGE_VERSION "0.2.0"

/* ========== mi_buffer_t ========== */

void mi_buffer_init(mi_buffer_t* buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

void mi_buffer_free(mi_buffer_t* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

void mi_buffer_reset(mi_buffer_t* buf) {
    buf->size = 0;
}

void mi_buffer_write(mi_buffer_t* buf, const char* data, size_t len) {
    if (len == 0) return;
    if (buf->size + len > buf->capacity) {
        size_t new_cap = buf->capacity ? buf->capacity * 2 : 4096;
        while (new_cap < buf->size + len) new_cap *= 2;
        char* new_data = realloc(buf->data, new_cap);
        if (!new_data) return;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

/* ========== fd-level stdio capture ========== */

static pthread_mutex_t g_io_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int saved_stdout;
    int saved_stderr;
    int saved_stdin;
    int pipe_out[2];  /* [0]=read, [1]=write */
    int pipe_err[2];
    int pipe_in[2];   /* [0]=read (becomes stdin), [1]=write (we feed data) */
    int has_stdin;     /* whether stdin was redirected */
} mi_capture_t;

/* Thread context for stdin writer */
typedef struct {
    int          fd;
    const void*  data;
    size_t       size;
} mi_stdin_writer_t;

static void* mi_stdin_writer_thread(void* arg) {
    mi_stdin_writer_t* w = (mi_stdin_writer_t*)arg;
    const char* p = (const char*)w->data;
    size_t remaining = w->size;
    while (remaining > 0) {
        int n = (int)MI_WRITE(w->fd, p, remaining > 32768 ? 32768 : remaining);
        if (n <= 0) break;
        p += n;
        remaining -= (size_t)n;
    }
    MI_CLOSE(w->fd);
    return NULL;
}

static int mi_capture_begin(mi_capture_t* cap,
                            const void* stdin_data, size_t stdin_size,
                            pthread_t* stdin_thread, mi_stdin_writer_t* stdin_ctx) {
    cap->has_stdin = (stdin_data != NULL && stdin_size > 0);

    /* stdout/stderr pipes use text mode (for fprintf compatibility on Windows) */
    if (MI_PIPE_TEXT(cap->pipe_out) != 0) return -1;
    if (MI_PIPE_TEXT(cap->pipe_err) != 0) {
        MI_CLOSE(cap->pipe_out[0]); MI_CLOSE(cap->pipe_out[1]);
        return -1;
    }

    if (cap->has_stdin) {
        /* stdin pipe uses binary mode (raw image data) */
        if (MI_PIPE_BIN(cap->pipe_in) != 0) {
            MI_CLOSE(cap->pipe_out[0]); MI_CLOSE(cap->pipe_out[1]);
            MI_CLOSE(cap->pipe_err[0]); MI_CLOSE(cap->pipe_err[1]);
            return -1;
        }
    }

    cap->saved_stdout = MI_DUP(STDOUT_FILENO);
    cap->saved_stderr = MI_DUP(STDERR_FILENO);
    cap->saved_stdin = cap->has_stdin ? MI_DUP(STDIN_FILENO) : -1;

    if (cap->saved_stdout < 0 || cap->saved_stderr < 0 ||
        (cap->has_stdin && cap->saved_stdin < 0)) {
        MI_CLOSE(cap->pipe_out[0]); MI_CLOSE(cap->pipe_out[1]);
        MI_CLOSE(cap->pipe_err[0]); MI_CLOSE(cap->pipe_err[1]);
        if (cap->has_stdin) { MI_CLOSE(cap->pipe_in[0]); MI_CLOSE(cap->pipe_in[1]); }
        if (cap->saved_stdout >= 0) MI_CLOSE(cap->saved_stdout);
        if (cap->saved_stderr >= 0) MI_CLOSE(cap->saved_stderr);
        if (cap->saved_stdin >= 0) MI_CLOSE(cap->saved_stdin);
        return -1;
    }

    fflush(stdout);
    fflush(stderr);
    MI_DUP2(cap->pipe_out[1], STDOUT_FILENO);
    MI_DUP2(cap->pipe_err[1], STDERR_FILENO);
    MI_CLOSE(cap->pipe_out[1]);
    MI_CLOSE(cap->pipe_err[1]);

    if (cap->has_stdin) {
        fflush(stdin);
        MI_DUP2(cap->pipe_in[0], STDIN_FILENO);
        MI_CLOSE(cap->pipe_in[0]);
        clearerr(stdin);
        fseek(stdin, 0, SEEK_SET);

        stdin_ctx->fd = cap->pipe_in[1];
        stdin_ctx->data = stdin_data;
        stdin_ctx->size = stdin_size;
        pthread_create(stdin_thread, NULL, mi_stdin_writer_thread, stdin_ctx);
    }

    return 0;
}

static void mi_drain_pipe(int fd, mi_buffer_t* buf) {
    char tmp[4096];
    int n;
    while ((n = (int)MI_READ(fd, tmp, sizeof(tmp))) > 0) {
        mi_buffer_write(buf, tmp, (size_t)n);
    }
}

static void mi_capture_end(mi_capture_t* cap, mi_buffer_t* out_buf, mi_buffer_t* err_buf,
                           pthread_t* stdin_thread) {
    fflush(stdout);
    fflush(stderr);

    MI_DUP2(cap->saved_stdout, STDOUT_FILENO);
    MI_DUP2(cap->saved_stderr, STDERR_FILENO);
    MI_CLOSE(cap->saved_stdout);
    MI_CLOSE(cap->saved_stderr);

    if (cap->has_stdin) {
        MI_DUP2(cap->saved_stdin, STDIN_FILENO);
        MI_CLOSE(cap->saved_stdin);
        pthread_join(*stdin_thread, NULL);
    }

    mi_drain_pipe(cap->pipe_out[0], out_buf);
    mi_drain_pipe(cap->pipe_err[0], err_buf);
    MI_CLOSE(cap->pipe_out[0]);
    MI_CLOSE(cap->pipe_err[0]);
}

/* ========== Generic tool runner ========== */

typedef int (*mi_tool_func_const)(int argc, const char* argv[]);
typedef int (*mi_tool_func_mut)(int argc, char* argv[]);

static int mi_run_tool(modernimage_context_t* ctx,
                       mi_tool_func_const tool_main,
                       int argc, const char* argv[]) {
    mi_buffer_reset(&ctx->out_buf);
    mi_buffer_reset(&ctx->err_buf);

    mi_capture_t cap;
    pthread_t stdin_thread;
    mi_stdin_writer_t stdin_ctx;

    pthread_mutex_lock(&g_io_mutex);

    if (mi_capture_begin(&cap, ctx->stdin_data, ctx->stdin_size,
                         &stdin_thread, &stdin_ctx) != 0) {
        pthread_mutex_unlock(&g_io_mutex);
        ctx->exit_code = -1;
        return -1;
    }

    ctx->exit_code = tool_main(argc, argv);

    mi_capture_end(&cap, &ctx->out_buf, &ctx->err_buf, &stdin_thread);

    pthread_mutex_unlock(&g_io_mutex);

    return ctx->exit_code;
}

static int mi_run_tool_avif(modernimage_context_t* ctx,
                            mi_tool_func_mut tool_main,
                            int argc, const char* argv[]) {
    /* avifenc takes char* argv[] (non-const) - create mutable copy */
    char** argv_copy = malloc(sizeof(char*) * (argc + 1));
    if (!argv_copy) {
        ctx->exit_code = -1;
        return -1;
    }
    for (int i = 0; i < argc; i++) {
        argv_copy[i] = strdup(argv[i]);
        if (!argv_copy[i]) {
            for (int j = 0; j < i; j++) free(argv_copy[j]);
            free(argv_copy);
            ctx->exit_code = -1;
            return -1;
        }
    }
    argv_copy[argc] = NULL;

    mi_buffer_reset(&ctx->out_buf);
    mi_buffer_reset(&ctx->err_buf);

    mi_capture_t cap;
    pthread_t stdin_thread;
    mi_stdin_writer_t stdin_ctx;

    pthread_mutex_lock(&g_io_mutex);

    if (mi_capture_begin(&cap, ctx->stdin_data, ctx->stdin_size,
                         &stdin_thread, &stdin_ctx) != 0) {
        pthread_mutex_unlock(&g_io_mutex);
        for (int i = 0; i < argc; i++) free(argv_copy[i]);
        free(argv_copy);
        ctx->exit_code = -1;
        return -1;
    }

    ctx->exit_code = tool_main(argc, argv_copy);

    mi_capture_end(&cap, &ctx->out_buf, &ctx->err_buf, &stdin_thread);

    pthread_mutex_unlock(&g_io_mutex);

    for (int i = 0; i < argc; i++) free(argv_copy[i]);
    free(argv_copy);

    return ctx->exit_code;
}

/* ========== Context lifecycle ========== */

modernimage_context_t* modernimage_context_new(void) {
    modernimage_context_t* ctx = calloc(1, sizeof(modernimage_context_t));
    if (!ctx) return NULL;
    mi_buffer_init(&ctx->out_buf);
    mi_buffer_init(&ctx->err_buf);
    ctx->exit_code = -1;
    ctx->stdin_data = NULL;
    ctx->stdin_size = 0;
    return ctx;
}

void modernimage_context_free(modernimage_context_t* ctx) {
    if (!ctx) return;
    mi_buffer_free(&ctx->out_buf);
    mi_buffer_free(&ctx->err_buf);
    free(ctx);
}

void modernimage_context_reset(modernimage_context_t* ctx) {
    if (!ctx) return;
    mi_buffer_reset(&ctx->out_buf);
    mi_buffer_reset(&ctx->err_buf);
    ctx->exit_code = -1;
    ctx->stdin_data = NULL;
    ctx->stdin_size = 0;
}

/* ========== Stdin injection ========== */

void modernimage_set_stdin(modernimage_context_t* ctx,
                           const void* data, size_t size) {
    if (!ctx) return;
    ctx->stdin_data = data;
    ctx->stdin_size = size;
}

/* ========== Public API ========== */

int modernimage_cwebp(modernimage_context_t* ctx, int argc, const char* argv[]) {
    return mi_run_tool(ctx, modernimage_cwebp_main, argc, argv);
}

int modernimage_gif2webp(modernimage_context_t* ctx, int argc, const char* argv[]) {
    return mi_run_tool(ctx, modernimage_gif2webp_main, argc, argv);
}

int modernimage_avifenc(modernimage_context_t* ctx, int argc, const char* argv[]) {
    return mi_run_tool_avif(ctx, modernimage_avifenc_main, argc, argv);
}

int modernimage_jpegtran(modernimage_context_t* ctx, int argc, const char* argv[]) {
    return mi_run_tool_avif(ctx, modernimage_jpegtran_main, argc, argv);
}

/* ========== Output access ========== */

size_t modernimage_get_stdout_size(const modernimage_context_t* ctx) {
    return ctx ? ctx->out_buf.size : 0;
}

size_t modernimage_get_stderr_size(const modernimage_context_t* ctx) {
    return ctx ? ctx->err_buf.size : 0;
}

size_t modernimage_copy_stdout(const modernimage_context_t* ctx,
                               char* buf, size_t buf_size) {
    if (!ctx || !buf || buf_size == 0) return 0;
    size_t to_copy = ctx->out_buf.size < buf_size ? ctx->out_buf.size : buf_size;
    if (to_copy > 0 && ctx->out_buf.data) memcpy(buf, ctx->out_buf.data, to_copy);
    return to_copy;
}

size_t modernimage_copy_stderr(const modernimage_context_t* ctx,
                               char* buf, size_t buf_size) {
    if (!ctx || !buf || buf_size == 0) return 0;
    size_t to_copy = ctx->err_buf.size < buf_size ? ctx->err_buf.size : buf_size;
    if (to_copy > 0 && ctx->err_buf.data) memcpy(buf, ctx->err_buf.data, to_copy);
    return to_copy;
}

int modernimage_get_exit_code(const modernimage_context_t* ctx) {
    return ctx ? ctx->exit_code : -1;
}

const char* modernimage_version(void) {
    return MODERNIMAGE_VERSION;
}
