/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2016, 2017 snickerbockers
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

#ifndef GDBSTUB_H_
#define GDBSTUB_H_

#ifndef ENABLE_DEBUGGER
#error This file should not be included unless the debugger is enabled
#endif

#include <stdint.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include "debugger.h"
#include "types.h"
#include "stringlib.h"

#ifdef __cplusplus
extern "C" {
#endif

// it's 'cause 1999 is the year the Dreamcast came out in America
#define GDB_PORT_NO 1999

// see sh_sh4_register_name in gdb/sh-tdep.c in the gdb source code
enum gdb_reg_order {
    R0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,

    PC, PR, GBR, VBR, MACH, MACL, SR, FPUL, FPSCR,

    FR0, FR1, FR2, FR3, FR4, FR5, FR6, FR7,
    FR8, FR9, FR10, FR11, FR12, FR13, FR14, FR15,

    SSR, SPC,

    R0B0, R1B0, R2B0, R3B0, R4B0, R5B0, R6B0, R7B0,
    R0B1, R1B1, R2B1, R3B1, R4B1, R5B1, R6B1, R7B1,

    N_REGS
};

struct gdb_stub {
    struct debugger *dbg;

    struct evconnlistener *listener;
    bool is_listening;
    struct bufferevent *bev;

    struct evbuffer *output_buffer;

    // the last unsuccessfully acknowledged packet, or empty if there is none
    struct string unack_packet;

    struct string input_packet;

    bool frontend_supports_swbreak;
};

void gdb_init(struct gdb_stub *stub, struct debugger *dbg);
void gdb_cleanup(void *arg);

void gdb_attach(void *argptr);

extern struct debug_frontend gdb_debug_frontend;

#ifdef __cplusplus
}
#endif

#endif
