#if defined(DDEBUG) && (DDEBUG)
#       define dd(...) fprintf(stderr, "lua *** %s: ", __func__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)

#   else

#include <stdarg.h>

static void dd(const char *fmt, ...) {
}

#    endif
