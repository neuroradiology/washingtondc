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

#include <stdio.h>

#include "MemoryMap.h"
#include "hw/gdrom/gdrom_reg.h"
#include "holly_intc.h"
#include "mem_code.h"
#include "dreamcast.h"
#include "hw/sh4/sh4.h"
#include "hw/sh4/sh4_dmac.h"

#include "sys_block.h"

#define N_SYS_REGS (ADDR_SYS_LAST - ADDR_SYS_FIRST + 1)
static reg32_t sys_regs[N_SYS_REGS];

static uint32_t reg_sb_c2dstat, reg_sb_c2dlen;

struct sys_mapped_reg;

typedef int(*sys_reg_read_handler_t)(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len);
typedef int(*sys_reg_write_handler_t)(struct sys_mapped_reg const *reg_info,
                                      void const *buf, addr32_t addr,
                                      unsigned len);

static int default_sys_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                        void *buf, addr32_t addr, unsigned len);
static int default_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                         void const *buf, addr32_t addr,
                                         unsigned len);
static int warn_sys_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len);
static int warn_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                      void const *buf, addr32_t addr,
                                      unsigned len);

/* write handler for registers that should be read-only */
static int sys_read_only_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                           void const *buf, addr32_t addr,
                                           unsigned len);

static int ignore_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                        void const *buf, addr32_t addr,
                                        unsigned len);

static int sys_sbrev_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len);

static int sb_c2dst_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len);
static int sb_c2dst_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                      void const *buf, addr32_t addr,
                                      unsigned len);

static int sb_c2dstat_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                       void *buf, addr32_t addr, unsigned len);
static int sb_c2dstat_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                        void const *buf, addr32_t addr,
                                        unsigned len);

static int sb_c2dlen_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                      void *buf, addr32_t addr, unsigned len);
static int sb_c2dlen_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                       void const *buf, addr32_t addr,
                                       unsigned len);

/* yay, interrrupt registers */
static struct sys_mapped_reg {
    char const *reg_name;

    addr32_t addr;

    unsigned len;

    sys_reg_read_handler_t on_read;
    sys_reg_write_handler_t on_write;
} sys_reg_info[] = {
    { "SB_C2DSTAT", 0x005f6800, 4,
      sb_c2dstat_reg_read_handler, sb_c2dstat_reg_write_handler },
    { "SB_C2DLEN", 0x005f6804, 4,
      sb_c2dlen_reg_read_handler, sb_c2dlen_reg_write_handler },
    { "SB_C2DST", 0x005f6808, 4,
      sb_c2dst_reg_read_handler, sb_c2dst_reg_write_handler },
    { "SB_SDSTAW", 0x5f6810, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_SDBAAW", 0x5f6814, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_SDWLT", 0x5f6818, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_SDLAS", 0x5f681c, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_SDST", 0x5f6820, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_DBREQM", 0x5f6840, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_BAVLWC", 0x5f6844, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_C2DPRYC", 0x5f6848, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    /* TODO: spec says default val if SB_C2DMAXL is 1, but bios writes 0 ? */
    { "SB_C2DMAXL", 0x5f684c, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_LMMODE0", 0x5f6884, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_LMMODE1", 0x5f6888, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_FFST", 0x5f688c, 4,
      default_sys_reg_read_handler, sys_read_only_reg_write_handler },

    { "SB_SBREV", 0x5f689c, 4,
      sys_sbrev_reg_read_handler, sys_read_only_reg_write_handler },

    /* TODO: spec says default val if SB_RBSPLT's MSB is 0, but bios writes 1 */
    { "SB_RBSPLT", 0x5f68a0, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    /* I can't  seem to find any info on what the register at 0x5f68a4 is */
    { "UNKNOWN_REG_5f68a4", 0x5f68a4, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    /* I can't  seem to find any info on what the register at 0x5f68ac is */
    { "UNKNOWN_REG_5f68ac", 0x5f68ac, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },

    { "SB_IML2NRM", 0x5f6910, 4,
      holly_reg_iml2nrm_read_handler, holly_reg_iml2nrm_write_handler },
    { "SB_IML2EXT", 0x5f6914, 4,
      holly_reg_iml2ext_read_handler, holly_reg_iml2ext_write_handler },
    { "SB_IML2ERR", 0x5f6918, 4,
      holly_reg_iml2err_read_handler, holly_reg_iml2err_write_handler },
    { "SB_IML4NRM", 0x5f6920, 4,
      holly_reg_iml4nrm_read_handler, holly_reg_iml4nrm_write_handler },
    { "SB_IML4EXT", 0x5f6924, 4,
      holly_reg_iml4ext_read_handler, holly_reg_iml4ext_write_handler },
    { "SB_IML4ERR", 0x5f6928, 4,
      holly_reg_iml4err_read_handler, holly_reg_iml4err_write_handler },
    { "SB_IML6NRM", 0x5f6930, 4,
      holly_reg_iml6nrm_read_handler, holly_reg_iml6nrm_write_handler },
    { "SB_IML6EXT", 0x5f6934, 4,
      holly_reg_iml6ext_read_handler, holly_reg_iml6ext_write_handler },
    { "SB_IML6ERR", 0x5f6938, 4,
      holly_reg_iml6err_read_handler, holly_reg_iml6err_write_handler },

    { "SB_PDTNRM", 0x5f6940, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_PDTEXT", 0x5f6944, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },

    /* arguably these ones should go into their own hw/g2 subdirectory... */
    { "SB_G2DTNRM", 0x5f6950, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },
    { "SB_G2DTEXT", 0x5f6954, 4,
      warn_sys_reg_read_handler, warn_sys_reg_write_handler },

    { "SB_ISTNRM", 0x5f6900, 4,
      holly_reg_istnrm_read_handler, holly_reg_istnrm_write_handler },
    { "SB_ISTEXT", 0x5f6904, 4,
      holly_reg_istext_read_handler, holly_reg_istext_write_handler },
    { "SB_ISTERR", 0x5f6908, 4,
      holly_reg_isterr_read_handler, holly_reg_isterr_write_handler },

    { NULL }
};

static int default_sys_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                        void *buf, addr32_t addr,
                                        unsigned len) {
    size_t idx = (addr - ADDR_SYS_FIRST) >> 2;
    memcpy(buf, idx + sys_regs, len);
    return MEM_ACCESS_SUCCESS;
}

static int default_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                         void const *buf, addr32_t addr,
                                         unsigned len) {
    size_t idx = (addr - ADDR_SYS_FIRST) >> 2;
    memcpy(idx + sys_regs, buf, len);
    return MEM_ACCESS_SUCCESS;
}

static int warn_sys_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    int ret_code = default_sys_reg_read_handler(reg_info, buf, addr, len);

    if (ret_code) {
        fprintf(stderr, "WARNING: read from system register %s\n",
                reg_info->reg_name);
    } else {
        switch (reg_info->len) {
        case 1:
            memcpy(&val8, buf, sizeof(val8));
            fprintf(stderr, "WARNING: read 0x%02x from system register %s\n",
                    (unsigned)val8, reg_info->reg_name);
            break;
        case 2:
            memcpy(&val16, buf, sizeof(val16));
            fprintf(stderr, "WARNING: read 0x%04x from system register %s\n",
                    (unsigned)val16, reg_info->reg_name);
            break;
        case 4:
            memcpy(&val32, buf, sizeof(val32));
            fprintf(stderr, "WARNING: read 0x%08x from system register %s\n",
                    (unsigned)val32, reg_info->reg_name);
            break;
        default:
            fprintf(stderr, "WARNING: read from system register %s\n",
                    reg_info->reg_name);
        }
    }

    return MEM_ACCESS_SUCCESS;
}

static int warn_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                      void const *buf, addr32_t addr,
                                      unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    switch (reg_info->len) {
    case 1:
        memcpy(&val8, buf, sizeof(val8));
        fprintf(stderr, "WARNING: writing 0x%02x to system register %s\n",
                (unsigned)val8, reg_info->reg_name);
        break;
    case 2:
        memcpy(&val16, buf, sizeof(val16));
        fprintf(stderr, "WARNING: writing 0x%04x to system register %s\n",
                (unsigned)val16, reg_info->reg_name);
        break;
    case 4:
        memcpy(&val32, buf, sizeof(val32));
        fprintf(stderr, "WARNING: writing 0x%08x to system register %s\n",
                (unsigned)val32, reg_info->reg_name);
        break;
    default:
        fprintf(stderr, "WARNING: writing to system register %s\n",
                reg_info->reg_name);
    }

    return default_sys_reg_write_handler(reg_info, buf, addr, len);
}

int sys_block_read(void *buf, size_t addr, size_t len) {
    struct sys_mapped_reg *curs = sys_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len == len) {
                return curs->on_read(curs, buf, addr, len);
            } else {
                error_set_feature("Whatever happens when you use an "
                                  "inappropriate length while writing to a "
                                  "system register");
                error_set_address(addr);
                error_set_length(len);
                PENDING_ERROR(ERROR_UNIMPLEMENTED);
                return MEM_ACCESS_FAILURE;
            }
        }
        curs++;
    }

    error_set_feature("accessing one of the system block registers");
    error_set_address(addr);
    PENDING_ERROR(ERROR_UNIMPLEMENTED);
    return MEM_ACCESS_FAILURE;
}

int sys_block_write(void const *buf, size_t addr, size_t len) {
    struct sys_mapped_reg *curs = sys_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len == len) {
                return curs->on_write(curs, buf, addr, len);
            } else {
                error_set_feature("Whatever happens when you use an "
                                  "inappropriate length while reading from a "
                                  "system register");
                error_set_address(addr);
                error_set_length(len);
                PENDING_ERROR(ERROR_UNIMPLEMENTED);
                return MEM_ACCESS_FAILURE;
            }
        }
        curs++;
    }

    error_set_feature("accessing one of the system block registers");
    error_set_address(addr);
    PENDING_ERROR(ERROR_UNIMPLEMENTED);
    return MEM_ACCESS_FAILURE;
}

static int sys_read_only_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                           void const *buf, addr32_t addr,
                                           unsigned len) {
    error_set_feature("Whatever happens when you try to write to a read-only "
                      "system block register");
    error_set_address(addr);
    error_set_length(len);
    PENDING_ERROR(ERROR_UNIMPLEMENTED);
    return MEM_ACCESS_FAILURE;
}

__attribute__((unused)) static int
ignore_sys_reg_write_handler(struct sys_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len) {
    return MEM_ACCESS_SUCCESS;
}

static int sys_sbrev_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                      void *buf, addr32_t addr, unsigned len) {
    uint32_t sbrev = 16;
    memcpy(buf, &sbrev, len);
    return MEM_ACCESS_SUCCESS;
}

static int sb_c2dst_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                     void *buf, addr32_t addr, unsigned len) {
    fprintf(stderr, "WARNING: reading 0 from SB_C2DST\n");

    memset(buf, 0, len);

    return 0;
}


static int sb_c2dst_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                      void const *buf, addr32_t addr,
                                      unsigned len) {
    uint32_t dat;
    memcpy(&dat, buf, sizeof(dat));

    if (dat) {
        sh4_dmac_channel2(dreamcast_get_cpu(), reg_sb_c2dstat, reg_sb_c2dlen);
    }

    return 0;
}

static int sb_c2dlen_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                      void *buf, addr32_t addr, unsigned len) {
    memcpy(buf, &reg_sb_c2dlen, len);

    fprintf(stderr, "WARNING: reading %08x from SB_C2DLEN\n",
            (unsigned)reg_sb_c2dlen);

    return 0;
}

static int sb_c2dlen_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                       void const *buf, addr32_t addr,
                                       unsigned len) {
    memcpy(&reg_sb_c2dlen, buf, sizeof(reg_sb_c2dlen));

    fprintf(stderr, "WARNING: writing %08x to SB_C2DLEN\n",
            (unsigned)reg_sb_c2dlen);

    return 0;
}

static int sb_c2dstat_reg_read_handler(struct sys_mapped_reg const *reg_info,
                                      void *buf, addr32_t addr, unsigned len) {
    memcpy(buf, &reg_sb_c2dstat, len);

    fprintf(stderr, "WARNING: reading %08x from SB_C2DSTAT\n",
            (unsigned)reg_sb_c2dstat);

    return 0;
}

static int sb_c2dstat_reg_write_handler(struct sys_mapped_reg const *reg_info,
                                       void const *buf, addr32_t addr,
                                       unsigned len) {
    memcpy(&reg_sb_c2dstat, buf, sizeof(reg_sb_c2dstat));

    fprintf(stderr, "WARNING: writing %08x to SB_C2DSTAT\n",
            (unsigned)reg_sb_c2dstat);

    return 0;
}
