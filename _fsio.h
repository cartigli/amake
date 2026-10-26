#ifndef FILE_HANDLER
#define FILE_HANDLER

#include <sys/types.h>

int fexists(const char *dir, const u_int32_t dlen, const char *file,
    const u_int32_t flen);

char *mann_mm(const char *const FSRC);

void free_fbuf(void);

#endif
