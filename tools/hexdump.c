/* hexdump.c - minimal hex+ASCII dumper for eyeballing PNG chunk boundaries.
 *
 * Usage: hexdump <file> [start] [length]
 *
 * This is deliberately dumb: no dependencies, no PNG awareness. The point
 * is to have a tool you trust completely (because it's ~60 lines) before
 * you start trusting a parser you just wrote.
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file> [start_offset] [length]\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    long start = (argc > 2) ? strtol(argv[2], NULL, 0) : 0;
    long want_len = (argc > 3) ? strtol(argv[3], NULL, 0) : -1; /* -1 = to EOF */

    if (fseek(f, start, SEEK_SET) != 0) {
        perror("fseek");
        fclose(f);
        return 1;
    }

    unsigned char buf[16];
    long offset = start;
    long remaining = want_len;
    size_t n;

    while ((n = fread(buf, 1, 16, f)) > 0) {
        if (want_len >= 0) {
            if (remaining <= 0) break;
            if ((long)n > remaining) n = (size_t)remaining;
            remaining -= (long)n;
        }

        printf("%08lx  ", offset);
        for (size_t i = 0; i < 16; i++) {
            if (i < n) printf("%02x ", buf[i]);
            else printf("   ");
            if (i == 7) printf(" ");
        }
        printf(" |");
        for (size_t i = 0; i < n; i++) {
            unsigned char c = buf[i];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");

        offset += (long)n;
        if (n < 16) break;
    }

    fclose(f);
    return 0;
}
