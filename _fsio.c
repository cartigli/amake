#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "_fsio.h"

#define _FD_ 1
#define _out_(str, len)        \
    {                          \
        write(_FD_, str, len); \
    }

#define print(msg)                            \
    {                                         \
        _out_(msg "\n", sizeof msg "\n" - 1); \
    }

static int fopens(const char *f)
{
    const int fd = open(f, O_RDONLY);
    if (fd == -1) {
        _out_("file does not exist: ", sizeof "file does not exist: ");
        _out_(f, strlen(f));
        _out_("\n", 1);
        return 0;
    }
    close(fd);
    return 1;
}

static char *fbuf     = 0;
static u_int32_t fcap = 0;

int fexists(const char *dir, const u_int32_t dlen, const char *file,
    const u_int32_t flen)
{
    if (!fbuf || fcap <= dlen + flen + 4) {
        if (fbuf) free(fbuf);
        fbuf = malloc(dlen + flen + 4); /* '/.c\0' */
        if (!fbuf) {
            print("NOMEM") return 0;
        }
    }
    memcpy(fbuf, dir, dlen);
    memcpy(fbuf + dlen, "/", 1);
    memcpy(fbuf + dlen + 1, file, flen);
    memcpy(fbuf + dlen + 1 + flen, ".c", 2);
    fbuf[dlen + flen + 3] = '\0';
    return fopens(fbuf);
}

void free_fbuf(void)
{
    free(fbuf);
    fbuf = 0;
    fcap = 0;
}

static int val_msource(const char *const FSRC, int *_fd, u_int32_t *_size)
{
    struct stat st;
    const int fd = open(FSRC, O_RDONLY);
    if (fd == -1) {
        print("NO BUILD FILE FOUND");
        return 1;
    }
    if (fstat(fd, &st) == -1 || st.st_size == 0) {
        print("COULD NOT STAT BUILD FILE/BUILD FILE IS EMPTY");
        close(fd);
        return 1;
    }
    *_fd   = fd;
    *_size = (u_int32_t)st.st_size;
    return 0;
}

static char *mapout_file(const int fd, const u_int32_t size)
{
    long rd;
    char *base = malloc(size + 1);
    if (!base) {
        close(fd);
        print("NOMEM");
        return 0;
    }
    rd = -1;
    for (;;) {
        lseek(fd, 0, SEEK_SET);
        rd = read(fd, base, size);
        if (rd == (long)size) {
            close(fd);
            base[rd] = '\0';
            return base;
        }
        if (rd == -1 && errno == EINTR) {
            print("sig-interupt while reading; retrying...");
            continue;
        }
        print("read interupted before file consumed");
        break;
    }
    close(fd);
    free(base);
    return NULL;
}

char *mann_mm(const char *const fsrc)
{
    u_int32_t size;
    char *mm;
    int fd;
    if (val_msource(fsrc, &fd, &size)) return 0;
    mm = mapout_file(fd, size);
    close(fd);
    return mm;
}
