/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2016. 2017 snickerbockers
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

#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "dreamcast.h"
#include "gfx/gfx_thread.h"
#include "win/win_thread.h"
#include "io/io_thread.h"
#include "hw/pvr2/framebuffer.h"
#include "gfx/opengl/opengl_output.h"
#include "mount.h"
#include "gdi.h"

static void print_usage(char const *cmd) {
    fprintf(stderr, "USAGE: %s [options] [IP.BIN 1ST_READ.BIN]\n\n", cmd);

    fprintf(stderr, "WashingtonDC Dreamcast Emulator\n\n");

    fprintf(stderr, "OPTIONS:\n"
            "\t-b <bios_path>\tpath to dreamcast boot ROM\n"
            "\t-f <flash_path>\tpath to dreamcast flash ROM image\n"
            "\t-g\t\tenable remote GDB backend\n"
            "\t-d\t\tenable direct boot (skip BIOS)\n"
            "\t-u\t\tskip IP.BIN and boot straight to 1ST_READ.BIN (only "
            "valid for direct boot)\n"
            "\t-s\t\tpath to dreamcast system call image (only needed for "
            "direct boot)\n"
            "\t-t\t\testablish serial server over TCP port 1998\n"
            "\t-h\t\tdisplay this message and exit\n"
            "\t-m\t\tmount the given image in the GD-ROM drive\n");
}

int main(int argc, char **argv) {
    int opt;
    char const *bios_path = NULL, *flash_path = NULL;
    char const *cmd = argv[0];
    bool enable_debugger = false;
    bool boot_direct = false, skip_ip_bin = false;
    char const *path_1st_read_bin = NULL, *path_ip_bin = NULL;
    char const *path_syscalls_bin = NULL;
    char const *path_gdi = NULL;
    bool enable_serial = false;

    while ((opt = getopt(argc, argv, "b:f:s:m:gduht")) != -1) {
        switch (opt) {
        case 'b':
            bios_path = optarg;
            break;
        case 'f':
            flash_path = optarg;
            break;
        case 'g':
            enable_debugger = true;
            break;
        case 'd':
#ifdef ENABLE_DIRECT_BOOT
            boot_direct = true;
#else
            fprintf(stderr, "unable to boot in direct-mode: it's not enabled!\n"
                    "rebuild with -DENABLE_DIRECT_BOOT\n");
            exit(1);
#endif
            break;
        case 'u':
#ifdef ENABLE_DIRECT_BOOT
            skip_ip_bin = true;
#else
            fprintf(stderr, "unable to boot in direct-mode: it's not enabled!\n"
                    "rebuild with -DENABLE_DIRECT_BOOT\n");
            exit(1);
#endif
            break;
        case 's':
            path_syscalls_bin = optarg;
            break;
        case 't':
            enable_serial = true;
            break;
        case 'm':
            path_gdi = optarg;
            break;
        case 'h':
            print_usage(cmd);
            exit(0);
        }
    }

    argv += optind;
    argc -= optind;

    if (path_gdi)
        mount_gdi(path_gdi);

    if (skip_ip_bin && !boot_direct) {
        fprintf(stderr, "Error: -u option is meaningless with -d!\n");
        exit(1);
    }

    if (path_syscalls_bin && !boot_direct)
        fprintf(stderr, "Warning: -s option is meaningless when not "
                "performing a direct boot (-d option)\n");

    if (boot_direct) {
        if (argc != 2) {
            print_usage(cmd);
            exit(1);
        }

        if (!path_syscalls_bin) {
            fprintf(stderr, "Error: cannot direct-boot without a system call "
                    "table (-s flag).\n");
            exit(1);
        }

        path_ip_bin = argv[0];
        path_1st_read_bin = argv[1];

        printf("direct boot enbaled, loading IP.BIN from %s and loading "
               "1ST_READ.BIN from %s\n", path_ip_bin, path_1st_read_bin);
    } else if (argc != 0 || !bios_path) {
        print_usage(cmd);
        exit(1);
    }

#ifdef ENABLE_DIRECT_BOOT
    if (boot_direct) {
        dreamcast_init_direct(path_ip_bin, path_1st_read_bin,
                              bios_path, flash_path, path_syscalls_bin,
                              skip_ip_bin);
    } else {
#endif
        dreamcast_init(bios_path, flash_path);
#ifdef ENABLE_DIRECT_BOOT
    }
#endif

    if (enable_serial) {
#ifdef ENABLE_SERIAL_SERVER
        dreamcast_enable_serial_server();
#else
        fprintf(stderr, "ERROR: Unable to enable TCP serial server\n"
                "Please rebuild with -DENABLE_SERIAL_SERVER\n");
        exit(1);
#endif
    }

    if (enable_debugger) {
#ifdef ENABLE_DEBUGGER
        dreamcast_enable_debugger();
#else
        fprintf(stderr, "ERROR: Unable to enable remote gdb stub.\n"
                "Please rebuild with -DENABLE_DEBUGGER=On\n");
        exit(1);
#endif
    }

    framebuffer_init(640, 480);
    win_thread_launch(640, 480);
    gfx_thread_launch(640, 480);
    io_thread_launch();

    dreamcast_run();

    printf("Waiting for gfx_thread to exit...\n");
    gfx_thread_join();
    printf("gfx_thread has exited.\n");

    printf("Waiting for win_thread to exit...\n");
    win_thread_join();
    printf("win_thread has exited.\n");

    printf("Waiting for io_thread to exit...\n");
    io_thread_join();
    printf("io_thread has exited.\n");

    if (mount_check())
        mount_eject();

    exit(0);
}
