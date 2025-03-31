#include "grepper.h"

void buffer_init(struct linebuf *l, int64_t size)
{
    memset(l, 0, sizeof(struct linebuf));
    l->bufsize = size;
    /* 
       Regex will use this buffer to do matching and temporarily place NULL at
       the end of line, so we reserve more bytes than needed.
       */
    l->_free = l->buffer = (char *)malloc(size + 33);
}

void buffer_release(struct linebuf *l)
{
    if (l->is_mmap)
        munmap(l->buffer, l->file_size);
    if (l->fd)
        close(l->fd);
}

bool buffer_reset(struct linebuf *l, int fd, bool allow_mmap)
{
    l->off = l->read = 0;
    l->fd = fd;
    l->allow_mmap = allow_mmap;
    l->is_mmap = l->overflowed = l->binary_matching = false;
    l->lines = l->len = l->datalen = 0;
    l->buffer = l->_free;
    if (!fd) {
        l->file_size = 0;
        return true;
    }
    l->file_size = lseek(fd, 0, SEEK_END);
    if (l->file_size < 0)
        return false;
    return true;
}

bool buffer_fill(struct linebuf *l)
{
    if (l->read >= l->file_size) {
        // End of file.
        l->len = l->datalen = 0;
        return true;
    }

    if (l->fd == 0)
        return true;

    if (l->allow_mmap && l->mmap_limit && l->file_size >= l->mmap_limit) {
        l->is_mmap = true;
        l->buffer = (char *)mmap(0, l->file_size, PROT_READ, MAP_SHARED, l->fd, 0);
        if (l->buffer == MAP_FAILED)
            return false;
        l->off = l->read = l->len = l->datalen = l->file_size;
        return true;
    }

    if (!l->binary_matching)
        l->lines += countbyte(l->buffer, l->buffer + l->len, '\n');

    // Move tailing bytes forward.
    memcpy(l->buffer, l->buffer + l->len, l->datalen - l->len);
    l->len = l->datalen = l->datalen - l->len;

    // Fill rest space.
    int n = pread(l->fd, l->buffer + l->len, l->bufsize - l->len, l->off);
    if (n < 0)
        return false;

    l->len += n;
    l->datalen += n;
    l->off += n;

    if (l->off >= l->file_size) {
        // We just reached EOF, no need to limit the buffer to full lines.
        l->read = l->off;
        return true;
    }

    // Truncate the buffer to full lines, this creates some tailing bytes which will
    // be processed in the next round.
    const char *end = indexlastbyte(l->buffer, l->buffer + l->datalen, '\n');
    if (end) {
        l->len = end - l->buffer + 1;
        l->read += l->len;
    } else if (l->datalen == l->bufsize) {
        l->overflowed = true;
        l->read += n;
    }

    // struct radvisory ra;
    // ra.ra_offset = l->off;
    // ra.ra_count = l->buflen;
    // fcntl(l->fd, F_RDADVISE, &ra);
    return true;
}

void buffer_free(struct linebuf *l)
{
    free(l->_free);
}
