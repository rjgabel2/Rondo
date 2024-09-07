#include "lcd.h"
#include "stdio.h"

// tile_ids from 0x100 to 0x17F are used for BG/Window tiles in $9000–$97FF
static u8 get_tile_pixel(GameBoy* gb, u16 tile_id, u8 x, u8 y) {
    u8 lsb = gb->vram[16 * tile_id + 2 * y];
    u8 msb = gb->vram[16 * tile_id + 2 * y + 1];
    lsb = (lsb >> (7 - x)) & 1;
    msb = (msb >> (7 - x)) & 1;
    return (msb << 1) + lsb;
}

// x and y are tile-based coords, not pixel-based
static u8 get_bg_tile(GameBoy* gb, u8 x, u8 y, bool is_win) {
    bool is_alt_map = is_win ? gb->win_map : gb->bg_map;
    u8* tile_map = is_alt_map ? (gb->vram + 0x1C00) : (gb->vram + 0x1800);
    return tile_map[y * 32 + x];
}

static void render_pixel(GameBoy* gb, u8 x, u8 y) {
    // Background/Window
    u16 tile_id = get_bg_tile(gb, x / 8, y / 8, false);
    if (!gb->tile_sel && (tile_id < 0x80)) {
        tile_id += 0x100;
    }
    u8 color = get_tile_pixel(gb, tile_id, x % 8, y % 8);

    // Set pixel in fbuf
    u8* buff = gb->fbuf;
    buff[x + SCREEN_WIDTH * y] = color;
}

void lcd_cycle(GameBoy* gb) {
    gb->dots++;
    if (gb->dots >= 376) {
        gb->dots = -80;
        gb->ly++;
        if (gb->ly >= 154) {
            gb->ly = 0;
        }
        if (gb->ly == SCREEN_HEIGHT) {
            // Set V-Blank flag in IF
            gb->if_ |= (1 << 0);
            gb->end_frame = true;
        }
    }

    if (gb->ly < SCREEN_HEIGHT && gb->dots < SCREEN_WIDTH && gb->dots >= 0) {
        render_pixel(gb, gb->dots, gb->ly);
    }
}