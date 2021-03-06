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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sh4.h"
#include "sh4_dmac.h"
#include "hw/sys/holly_intc.h"
#include "maple_device.h"
#include "error.h"

#include "maple.h"

#define MAPLE_LENGTH_SHIFT 0
#define MAPLE_LENGTH_MASK (0xff << MAPLE_LENGTH_SHIFT)

#define MAPLE_PORT_SHIFT 16
#define MAPLE_PORT_MASK (0x3 << MAPLE_PORT_SHIFT)

#define MAPLE_PTRN_SHIFT 8
#define MAPLE_PTRN_MASK (0x7 << MAPLE_PTRN_SHIFT)

#define MAPLE_LAST_SHIFT 31
#define MAPLE_LAST_MASK (1 << MAPLE_LAST_SHIFT)

#define MAPLE_CMD_SHIFT 0
#define MAPLE_CMD_MASK (0xff << MAPLE_CMD_SHIFT)

#define MAPLE_ADDR_SHIFT 8
#define MAPLE_ADDR_MASK (0xff << MAPLE_ADDR_SHIFT)

#define MAPLE_PACK_LEN_SHIFT 24
#define MAPLE_PACK_LEN_MASK (0xff << MAPLE_PACK_LEN_SHIFT)

static void maple_handle_devinfo(struct maple_frame *frame);
static void maple_handle_getcond(struct maple_frame *frame);

static void maple_decode_frame(struct maple_frame *frame_out,
                               uint32_t const dat[3]);

static DEF_ERROR_INT_ATTR(maple_command);

void maple_handle_frame(struct maple_frame *frame) {
    MAPLE_TRACE("frame received!\n");
    MAPLE_TRACE("\tlength: %u\n", frame->input_len);
    MAPLE_TRACE("\tport: %u\n", frame->port);
    MAPLE_TRACE("\tpattern: %u\n", frame->ptrn);
    MAPLE_TRACE("\treceive address: 0x%08x\n", (unsigned)frame->recv_addr);
    MAPLE_TRACE("\tcommand: %02x\n", (unsigned)frame->cmd);
    MAPLE_TRACE("\tmaple address: %02x\n", frame->maple_addr);
    MAPLE_TRACE("\tpacket length: %u\n", frame->pack_len);

    if (frame->last_frame)
        MAPLE_TRACE("\tthis was the last frame\n");
    else
        MAPLE_TRACE("\tthis was not the last frame\n");

    switch (frame->cmd) {
    case MAPLE_CMD_DEVINFO:
        maple_handle_devinfo(frame);
        break;
    case MAPLE_CMD_GETCOND:
        maple_handle_getcond(frame);
        break;
    default:
        error_set_feature("ERROR: no handler for maplebus command frame");
        error_set_maple_command(frame->cmd);
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }
}

static void maple_handle_devinfo(struct maple_frame *frame) {
    MAPLE_TRACE("DEVINFO maplebus frame received\n");

    struct maple_device *dev = maple_device_get(frame->maple_addr);

    if (dev->enable) {
        struct maple_devinfo devinfo;
        maple_device_info(dev, &devinfo);
        maple_compile_devinfo(&devinfo, frame->output_data);
        frame->output_len = MAPLE_DEVINFO_SIZE;
        maple_write_frame_resp(frame, MAPLE_RESP_DEVINFO);
    } else {
        // this port/unit combo is not plugged in
        frame->output_len = 0;
        maple_write_frame_resp(frame, MAPLE_RESP_NONE);
    }

    holly_raise_nrm_int(HOLLY_MAPLE_ISTNRM_DMA_COMPLETE);
}

static void maple_handle_getcond(struct maple_frame *frame) {
    MAPLE_TRACE("GETCOND maplebus frame received\n");

    struct maple_device *dev = maple_device_get(frame->maple_addr);

    if (dev->enable) {
        struct maple_cond cond;

        maple_device_cond(dev, &cond);
        maple_compile_cond(&cond, frame->output_data);
        frame->output_len = MAPLE_COND_SIZE;
        maple_write_frame_resp(frame, MAPLE_RESP_DATATRF);
    } else {
        error_set_feature("proper response for when the guest tries to send "
                          "the GETCOND command to an empty maple port");
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }

    holly_raise_nrm_int(HOLLY_MAPLE_ISTNRM_DMA_COMPLETE);
}

void maple_write_frame_resp(struct maple_frame *frame, unsigned resp_code) {
    unsigned len = frame->output_len / 4;
    uint32_t pkt_hdr = ((resp_code << MAPLE_CMD_SHIFT) & MAPLE_CMD_MASK) |
        ((frame->maple_addr << MAPLE_ADDR_SHIFT) & MAPLE_ADDR_MASK) |
        ((len << MAPLE_PACK_LEN_SHIFT) & MAPLE_PACK_LEN_MASK);

    sh4_dmac_transfer_to_mem(frame->recv_addr, sizeof(pkt_hdr), 1, &pkt_hdr);
    if (len) {
        sh4_dmac_transfer_to_mem(frame->recv_addr + 4, 1,
                                 frame->output_len, frame->output_data);
    }
}

static void maple_decode_frame(struct maple_frame *frame_out,
                               uint32_t const dat[3]) {
    uint32_t msg_length_port = dat[0];
    uint32_t recv_addr = dat[1];
    uint32_t cmd_addr_pack_len = dat[2];

    for (unsigned idx = 0; idx < 3; idx++)
        MAPLE_TRACE("%08x\n", dat[idx]);

    frame_out->input_len = (msg_length_port & MAPLE_LENGTH_MASK) >>
        MAPLE_LENGTH_SHIFT;
    frame_out->input_len *= 4;
    frame_out->port = (msg_length_port & MAPLE_PORT_MASK) >> MAPLE_PORT_SHIFT;
    frame_out->ptrn = (msg_length_port & MAPLE_PTRN_MASK) >> MAPLE_PTRN_SHIFT;
    frame_out->last_frame = (bool)(msg_length_port & MAPLE_LAST_MASK);

    frame_out->cmd = (enum maple_cmd)((cmd_addr_pack_len & MAPLE_CMD_MASK) >>
                                      MAPLE_CMD_SHIFT);
    frame_out->maple_addr = (cmd_addr_pack_len & MAPLE_ADDR_MASK) >>
        MAPLE_ADDR_SHIFT;
    frame_out->pack_len = (cmd_addr_pack_len & MAPLE_PACK_LEN_MASK) >>
        MAPLE_PACK_LEN_SHIFT;

    frame_out->recv_addr = recv_addr;

    if (frame_out->input_len != (4 * frame_out->pack_len)) {
        // IDK if these two values are supposed to always be the same or not
        error_set_feature("maple frames with differing lengths");
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }
}

uint32_t maple_read_frame(struct maple_frame *frame_out, uint32_t addr) {
    uint32_t frame_hdr[3];

    sh4_dmac_transfer_from_mem(addr, sizeof(frame_hdr[0]), 3, frame_hdr);
    maple_decode_frame(frame_out, frame_hdr);

    addr += 12;

    if (frame_out->input_len) {
        sh4_dmac_transfer_from_mem(addr, 4, frame_out->input_len / 4,
                                   frame_out->input_data);
    }

    addr += frame_out->input_len;

    return addr;
}

void maple_do_trace(char const *msg, ...) {
    va_list var_args;
    va_start(var_args, msg);

    printf("MAPLE: ");

    vprintf(msg, var_args);

    va_end(var_args);
}

void maple_addr_unpack(unsigned addr, unsigned *port_out, unsigned *unit_out) {
    unsigned unit, port;

    if ((addr & 0x3f) == 0x20)
        unit = 0;
    else if ((addr & 0x1f) == 1)
        unit = 1;
    else if ((addr & 0x1f) == 2)
        unit = 2;
    else if ((addr & 0x1f) == 4)
        unit = 3;
    else if ((addr & 0x1f) == 8)
        unit = 4;
    else if ((addr & 0x1f) == 16)
        unit = 5;
    else
        RAISE_ERROR(ERROR_INTEGRITY);

    port = (addr >> 6) & 0x3;

    *port_out = port;
    *unit_out = unit;
}

unsigned maple_addr_pack(unsigned port, unsigned unit) {
#ifdef INVARIANTS
    if (port >= MAPLE_PORT_COUNT || unit >= MAPLE_UNIT_COUNT)
        RAISE_ERROR(ERROR_INTEGRITY);
#endif

    unsigned addr;

    if (unit > 0)
        addr =  (1 << (unit - 1)) & 0x1f;
    else
        addr = 0x20;

    addr |= port << 6;

    return addr;
}
