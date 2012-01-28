/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */
 
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <xf86drm.h>
#include <string.h>
#include <stdio.h>
#include "libpscnv.h"
#include <sys/mman.h>

#define IB_SIZE 0x2000

#define  NVC0_2D_DST_FORMAT								0x00000200
#define   NVC0_2D_DST_FORMAT_A8R8G8B8_UNORM						0x000000cf
#define  NVC0_2D_DST_LINEAR								0x00000204
#define  NVC0_2D_DST_TILE_MODE								0x00000208
#define  NVC0_2D_DST_DEPTH								0x0000020c
#define  NVC0_2D_DST_LAYER								0x00000210
#define  NVC0_2D_DST_PITCH								0x00000214
#define  NVC0_2D_DST_WIDTH								0x00000218
#define  NVC0_2D_DST_HEIGHT								0x0000021c
#define  NVC0_2D_DST_ADDRESS_HIGH							0x00000220
#define  NVC0_2D_DST_ADDRESS_LOW							0x00000224
#define  NVC0_2D_SRC_FORMAT								0x00000230
#define  NVC0_2D_SRC_LINEAR								0x00000234
#define  NVC0_2D_SRC_TILE_MODE								0x00000238
#define  NVC0_2D_SRC_DEPTH								0x0000023c
#define  NVC0_2D_SRC_LAYER								0x00000240
#define  NVC0_2D_SRC_PITCH								0x00000244
#define  NVC0_2D_SRC_WIDTH								0x00000248
#define  NVC0_2D_SRC_HEIGHT								0x0000024c
#define  NVC0_2D_SRC_ADDRESS_HIGH							0x00000250
#define  NVC0_2D_SRC_ADDRESS_LOW							0x00000254
#define  NVC0_2D_CLIP_X									0x00000280
#define  NVC0_2D_CLIP_Y									0x00000284
#define  NVC0_2D_CLIP_W									0x00000288
#define  NVC0_2D_CLIP_H									0x0000028c
#define  NVC0_2D_CLIP_ENABLE								0x00000290
#define  NVC0_2D_COLOR_KEY_FORMAT							0x00000294
#define   NVC0_2D_COLOR_KEY_FORMAT_16BPP						0x00000000
#define   NVC0_2D_COLOR_KEY_FORMAT_15BPP						0x00000001
#define   NVC0_2D_COLOR_KEY_FORMAT_24BPP						0x00000002
#define   NVC0_2D_COLOR_KEY_FORMAT_30BPP						0x00000003
#define   NVC0_2D_COLOR_KEY_FORMAT_8BPP							0x00000004
#define   NVC0_2D_COLOR_KEY_FORMAT_16BPP2						0x00000005
#define   NVC0_2D_COLOR_KEY_FORMAT_32BPP						0x00000006
#define  NVC0_2D_COLOR_KEY								0x00000298
#define  NVC0_2D_COLOR_KEY_ENABLE							0x0000029c
#define  NVC0_2D_ROP									0x000002a0
#define  NVC0_2D_OPERATION								0x000002ac
#define   NVC0_2D_OPERATION_SRCCOPY_AND							0x00000000
#define   NVC0_2D_OPERATION_ROP_AND							0x00000001
#define   NVC0_2D_OPERATION_BLEND_AND							0x00000002
#define   NVC0_2D_OPERATION_SRCCOPY							0x00000003
#define   NVC0_2D_OPERATION_SRCCOPY_PREMULT						0x00000004
#define   NVC0_2D_OPERATION_BLEND_PREMULT						0x00000005
#define  NVC0_2D_PATTERN_FORMAT								0x000002e8
#define   NVC0_2D_PATTERN_FORMAT_16BPP							0x00000000
#define   NVC0_2D_PATTERN_FORMAT_15BPP							0x00000001
#define   NVC0_2D_PATTERN_FORMAT_32BPP							0x00000002
#define   NVC0_2D_PATTERN_FORMAT_8BPP							0x00000003
#define  NVC0_2D_PATTERN_COLOR(x)							(0x000002f0+((x)*4))
#define  NVC0_2D_PATTERN_COLOR__SIZE							0x00000002
#define  NVC0_2D_PATTERN_BITMAP(x)							(0x000002f8+((x)*4))
#define  NVC0_2D_PATTERN_BITMAP__SIZE							0x00000002
#define  NVC0_2D_DRAW_SHAPE								0x00000580
#define   NVC0_2D_DRAW_SHAPE_POINTS							0x00000000
#define   NVC0_2D_DRAW_SHAPE_LINES							0x00000001
#define   NVC0_2D_DRAW_SHAPE_LINE_STRIP							0x00000002
#define   NVC0_2D_DRAW_SHAPE_TRIANGLES							0x00000003
#define   NVC0_2D_DRAW_SHAPE_RECTANGLES							0x00000004
#define  NVC0_2D_DRAW_COLOR_FORMAT							0x00000584
#define   NVC0_2D_DRAW_COLOR_FORMAT_A8R8G8B8_UNORM					0x000000cf
#define  NVC0_2D_DRAW_COLOR								0x00000588
#define  NVC0_2D_DRAW_POINT16								0x000005e0
#define   NVC0_2D_DRAW_POINT16_X_SHIFT							0
#define   NVC0_2D_DRAW_POINT16_X_MASK							0x0000ffff
#define   NVC0_2D_DRAW_POINT16_Y_SHIFT							16
#define   NVC0_2D_DRAW_POINT16_Y_MASK							0xffff0000
#define  NVC0_2D_DRAW_POINT32_X(x)							(0x00000600+((x)*8))
#define  NVC0_2D_DRAW_POINT32_X__SIZE							0x00000040
#define  NVC0_2D_DRAW_POINT32_Y(x)							(0x00000604+((x)*8))
#define  NVC0_2D_DRAW_POINT32_Y__SIZE							0x00000040
#define  NVC0_2D_BLIT_DST_X								0x000008b0
#define  NVC0_2D_BLIT_DST_Y								0x000008b4
#define  NVC0_2D_BLIT_DST_W								0x000008b8
#define  NVC0_2D_BLIT_DST_H								0x000008bc
#define  NVC0_2D_BLIT_DU_DX_FRACT							0x000008c0
#define  NVC0_2D_BLIT_DU_DX_INT								0x000008c4
#define  NVC0_2D_BLIT_DV_DY_FRACT							0x000008c8
#define  NVC0_2D_BLIT_DV_DY_INT								0x000008cc
#define  NVC0_2D_BLIT_SRC_X_FRACT							0x000008d0
#define  NVC0_2D_BLIT_SRC_X_INT								0x000008d4
#define  NVC0_2D_BLIT_SRC_Y_FRACT							0x000008d8
#define  NVC0_2D_BLIT_SRC_Y_INT								0x000008dc

struct nvchan {
   uint32_t vid;
   uint32_t cid;
   uint64_t ch_ofst;
   volatile uint32_t *regs;

   volatile uint32_t *pb;
   uint32_t pb_gem;
   uint64_t pb_ofst;

   volatile uint32_t *ib;
   uint32_t ib_gem;
   uint64_t ib_ofst;
   uint64_t ib_virt;

   int pb_base;
   int pb_pos;
   int ib_pos;
} ctx;

struct buf {
   uint64_t virt;
   uint64_t ofst;
   uint32_t *map;
   uint32_t gem;
   uint32_t size;
};

static int buf_new(int fd, struct buf *buf, uint32_t size)
{
   static int serial = 0;
   int ret;

   buf->size = size;

   ret = pscnv_gem_new(fd, 0xb0b00000 + serial++, PSCNV_GEM_CONTIG, 0xfe, size, 0,
                       &buf->gem, &buf->ofst);
   if (ret) {
      fprintf(stderr, "buf%i: gem_new failed: %s\n",
              serial, strerror(-ret));
      return ret;
   }

   buf->map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, buf->ofst);
   if (!buf->map) {
      fprintf(stderr, "buf%i: failed to map\n", serial);
      return -1;
   }

   ret = pscnv_vspace_map(fd, ctx.vid, buf->gem, 0x20000000, 1ULL << 32,
                          0, 0, &buf->virt);
   if (ret) {
      fprintf(stderr, "buf%i: vspace map failed: %s\n", serial, strerror(-ret));
      return ret;
   }
   printf("buf%i: virtual address = 0x%010lx\n", serial, buf->virt);
   return 0;
}

static inline void clflush(volatile void *p)
{
   __asm__ __volatile__ ("clflush %0"
                         : : "m" ((unsigned long)p) : "memory");
}

static inline void mfence()
{
   __asm__ __volatile__ ("mfence");
}

static inline void BEGIN_RING(int c, uint32_t m, int s)
{
   ctx.pb[ctx.pb_pos++] = (0x2 << 28) | (s << 16) | (c << 13) | (m >> 2);
}

static inline void CONST_RING(int c, uint32_t m, int s)
{
   ctx.pb[ctx.pb_pos++] = (0x6 << 28) | (s << 16) | (c << 13) | (m >> 2);
}

static inline void OUT_RING(uint32_t d)
{
   ctx.pb[ctx.pb_pos++] = d;
}

static inline void OUT_RINGf(float f)
{
   union {
      float f32;
      uint32_t u32;
   } u;
   u.f32 = f;
   ctx.pb[ctx.pb_pos++] = u.u32;
}

static inline void FIRE_RING()
{
   uint64_t virt = ctx.ib_virt + ctx.pb_base * 4;
   uint32_t size = (ctx.pb_pos - ctx.pb_base) * 4;

   printf("BFORE: 0x88/0x8c = %i/%i\n",
          ctx.regs[0x88 / 4], ctx.regs[0x8c / 4]);

   if (!size)
      return;
   
   ctx.ib[ctx.ib_pos * 2 + 0] = virt;
   ctx.ib[ctx.ib_pos * 2 + 1] = (virt >> 32) | (size << 8);
   mfence();

   printf("FIRE_RING [%i]: 0x%08x 0x%08x\n", ctx.ib_pos + 1,
          ctx.ib[ctx.ib_pos * 2 + 0],
          ctx.ib[ctx.ib_pos * 2 + 1]);

   ++ctx.ib_pos;

   ctx.regs[0x8c / 4] = ctx.ib_pos;

   usleep(10);

   printf("AFTER: 0x88/0x8c = %i/%i\n",
          ctx.regs[0x88 / 4], ctx.regs[0x8c / 4]);

   ctx.pb_base = ctx.pb_pos;
}

int main(int argc, char **argv)
{
   int fd;
   int i, ret;
   int fd2;

   printf("opening drm device 1st\n");
   fd = drmOpen("pscnv", 0);
   printf("opening drm device 2nd\n");
   fd2 = 0; // drmOpen("pscnv", 0);

   if (fd == -1 || fd2 == -1) {
      perror("failed to open device");
      return 1;
   }

   ctx.pb_pos = 0x1000 / 4;
   ctx.pb_base = ctx.pb_pos;
   ctx.ib_pos = 0;

   ret = pscnv_gem_new(fd, 0xf1f0c0de, PSCNV_GEM_CONTIG, 0,
		       IB_SIZE, 0, &ctx.ib_gem, &ctx.ib_ofst);
   if (ret) {
      fprintf(stderr, "gem_new failed: %s\n", strerror(-ret));
      return 1;
   }
   printf("gem_new: h %d m %lx\n", ctx.ib_gem, ctx.ib_ofst);

   ctx.ib = mmap(0, IB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ctx.ib_ofst);
   if (!ctx.ib)
      return 1;
   ctx.pb = ctx.ib;
   printf("IB at %p\n", ctx.ib);

   ret = pscnv_vspace_new(fd, &ctx.vid);
   if (ret) {
      fprintf(stderr, "vspace_new: %s\n", strerror(-ret));
      return 1;
   }
   printf("vid %d\n", ctx.vid);

   ret = pscnv_chan_new(fd, ctx.vid, &ctx.cid, &ctx.ch_ofst);
   if (ret) {
      fprintf(stderr, "chan_new failed: %s\n", strerror(-ret));
      return 1;
   }
   printf("cid %d regs %lx\n", ctx.cid, ctx.ch_ofst);


   ret = pscnv_vspace_map(fd, ctx.vid, ctx.ib_gem, 0x20000000, 1ULL << 32,
                          1, 0, &ctx.ib_virt);
   if (ret) {
      fprintf(stderr, "vspace_map of IB failed: %s\n", strerror(-ret));
      return 1;
   }
   printf ("IB virtual %lx, doing fifo_init next\n", ctx.ib_virt);

   ret = pscnv_fifo_init_ib(fd, ctx.cid, 0xbeef, 0, 1, ctx.ib_virt, 10);
   if (ret) {
      fprintf(stderr, "fifo_init failed: %s\n", strerror(-ret));
      return 1;
   }
   printf("FIFO initialized\n");

   ctx.regs = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ctx.ch_ofst);
   if (!ctx.regs) {
      fprintf(stderr, "ERROR: mmap of FIFO regs failed\n");
      return 1;
   }
   printf("fifo regs at %p\n", ctx.regs);

   struct buf cbuf;
   if (buf_new(fd, &cbuf, 0x1000))
       return -1;
   if (buf_new(fd, &cbuf, 0x1000))
       return -1;
   if (buf_new(fd, &cbuf, 128 * 128 * 4))
       return -1;

   for (i = 0; i < 0x1000 / 4; ++i)
	   cbuf.map[i] = 0xfafafafa;

   const int eng2d = 1;

   BEGIN_RING(eng2d, 0x0000, 1);
   OUT_RING  (0x902d);
   BEGIN_RING(eng2d, NVC0_2D_CLIP_ENABLE, 1);
   OUT_RING  (0);
   BEGIN_RING(eng2d, NVC0_2D_COLOR_KEY_ENABLE, 1);
   OUT_RING  (0);
   BEGIN_RING(eng2d, 0x0884, 1);
   OUT_RING  (0x3f);
   BEGIN_RING(eng2d, 0x0888, 1);
   OUT_RING  (1);
   BEGIN_RING(eng2d, NVC0_2D_ROP, 1);
   OUT_RING  (0x55);
   BEGIN_RING(eng2d, NVC0_2D_OPERATION, 1);
   OUT_RING  (NVC0_2D_OPERATION_SRCCOPY);
   BEGIN_RING(eng2d, NVC0_2D_BLIT_DU_DX_FRACT, 4);
   OUT_RING  (0);
   OUT_RING  (1);
   OUT_RING  (0);
   OUT_RING  (1);
   BEGIN_RING(eng2d, NVC0_2D_DRAW_SHAPE, 2);
   OUT_RING  (4);
   OUT_RING  (NVC0_2D_DRAW_COLOR_FORMAT_A8R8G8B8_UNORM);
   BEGIN_RING(eng2d, NVC0_2D_PATTERN_FORMAT, 2);
   OUT_RING  (NVC0_2D_PATTERN_FORMAT_32BPP);
   OUT_RING  (1);

   FIRE_RING ();

   BEGIN_RING(eng2d, NVC0_2D_DST_FORMAT, 5);
   OUT_RING  (NVC0_2D_DST_FORMAT_A8R8G8B8_UNORM);
   OUT_RING  (0);
   OUT_RING  (0x10);
   OUT_RING  (1);
   OUT_RING  (0);
   BEGIN_RING(eng2d, NVC0_2D_DST_FORMAT + 0x18, 4);
   OUT_RING  (128);
   OUT_RING  (128);
   OUT_RING  (cbuf.virt >> 32);
   OUT_RING  (cbuf.virt);

   BEGIN_RING(eng2d, NVC0_2D_DRAW_SHAPE, 3);
   OUT_RING  (NVC0_2D_DRAW_SHAPE_RECTANGLES);
   OUT_RING  (NVC0_2D_DRAW_COLOR_FORMAT_A8R8G8B8_UNORM);
   OUT_RING  (0xaca5cade);

   BEGIN_RING(eng2d, NVC0_2D_DRAW_POINT32_X(0), 4);
   OUT_RING  (2);
   OUT_RING  (0);
   OUT_RING  (128);
   OUT_RING  (128);

   FIRE_RING ();

   usleep(1000000);

   for (i = 0; i < 4; ++i)
      printf("BUF[%i] = 0x%08x\n", i, cbuf.map[i]);

   close(fd);
   close(fd2);
        
   return 0;
}
