#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "png_decoder.h"
#include "zlib_wrapper.h"
#include "inflate.h"
#include "png_unfilter.h"

void *__real_malloc(size_t size);
void *__real_calloc(size_t num, size_t size);
void *__real_realloc(void *ptr, size_t size);
void  __real_free(void *ptr);

static size_t g_current_allocated_bytes = 0;
static size_t g_total_allocations = 0;
static size_t g_total_frees = 0;

typedef struct mem_header {
    size_t size;
    uint32_t magic;
} mem_header_t;

#define MEM_MAGIC 0xDEADBEEF

void *__wrap_malloc(size_t size) {
    if (size == 0) size = 1;
    mem_header_t *hdr = (mem_header_t *)__real_malloc(sizeof(mem_header_t) + size);
    if (!hdr) return NULL;
    hdr->size = size;
    hdr->magic = MEM_MAGIC;
    g_current_allocated_bytes += size;
    g_total_allocations++;
    return (void *)(hdr + 1);
}

void *__wrap_calloc(size_t num, size_t size) {
    size_t total = num * size;
    void *p = __wrap_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *__wrap_realloc(void *ptr, size_t new_size) {
    if (!ptr) return __wrap_malloc(new_size);
    if (new_size == 0) {
        mem_header_t *hdr = ((mem_header_t *)ptr) - 1;
        assert(hdr->magic == MEM_MAGIC);
        hdr->magic = 0;
        g_current_allocated_bytes -= hdr->size;
        g_total_frees++;
        __real_free(hdr);
        return NULL;
    }
    mem_header_t *old_hdr = ((mem_header_t *)ptr) - 1;
    assert(old_hdr->magic == MEM_MAGIC);
    size_t old_size = old_hdr->size;
    void *new_p = __wrap_malloc(new_size);
    if (!new_p) return NULL;
    size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_p, ptr, copy_size);
    old_hdr->magic = 0;
    g_current_allocated_bytes -= old_size;
    g_total_frees++;
    __real_free(old_hdr);
    return new_p;
}

void __wrap_free(void *ptr) {
    if (!ptr) return;
    mem_header_t *hdr = ((mem_header_t *)ptr) - 1;
    assert(hdr->magic == MEM_MAGIC);
    hdr->magic = 0;
    g_current_allocated_bytes -= hdr->size;
    g_total_frees++;
    __real_free(hdr);
}

static int run_decode_cycle(const char *path) {
    png_container_t container;
    char err[256];
    png_status_t pstatus = png_read_container(path, &container, err, sizeof(err));
    if (pstatus != PNG_OK) {
        return 1;
    }

    int channels = (container.ihdr.color_type == PNG_COLOR_TYPE_RGBA) ? 4 : 3;

    const uint8_t *deflate_data;
    size_t deflate_len;
    zlib_wrapper_status_t zstatus = zlib_wrapper_strip(
        container.idat_data, container.idat_size, &deflate_data, &deflate_len);
    if (zstatus != ZLIB_WRAPPER_OK) {
        png_container_free(&container);
        return 1;
    }

    inflate_buffer_t inflated;
    inflate_status_t istatus = inflate(deflate_data, deflate_len, &inflated);
    if (istatus != INFLATE_OK) {
        png_container_free(&container);
        return 1;
    }

    zstatus = zlib_wrapper_check_adler32(container.idat_data, container.idat_size,
                                          inflated.data, inflated.size);
    if (zstatus != ZLIB_WRAPPER_OK) {
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }

    if ((size_t)channels == 0 ||
        (size_t)container.ihdr.width > SIZE_MAX / (size_t)channels) {
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }
    size_t stride = (size_t)container.ihdr.width * (size_t)channels;
    size_t scanline_len = stride + 1;
    if ((size_t)container.ihdr.height > SIZE_MAX / scanline_len) {
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }
    size_t expected_size = (size_t)container.ihdr.height * scanline_len;
    if (inflated.size != expected_size) {
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }

    png_pixels_t pixels;
    png_unfilter_status_t ustatus = png_unfilter(
        inflated.data, inflated.size,
        container.ihdr.width, container.ihdr.height, channels,
        &pixels);

    inflate_buffer_free(&inflated);
    png_container_free(&container);

    if (ustatus != PNG_UNFILTER_OK) {
        return 1;
    }

    png_pixels_free(&pixels);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.png>\n", argv[0]);
        return 1;
    }

    /* Pass 1: warm up CRT internal allocations */
    run_decode_cycle(argv[1]);

    /* Pass 2: track exact delta */
    size_t bytes_before = g_current_allocated_bytes;
    int ret = run_decode_cycle(argv[1]);
    size_t bytes_after = g_current_allocated_bytes;

    if (bytes_after != bytes_before) {
        fprintf(stderr, "Memory leak in %s: %lld bytes leaked (before: %zu, after: %zu)\n",
                argv[1], (long long)(bytes_after - bytes_before), bytes_before, bytes_after);
        return 2;
    }

    return ret;
}
