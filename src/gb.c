#include "gb.h"
#include "cpu.h"
#include "lcd.h"
#include "stdio.h"
#include "stdlib.h"

// Critical memory allocation, abort on failure
void* crit_alloc(size_t size) {
    void* ptr = calloc(size, 1);
    if (!ptr) {
        printf("Memory allocation failed!");
        exit(1);
    }
    return ptr;
}

GameBoy* make_gb(u8* rom, size_t size) {
    if (size < 0x8000) {
        printf("File must be at least 0x8000 bytes\n");
        return NULL;
    }

    // Header checks
    u8 rom_size = rom[0x0148];
    if (rom_size > 8) {
        printf("Header byte 0x0148 (rom size) must not be greather than 8\n");
        return NULL;
    }
    if (0x8000 * ((size_t)1 << rom_size) != size) {
        printf("Header size mismatch\n");
        return NULL;
    }

    // Determine console type
    GameBoy* gb;
    if (rom[0x0143] == 0x80 || rom[0x0143] == 0xC0) {
        // Game Boy Color
        printf("Game Boy Color not implemented yet!\n");
        exit(1);
    } else if (rom[0x0146] == 0x03) {
        // Super Game Boy
        printf("Super Game Boy not implemented yet!\n");
        exit(1);
    } else {
        // Monochrome/Original Game Boy
        gb = crit_alloc(sizeof(GameBoy));
        gb->type = DMG;
    }

    gb->vram = crit_alloc(gb->type == CGB ? 0x4000 : 0x2000);
    gb->wram_lo = crit_alloc(gb->type == CGB ? 0x8000 : 0x2000);
    gb->wram_hi = gb->wram_lo + 0x1000;
    gb->oam = crit_alloc(0xA0);
    gb->hram = crit_alloc(0x7F);

    // Cartridge stuff
    if (rom[0x147] != 0x00) {
        printf("Only standard (no mapper) carts supported right now");
        exit(1);
    }
    gb->rom_lo = rom;
    gb->rom_hi = rom + 0x4000;
    gb->cartram = NULL;

    // Initialize registers
    // (TODO: Make these actually correct later;)
    gb->a = 0;
    gb->f_z = false;
    gb->f_n = false;
    gb->f_h = false;
    gb->f_c = false;
    gb->bc = 0;
    gb->de = 0;
    gb->hl = 0;
    gb->pc = 0x0100;
    gb->sp = 0xFFFE;

    gb->ime = false;

    gb->lcd_en = true;

    return gb;
}

void destroy_gb(GameBoy* gb) {
    free(gb->vram);
    free(gb->cartram);
    free(gb->wram_lo);
    free(gb->oam);
    free(gb->hram);
    free(gb);
}

void run_frame(GameBoy* gb) {
    while (!gb->end_frame) {
        // if (gb->pc == 0x2E4) {
        //     printf("Reached\n");
        //     exit(1);
        // }
        run_opcode(gb);
    }
    gb->end_frame = false;
}

u8 io_read(GameBoy* gb, u16 addr) {
    addr &= 0x7F;

    // Audio registers
    if (0x10 <= addr && addr <= 0x3f) {
        return 0x00;
    }

    switch (addr) {
    case 0x00: // P1 (FF00)
        return 0xFF;
    case 0x01: // SB (FF01)
        return gb->sb;
    case 0x02: // SC (FF02)
        return gb->sc;
    case 0x04: // DIV (FF04)
        return gb->div >> 8;
    case 0x05: // TIMA (FF05)
        return gb->tima;
    case 0x06: // TMA (FF06)
        return gb->tma;
    case 0x07: // TAC (FF07)
        return (gb->tac_en << 2) | gb->tac_clk | 0xF8;
    case 0x0F: // IF (FF0F)
        return gb->if_;
    case 0x40: // LCDC (FF40)
        return (gb->lcd_en << 7) | (gb->win_map << 6) | (gb->win_en << 5) |
               (gb->tile_sel << 4) | (gb->bg_map << 3) | (gb->obj_size << 2) |
               (gb->obj_en << 1) | (gb->bg_en << 0);
    case 0x41: // STAT (FF41)
        return gb->stat;
    case 0x42: // SCY (FF42)
        return gb->scy;
    case 0x43: // SCX (FF43)
        return gb->scx;
    case 0x44: // LY (FF44)
        return gb->ly;
    case 0x45: // LYC (FF45)
        return gb->lyc;
    case 0x46: // DMA (FF46)
        return gb->dma;
    case 0x47: // BGP (FF47)
        return (gb->bgp[0] << 0) | (gb->bgp[1] << 2) | (gb->bgp[2] << 4) |
               (gb->bgp[3] << 6);
    case 0x48: // OBP0 (FF48)
        return (gb->obp0[0] << 0) | (gb->obp0[1] << 2) | (gb->obp0[2] << 4) |
               (gb->obp0[3] << 6);
    case 0x49: // OBP1 (FF49)
        return (gb->obp1[0] << 0) | (gb->obp1[1] << 2) | (gb->obp1[2] << 4) |
               (gb->obp1[3] << 6);
    case 0x4A: // WY (FF4A)
        return gb->wy;
    case 0x4B: // WX (FF4B)
        return gb->wx;
    default:
        printf("Unimplemented read at IO address %x\n", (int)addr);
        exit(1);
    }
}

u8 read(GameBoy* gb, u16 addr) {
    if (addr < 0x8000) {
        // 0x0000 - 0x7FFF (ROM)
        u8* ptr = (addr & 0x4000) ? gb->rom_hi : gb->rom_lo;
        return ptr ? ptr[addr & 0x3FFF] : 0xFF;
    } else if (addr < 0xA000) {
        // 0x8000 - 0x9FFF (VRAM)
        return gb->vram[addr % 0x2000];
    } else if (addr < 0xC000) {
        // 0xA000 - 0xBFFF (External RAM)
        // TODO: implement external RAM
        return 0xFF;
    } else if (addr < 0xFE00) {
        // 0xC000 - 0xFDFF (WRAM)
        // Designed to account for echo RAM
        u8* ptr = (addr & 0x1000) ? gb->wram_hi : gb->wram_lo;
        return ptr[addr & 0x0FFF];
    } else if (addr < 0xFEA0) {
        // 0xFE00 - 0xFE9F (OAM)
        return gb->oam[addr & 0xFF];
    } else if (addr < 0xFF00) {
        // 0xFEA0 - 0xFEFF (unused)
        return 0xFF;
    } else if (addr < 0xFF80) {
        // 0xFF00 - 0xFF7F (IO)
        return io_read(gb, addr);
    } else if (addr < 0xFFFF) {
        // 0xFF80 - 0xFFFE (HRAM)
        return gb->hram[addr & 0x7F];
    } else {
        return gb->ie;
    }
}

void io_write(GameBoy* gb, u16 addr, u8 data) {
    addr &= 0x7F;

    // Audio registers
    if (0x10 <= addr && addr <= 0x3f) {
        return;
    }

    switch (addr) {
    case 0x00: // P1 (FF00)
        break;
    case 0x01: // SB (FF01)
        gb->sb = data;
        break;
    case 0x02: // SC (FF02)
        gb->sc = data;
        break;
    case 0x04: // DIV (FF04)
        gb->div = 0;
        break;
    case 0x05: // TIMA (FF05)
        gb->tima = data;
        break;
    case 0x06: // TMA (FF06)
        gb->tma = data;
        break;
    case 0x07: // TAC (FF07)
        gb->tac_en = data & (1 << 2);
        gb->tac_clk = data & 0x3;
        break;
    case 0x0F: // IF (FF0F)
        gb->if_ = data & 0x1F;
        break;
    case 0x40: // LCDC (FF40)
        gb->lcd_en = data & (1 << 7);
        gb->win_map = data & (1 << 6);
        gb->win_en = data & (1 << 5);
        gb->tile_sel = data & (1 << 4);
        gb->bg_map = data & (1 << 3);
        gb->obj_size = data & (1 << 2);
        gb->obj_en = data & (1 << 1);
        gb->bg_en = data & (1 << 0);
        break;
    case 0x41: // STAT (FF41)
        gb->stat = data;
        break;
    case 0x42: // SCY (FF42)
        gb->scy = data;
        break;
    case 0x43: // SCX (FF43)
        gb->scx = data;
        break;
    case 0x44: // LY (FF44)
        break;
    case 0x45: // LYC (FF45)
        gb->lyc = data;
        break;
    case 0x46: // DMA (FF46)
        gb->dma = data;
        break;
    case 0x47: // BGP (FF47)
        gb->bgp[0] = (data >> 0) & 0x3;
        gb->bgp[1] = (data >> 2) & 0x3;
        gb->bgp[2] = (data >> 4) & 0x3;
        gb->bgp[3] = (data >> 6) & 0x3;
        break;
    case 0x48: // OBP0 (FF48)
        gb->obp0[0] = (data >> 0) & 0x3;
        gb->obp0[1] = (data >> 2) & 0x3;
        gb->obp0[2] = (data >> 4) & 0x3;
        gb->obp0[3] = (data >> 6) & 0x3;
        break;
    case 0x49: // OBP1 (FF49)
        gb->obp1[0] = (data >> 0) & 0x3;
        gb->obp1[1] = (data >> 2) & 0x3;
        gb->obp1[2] = (data >> 4) & 0x3;
        gb->obp1[3] = (data >> 6) & 0x3;
        break;
    case 0x4A: // WY (FF4A)
        gb->wy = data;
        break;
    case 0x4B: // WX (FF4B)
        gb->wx = data;
        break;
    case 0x7F:
        // Tetris writes here due to a software bug
        break;
    default:
        printf("Unimplemented write %x at IO address %x\n", (int)data,
               (int)addr);
        exit(1);
    }
}

void write(GameBoy* gb, u16 addr, u8 data) {
    if (addr < 0x8000) {
        // 0x0000 - 0x7FFF (ROM)
    } else if (addr < 0xA000) {
        // 0x8000 - 0x9FFF (VRAM)
        gb->vram[addr % 0x2000] = data;
    } else if (addr < 0xC000) {
        // 0xA000 - 0xBFFF (External RAM)
        // TODO: implement external RAM
    } else if (addr < 0xFE00) {
        // 0xC000 - 0xFDFF (WRAM)
        // Designed to account for echo RAM
        u8* ptr = (addr & 0x1000) ? gb->wram_hi : gb->wram_lo;
        ptr[addr & 0x0FFF] = data;
    } else if (addr < 0xFEA0) {
        // 0xFE00 - 0xFE9F (OAM)
        gb->oam[addr & 0xFF] = data;
    } else if (addr < 0xFF00) {
        // 0xFEA0 - 0xFEFF (unused)
    } else if (addr < 0xFF80) {
        // 0xFF00 - 0xFF7F (IO)
        io_write(gb, addr, data);
    } else if (addr < 0xFFFF) {
        // 0xFF80 - 0xFFFE (HRAM)
        gb->hram[addr & 0x7F] = data;
    } else {
        // 0xFFFF (IE)
        gb->ie = data & 0x1F;
    }
}

void cycle(GameBoy* gb) {
    gb->cycles += 2;
    if (gb->lcd_en) {
        for (int i = 0; i < 4; i++) {
            lcd_cycle(gb);
        }
    }
}
