#include "includes.h"

extern u64 simpleswap32(u32 *A, u32 *B, u32 sizeInBytes);

//EWRAM_BSS char rom_is_compressed=0;	//The loaded rom is compressed
EWRAM_BSS char ewram_owner_is_sram = 0;	//After reading SRAM to the buffer, rom needs to be re-decompressed
//char sprite_vram_in_use=0;	//this forbids using the cheat finder (disabled because cheat finder no longer uses VRAM)
EWRAM_BSS char do_not_decompress = 0;	//while loading state, do not decompress rom, just build tables
EWRAM_BSS char do_not_reset_all = 0;	//when loading an old savestate, do not call "reset all"

EWRAM_BSS u32 my_checksum = 0;	//set by init_cache, called by loadcart

#if MAPPER_OVERLAYS
APPEND void LoadMapperOverlay(int mapperNumber);
#endif

#if SAVE
void redecompress()
{
	u8 *nesheader=rombase-16;
	if (cartflags&TRAINER) nesheader-=512;
	
	ewram_owner_is_sram=0;
	init_cache(nesheader,0);
}
#endif

#if COMPY
static void init_cache_deferred(u8* nes_header);
#endif

APPEND static void read_rom_header(u8 *nesheader)
{
	int romsize;
	int vromsize;

	//Get mapper_number and cartflags
	{
		u8 flags1,flags2;
		flags1=nesheader[6];
		flags2=nesheader[7];
		if (flags2 & 0x0E) flags2=0;
		mapper = (flags1 >> 4) + (flags2 & 0xF0);
		cartflags = (flags1 & 0x0F) + ((flags2 & 0x0F)<<4);
		//disable SRAM if running multiboot
		if (!using_flashcart())
		{
			cartflags&=~SRAM_;
		}
		fourscreen=(cartflags & 0x18)!=0;
		if (mapper==5) fourscreen=true;
		if (mapper==9) fourscreen=false;
		if (mapper==99) cartflags|=VS;
	}

	//Get rom and vrom bases, sizes, and masks
	rombase = nesheader+16;
	if (cartflags&TRAINER)
	{
		rombase+=512;
	}
	rompages=nesheader[4];
	romsize=16*1024*rompages;
	rommask=romsize-1;

	vrompages=nesheader[5];
		
	//round vrom pages up to next power of 2
	if (vrompages>0)
	{
		int new_vrom_pages=1;
		while (vrompages>new_vrom_pages)
		{
			new_vrom_pages*=2;
		}
		vrombase=rombase+romsize;
		vrompages=new_vrom_pages;
	}
	else
	{
		vrombase=NES_VRAM;
	}
	
	vromsize=8*1024*(vrompages>0?vrompages:1);

	if (vrompages>0)
	{
		bankable_vrom=1;
		has_vram=0;
	}
	else
	{
		bankable_vrom=0;
		has_vram=1;
		
		vram_page_mask=0xFF;
		vram_page_base=0;
	}
#if MIXED_VRAM_VROM
	//double memory map size for TQROM, which has mirrored VRAM past first 64k of VROM
	if (mapper==TQROM)
	{
		vromsize*=2;
	}
	switch (mapper)
	{
		case 74:
		case TQROM:
			has_vram=1;
	}
	if (mapper == VRC7 || mapper == 163)
	{
		bankable_vrom=1;
	}
#endif

	vrommask=vromsize-1;
}

APPEND_DATA static const unsigned char prg_bank_size_table[] =
{
	4,64,69,100,
	7,11,15,25,34,40,42,66,77,79,86,105,163,228,100,20
};

APPEND static int lookup_mapper_arr(int mapper, const unsigned char *p)
{
	int value = 0;
	while (true)
	{
		int m = *p++;
		if (m == mapper) break;
		if (m == 100) value++;
		if (m == 20) break;
	}
	return value;
}

APPEND static int get_prg_bank_size(int mapper)
{
	const unsigned char numbers[] = {8, 32, 16};
	int page_size = numbers[lookup_mapper_arr(mapper, prg_bank_size_table)];
	if (rompages == 2 ||
		(mapper == 1 && rompages > 16) ||
		(mapper == 1 && vrompages == 1))
	{
		page_size = 32;
	}
	if (rompages == 1) page_size = 16;
	return page_size;
}


#if 0
static int get_prg_bank_size(int mapper)
{
	int page_size;
	switch (mapper)
	{
		case 4:
//		case 5:
		case 64:
		case 69:
			page_size=8; break;
		case 7:
		case 11:
		case 15:
		case 25: //?
		case 34:
		case 40:
		case 42:
		case 77:
		case 79:
		case 86:
		case 66:
		case 105:
		case 163:
		case 228:
			page_size=32; break;
		default:
			page_size=16; break;
	}
	if (rompages==2)  //was (mapper == 0 && rompages==2)
	{
		page_size=32;
	}
	if (mapper==1 && rompages>16)
	{
		page_size=32;
	}
	if (mapper==1 && vrompages == 1)
	{
		page_size=32;
	}
	return page_size;
}
#endif

#if COMPY
extern void add_deferred_call(int arg1, int arg2, int arg3, int FP);
#define DEFERRED_CALL(func, arg1, arg2, arg3) add_deferred_call((int)(arg1), (int)(arg2), (int)(arg3), (int)(void*)&(func))
extern void set_deferred_call_ptrs(u32 *ptr);
extern void run_deferred_calls();
#endif

APPEND void loadcart2(int rom_number, int emu_flags, int loading_state);



void loadcart(int rom_number, int emu_flags, int loading_state)
{
	#if COMPY
	if (_instant_prg_banks != NULL)
	{
		loadcart_asm();
		return;
	}
	#endif
	
	loadcart2(rom_number, emu_flags, loading_state);
	#if COMPY
	run_deferred_calls();
	#endif
	
	return;
}

APPEND void loadcart2(int rom_number, int emu_flags, int loading_state)
{	
	//loading_state: 0 if called from loadstate, 1 if called from rom menu
//	u8 *p;
	u8 *romname_p;
	u8 *nesheader;
	
	if (emu_flags & EMUFLAGS_PALTIMING)
	{
		emu_flags |= EMUFLAGS_FPS50;
	}
	
//	int i;
	
#if SAVE
	if (loading_state==1)
	{
		if(autostate&1)
		{
			romnumber=rom_number;
			emuflags=emu_flags;
			quickload();	//this calls loadcart, then loads a savestate
			return;
		}
	}
#endif

	Sound_hardware_reset();
	
	#if RESET_ALL
	if (!do_not_reset_all)
	{
		reset_all();
	}
	#endif
	
	romnumber=rom_number;
	emuflags=emu_flags;
	
	frameready=0;
	firstframeready=0;
	
	/*
	ASM pre-tasks:
	reset_all
	mark frame and firstframe as not ready
	
	
	New tasks:
	Take care of loading SRAM and finding out whether an initial savestate exists before decompressing
	
	
	
	C tasks:
	clear memory blocks (nes ram, nes sram, mapperstate, 'low g man fix')
	Read NES header
	Set up VRAM/VROM decision
	do all the init_cache stuff?
	set up initial banks
	hackflags?
	
	init sprite cache
	
	
	ASM tasks:
	set 'bg maps'
	set scanline hooks and stuff
	
	
	*/
	
	//?
	bank6[0]=0xFF;
	
	romname_p=findrom(romnumber);
	nesheader=romname_p+48;
	
	//?
//	hackflags=0;
//	hackflags3=!(emuflags&2);
//	//force PPU hack on
//	emuflags|=1;
	
	read_rom_header(nesheader);	//sets rombase
#if MAPPER_OVERLAYS
	LoadMapperOverlay(mapper_number);
#endif
	
	//Reset RAM, SRAM, etc...
	//memset32(NES_RAM,0,2048);
	memset32(NES_SRAM,0,8192);
	memset32(mapperstate,0,32);
	
	if (cartflags&TRAINER)
	{
		//FIXME: broken for compressed roms
		memcpy32(NES_SRAM+0x1000,rombase-512,512);
	}
	if (!(cartflags&SRAM_))
	{
		//low G man fix...shouldn't we do this by checksum instead?
		NES_SRAM[0x1C7D]=0x7C;
		NES_SRAM[0x1D7D]=0x7D;
	}
#if SAVE
	if (loading_state==1)
	{
		get_saved_sram();
	}
#endif

	#if CHEATFINDER
	setup_cheatfinder(NULL,0);
	if (loading_state==1)
	{
		#if SAVE
		cheatload();
		#endif
	}
	#endif
	
	#if COMPY
	set_deferred_call_ptrs(NES_RAM);
	init_cache_deferred(nesheader);
	DEFERRED_CALL(init_sprite_cache,0,0,0);
	DEFERRED_CALL(loadcart_asm,0,0,0);
	DEFERRED_CALL(memset32,NES_RAM,0,2048);
	#else
	init_cache(nesheader,1);
	init_sprite_cache();
	loadcart_asm();
	memset32(NES_RAM,0,2048);
	#endif


}

#if CHEATFINDER
u8 *get_end_of_cache()
{
	int chr_table_size;
	int prg_table_size;
	
	u8 *end_of_cache=end_of_exram;
	if (!fourscreen)
	{
		end_of_cache+=2048;
	}
	if (vrompages>0)
	{
		chr_table_size=vrompages*8*4;
		if (mapper==TQROM)
		{
			chr_table_size*=2;
		}
		if (has_vram==0)
		{
			if (!fourscreen)
			{
				end_of_cache+=8192;
			}
		}
	}
	else
	{
		chr_table_size=8*4;
	}
	end_of_cache-=chr_table_size;
	prg_table_size=rompages*PRG_16*4;
	end_of_cache-=prg_table_size;
	
	return end_of_cache;
}
#endif

#if !COMPY
APPEND static void init_cache_clear_vram()
{
	//clear VRAM
	memset32((void*)0x06000000,0,0x2000);	//tiles 1
	memset32((void*)0x06004000,0,0x2000);	//tiles 2
	//memset32((void*)0x06006000,0,0x1000);	//tilemap (not 4-screen)  //disabled because it's now where scanline effect buffers go
	if (!do_not_decompress)
	{
		memset32((void*)0x06008000,0,0x8000);	//rest of VRAM
		memset32((void*)0x06010000,0,0x8000);	//sprite VRAM and extra bank vram
	}	
}
#endif

APPEND static u8* allocate_end_variables(int do_reset)
{
	u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const extra_nametables=VRAM+0x07000; //4-screen gets this
	u8 *end_of_cache=end_of_exram;
	
	if (!fourscreen)
	{
		end_of_cache+=2048;
	}
	
	if (vrompages>0)
	{
		int chr_table_size=vrompages*8*4;
		int prg_table_size=rompages*PRG_16*4;
		
		if (mapper==TQROM)
		{
			chr_table_size*=2;
		}
		
		if (has_vram==0)
		{
			//go 8192 ahead since there's no VRAM
			if (!fourscreen)
			{
				end_of_cache+=8192;
			}
		}
		
		//vrom banks, each takes up 4 bytes, 8 of them per 8k
		instant_chr_banks=(u8**)((u8*)end_of_cache-(chr_table_size));
		//rombanks, each takes up 4 bytes, 2 of them per 16k
		instant_prg_banks=(u8**)((u8*)instant_chr_banks-(prg_table_size));
		//don't overlap
		end_of_cache=(u8*)instant_prg_banks;
	}
	else
	{
		int chr_table_size=8*4;
		int prg_table_size=rompages*PRG_16*4;
		//no vrom banks, just use VRAM
		instant_chr_banks=(u8**)((u8*)end_of_cache-chr_table_size);
		//rombanks, each takes up 4 bytes, 2 of them per 16k
		instant_prg_banks=(u8**)((u8*)instant_chr_banks-prg_table_size);
		//don't overlap
		end_of_cache=(u8*)instant_prg_banks;
	}
	
	//setup buffers
	if (do_reset==1)
	{
		stop_dma_interrupts();
		if (!fourscreen)
		{
			//place scanline buffers into ram otherwise occupied by other 2 nametables
			u8 * p= extra_nametables + 0x1000;
			p -= 164*4;
			_dma0buff = (u32*)p;
			p -= 164*2;
			_dma1buff = (u16*)p;
			p -= 164*2;
			_dma3buff = (u16*)p;

			p -= 240*4;
			_scrollbuff = (u32*)p;
			p -= 240*4;
			_dmascrollbuff = (u32*)p;
			
			p -= 240*2;
			_dispcntbuff = (u16*)p;
			_dmadispcntbuff = DISPCNTBUFF2;

			//reset_buffers();
			//880 bytes free in that vram block
		}
		else
		{
			//place scanline buffers into EWRAM, or if there is VRAM available, put them there?
			u8 * p = end_of_cache;
			p -= 164*4;
			_dma0buff = (u32*)p;
			p -= 164*2;
			_dma1buff = (u16*)p;
			p -= 164*2;
			_dma3buff = (u16*)p;

			p -= 240*4;
			_scrollbuff = (u32*)p;
			p -= 240*4;
			_dmascrollbuff = (u32*)p;
			
			p -= 240*2;
			_dispcntbuff = (u16*)p;
			_dmadispcntbuff = DISPCNTBUFF2;

			//reset_buffers();
			
			end_of_cache = p;
		}
	}
	return end_of_cache;
}

#if COMPY

APPEND static void assign_pages_deferred(int comptype, u8 *_rombase, int page_size)
{
	if (vrompages==0)
	{
		DEFERRED_CALL(assign_chr_pages,NES_VRAM,0,8);
	}
	
	//assign pages!
	u8 *_vrombase;
	int prg_entries,chr_entries;
	
	if (cartflags&TRAINER)
	{
		_rombase+=512;
	}
	
	prg_entries=rompages*PRG_16;
	DEFERRED_CALL(assign_prg_pages,_rombase,0,prg_entries);
	
	_vrombase=_rombase+rompages*16384;
	chr_entries=vrompages*8;
	if (vrompages==0)
	{
		_vrombase=NES_VRAM;
		chr_entries=8;
	}
	DEFERRED_CALL(assign_chr_pages,_vrombase,0,chr_entries);
	
	u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
	u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (24k with vrom_bank_2 folllowing it)
	u8 *const vrom_bank_2=VRAM+0x10000; //16k size
	u8 *const novrom_bank=VRAM+0x08000; //48k size

	//TURBO
	if (comptype == 0 || comptype == 1)
	{
		//copy to VRAM for speed
		int pagesToCopy = 2;
		int firstPage = rompages - 2;
		if (rompages == 1)
		{
			pagesToCopy = 1;
			firstPage = 0;
		}
		if (bankable_vrom == 0 || vrompages == 1)
		{
			if (page_size == 32)
			{
				firstPage = 0;
				pagesToCopy = 2;
			}
			DEFERRED_CALL(assign_prg_pages2,novrom_bank, firstPage * PRG_16, pagesToCopy * PRG_16);
			
			if (page_size < 32)
			{
				//copy the first 16k too
				pagesToCopy = 1;
				firstPage = 0;
				//memcpy_if_okay(vrom_bank_2, _rombase + firstPage * 16384, pagesToCopy * 16384);
				DEFERRED_CALL(assign_prg_pages2,vrom_bank_2, firstPage * PRG_16, pagesToCopy * PRG_16);
			}
		}
		else
		{
			pagesToCopy = 1;
			firstPage = rompages - pagesToCopy;
			//memcpy_if_okay(vrom_bank_2, _rombase + firstPage * 16384, pagesToCopy * 16384);
			DEFERRED_CALL(assign_prg_pages2,vrom_bank_2, firstPage*PRG_16,pagesToCopy*PRG_16);
			
			//If page size is 32k, nothing is in GBA VRAM,
			//and NES VROM/VRAM is bankable, then copy 256 bytes before the page
			//so that back branches from C0xx to BFxx work.  Fixes Arkista's Ring
			//if (page_size == 32)
			//{
			//	memcpy_if_okay(vrom_bank_2 - 256, _rombase + firstPage * 16384 - 256, 256);
			//}
		}
	}



	if (comptype==2)
	{
		if (vrompages==0)
		{
			if (page_size != 32)
			{
				//assign page 3
				DEFERRED_CALL(assign_prg_pages2,vrom_bank_2, 3*PRG_16,PRG_16);
				//assign page D
				DEFERRED_CALL(assign_prg_pages2,_rombase+16384*3,13*PRG_16,PRG_16);
				//assign page EF
				DEFERRED_CALL(assign_prg_pages2,novrom_bank,14*PRG_16,2*PRG_16);
			}
			else
			{
				//assign page 3
				DEFERRED_CALL(assign_prg_pages2,vrom_bank_2,3*PRG_16,PRG_16);
				//assign page D
				DEFERRED_CALL(assign_prg_pages2,_rombase+16384*3,13*PRG_16,PRG_16);
				//assign page EF
				DEFERRED_CALL(assign_prg_pages2,_rombase,14*PRG_16,2*PRG_16);
				//assign page 01
				DEFERRED_CALL(assign_prg_pages2,novrom_bank,0,2*PRG_16);
			}
		}
		else
		{
			//assign page E (8k)
			DEFERRED_CALL(assign_chr_pages2,vrom_bank_0,96,8);
			//assign page EF (24k)
			DEFERRED_CALL(assign_chr_pages2,vrom_bank_1,104,24);

			u8 *const a = _rombase + 7 * 16384 + 256;
			//0123456F89ABCD E 7	- bank 7 and F are now swapped
			//reassign 89ABCD
			DEFERRED_CALL(assign_chr_pages2,a + 16384, 0, 6 * 16);
			//assign F
			DEFERRED_CALL(assign_chr_pages2,a, 128 - 16, 16);
			//assign 7
			DEFERRED_CALL(assign_prg_pages2,vrom_bank_2, 7 * PRG_16, 1 * PRG_16);

		}
	}	
}

#else

static void assign_pages(int comptype, u8 *_rombase, int page_size)
{
	if (vrompages==0)
	{
		assign_chr_pages(NES_VRAM,0,8);
	}
	
	//assign pages!
	u8 *_vrombase;
	int prg_entries,chr_entries;
	
	if (cartflags&TRAINER)
	{
		_rombase+=512;
	}
	
	prg_entries=rompages*PRG_16;
	assign_prg_pages(_rombase,0,prg_entries);
	
	_vrombase=_rombase+rompages*16384;
	chr_entries=vrompages*8;
	if (vrompages==0)
	{
		_vrombase=NES_VRAM;
		chr_entries=8;
	}
	assign_chr_pages(_vrombase,0,chr_entries);
	
	u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
	u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (24k with vrom_bank_2 folllowing it)
	u8 *const vrom_bank_2=VRAM+0x10000; //16k size
	u8 *const novrom_bank=VRAM+0x08000; //48k size

	//TURBO
	if (comptype == 0 || comptype == 1)
	{
		//copy to VRAM for speed
		int pagesToCopy = 2;
		int firstPage = rompages - 2;
		if (rompages == 1)
		{
			pagesToCopy = 1;
			firstPage = 0;
		}
		if (bankable_vrom == 0 || vrompages == 1)
		{
			if (page_size == 32)
			{
				firstPage = 0;
				pagesToCopy = 2;
			}
			assign_prg_pages2(novrom_bank, firstPage * PRG_16, pagesToCopy * PRG_16);
			
			if (page_size < 32)
			{
				//copy the first 16k too
				pagesToCopy = 1;
				firstPage = 0;
				//memcpy_if_okay(vrom_bank_2, _rombase + firstPage * 16384, pagesToCopy * 16384);
				assign_prg_pages2(vrom_bank_2, firstPage * PRG_16, pagesToCopy * PRG_16);
			}
		}
		else
		{
			pagesToCopy = 1;
			firstPage = rompages - pagesToCopy;
			//memcpy_if_okay(vrom_bank_2, _rombase + firstPage * 16384, pagesToCopy * 16384);
			assign_prg_pages2(vrom_bank_2, firstPage*PRG_16,pagesToCopy*PRG_16);
			
			//If page size is 32k, nothing is in GBA VRAM,
			//and NES VROM/VRAM is bankable, then copy 256 bytes before the page
			//so that back branches from C0xx to BFxx work.  Fixes Arkista's Ring
			//if (page_size == 32)
			//{
			//	memcpy_if_okay(vrom_bank_2 - 256, _rombase + firstPage * 16384 - 256, 256);
			//}
		}
	}



	if (comptype==2)
	{
		if (vrompages==0)
		{
			if (page_size != 32)
			{
				//assign page 3
				assign_prg_pages2(vrom_bank_2, 3*PRG_16,PRG_16);
				//assign page D
				assign_prg_pages2(_rombase+16384*3,13*PRG_16,PRG_16);
				//assign page EF
				assign_prg_pages2(novrom_bank,14*PRG_16,2*PRG_16);
			}
			else
			{
				//assign page 3
				assign_prg_pages2(vrom_bank_2,3*PRG_16,PRG_16);
				//assign page D
				assign_prg_pages2(_rombase+16384*3,13*PRG_16,PRG_16);
				//assign page EF
				assign_prg_pages2(_rombase,14*PRG_16,2*PRG_16);
				//assign page 01
				assign_prg_pages2(novrom_bank,0,2*PRG_16);
			}
		}
		else
		{
			//assign page E (8k)
			assign_chr_pages2(vrom_bank_0,96,8);
			//assign page EF (24k)
			assign_chr_pages2(vrom_bank_1,104,24);

			u8 *const a = _rombase + 7 * 16384 + 256;
			//0123456F89ABCD E 7	- bank 7 and F are now swapped
			//reassign 89ABCD
			assign_chr_pages2(a + 16384, 0, 6 * 16);
			//assign F
			assign_chr_pages2(a, 128 - 16, 16);
			//assign 7
			assign_prg_pages2(vrom_bank_2, 7 * PRG_16, 1 * PRG_16);

		}
	}	
}

#endif

#if COMPY
APPEND static u8* deferred_decompress_rom(u8 *nes_header, u8 *cachebase, int page_size, int comptype)
{
	u8* compsrc = nes_header + 20;
	u8* compdest = cachebase;
	u8* mem_end = (u8*)0x02040000;
	u8* cache_end_of_rom;
	
	u8 *const VRAM = (u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0 = VRAM + 0x0A000; //8k size
	u8 *const vrom_bank_1 = VRAM + 0x0E000; //8k size  (24k with vrom_bank_2 folllowing it)
	u8 *const vrom_bank_2 = VRAM + 0x10000; //16k size
	u8 *const novrom_bank = VRAM + 0x08000; //48k size

	//FIXME for pogoshell
	u32 filesize= *(u32*)(&nes_header[-16]);
	filesize=((filesize-1)|3)+1;
	filesize-=16;
	
	if (pogoshell) filesize = pogoshell_filesize;
	
	
	u32 prg_pos=*(u32*)(&nes_header[8]) >> 8;
	
	u8 *compsrc2 = NULL;
	u32 romsize1, romsize2;
	if (comptype == 2)
	{
		romsize1 = prg_pos;
		romsize2 = filesize - prg_pos;
		
		compsrc2 = compsrc + romsize1;
	}
	else
	{
		romsize1 = filesize;
		romsize2 = 0;
	}
	int do_vram_copy = 0;
	
	if (nes_header + 32 + filesize >= mem_end)
	{
		do_vram_copy = 2;
	}
	
	//rom_is_compressed=1;
	ewram_owner_is_sram=0;

	//move to the end of EWRAM
	if (compsrc<(u8*)0x08000000)
	{
		u8* compcopy;
		stop_dma_interrupts();
		
		//needresume=true;
		
		u32 copysize = filesize - 4;
		
		if (comptype > 0)
		{
			if (comptype == 2)
			{
				//if (filesize >= 180*1024)
				{
					if (romsize2 >= 32768)
					{
						if (do_vram_copy == 0)
						{
							do_vram_copy = 1;
							u8 *last32k_compressed = compsrc + filesize - 4;
							DEFERRED_CALL(memcpy32,VRAM + 0x0000, last32k_compressed - 0x8000, 0x2000);
							DEFERRED_CALL(memcpy32,VRAM + 0x4000, last32k_compressed - 0x6000, 0x2000);
							DEFERRED_CALL(memcpy32,VRAM + 0x14000, last32k_compressed - 0x4000, 0x4000);
						}
					}
					copysize -= 0x8000;
				}
			}
			compcopy = mem_end - copysize;
			//compcopy = end_of_cache - filesize;
			DEFERRED_CALL(memmove32,compcopy,compsrc,copysize);
			compsrc = compcopy;
			compsrc2 = compsrc + romsize1;
		}
	}
	if (!ROMMENU && comptype == 0 && compsrc<(u8*)0x08000000)
	{
		//memory copy to final location
		compsrc = nes_header + 16;
		DEFERRED_CALL(memmove32,compdest, compsrc, filesize);
		cache_end_of_rom=192*1024+cachebase;
	}
	else if (ROMMENU && comptype == 0 && compsrc>=(u8*)0x08000000)
	{
		compdest = nes_header + 16;
	}
	if (comptype==1)
	{
		//one chunk, 192k or smaller
		DEFERRED_CALL(depack,compsrc,compdest,0);
		cache_end_of_rom=192*1024+cachebase;

//				Two_Words temp;
//				u8 *next_src, *next_dest;
//				
//				//one chunk, 192k or smaller
//				temp=depack_2(compsrc,compdest);
//				next_src=(u8*)(temp.word1);
//				next_dest=(u8*)(temp.word2);
//				
//				
//				cache_end_of_rom=next_dest;
	}
	if (comptype == 0 || comptype == 1)
	{
		//copy to VRAM for speed
		int pagesToCopy = 2;
		int firstPage = rompages - 2;
		if (rompages == 1)
		{
			pagesToCopy = 0;
			firstPage = 0;
		}
		if (bankable_vrom == 0 || vrompages == 1)
		{
			if (page_size == 32)
			{
				firstPage = 0;
				pagesToCopy = 2;
			}
			DEFERRED_CALL(memcpy32,novrom_bank,compdest + firstPage * 16384, pagesToCopy * 16384);
			//assign_prg_pages2(novrom_bank, firstpage * PRG_16, pages_to_copy * PRG_16);
			
			if (page_size < 32)
			{
				//copy the first 16k too
				pagesToCopy = 1;
				firstPage = 0;
				DEFERRED_CALL(memcpy32,vrom_bank_2, compdest + firstPage * 16384, pagesToCopy * 16384);
				//assign_prg_pages2(vrom_bank_2, firstPage * PRG_16, pagesToCopy * PRG_16);
			}
		}
		else
		{
			pagesToCopy = 1;
			firstPage = rompages - pagesToCopy;
			DEFERRED_CALL(memcpy32,vrom_bank_2, compdest + firstPage * 16384, pagesToCopy * 16384);
			//assign_prg_pages2(vrom_bank_2, firstpage*PRG_16,pages_to_copy*PRG_16);
			
			//If page size is 32k, nothing is in GBA VRAM,
			//and NES VROM/VRAM is bankable, then copy 256 bytes before the page
			//so that back branches from C0xx to BFxx work.  Fixes Arkista's Ring
			if (page_size == 32)
			{
				DEFERRED_CALL(memcpy32,vrom_bank_2 - 256, compdest + firstPage * 16384 - 256, 256);
			}
		}
	}
	if (comptype==2)
	{
		u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
		u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
		u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (really 24k)
		u8 *const vrom_bank_2=VRAM+0x10000; //16k size
	//	u8 *const vrom_bank_3=VRAM+0x12000; //continued from previous
		u8 *const novrom_bank=VRAM+0x08000; //48k size


//				sprite_vram_in_use=1;
		//256k size!

		//Do Last 128k
		DEFERRED_CALL(depack,compsrc,compdest,0);
		
		compdest+=128*1024;
		if (vrompages==0)
		{
			//first, after calling depack on last 128k, we have this:
			//89ABCDEF
			//89ABC EF D			- copy banks EF and D into VRAM
			
			//If using VRAM COPY mode:
			//89A[END32K]...[COMP] EF D <BC>
			//relocate END32K to the end of EWRAM immediately after COMP
			//89A...[COMP][END32K] EF D <BC>
			//89A01234567... EF D <BC>  //decompress to 3rd slot
			//0123456789A... EF D <BC>  //call "swapmem" to rearrange banks
			//0123456789ABC  EF D //copy banks BC to EWRAM
			
			//Otherwise (not using VRAM COPY mode)
			//89ABC01234567 EF D	- decompress banks 01234567 into 5th slot
			//0123456789ABC EF D	- call "swapmem" to rearrange the banks
			
			//finally:
			//012D456789ABC EF 3	- swap bank 3 with D?  (why did we do this again?  I forgot?  Did it fix a game?)

			//for 32k bankswitching games: (01 bank in VRAM for speed)
			//EF2D456789ABC 01 3	- move banks 01 into VRAM for speed!

			compdest-=32*1024;
			//last 32k goes to VRAM
			DEFERRED_CALL(memcpy32,novrom_bank,compdest,32*1024);
			compdest-=16*1024;
			//now last 16k goes to VRAM, but that will change, we'll be putting C000-10000 into vram eventually
			DEFERRED_CALL(memcpy32,vrom_bank_2,compdest,16*1024);
			
			if (do_vram_copy)
			{
				//Swap bank BC with END32K
				DEFERRED_CALL(simpleswap32,VRAM + 0x0000, cachebase + 3 * 16384, 0x2000);
				DEFERRED_CALL(simpleswap32,VRAM + 0x4000, cachebase + 3 * 16384 + 0x2000, 0x2000);
				DEFERRED_CALL(simpleswap32,VRAM + 0x14000, cachebase + 3 * 16384 + 0x4000, 0x4000);
				
				int compsrc2_bytepos = (u32)compsrc2 & 0xFF;
				u8 *compsrc2_aligned = (u8*)((u32)(compsrc2) & ~0xFF);
				u8 *mem_end = (u8*)0x02040000;
				u8 *compsrc2_new_aligned = compsrc2_aligned - 0x8000;
				u8 *compsrc2_new = compsrc2_new_aligned + compsrc2_bytepos;
				
				//rearrange END32K to come after the compressed rom
				DEFERRED_CALL(memmove32,compsrc2_new_aligned, cachebase + 3 * 16384, 0x8000);
				DEFERRED_CALL(swapmem,compsrc2_aligned, compsrc2_new_aligned, mem_end - compsrc2_aligned);
				
				
				
				compsrc = compsrc2_new;
				compdest = cachebase + 16384 * 3;
				
				//Decompress after 89A
				DEFERRED_CALL(depack, compsrc, compdest,0);
				
				//Rearrange banks 01234567 to come before 89A
				DEFERRED_CALL(swapmem,cachebase + 16384 * 3, cachebase, 8 * 16384);
				
				//move bank BC back out of VRAM
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 11, VRAM + 0x0000, 0x2000);
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 11 + 0x2000, VRAM + 0x4000, 0x2000);
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 11 + 0x4000, VRAM + 0x14000, 0x4000);
				
				//Clear VRAM
				DEFERRED_CALL(memset32,VRAM + 0x0000, 0, 0x2000);
				DEFERRED_CALL(memset32,VRAM + 0x4000, 0, 0x2000);
				DEFERRED_CALL(memset32,VRAM + 0x14000, 0, 0x4000);
			}
			else
			{
				compsrc+=prg_pos;
				DEFERRED_CALL(depack,compsrc,compdest,0);

				//now rearrange first 5 with last 8
				DEFERRED_CALL(swapmem,cachebase+16384*5,cachebase, 8*16384);
			}

			//finaly swap page 3 and page D  (forgot why we did this)
			DEFERRED_CALL(simpleswap32,cachebase+3*16384,vrom_bank_2, 16384);

			cache_end_of_rom = 13*16384+cachebase;	//okay to do because 208K > 192K

			//is this a 32k switching game?  Swap page 0,1 with pages E,F
			if (page_size==32)
			{
				DEFERRED_CALL(simpleswap32,novrom_bank, cachebase, 32768);
			}
		}
		else
		{
			//first, after calling depack on last 128k, we have this:
			//89ABCDEF
			//89ABCD E F			- copy banks E and F into VRAM

			//If using VRAM COPY mode:
			//89AB[END32K]...[COMP] E F <CD>
			//relocate END32K to the end of EWRAM immediately after COMP
			//89AB...[COMP][END32K] E F <CD>
			//89AB01234567... EF <C>  //decompress to 4th slot
			//0123456789AB... EF <CD>  //call "swapmem" to rearrange banks
			//0123456789ABCD  EF //copy banks CD to EWRAM
			//otherwise:
			//89ABCD01234567 E F	- extract banks 01234567 into 6th slot
			//0123456789ABCD E F	- call "swapmem" to rearrange the banks
			
			//finally, for speed:
			//0123456F89ABCD E 7	- swap bank 7 and F
			
			compdest-=24*1024;
			DEFERRED_CALL(memcpy32,vrom_bank_1,compdest,24*1024);
			compdest-=8*1024;
			DEFERRED_CALL(memcpy32,vrom_bank_0,compdest,8*1024);
			
			if (do_vram_copy)
			{
				//Swap bank CD with END32K
				DEFERRED_CALL(simpleswap32,VRAM + 0x0000, cachebase + 4 * 16384, 0x2000);
				DEFERRED_CALL(simpleswap32,VRAM + 0x4000, cachebase + 4 * 16384 + 0x2000, 0x2000);
				DEFERRED_CALL(simpleswap32,VRAM + 0x14000, cachebase + 4 * 16384 + 0x4000, 0x4000);
				
				int compsrc2_bytepos = (u32)compsrc2 & 0xFF;
				u8 *compsrc2_aligned = (u8*)((u32)(compsrc2) & ~0xFF);
				u8 *mem_end = (u8*)0x02040000;
				u8 *compsrc2_new_aligned = compsrc2_aligned - 0x8000;
				u8 *compsrc2_new = compsrc2_new_aligned + compsrc2_bytepos;
				
				//rearrange END32K to come after the compressed rom
				DEFERRED_CALL(memmove32,compsrc2_new_aligned, cachebase + 4 * 16384, 0x8000);
				DEFERRED_CALL(swapmem,compsrc2_aligned, compsrc2_new_aligned, mem_end - compsrc2_aligned);
				
				compsrc = compsrc2_new;
				
				compdest = cachebase + 16384 * 4;
				
				//Decompress after 89AB
				DEFERRED_CALL(depack,compsrc, compdest,0);
				
				//Rearrange banks 01234567 to come before 89AB
				DEFERRED_CALL(swapmem,cachebase + 16384 * 4, cachebase, 8 * 16384);
				
				//move bank CD back out of VRAM
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 12, VRAM + 0x0000, 0x2000);
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 12 + 0x2000, VRAM + 0x4000, 0x2000);
				DEFERRED_CALL(memcpy32,cachebase + 16384 * 12 + 0x4000, VRAM + 0x14000, 0x4000);
				
				//Clear VRAM
				DEFERRED_CALL(memset32,VRAM + 0x0000, 0, 0x2000);
				DEFERRED_CALL(memset32,VRAM + 0x4000, 0, 0x2000);
				DEFERRED_CALL(memset32,VRAM + 0x14000, 0, 0x4000);
			}
			else
			{
				compsrc+=prg_pos;
				DEFERRED_CALL(depack,compsrc,compdest,0);

				//now rearrange first 6 with last 8
				DEFERRED_CALL(swapmem,cachebase+16384*6,cachebase, 8*16384);
			}

			//compsrc+=prg_pos;
			//depack(compsrc,compdest);

			//now rearrange first 6 with last 8
			//swapmem( cachebase + 16384 * 6, cachebase, 8 * 16384);
			
			//place bank 7 into VRAM for speed  (swap with F)
			DEFERRED_CALL(simpleswap32,cachebase + 16384 * 7, vrom_bank_2, 16384);
			
			//advance bank 6 and above by 256 bytes
			const int amountToAdvance = 256;
			u8 *memoryToMove = cachebase + 16384 * 7;
			u8 *moveDest = memoryToMove + amountToAdvance;
			u8 *memoryToCopy = vrom_bank_2;
			
			//extern void breakpoint(void);
			//DEFERRED_CALL(breakpoint,0,0,0);

			DEFERRED_CALL(memmove32,moveDest, memoryToMove, 7 * 16384);
			DEFERRED_CALL(memcpy32, memoryToMove, vrom_bank_2, amountToAdvance);
			
			cache_end_of_rom = 14 * 16384 + cachebase + 256;	//okay because 224K > 192K
		}
	}
	if (compsrc<(u8*)0x08000000)
	{
		//cleanup
		DEFERRED_CALL(reset_buffers,0,0,0);
		DEFERRED_CALL(cls,0x0F,0,0);
		assign_pages_deferred(comptype, cachebase, page_size);
		//init_cache(nes_header, 0);
		//return;
	}
	return cache_end_of_rom;
	
}

#else

static u8* decompress_rom(u8 *nes_header, u8 *cachebase, int page_size, int comptype)
{
	u8* compsrc = nes_header + 20;
	u8* compdest = cachebase;
	u8* mem_end = (u8*)0x02040000;
	u8* cache_end_of_rom;

	u8 *const VRAM = (u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0 = VRAM + 0x0A000; //8k size
	u8 *const vrom_bank_1 = VRAM + 0x0E000; //8k size  (24k with vrom_bank_2 folllowing it)
	u8 *const vrom_bank_2 = VRAM + 0x10000; //16k size
	u8 *const novrom_bank = VRAM + 0x08000; //48k size

	//FIXME for pogoshell
	u32 filesize= *(u32*)(&nes_header[-16]);
	filesize=((filesize-1)|3)+1;
	filesize-=16;
	
	if (pogoshell) filesize = pogoshell_filesize;
	
	
	u32 prg_pos=*(u32*)(&nes_header[8]) >> 8;
	
	//rom_is_compressed=1;
	ewram_owner_is_sram=0;

	//move to the end of EWRAM
	if (compsrc<(u8*)0x08000000)
	{
		u8* compcopy;
		stop_dma_interrupts();
		
#if COMPY
	{
		//Temporary hack: Restore saved EWRAM
		memcpy32((u8*)0x2040000 - 0x8000, (u8*)0x06000000, 0x2000);
		memcpy32((u8*)0x2040000 - 0x6000, (u8*)0x06004000, 0x2000);
		memcpy32((u8*)0x2040000 - 0x4000, (u8*)0x06014000, 0x4000);
	}
#endif
		
		//needresume=true;
		
		if (comptype > 0)
		{
			compcopy = mem_end - filesize;
			//compcopy = end_of_cache - filesize;
			memmove32(compcopy,compsrc,filesize);
			compsrc=compcopy;
		}
	}
	if (!ROMMENU && comptype == 0 && compsrc<(u8*)0x08000000)
	{
		//memory copy to final location
		compsrc = nes_header + 16;
		memmove32(compdest, compsrc, filesize);
		cache_end_of_rom=192*1024+cachebase;
	}
	else if (ROMMENU && comptype == 0 && compsrc>=(u8*)0x08000000)
	{
		compdest = nes_header + 16;
	}
	if (comptype==1)
	{
		//one chunk, 192k or smaller
		depack(compsrc,compdest);
		cache_end_of_rom=192*1024+cachebase;

//				Two_Words temp;
//				u8 *next_src, *next_dest;
//				
//				//one chunk, 192k or smaller
//				temp=depack_2(compsrc,compdest);
//				next_src=(u8*)(temp.word1);
//				next_dest=(u8*)(temp.word2);
//				
//				
//				cache_end_of_rom=next_dest;
	}
	if (comptype == 0 || comptype == 1)
	{
		//copy to VRAM for speed
		int pagesToCopy = 2;
		int firstPage = rompages - 2;
		if (rompages == 1)
		{
			pagesToCopy = 0;
			firstPage = 0;
		}
		if (bankable_vrom == 0 || vrompages == 1)
		{
			if (page_size == 32)
			{
				firstPage = 0;
				pagesToCopy = 2;
			}
			memcpy32(novrom_bank,compdest + firstPage * 16384, pagesToCopy * 16384);
			//assign_prg_pages2(novrom_bank, firstpage * PRG_16, pages_to_copy * PRG_16);
			
			if (page_size < 32)
			{
				//copy the first 16k too
				pagesToCopy = 1;
				firstPage = 0;
				memcpy32(vrom_bank_2, compdest + firstPage * 16384, pagesToCopy * 16384);
				//assign_prg_pages2(vrom_bank_2, firstPage * PRG_16, pagesToCopy * PRG_16);
			}
		}
		else
		{
			pagesToCopy = 1;
			firstPage = rompages - pagesToCopy;
			memcpy32(vrom_bank_2, compdest + firstPage * 16384, pagesToCopy * 16384);
			//assign_prg_pages2(vrom_bank_2, firstpage*PRG_16,pages_to_copy*PRG_16);
			
			//If page size is 32k, nothing is in GBA VRAM,
			//and NES VROM/VRAM is bankable, then copy 256 bytes before the page
			//so that back branches from C0xx to BFxx work.  Fixes Arkista's Ring
			if (page_size == 32)
			{
				memcpy32(vrom_bank_2 - 256, compdest + firstPage * 16384 - 256, 256);
			}
		}
	}
	if (comptype==2)
	{
		u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
		u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
		u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (really 24k)
		u8 *const vrom_bank_2=VRAM+0x10000; //16k size
	//	u8 *const vrom_bank_3=VRAM+0x12000; //continued from previous
		u8 *const novrom_bank=VRAM+0x08000; //48k size


//				sprite_vram_in_use=1;
		//256k size!

		//Do Last 128k
		depack(compsrc,compdest);

		compdest+=128*1024;
		if (vrompages==0)
		{
			//first, after calling depack on last 128k, we have this:
			//89ABCDEF
			//89ABC EF D			- copy banks EF and D into VRAM
			//then:
			//89ABC01234567 EF D	- decompress banks 01234567 into 5th slot
			//then:
			//0123456789ABC EF D	- call "swapmem" to rearrange the banks
			//finally:
			//012D456789ABC EF 3	- swap bank 3 with D?  (why did we do this again?  I forgot?  Did it fix a game?)

			//for 32k bankswitching games: (01 bank in VRAM for speed)
			//EF2D456789ABC 01 3	- move banks 01 into VRAM for speed!

			compdest-=32*1024;
			//last 32k goes to VRAM
			memcpy32(novrom_bank,compdest,32*1024);
			compdest-=16*1024;
			//now last 16k goes to VRAM, but that will change, we'll be putting C000-10000 into vram eventually
			memcpy32(vrom_bank_2,compdest,16*1024);

			compsrc+=prg_pos;
			depack(compsrc,compdest);

			//now rearrange first 5 with last 8
			swapmem(cachebase+16384*5,cachebase, 8*16384);

			//finaly swap page 3 and page D  (forgot why we did this)
			simpleswap32(cachebase+3*16384,vrom_bank_2, 16384);

			cache_end_of_rom = 13*16384+cachebase;	//okay to do because 208K > 192K

			//is this a 32k switching game?  Swap page 0,1 with pages E,F
			if (page_size==32)
			{
				simpleswap32(novrom_bank, cachebase, 32768);
			}
		}
		else
		{
			//first, after calling depack on last 128k, we have this:
			//89ABCDEF
			//89ABCD E F			- copy banks E and F into VRAM
			//then:
			//89ABCD01234567 E F	- extract banks 01234567 into 6th slot
			//0123456789ABCD E F	- call "swapmem" to rearrange the banks
			//finally, for speed:
			//0123456F89ABCD E 7	- swap bank 7 and F
			
			compdest-=24*1024;
			memcpy32(vrom_bank_1,compdest,24*1024);
			compdest-=8*1024;
			memcpy32(vrom_bank_0,compdest,8*1024);

			compsrc+=prg_pos;
			depack(compsrc,compdest);

			//now rearrange first 6 with last 8
			swapmem( cachebase + 16384 * 6, cachebase, 8 * 16384);
			//place bank 7 into VRAM for speed  (swap with F)
			simpleswap32(cachebase + 16384 * 7, vrom_bank_2, 16384);
			
			//advance bank 6 and above by 256 bytes
			const int amountToAdvance = 256;
			u8 *memoryToMove = cachebase + 16384 * 7;
			u8 *moveDest = memoryToMove + amountToAdvance;
			u8 *memoryToCopy = vrom_bank_2;
			memmove32(moveDest, memoryToMove, 7 * 16384);
			memcpy32(memoryToMove, vrom_bank_2, amountToAdvance);

			cache_end_of_rom = 14 * 16384 + cachebase + 256;	//okay because 224K > 192K
		}
	}
	if (compsrc<(u8*)0x08000000)
	{
		//cleanup
		reset_buffers();
		cls(0x0F);
		assign_pages(comptype, cachebase, page_size);
		//init_cache(nes_header, 0);
		//return;
	}
	return cache_end_of_rom;
	
}

#endif

#if COMPY
APPEND static void init_cache_deferred(u8* nes_header)
{
	const int do_reset = 1;
	
	u32 comp_sig;
	u32 prg_pos;
	int comptype;  //0 = uncompressed, 1 = one chunk up to 192k, 2 = two 128k chunks, first chr
	int doNotDecompress = do_not_decompress;
	
	//Declare the possible holes in VRAM to stick memory into
	u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
	u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (really 24k)
	u8 *const vrom_bank_2=VRAM+0x10000; //16k size
//	u8 *const vrom_bank_3=VRAM+0x12000; //continued from previous
	u8 *const novrom_bank=VRAM+0x08000; //48k size
	
	u8 *const extra_nametables=VRAM+0x07000; //4-screen gets this
	
	u8* dest;
	u8* cachebase;
	u8* cache_end_of_rom;
	
	int i;
	
	//u8 *end_of_cache=end_of_exram;
	
	int page_size;
	
	bool dont_use_turbo=false;
	
//	sprite_vram_in_use=0;
	
	dest=ewram_start;
	u32 destValue = (u32)dest;
	if ((u32)nes_header < 0x08000000)
	{
		destValue += 64;
	}
	destValue += 255;
	destValue &= 0xFFFFFF00;
	dest = (void*)destValue;
	
	page_size=get_prg_bank_size(mapper);
	
	comp_sig=*(u32*)(&nes_header[12]);
	prg_pos=*(u32*)(&nes_header[8]);
	prg_pos>>=8;
	comptype=0;
	if (comp_sig==0x33335041) comptype=1;
	if (comptype==1 && prg_pos!=0) comptype=2;
	
	if (do_reset!=0)
	{
		stop_dma_interrupts();
	}
	
	
	cachebase = dest;
	cache_end_of_rom=cachebase+192*1024;
	
	u8 *end_of_cache = allocate_end_variables(do_reset);

	//do we have enough memory to store the ROM?
	if (comptype != 0)
	{
		int totalSize = rompages * 16384 + vrompages * 8192;
		int extraSize = 32768;
		if (vrompages == 0 && !bankable_vrom)
		{
			extraSize = 49152;
		}
		extern u8 __eheap_start[];
		extraSize += 256;
		int maxSize = (end_of_cache - __eheap_start) + extraSize;
		if (totalSize > maxSize)
		{
			//refuse to decompress the ROM
			doNotDecompress = 1;
			breakpoint();
		}
	}
	
	//DECOMPRESS ROM
	cache_end_of_rom = deferred_decompress_rom(nes_header, cachebase, page_size, comptype);
	
	#if MIXED_VRAM_VROM
	if (mapper==74)
	{
		if (rompages==32)
		{
			DEFERRED_CALL(assign_chr_pages2, NES_VRAM, 8, 2);
			vram_page_base=8;
		}
		else
		{
			DEFERRED_CALL(assign_chr_pages2, NES_VRAM, 0, 2);
			vram_page_base=0;
		}
		bankable_vrom=1;
		has_vram=1;
		vram_page_mask=0xFF;
	}
	if (mapper==TQROM) //tqrom
	{
		for (i = 64; i < 128; i += 8)
		{
			DEFERRED_CALL(assign_chr_pages2, NES_VRAM, i, 8);
		}
		bankable_vrom=1;
		has_vram=1;
		vram_page_mask=0xFF;
		vram_page_base=64;
	}
	if (mapper==VRC7) //VRC7
	{
		bankable_vrom=1;
	}
	#endif
	
	//finally get checksum
	u32 my_checksum = *(u32*)(nes_header + 16);

#if USE_GAME_SPECIFIC_HACKS
	//GAME SPECIFIC HACKS!
	{
		switch (my_checksum)
		{
			case 0x612279F7: //Magic of Scheherazade
			{
				//swap bank 3 and bank 6
				u8 *bank3 = cachebase + 3 * 16384;
				u8 *bank6 = cachebase + 6 * 16384;
				
				DEFERRED_CALL(simpleswap32, bank3, bank6, 16384);
				DEFERRED_CALL(assign_prg_pages2, bank3, 6 * PRG_16, PRG_16);
				DEFERRED_CALL(assign_prg_pages2, bank6, 3 * PRG_16, PRG_16);
			}
			break;
		}
	}
#endif
	
	
//	flushcache();
//	preloadcache();
//	usingcache=1;
//	usingcompcache=0;
	//finally: the cheat finder...
	
#if CHEATFINDER
	if (do_reset!=0)
	{
		DEFERRED_CALL(setup_cheatfinder,cache_end_of_rom,1,0);	//remove cheatfinder if it doesn't fit
	}
#endif
	
	//fix VS games
	if (do_reset)
	{
		DEFERRED_CALL(paletteinit,0,0,0);
	}
	
	//make sure DIPSCNT buffers aren't zeroed
	if (do_reset == 0)
	{
		DEFERRED_CALL(reset_buffers,0,0,0);
	}
	
	//resume interrupt system
	DEFERRED_CALL(resume_interrupts,0,0,0);
}

#else

void init_cache(u8* nes_header, int do_reset)
{
	//do_reset:
	// 0 = called from redecompress and we don't want to reset any memory, but want to perform decompression/copies
	// 1 = called from Loadcart and we want to reset everything

#if COMPY
	do_reset = 1;
#endif
	
	u32 comp_sig;
	u32 prg_pos;
	int comptype;  //0 = uncompressed, 1 = one chunk up to 192k, 2 = two 128k chunks, first chr
	int doNotDecompress = do_not_decompress;
	
	//Declare the possible holes in VRAM to stick memory into
	u8 *const VRAM=(u8*)0x06000000; //VRAM macro is defined in Libgba, so we #undef-ed it to use it here
	u8 *const vrom_bank_0=VRAM+0x0A000; //8k size
	u8 *const vrom_bank_1=VRAM+0x0E000; //8k size  (really 24k)
	u8 *const vrom_bank_2=VRAM+0x10000; //16k size
//	u8 *const vrom_bank_3=VRAM+0x12000; //continued from previous
	u8 *const novrom_bank=VRAM+0x08000; //48k size
	
	u8 *const extra_nametables=VRAM+0x07000; //4-screen gets this
	
	u8* dest;
	u8* cachebase;
	u8* cache_end_of_rom;
	
	int i;
	
	//u8 *end_of_cache=end_of_exram;
	
	int page_size;
	
	bool dont_use_turbo=false;
	
//	sprite_vram_in_use=0;
	
	dest=ewram_start;
	u32 destValue = (u32)dest;
	if ((u32)nes_header < 0x08000000)
	{
		destValue += 64;
	}
	destValue += 255;
	destValue &= 0xFFFFFF00;
	dest = (void*)destValue;
	
	page_size=get_prg_bank_size(mapper);
	
	comp_sig=*(u32*)(&nes_header[12]);
	prg_pos=*(u32*)(&nes_header[8]);
	prg_pos>>=8;
	comptype=0;
	if (comp_sig==0x33335041) comptype=1;
	if (comptype==1 && prg_pos!=0) comptype=2;
	
	//if (nes_header<(u8*)0x08000000 && comptype != 0)
	//{
	//	//Do not try to decompress twice if this was run from Multiboot
	//	static int hasrun=0;
	//	if (hasrun)
	//	{
	//		//return;
	//		doNotDecompress = 1;
	//	}
	//	hasrun=1;
	//}

	if (do_reset!=0)
	{
		stop_dma_interrupts();
		#if !COMPY
		init_cache_clear_vram();
		#endif
	}
	
	//reset VRAM, VRAM4 if needed
	if (do_reset!=0)
	{
		if (has_vram)
		{
			memset32(NES_VRAM,0,8192);
		}
		if (fourscreen==true)
		{
			memset32(NES_VRAM4,0,2048);
		}
	}
	
	cachebase = dest;
	cache_end_of_rom=cachebase+192*1024;
	
	u8 *end_of_cache = allocate_end_variables(do_reset);
	/*
	if (!fourscreen)
	{
		end_of_cache+=2048;
	}
	
	if (vrompages>0)
	{
		int chr_table_size=vrompages*8*4;
		int prg_table_size=rompages*PRG_16*4;
		
		if (mapper==TQROM)
		{
			chr_table_size*=2;
		}
		
		if (has_vram==0)
		{
			//go 8192 ahead since there's no VRAM
			if (!fourscreen)
			{
				end_of_cache+=8192;
			}
		}
		
		//vrom banks, each takes up 4 bytes, 8 of them per 8k
		instant_chr_banks=(u8**)((u8*)end_of_cache-(chr_table_size));
		//rombanks, each takes up 4 bytes, 2 of them per 16k
		instant_prg_banks=(u8**)((u8*)instant_chr_banks-(prg_table_size));
		//don't overlap
		end_of_cache=(u8*)instant_prg_banks;
	}
	else
	{
		int chr_table_size=8*4;
		int prg_table_size=rompages*PRG_16*4;
		//no vrom banks, just use VRAM
		instant_chr_banks=(u8**)((u8*)end_of_cache-chr_table_size);
		//rombanks, each takes up 4 bytes, 2 of them per 16k
		instant_prg_banks=(u8**)((u8*)instant_chr_banks-prg_table_size);
		//don't overlap
		end_of_cache=(u8*)instant_prg_banks;
	}
	*/
	
	//do we have enough memory to store the ROM?
	if (comptype != 0)
	{
		int totalSize = rompages * 16384 + vrompages * 8192;
		int extraSize = 32768;
		if (vrompages == 0 && !bankable_vrom)
		{
			extraSize = 49152;
		}
		extern u8 __eheap_start[];
		extraSize += 256;
		int maxSize = (end_of_cache - __eheap_start) + extraSize;
		if (totalSize > maxSize)
		{
			//refuse to decompress the ROM
			doNotDecompress = 1;
			breakpoint();
		}
	}
	
	/*
	//setup buffers
	if (do_reset==1)
	{
		if (!fourscreen)
		{
			//place scanline buffers into ram otherwise occupied by other 2 nametables
			u8 * p= extra_nametables + 0x1000;
			p -= 164*4;
			_dma0buff = (u32*)p;
			p -= 164*2;
			_dma1buff = (u16*)p;
			p -= 164*2;
			_dma3buff = (u16*)p;

			p -= 240*4;
			_scrollbuff = (u32*)p;
			p -= 240*4;
			_dmascrollbuff = (u32*)p;
			
			p -= 240*2;
			_dispcntbuff = (u16*)p;
			_dmadispcntbuff = DISPCNTBUFF2;

			reset_buffers();
			//880 bytes free in that vram block
		}
		else
		{
			//place scanline buffers into EWRAM, or if there is VRAM available, put them there?
			u8 * p = end_of_cache;
			p -= 164*4;
			_dma0buff = (u32*)p;
			p -= 164*2;
			_dma1buff = (u16*)p;
			p -= 164*2;
			_dma3buff = (u16*)p;

			p -= 240*4;
			_scrollbuff = (u32*)p;
			p -= 240*4;
			_dmascrollbuff = (u32*)p;
			
			p -= 240*2;
			_dispcntbuff = (u16*)p;
			_dmadispcntbuff = DISPCNTBUFF2;

			reset_buffers();
			
			end_of_cache = p;
		}
	}
	*/
	
	//assign pages!
	{
		u8 *_rombase;
		
		if (comptype==0)
		{
			_rombase=nes_header+16;
		}
		else
		{
			//rom_is_compressed=1;
			_rombase=cachebase;
		}
		assign_pages(comptype, _rombase, page_size);
	}
	/*
	if (vrompages==0)
	{
		assign_chr_pages(NES_VRAM,0,8);
	}*/
	
	
	//assign pages!
	{
		/*
		u8 *_rombase, *_vrombase;
		int prg_entries,chr_entries;
		
		if (comptype==0)
		{
			_rombase=nes_header+16;
		}
		else
		{
			//rom_is_compressed=1;
//			ewram_owner_is_sram=1;
			_rombase=cachebase;
		}
		if (cartflags&TRAINER)
		{
			_rombase+=512;
		}

		prg_entries=rompages*PRG_16;
		assign_prg_pages(_rombase,0,prg_entries);

		_vrombase=_rombase+rompages*16384;
		chr_entries=vrompages*8;
		if (vrompages==0)
		{
			_vrombase=NES_VRAM;
			chr_entries=8;
		}
		assign_chr_pages(_vrombase,0,chr_entries);
		
		if (comptype==2)
		{
			if (vrompages==0)
			{
				if (page_size != 32)
				{
					//assign page 3
					assign_prg_pages2(vrom_bank_2, 3*PRG_16,PRG_16);
					//assign page D
					assign_prg_pages2(_rombase+16384*3,13*PRG_16,PRG_16);
					//assign page EF
					assign_prg_pages2(novrom_bank,14*PRG_16,2*PRG_16);
				}
				else
				{
					//assign page 3
					assign_prg_pages2(vrom_bank_2,3*PRG_16,PRG_16);
					//assign page D
					assign_prg_pages2(_rombase+16384*3,13*PRG_16,PRG_16);
					//assign page EF
					assign_prg_pages2(cachebase,14*PRG_16,2*PRG_16);
					//assign page 01
					assign_prg_pages2(novrom_bank,0,2*PRG_16);
				}
				
			}
			else
			{
				//assign page E (8k)
				assign_chr_pages2(vrom_bank_0,96,8);
				//assign page EF (24k)
				assign_chr_pages2(vrom_bank_1,104,24);
			}
		}
		*/

		if (!doNotDecompress)
		{
			cache_end_of_rom = decompress_rom(nes_header, cachebase, page_size, comptype);
		}
		/*
		{
			u8* compsrc=nes_header+20;
			u8* compdest=cachebase;
			u8* mem_end=(u8*)0x02040000;

			u32 filesize= *(u32*)(&nes_header[-16]);
			filesize=((filesize-1)|3)+1;
			filesize-=16;

			//rom_is_compressed=1;
			ewram_owner_is_sram=0;

			//move to the end of EWRAM
			if (compsrc<(u8*)0x08000000)
			{
				u8* compcopy;
				stop_dma_interrupts();
				//needresume=true;
				
				compcopy = mem_end - filesize;
				//compcopy = end_of_cache - filesize;
				memmove32(compcopy,compsrc,filesize);
				compsrc=compcopy;
			}

			if (comptype==1)
			{
				//one chunk, 192k or smaller
				depack(compsrc,compdest);
				cache_end_of_rom=192*1024+cachebase;

//				Two_Words temp;
//				u8 *next_src, *next_dest;
//				
//				//one chunk, 192k or smaller
//				temp=depack_2(compsrc,compdest);
//				next_src=(u8*)(temp.word1);
//				next_dest=(u8*)(temp.word2);
//				
//				
//				cache_end_of_rom=next_dest;
			}
			if (comptype==2)
			{
//				sprite_vram_in_use=1;
				//256k size!

				//Do Last 128k
				depack(compsrc,compdest);

				compdest+=128*1024;
				if (vrompages==0)
				{
					//first, after calling depack on last 128k, we have this:
					//89ABCDEF
					//89ABC EF D			- copy banks EF and D into VRAM
					//then:
					//89ABC01234567 EF D	- decompress banks 01234567 into 5th slot
					//then:
					//0123456789ABC EF D	- call "swapmem" to rearrange the banks
					//finally:
					//012D456789ABC EF 3	- swap bank 3 with D?  (why did we do this again?  I forgot?  Did it fix a game?)
					
					//for 32k bankswitching games: (01 bank in VRAM for speed)
					//EF2D456789ABC 01 3	- move banks 01 into VRAM for speed!
					
					compdest-=32*1024;
					//last 32k goes to VRAM
					memcpy32(novrom_bank,compdest,32*1024);
					compdest-=16*1024;
					//now last 16k goes to VRAM, but that will change, we'll be putting C000-10000 into vram eventually
					memcpy32(vrom_bank_2,compdest,16*1024);

					compsrc+=prg_pos;
					depack(compsrc,compdest);

					//now rearrange first 5 with last 8
					swapmem(cachebase+16384*5,cachebase, 8*16384);

					//finaly swap page 3 and page D  (forgot why we did this)
					simpleswap32(cachebase+3*16384,vrom_bank_2, 16384);
					
					cache_end_of_rom = 13*16384+cachebase;	//okay to do because 208K > 192K
					
					//is this a 32k switching game?  Swap page 0,1 with pages E,F
					if (page_size==32)
					{
						simpleswap32(novrom_bank, cachebase, 32768);
					}
				}
				else
				{
					//first, after calling depack on last 128k, we have this:
					//89ABCDEF
					//89ABCD E F			- copy banks E and F into VRAM
					//then:
					//89ABCD01234567 E F	- extract banks 01234567 into 6th slot
					//finally:
					//0123456789ABCD E F	- call "swapmem" to rearrange the banks

					compdest-=24*1024;
					memcpy32(vrom_bank_1,compdest,24*1024);
					compdest-=8*1024;
					memcpy32(vrom_bank_0,compdest,8*1024);

					compsrc+=prg_pos;
					depack(compsrc,compdest);

					//now rearrange first 6 with last 8
					swapmem( cachebase+16384*6, cachebase, 8*16384);

					cache_end_of_rom = 14*16384+cachebase;	//okay because 224K > 192K
				}
			}
			if (compsrc<(u8*)0x08000000)
			{
				//cleanup
				reset_buffers();
				cls(4);
				init_cache(nes_header, 0);
				return;
			}
		}
		*/

		/*
		#if USE_ACCELERATION

		//Increase performance by storing the most used ROM page in VRAM!
		if (!dont_use_turbo)
		{
			//for the hell of it... copy first 128k to EWRAM
			if (comptype==0)
			{
				int pages_to_copy=rompages;
				if (rompages > 8 && rompages<=16 && page_size==32)
				{
					pages_to_copy=8;
				}
				if (pages_to_copy<=8)
				{
					int firstpage=0;
					
					memcpy_if_okay(cachebase,_rombase+firstpage*16384,pages_to_copy*16384);
					assign_prg_pages2(cachebase,firstpage*PRG_16,pages_to_copy*PRG_16);
					
//					cachebase+=128*1024;
				}
				if (!doNotDecompress)
				{
					ewram_owner_is_sram=0;
				}
				
			}
			
			if (comptype==0 || comptype==1)
			{
				int pages_to_copy=2;
				int firstpage=rompages-2;
				
				if (rompages==1)
				{
					pages_to_copy=1;
					firstpage=0;
				}
				
				if (bankable_vrom==0 || vrompages==1)
				{
					if (page_size==32)
					{
						firstpage=0;
						pages_to_copy=2;
					}
					
					
					memcpy_if_okay(novrom_bank,_rombase+firstpage*16384,pages_to_copy*16384);
					assign_prg_pages2(novrom_bank,firstpage*PRG_16,pages_to_copy*PRG_16);
					//sprite_vram_in_use=1;
					if (page_size!=32)
					{
						//one more page!  first page for some reason
						pages_to_copy=1;
						firstpage=0;
						memcpy_if_okay(vrom_bank_2,_rombase+firstpage*16384,pages_to_copy*16384);
						assign_prg_pages2(vrom_bank_2,firstpage*PRG_16,pages_to_copy*PRG_16);
					}
				}
				else
				{
					pages_to_copy=1;
					firstpage=rompages-pages_to_copy;
					memcpy_if_okay(vrom_bank_2,_rombase+firstpage*16384,pages_to_copy*16384);
					assign_prg_pages2(vrom_bank_2,firstpage*PRG_16,pages_to_copy*PRG_16);
					//sprite_vram_in_use=1;
					
					//If page size is 32k, nothing is in GBA VRAM,
					//and NES VROM/VRAM is bankable, then copy 256 bytes before the page
					//so that back branches from C0xx to BFxx work.  Fixes Arkista's Ring
					if (page_size == 32)
					{
						memcpy_if_okay(vrom_bank_2-256,_rombase+firstpage*16384-256,256);
					}
				}
			}
			else if (comptype==2)
			{
				if (vrompages!=0)
				{
					u8 *b, *a;
					//we got this
					//0123456789ABCD E F
					//we swap and get
					//0123456F89ABCD E 7

					int firstpage=rompages-1;
					int pages_to_copy=1;
					u8* rom_page_addr=_rombase+firstpage*16384;
					//swap ROM page 7 with vram data
					simpleswap_if_okay(rom_page_addr,vrom_bank_2,16384);
					assign_prg_pages2(vrom_bank_2,firstpage*PRG_16,pages_to_copy*PRG_16);
					
					//new stuff: give 256 bytes to bank 6
					b = rom_page_addr;
					a = b + 256;
					if (!doNotDecompress)
					{
						memmove32(a, b, 7 * 16 * 1024);
						memcpy32(b, vrom_bank_2, 256);
					}
					//reassign 89ABCD
					assign_chr_pages2(a+16384,0,6*16);
					//assign F
					assign_chr_pages2(a,128-16,16);
				}
//				sprite_vram_in_use=1;
			}
		}
		#endif
		*/

	}
	
/*
	if (vrompages>0)
	{
		bankable_vrom=1;
		has_vram=0;
	}
	else
	{
		bankable_vrom=0;
		has_vram=1;
		vram_page_mask=0xFF;
		vram_page_base=0;
	}
*/	
	
	#if MIXED_VRAM_VROM
	if (mapper==74)
	{
		if (rompages==32)
		{
			instant_chr_banks[8]=NES_VRAM;
			instant_chr_banks[9]=NES_VRAM+1024;
			vram_page_base=8;
		}
		else
		{
			instant_chr_banks[0]=NES_VRAM;
			instant_chr_banks[1]=NES_VRAM+1024;
			vram_page_base=0;
		}
		bankable_vrom=1;
		has_vram=1;
		vram_page_mask=0xFF;
	}
	if (mapper==TQROM) //tqrom
	{
		for (i=64;i<128;i++)
		{
			instant_chr_banks[i]=NES_VRAM+(i&7)*1024;
		}
		bankable_vrom=1;
		has_vram=1;
		vram_page_mask=0xFF;
		vram_page_base=64;
	}
	if (mapper==VRC7) //VRC7
	{
		bankable_vrom=1;
	}
	#endif
	
	//finally get checksum
	my_checksum=checksum(nes_header+16);

#if USE_GAME_SPECIFIC_HACKS
	//GAME SPECIFIC HACKS!
	{
		switch (my_checksum)
		{
			case 0x612279F7: //Magic of Scheherazade
			{
				make_contiguous(cachebase, 3, 7);
			}
			break;
		}
	}
#endif
	
	
//	flushcache();
//	preloadcache();
//	usingcache=1;
//	usingcompcache=0;
	//finally: the cheat finder...
	
#if CHEATFINDER
	if (do_reset!=0)
	{
		setup_cheatfinder(cache_end_of_rom,1);	//remove cheatfinder if it doesn't fit
	}
#endif
	
	//fix VS games
	if (do_reset)
	{
		paletteinit();
	}
	
	//make sure DIPSCNT buffers aren't zeroed
	if (do_reset == 0)
	{
		reset_buffers();
	}
	
	//resume interrupt system
	resume_interrupts();

}

#endif	

#if CHEATFINDER
void setup_cheatfinder(u8 *cache_end_of_rom, int mode)
{
	//mode 0 - make it exist
	//mode 1 - delete it if it shouldn't exist
	u8 *end_of_cache=get_end_of_cache();

	const int cheatfinder_vars_size=8192+2048+(8192+2048)/8+900;	//12420
	u8 *cheatfinder_location=end_of_cache-cheatfinder_vars_size;
	
	if (mode==0)
	{
		u8* next=cheatfinder_location;
		cheatfinder_bits=(u32*)next;
			next+=1280;
		cheatfinder_values=next; //size 10240
			next+=10240;
		cheatfinder_cheats=next; //size 900?
			next+=900;
	}
	if (mode==1)
	{
		if (cheatfinder_location<cache_end_of_rom)
		{
			cheatfinder_bits=NULL;
			cheatfinder_values=NULL;
			cheatfinder_cheats=NULL;
		}
	}
}
#endif

void stop_dma_interrupts()
{
	okay_to_run_hdma = 0;
	//stop all DMAs
	REG_DM0CNT_H=0;
	REG_DM1CNT_H=0;
	REG_DM3CNT_H=0;
	//REG_IME=0;
	
	REG_DISPCNT&=~0x0100; //disable BG 0
	REG_DISPCNT&=~(7 | FORCE_BLANK); //force mode 0, and turn off force blank
	REG_DISPCNT|=BG2_EN; //text on BG2

}
void resume_interrupts()
{
	okay_to_run_hdma = 1;
	//REG_IME=1;
}

typedef union
{
	u64 doubleword;
	struct
	{
		u32 a;
		u32 b;
	} words;
} doubleword;

static __inline u32 mod(int a, int b)
{
	if (b < 2) return 0;
	while (a > b)
	{
		a-=b;
	}
	return a;
}

void swapmem (u32* A, u32 *B, u32 Asize) //for overlapping memory regions, size in bytes
//B is a base
{
	u32 Bsize;
	do
	{
		doubleword dw;
		dw.doubleword = simpleswap32(A,B,Asize);
		A = (u32*)dw.words.a;
		B = (u32*)dw.words.b;
		
		//replaces this code:
		//for (i=0;i<Asize;i++)
		//{
		//	t=*A;
		//	*A=*B;
		//	*B=t;
		//	A++;
		//	B++;
		//}
		Bsize=(u8*)A-(u8*)B;
		Asize=mod(Asize, Bsize);
		A=(u32*)((u8*)B+Bsize-Asize);
	} while (Asize!=0);
}

/*
//Replaced with simpleswap32()
void simpleswap (void* a_in, void* b_in, u32 size) //size in bytes, must be aligned
{
	u32 *a=(u32*)a_in, *b=(u32*)b_in;
	u32 i,t;
	size/=4;
	for (i=0;i<size;i++)
	{
		t=a[i];
		a[i]=b[i];
		b[i]=t;
	}
}
*/

#if MAPPER_OVERLAYS

APPEND_DATA const unsigned char MapperNumberOverlayList[] =
{
	4, 64, 74, 118, 119, 206, 245, 249, 100, 
	18, 19, 69, 210, 100, 
	16, 163, 100, 
	1, 15, 67, 105, 100, 
	9, 10, 17, 22, 33, 42, 73, 75, 80, 228, 100, 
	0, 2, 3, 7, 11, 32, 34, 40, 66, 68, 70, 71, 76, 77, 78, 79, 82, 86, 88, 92, 99, 151, 152, 180, 232, 100, 
	21, 23, 24, 25, 26, 65, 72, 85, 87, 93, 94, 97, 184, 100, 
	5, 100, 20, 
};

extern unsigned char __load_start_iwram0[], __load_start_iwram1[], __load_start_iwram2[],
					__load_start_iwram3[], __load_start_iwram4[], __load_start_iwram5[],
					__load_start_iwram6[], __load_start_iwram7[], __load_start_iwram8[],
					__load_start_iwram9[];
extern unsigned char __load_stop_iwram0[], __load_stop_iwram1[], __load_stop_iwram2[],
					__load_stop_iwram3[], __load_stop_iwram4[], __load_stop_iwram5[],
					__load_stop_iwram6[], __load_stop_iwram7[], __load_stop_iwram8[],
					__load_stop_iwram9[];

extern unsigned char __iwram_overlay_start[];

APPEND_DATA const unsigned char *const MapperOverlaySource[] =
{
	__load_start_iwram0,
	__load_start_iwram1,
	__load_start_iwram2,
	__load_start_iwram3,
	__load_start_iwram4,
	__load_start_iwram5,
	__load_start_iwram6,
	__load_start_iwram7,
	__load_start_iwram8,
	__load_start_iwram9,
};

APPEND_DATA const unsigned char *const MapperOverlayEnd[] =
{
	__load_stop_iwram0,
	__load_stop_iwram1,
	__load_stop_iwram2,
	__load_stop_iwram3,
	__load_stop_iwram4,
	__load_stop_iwram5,
	__load_stop_iwram6,
	__load_stop_iwram7,
	__load_stop_iwram8,
	__load_stop_iwram9,
};

APPEND u32 GetMapperOverlayMaxSize()
{
	//find overlay source size (to avoid hardcoding to 1768)
	u32 size = 0;
	for (int i = 0; i < ARRSIZE(MapperOverlaySource) - 1; i++)
	{
		u32 sectionSize = (u32)MapperOverlayEnd[i] - (u32)MapperOverlaySource[i];
		if (sectionSize > size) size = sectionSize;
	}
	return size;
}

APPEND void LoadMapperOverlay(int mapperNumber)
{
	const int DEFAULT_MAPPER_OVERLAY_NUMBER = 4;
	//const int MAPPER_OVERLAY_SIZE = 1768;
	
	u32 size = GetMapperOverlayMaxSize();
		
	int overlayNumber = lookup_mapper_arr(mapperNumber, MapperNumberOverlayList);
	if (overlayNumber == 8) overlayNumber = DEFAULT_MAPPER_OVERLAY_NUMBER;
	unsigned char *dest = __iwram_overlay_start;
	const unsigned char *src = MapperOverlaySource[overlayNumber];
	memcpy32(dest, src, size);
}

#endif
