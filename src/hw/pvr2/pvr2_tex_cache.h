/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2017 snickerbockers
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 ******************************************************************************/

#ifndef PVR2_TEX_CACHE_H_
#define PVR2_TEX_CACHE_H_

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pvr2_ta.h"

struct geo_buf;

/*
 * this is arbitrary.  I chose a value that is smaller than it should be
 * because I want to make sure the system can properly swap textures in/out
 * of the cache.
 */
#define PVR2_TEX_CACHE_SIZE 128
#define PVR2_TEX_CACHE_MASK (PVR2_TEX_CACHE_SIZE - 1)

struct pvr2_tex {
    uint32_t addr_first, addr_last;

    unsigned w, h;
    int pix_fmt;

    // if this is not set then this part of the cache is empty
    bool valid;

    bool twiddled;

    /*
     * if this is set, it means that this entry in the texture cache has
     * changed since the last update.  If this is not set, then the data in
     * dat is not valid (although the data in the corresponding entry in
     * OpenGL's tex cache is).
     */
    bool dirty;

    // texture data (if dirty is true)
    uint8_t *dat;
};

// insert the given texture into the cache
struct pvr2_tex *pvr2_tex_cache_add(uint32_t addr,
                                    unsigned w, unsigned h,
                                    int pix_fmt, bool twiddled);

struct pvr2_tex *pvr2_tex_cache_find(uint32_t addr, unsigned w,
                                     unsigned h, int pix_fmt, bool twiddled);

void pvr2_tex_cache_notify_write(uint32_t addr_first, uint32_t len);

int pvr2_tex_cache_get_idx(struct pvr2_tex const *tex);

/*
 * this function sends the texture cache over to the rendering thread
 * by copying it to the given geo_buf
 */
void pvr2_tex_cache_xmit(struct geo_buf *out);

#endif
