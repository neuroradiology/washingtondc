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

#include "types.hpp"
#include "MemoryMap.hpp"
#include "BaseException.hpp"

#include "gdrom_reg.hpp"

bool bsy_signal; /* get off the phone! */
bool drq_signal;

unsigned n_bytes_received;

static const size_t N_GDROM_REGS = ADDR_GDROM_LAST - ADDR_GDROM_FIRST + 1;
static reg32_t gdrom_regs[N_GDROM_REGS];

typedef int(*gdrom_reg_read_handler_t)(
    struct gdrom_mem_mapped_reg const *reg_info,
    void *buf, addr32_t addr, unsigned len);
typedef int(*gdrom_reg_write_handler_t)(
    struct gdrom_mem_mapped_reg const *reg_info,
    void const *buf, addr32_t addr, unsigned len);

static int
default_gdrom_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                               void *buf, addr32_t addr, unsigned len);
static int
default_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                                void const *buf, addr32_t addr, unsigned len);
static int
warn_gdrom_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                            void *buf, addr32_t addr, unsigned len);
static int
warn_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len);
static int
ignore_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                               void const *buf, addr32_t addr, unsigned len);

static int
gdrom_alt_status_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                              void *buf, addr32_t addr, unsigned len);
static int
gdrom_status_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                          void *buf, addr32_t addr, unsigned len);

static int
gdrom_error_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                             void *buf, addr32_t addr, unsigned len);

static int
gdrom_cmd_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                            void const *buf, addr32_t addr, unsigned len);

static int
gdrom_data_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len);


static bool pending_gdrom_irq = false;

static struct gdrom_mem_mapped_reg {
    char const *reg_name;

    addr32_t addr;

    unsigned len;

    gdrom_reg_read_handler_t on_read;
    gdrom_reg_write_handler_t on_write;
} gdrom_reg_info[] = {
    { "Drive Select", 0x5f7098, 4,
      warn_gdrom_reg_read_handler, warn_gdrom_reg_write_handler },
    { "Alt status/device control", 0x5f7018, 4,
      gdrom_alt_status_read_handler, warn_gdrom_reg_write_handler },
    { "status/command", 0x5f709c, 4,
      gdrom_status_read_handler, gdrom_cmd_reg_write_handler },
    { "GD-ROM Data", 0x5f7080, 4,
      warn_gdrom_reg_read_handler, gdrom_data_reg_write_handler },
    { "Error/features", 0x5f7084, 4,
      gdrom_error_reg_read_handler, warn_gdrom_reg_write_handler },
    { "Interrupt reason/sector count", 0x5f7088, 4,
      warn_gdrom_reg_read_handler, warn_gdrom_reg_write_handler },
    { "Sector number", 0x5f708c, 4,
      warn_gdrom_reg_read_handler, warn_gdrom_reg_write_handler },
    { "Byte Count (low)", 0x5f7090, 4,
      warn_gdrom_reg_read_handler, warn_gdrom_reg_write_handler },
    { "Byte Count (high)", 0x5f7094, 4,
      warn_gdrom_reg_read_handler, warn_gdrom_reg_write_handler },
    { NULL }
};

int gdrom_reg_read(void *buf, size_t addr, size_t len) {
    struct gdrom_mem_mapped_reg *curs = gdrom_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len >= len) {
                return curs->on_read(curs, buf, addr, len);
            } else {
                BOOST_THROW_EXCEPTION(UnimplementedError() <<
                                      errinfo_feature("Whatever happens when "
                                                      "you use an inapproriate "
                                                      "length while reading "
                                                      "from a gdrom "
                                                      "register") <<
                                      errinfo_guest_addr(addr) <<
                                      errinfo_length(len));
            }
        }
        curs++;
    }

    BOOST_THROW_EXCEPTION(UnimplementedError() <<
                          errinfo_feature("reading from one of the "
                                          "gdrom registers") <<
                          errinfo_guest_addr(addr));
}

int gdrom_reg_write(void const *buf, size_t addr, size_t len) {
    struct gdrom_mem_mapped_reg *curs = gdrom_reg_info;

    while (curs->reg_name) {
        if (curs->addr == addr) {
            if (curs->len >= len) {
                return curs->on_write(curs, buf, addr, len);
            } else {
                BOOST_THROW_EXCEPTION(UnimplementedError() <<
                                      errinfo_feature("Whatever happens when "
                                                      "you use an inapproriate "
                                                      "length while writing to "
                                                      "a gdrom register") <<
                                      errinfo_guest_addr(addr) <<
                                      errinfo_length(len));
            }
        }
        curs++;
    }

    BOOST_THROW_EXCEPTION(UnimplementedError() <<
                          errinfo_feature("writing to one of the "
                                          "gdrom registers") <<
                          errinfo_guest_addr(addr));
}

static int
default_gdrom_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                               void *buf, addr32_t addr, unsigned len) {
    size_t idx = (addr - ADDR_GDROM_FIRST) >> 2;
    memcpy(buf, idx + gdrom_regs, len);
    return 0;
}

static int
default_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                                void const *buf, addr32_t addr, unsigned len) {
    size_t idx = (addr - ADDR_GDROM_FIRST) >> 2;
    memcpy(idx + gdrom_regs, buf, len);
    return 0;
}

static int
warn_gdrom_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                         void *buf, addr32_t addr, unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    int ret_code = default_gdrom_reg_read_handler(reg_info, buf, addr, len);

    if (ret_code) {
        std::cerr << "WARNING: read from gdrom register " <<
            reg_info->reg_name << std::endl;
    } else {
        switch (len) {
        case 1:
            memcpy(&val8, buf, sizeof(val8));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(2) <<
                unsigned(val8) << " from gdrom register " <<
                reg_info->reg_name << std::endl;
            break;
        case 2:
            memcpy(&val16, buf, sizeof(val16));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(4) <<
                unsigned(val16) << " from gdrom register " <<
                reg_info->reg_name << std::endl;
            break;
        case 4:
            memcpy(&val32, buf, sizeof(val32));
            std::cerr << "WARNING: read 0x" <<
                std::hex << std::setfill('0') << std::setw(8) <<
                unsigned(val32) << " from gdrom register " <<
                reg_info->reg_name << std::endl;
            break;
        default:
            std::cerr << "WARNING: read from gdrom register " <<
                reg_info->reg_name << std::endl;
        }
    }

    return 0;
}

static int
warn_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                          void const *buf, addr32_t addr, unsigned len) {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    switch (len) {
    case 1:
        memcpy(&val8, buf, sizeof(val8));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(2) <<
            unsigned(val8) << " to gdrom register " <<
            reg_info->reg_name << std::endl;
        break;
    case 2:
        memcpy(&val16, buf, sizeof(val16));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(4) <<
            unsigned(val16) << " to gdrom register " <<
            reg_info->reg_name << std::endl;
        break;
    case 4:
        memcpy(&val32, buf, sizeof(val32));
        std::cerr << "WARNING: writing 0x" <<
            std::hex << std::setfill('0') << std::setw(8) <<
            unsigned(val32) << " to gdrom register " <<
            reg_info->reg_name << std::endl;
        break;
    default:
        std::cerr << "WARNING: reading from gdrom register " <<
            reg_info->reg_name << std::endl;
    }

    return default_gdrom_reg_write_handler(reg_info, buf, addr, len);
}

static int
ignore_gdrom_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                               void const *buf, addr32_t addr, unsigned len) {
    /* do nothing */
    return 0;
}

static int
gdrom_alt_status_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                              void *buf, addr32_t addr, unsigned len) {
    // immediately acknowledge receipt of everything for now
    // uint32_t val =  0 /*int(!pending_gdrom_irq)*//*1*/ << 7; // BSY
    // val |= 1 << 6; // DRDY
    // val |= 1 << 3; // DRQ
    uint32_t val = (int(bsy_signal) << 7) | (int(drq_signal) << 3);

    std::cout << "WARNING: read " << val << " from alternate GDROM status " <<
        "register" << std::endl;

    memcpy(buf, &val, len > 4 ? 4 : len);

    return 0;
}

static int
gdrom_status_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                          void *buf, addr32_t addr, unsigned len) {
    // immediately acknowledge receipt of everything for now
    // uint32_t val =  0 /*int(!pending_gdrom_irq)*//*1*/ << 7; // BSY
    // val |= 1 << 6; // DRDY
    // val |= 1 << 3; // DRQ
    uint32_t val = (int(bsy_signal) << 7) | (int(drq_signal) << 3);

    pending_gdrom_irq = false;

    std::cout << "WARNING: read " << val << " from GDROM status register"
              << std::endl;

    memcpy(buf, &val, len > 4 ? 4 : len);

    return 0;
}

static int
gdrom_error_reg_read_handler(struct gdrom_mem_mapped_reg const *reg_info,
                             void *buf, addr32_t addr, unsigned len) {
    uint32_t val = 0;

    memcpy(buf, &val, len > 4 ? 4 : len);

    return 0;
}

static int
gdrom_cmd_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                            void const *buf, addr32_t addr, unsigned len) {
    reg32_t val = 0;
    size_t n_bytes = len < sizeof(val) ? len : sizeof(val);

    memcpy(&val, buf, n_bytes);
    std::cout << "WARNING: write " << std::hex << val <<
        " to gd-rom command register (" << n_bytes << " bytes)" << std::endl;

    if (val == 0xa0) {
        drq_signal = true;
    }
    pending_gdrom_irq = true;

    return 0;
}

static int
gdrom_data_reg_write_handler(struct gdrom_mem_mapped_reg const *reg_info,
                             void const *buf, addr32_t addr, unsigned len) {
    uint32_t dat = 0;
    size_t n_bytes = len < sizeof(dat) ? len : sizeof(dat);

    memcpy(&dat, buf, n_bytes);

    std::cout << "WARNING: write " << std::hex << dat << "to gdrom data "
        "register (" << len << " bytes)" << std::endl;

    n_bytes_received += 2;

    if (n_bytes_received >= 12) {
        n_bytes_received = 0;
        drq_signal = false;
        bsy_signal = false;
        pending_gdrom_irq = true;
    }

    return 0;
}

bool gdrom_irq() {
    return pending_gdrom_irq;
}
