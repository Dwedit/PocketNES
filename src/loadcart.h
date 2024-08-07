#ifndef __LOADCART_H__
#define __LOADCART_H__

#ifdef __cplusplus
extern "C" {
#endif

#define TQROM 119
#define VRC7 85

#define MIRROR	0x01
#define SRAM_	0x02
#define TRAINER	0x04
#define SCREEN4	0x08
#define VS		0x10

//extern char rom_is_compressed;
extern char ewram_owner_is_sram;
extern char sprite_vram_in_use;
extern char do_not_decompress;
extern char do_not_reset_all;

void redecompress(void);
//static void read_rom_header(u8 *nesheader);
//static int get_prg_bank_size(int mapper);
void loadcart(int rom_number, int emu_flags, int loading_state);
void init_cache(u8* nes_header, int do_reset);
void suspend_hdma(void);
void suspend_hdma_and_hide_bg0(void);
void resume_hdma(void);
void swapmem (u32* A, u32*B, u32 Asize);
void simpleswap (void* a_in, void* b_in, u32 size);

void setup_cheatfinder(u8 *cache_end_of_rom, int mode);

#ifdef __cplusplus
}
#endif

#endif
