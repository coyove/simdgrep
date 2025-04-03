#include "grepper.h"
#include <stdatomic.h>

static int32_t zero = 0;

void buffer_init(struct linebuf *l, int64_t size, int64_t mmap_limit) {
    memset(l, 0, sizeof(struct linebuf));
    l->bufsize = size;
    /*
       Regex will use this buffer to do matching and temporarily place NULL at
       the end of buffer, so we reserve more bytes than needed.
       */
    l->_free = l->buffer = (char *)malloc(size * 2 + 32);
    l->mmap_limit = mmap_limit;
}

void buffer_release(struct linebuf *l) {
    if (l->is_mmap)
        munmap(l->buffer, l->file_size);
    if (l->fd)
        close(l->fd);
}

bool buffer_reset(struct linebuf *l, int fd, bool allow_mmap)
{
    while (!atomic_compare_exchange_weak(&l->_flock, &zero, 1));
    l->off = l->read = 0;
    l->fd = fd;
    l->allow_mmap = allow_mmap;
    l->is_mmap = l->overflowed = l->binary_matching = false;
    l->lines = l->len = l->truelen = 0;
    l->buffer = l->_free;
    if (!fd) {
        l->file_size = 0;
    } else {
        l->file_size = lseek(fd, 0, SEEK_END);
        if (l->file_size < 0) {
            atomic_store(&l->_flock, 0);
            return false;
        }
    }
    atomic_store(&l->_flock, 0);
    return true;
}

static bool _buffer_fill_locked(struct linebuf *l) {
  if (l->read >= l->file_size) {
    // End of file.
    l->len = l->truelen = 0;
    return true;
  }

  if (l->fd == 0)
    return true;

  if (l->allow_mmap && l->mmap_limit && l->file_size >= l->mmap_limit) {
    l->is_mmap = true;
    l->buffer = (char *)mmap(0, l->file_size, PROT_READ, MAP_SHARED, l->fd, 0);
    if (l->buffer == MAP_FAILED)
      return false;
    l->off = l->read = l->len = l->truelen = l->file_size;
    return true;
  }

  if (!l->binary_matching)
    l->lines += countbyte(l->buffer, l->buffer + l->len, '\n');

  // Move tailing bytes forward.
  memcpy(l->buffer, l->buffer + l->len, l->truelen - l->len);
  l->len = l->truelen = l->truelen - l->len;

  if (l->off < l->file_size && l->truelen < l->bufsize) {
    // Fill rest space.
    int n = pread(l->fd, l->buffer + l->len, l->bufsize - l->truelen, l->off);
    if (n < 0)
      return false;
    l->len += n;
    l->truelen += n;
    l->off += n;
  }

  if (l->off >= l->file_size) {
    // We just reached EOF, no need to limit the buffer to full lines.
    l->read = l->off;
    return true;
  }

  // Truncate the buffer to full lines, this creates some tailing bytes which
  // will be processed in the next round.
  const char *end = indexlastbyte(l->buffer, l->buffer + l->truelen, '\n');
  if (end) {
    l->len = end - l->buffer + 1;
  } else if (l->len >= l->bufsize) {
    l->overflowed = true;
  }
  l->read += l->len;
  return true;
}

bool buffer_fill(struct grepper_ctx *ctx) {
    struct linebuf *l = &ctx->lbuf;
    // Loop can only take place when another worker is helping the current thread
    // to prefetch the next buffer, we can only wait.
NEXT:
    while (!atomic_compare_exchange_weak(&l->_flock, &zero, 1)) {
        struct timespec req = {.tv_sec = 0, .tv_nsec = 100000};
        nanosleep(&req, NULL);
    }

    bool res = _buffer_fill_locked(l);

    atomic_store(&l->_flock, 0);
    return res;
}

bool buffer_prefill(struct linebuf *l) {
  if (!atomic_compare_exchange_weak(&l->_flock, &zero, 1))
    return false;

  if (l->fd == 0 || l->read >= l->file_size || l->off >= l->file_size)
    goto SKIP;

  if (l->truelen > l->bufsize) // already prefilled
    goto SKIP;

  int n = pread(l->fd, l->buffer + l->truelen, l->bufsize, l->off);
  if (n < 0)
    goto SKIP;

  l->truelen += n;
  l->off += n;

  atomic_store(&l->_flock, 0);
  return true;

SKIP:
  atomic_store(&l->_flock, 0);
  return false;
}

void buffer_free(struct linebuf *l)
{
    free(l->_free);
}
