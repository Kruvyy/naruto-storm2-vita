/* Unpacks the game's compressed .xfbin files (magic 0x0FF512ED, "XCTD").
 *
 * Layout, from the game's reader (sub_8269F4A8, sub_826A1418) and the files
 * (big-endian):
 *   +0  magic 0x0FF512ED
 *   +4  0x01000000 (version?)
 *   +8  4 unknown bytes (checksum?)
 *   +12 flags: block size 0x8000 << ((v >> 4) & 3); block count (v >> 6) & 0xFFFF;
 *       (v & 0x00C00000) != 0 -> 32-bit table entries, else 20-bit
 *   +16 table, bit-packed, one entry per block: the block's uncompressed size
 *       (padded to whole 32-bit words)
 *   then per block: frames [u16 size][LZX data], 0x8000 output bytes each
 *       (the last one shorter); each block is its own LZX stream.
 *
 * usage: xctd_unpack <in.xfbin> <out.bin>
 * The output is the user's own game data: never distribute it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "mspack.h"  /* before lzx.h */
#include "lzx.h"

typedef struct {
  const uint8_t* buf;
  size_t size, pos;
} MemFile;

static int mread(struct mspack_file* f, void* b, int n) {
  MemFile* m = (MemFile*)f;
  size_t left = m->size - m->pos;
  size_t k = (size_t)n < left ? (size_t)n : left;
  memcpy(b, m->buf + m->pos, k);
  m->pos += k;
  return (int)k;
}
static int mwrite(struct mspack_file* f, void* b, int n) {
  MemFile* m = (MemFile*)f;
  size_t left = m->size - m->pos;
  size_t k = (size_t)n < left ? (size_t)n : left;
  memcpy((uint8_t*)m->buf + m->pos, b, k);
  m->pos += k;
  return (int)k;
}
static void* alloc_(struct mspack_system* s, size_t n) {
  (void)s;
  return calloc(n, 1);
}
static void free_(void* p) { free(p); }
static void copy_(void* s, void* d, size_t n) { memcpy(d, s, n); }

static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t bits_at(const uint8_t* table, size_t bit, int count) {
  uint32_t v = 0;
  for (int i = 0; i < count; ++i, ++bit) {
    v = (v << 1) | ((table[bit >> 3] >> (7 - (bit & 7))) & 1);
  }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: xctd_unpack in.xfbin out.bin\n");
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  const size_t n = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* in = malloc(n);
  if (fread(in, 1, n, f) != n) return 1;
  fclose(f);
  if (n < 16 || be32(in) != 0x0FF512EDu) {
    fprintf(stderr, "not XCTD\n");
    return 3;
  }
  const uint32_t flags = be32(in + 12);
  const uint32_t block_size = 0x8000u << ((flags >> 4) & 3);
  const uint32_t blocks = (flags >> 6) & 0xFFFF;
  const int entry_bits = (flags & 0x00C00000u) ? 32 : 20;
  const size_t table_words = ((size_t)entry_bits * blocks + 31) / 32;
  size_t pos = 16 + table_words * 4;
  int window_bits = 15;
  while ((1u << window_bits) < block_size) ++window_bits;

  size_t total = 0;
  for (uint32_t b = 0; b < blocks; ++b) total += bits_at(in + 16, (size_t)b * entry_bits, entry_bits);
  uint8_t* out = malloc(total ? total : 1);
  size_t out_pos = 0;

  struct mspack_system sys = {0};
  sys.read = mread;
  sys.write = mwrite;
  sys.alloc = alloc_;
  sys.free = free_;
  sys.copy = copy_;
  uint8_t* packed = malloc(n);
  for (uint32_t b = 0; b < blocks; ++b) {
    const uint32_t usize = bits_at(in + 16, (size_t)b * entry_bits, entry_bits);
    size_t packed_n = 0;
    /* Blocks can be preceded by zero padding (seen as a 0x0000 frame size). */
    while (pos + 2 <= n && in[pos] == 0 && in[pos + 1] == 0) pos += 2;
    for (uint32_t covered = 0; covered < usize;) {
      if (pos + 2 > n) {
        fprintf(stderr, "block %u: truncated\n", b);
        return 4;
      }
      /* XMemCompress frame header: [u16 compressed size] for a full 0x8000
         frame, or [0xFF][u16 uncompressed][u16 compressed] for a short one. */
      uint32_t frame_out = 0x8000;
      size_t len;
      if (in[pos] == 0xFF) {
        frame_out = ((uint32_t)in[pos + 1] << 8) | in[pos + 2];
        len = ((size_t)in[pos + 3] << 8) | in[pos + 4];
        pos += 5;
      } else {
        len = ((size_t)in[pos] << 8) | in[pos + 1];
        pos += 2;
      }
      covered += frame_out;
      if (pos + len > n) {
        fprintf(stderr, "block %u: frame past the end\n", b);
        return 4;
      }
      memcpy(packed + packed_n, in + pos, len);
      packed_n += len;
      pos += len;
    }
    MemFile src = {packed, packed_n, 0}, dst = {out + out_pos, usize, 0};
    struct lzxd_stream* lz = lzxd_init(&sys, (struct mspack_file*)&src,
                                       (struct mspack_file*)&dst, window_bits, 0, 0x8000,
                                       (off_t)usize, 0);
    const int r = lz ? lzxd_decompress(lz, (off_t)usize) : -1;
    if (lz) lzxd_free(lz);
    if (r != 0 || dst.pos != usize) {
      fprintf(stderr, "block %u: LZX error %d (%zu of %u bytes)\n", b, r, dst.pos, usize);
      return 5;
    }
    out_pos += usize;
  }
  FILE* o = fopen(argv[2], "wb");
  fwrite(out, 1, out_pos, o);
  fclose(o);
  fprintf(stderr, "%u blocks of %u, %zu -> %zu bytes, used %zu of %zu input, starts %.4s\n",
          blocks, block_size, n, out_pos, pos, n, (const char*)out);
  return 0;
}
