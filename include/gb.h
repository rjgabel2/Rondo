#ifndef RONDO_GB_H
#define RONDO_GB_H

#include "stdbool.h"
#include "stdint.h"

#define RONDO_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144

// Macro to define CPU register pairs
#if RONDO_BIG_ENDIAN
#define REG_DEF(HI, LO)                                                        \
    union {                                                                    \
        u16 HI##LO;                                                            \
        struct {                                                               \
            u8 HI, LO;                                                         \
        };                                                                     \
    };
#else
#define REG_DEF(HI, LO)                                                        \
    union {                                                                    \
        u16 HI##LO;                                                            \
        struct {                                                               \
            u8 LO, HI;                                                         \
        };                                                                     \
    };
#endif

typedef enum { DMG, SGB, CGB } GBType;

typedef struct {
    GBType type;
    void* fbuf;
    bool end_frame;

    // Pointers to various regions of the GB's memory map
    // 0x0000-0x3FFF
    u8* rom_lo;
    // 0x4000-0x7FFF
    u8* rom_hi;
    // 0x8000-0x9FFF
    u8* vram;
    // 0xA000-0xBFFF
    u8* cartram;
    // 0xC000-0xCFFF
    u8* wram_lo;
    // 0xD000-0xDFFF
    u8* wram_hi;
    // 0xFE00-0xFE9F
    u8* oam;
    // 0xFF80-0xFFFF
    u8* hram;

    // Internal CPU registers and flags
    u8 a;
    bool f_z, f_n, f_h, f_c;
    u16 pc, sp;
    REG_DEF(b, c)
    REG_DEF(d, e)
    REG_DEF(h, l)

    bool ime;

    u8 sb; // FF01
    u8 sc; // FF02

    // Timer registers
    u16 div; // Upper 8 bits are FF04
    u8 tima; // FF05
    u8 tma;  // FF06
    // TAC (FF07)
    bool tac_en; // Bit 2
    u8 tac_clk;  // Bits 0-1

    u8 if_; // FF0F

    // LCDC (FF40)
    bool lcd_en;   // Bit 7
    bool win_map;  // Bit 6
    bool win_en;   // Bit 5
    bool tile_sel; // Bit 4
    bool bg_map;   // Bit 3
    bool obj_size; // Bit 2
    bool obj_en;   // Bit 1
    bool bg_en;    // Bit 0

    // STAT (FF41)
    u8 stat;

    u8 scy;     // FF42
    u8 scx;     // FF43
    u8 ly;      // FF44
    u8 lyc;     // FF45
    u8 dma;     // FF46
    u8 bgp[4];  // FF47
    u8 obp0[4]; // FF48
    u8 obp1[4]; // FF49
    u8 wy;      // FF4A
    u8 wx;      // FF4B

    u8 ie; // FFFF

    // Internal stuff
    u32 cycles;
    // Ranges from -80 to 375 on each scanline
    s16 dots;
} GameBoy;

// Return null if there was a problem
GameBoy* make_gb(u8* rom, size_t size);
void destroy_gb(GameBoy* gb);

void run_frame(GameBoy* gb);

u8 read(GameBoy* gb, u16 addr);
void write(GameBoy* gb, u16 addr, u8 data);

void cycle(GameBoy* gb);

#endif