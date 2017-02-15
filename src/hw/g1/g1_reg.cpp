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

#include <cstring>
#include <iostream>

#include "g1_reg.hpp"

#include "types.hpp"
#include "MemoryMap.hpp"
#include "BaseException.hpp"

static const size_t N_G1_REGS = ADDR_G1_LAST - ADDR_G1_FIRST + 1;
static reg32_t g1_regs[N_G1_REGS];

struct g1_mem_mapped_reg;

typedef int(*g1_reg_read_handler_t)(struct g1_mem_mapped_reg const *reg_info,
                                    void *buf, addr32_t addr, unsigned len);
typedef int(*g1_reg_write_handler_t)(struct g1_mem_mapped_reg const *reg_info,
                                     void const *buf, addr32_t addr,
                                     unsigned len);

static int
default_g1_reg_read_handler(struct g1_mem_mapped_reg const *reg_info,
                            void *buf, addr32_t addr, unsigned len);
static int
default_g1_reg_write_handler(struct g1_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len);
static int
warn_g1_reg_read_handler(struct g1_mem_mapped_reg const *reg_info,
                         void *buf, addr32_t addr, unsigned len);
static int
warn_g1_reg_write_handler(struct g1_mem_mapped_reg const *reg_info,
                          void const *buf, addr32_t addr, unsigned len);

static struct g1_mem_mapped_reg {
    char const *reg_name;

    addr32_t addr;

    unsigned len;

    g1_reg_read_handler_t on_read;
    g1_reg_write_handler_t on_write;
} g1_reg_info[] = {
    // XXX this is supposed to be write-only, but currently it's readable
    { "SB_G1RRC", 0x005f7480, 4,
      warn_g1_reg_read_handler, warn_g1_reg_write_handler },
    { "UNKNOWN", 0x005f74e4, 4,
      warn_g1_reg_read_handler, warn_g1_reg_write_handler },
    { NULL }
};

int g1_reg_read(void *buf, size_t addr, size_t len) {
    struct g1_mem_mapped_reg *curs = g1_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len >= len) {
                return curs->on_read(curs, buf, addr, len);
            } else {
                BOOST_THROW_EXCEPTION(UnimplementedError() <<
                                      errinfo_feature("Whatever happens when "
                                                      "you use an inapproriate "
                                                      "length while reading "
                                                      "from a g1 "
                                                      "register") <<
                                      errinfo_guest_addr(addr) <<
                                      errinfo_length(len));
            }
        }
        curs++;
    }

    BOOST_THROW_EXCEPTION(UnimplementedError() <<
                          errinfo_feature("reading from one of the "
                                          "g1 registers") <<
                          errinfo_guest_addr(addr));
}

int g1_reg_write(void const *buf, size_t addr, size_t len) {
    struct g1_mem_mapped_reg *curs = g1_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len >= len) {
                return curs->on_write(curs, buf, addr, len);
            } else {
                BOOST_THROW_EXCEPTION(UnimplementedError() <<
                                      errinfo_feature("Whatever happens when "
                                                      "you use an inapproriate "
                                                      "length while writing to "
                                                      "a g1 register") <<
                                      errinfo_guest_addr(addr) <<
                                      errinfo_length(len));
            }
        }
        curs++;
    }

    BOOST_THROW_EXCEPTION(UnimplementedError() <<
                          errinfo_feature("writing to one of the "
                                          "g1 registers") <<
                          errinfo_guest_addr(addr));
}

static int
default_g1_reg_read_handler(struct g1_mem_mapped_reg const *reg_info,
                            void *buf, addr32_t addr, unsigned len) {
    size_t idx = addr - ADDR_G1_FIRST;
    memcpy(buf, idx + g1_regs, len);
    return 0;
}

static int
default_g1_reg_write_handler(struct g1_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len) {
    size_t idx = addr - ADDR_G1_FIRST;
    memcpy(idx + g1_regs, buf, len);
    return 0;
}

static int
warn_g1_reg_read_handler(struct g1_mem_mapped_reg const *reg_info,
                            void *buf, addr32_t addr, unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    int ret_code = default_g1_reg_read_handler(reg_info, buf, addr, len);

    if (ret_code) {
        std::cerr << "WARNING: read from g1 register " <<
            reg_info->reg_name << std::endl;
    } else {
        switch (len) {
        case 1:
            memcpy(&val8, buf, sizeof(val8));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(2) <<
                unsigned(val8) << " from g1 register " <<
                reg_info->reg_name << std::endl;
            break;
        case 2:
            memcpy(&val16, buf, sizeof(val16));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(4) <<
                unsigned(val16) << " from g1 register " <<
                reg_info->reg_name << std::endl;
            break;
        case 4:
            memcpy(&val32, buf, sizeof(val32));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(8) <<
                unsigned(val32) << " from g1 register " <<
                reg_info->reg_name << std::endl;
            break;
        default:
            std::cerr << "WARNING: read from g1 register " <<
                reg_info->reg_name << std::endl;
        }
    }

    return 0;
}

static int
warn_g1_reg_write_handler(struct g1_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    switch (len) {
    case 1:
        memcpy(&val8, buf, sizeof(val8));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(2) <<
            unsigned(val8) << " to g1 register " <<
            reg_info->reg_name << std::endl;
        break;
    case 2:
        memcpy(&val16, buf, sizeof(val16));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(4) <<
            unsigned(val16) << " to g1 register " <<
            reg_info->reg_name << std::endl;
        break;
    case 4:
        memcpy(&val32, buf, sizeof(val32));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(8) <<
            unsigned(val32) << " to g1 register " <<
            reg_info->reg_name << std::endl;
        break;
    default:
        std::cerr << "WARNING: reading from g1 register " <<
            reg_info->reg_name << std::endl;
    }

    return default_g1_reg_write_handler(reg_info, buf, addr, len);
}
