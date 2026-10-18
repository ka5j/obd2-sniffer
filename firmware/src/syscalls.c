/*
 * Minimal newlib system calls.
 *
 * The firmware has no heap: _sbrk always fails, so an accidental malloc()
 * returns NULL instead of silently eating the stack's RAM. Use
 * console_printf() for output; stdout via printf() also works (unbuffered)
 * but pulls in more of newlib.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include "console.h"

int _write(int fd, const char *buf, int len);
int _read(int fd, char *buf, int len);
int _close(int fd);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _lseek(int fd, int offset, int whence);
void *_sbrk(ptrdiff_t increment);
int _getpid(void);
int _kill(int pid, int sig);
void _exit(int status);

int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    console_write(buf, (size_t)len);
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR; /* character device: stdio won't try to seek */
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}
