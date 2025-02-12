#include "includes.h"

EWRAM_BSS int roms;//total number of roms
EWRAM_BSS volatile int selectedrom=0;
EWRAM_BSS volatile int selected_rom_options = 0;
EWRAM_BSS volatile int rommenu_state = 0;

typedef enum
{
	DISABLED = 0,
	APPEARING,
	IN_MENU,
	RELEASING_BUTTON,
	DISAPPEARING,
	FINISHED
} RommenuState;

#if ROMMENU

//"romnumber" = loaded rom, "selectedrom" = rom selected at menu

void rommenu(void)
{
	//The rom menu runs in a background thread, so input stays responsive while a game is being decompressed.
	//This code is the main thread.  It handles:
	//  requests for the game to change
	//  loading a different game
	//  running the game for one frame (while the game isn't changing)
	//  exiting out of the rom menu once we've confirmed the game
	
	cls(3);
	//rommenu_frame (background code) handles the position of the UI for the rom menu
	setdarknessgs(16);
	#if SAVE
		backup_nes_sram(1);
		//this now has its own delete menu built into the function
	#endif
	#if LINK
	resetSIO((joycfg&~0xff000000) + 0x20000000);//back to 1P
	#endif

	if(pogoshell || roms <= 1)
	{
		loadcart(0,emuflags&0x304,1);		//Also save country
		//SRAM loading happens inside of loadcart
	}
	else
	{
		ui_x = 256;
		rommenu_state = DISABLED;  //makes the rom menu do nothing until we've set its state to appearing
		move_ui();
		int sel = selectedrom;
		int opt = drawmenu(sel);
		selected_rom_options = opt;
		setdarknessgs(8);
		rommenu_state = APPEARING;  //makes the rom menu ready to go, scrolling on from the right
		loadcart(sel,opt|(emuflags&0x300),1);
		do
		{
			sel = selectedrom;
			if (sel != romnumber)
			{
				opt = selected_rom_options;
				loadcart(sel,opt|(emuflags&0x300),1);
			}
			else
			{
				run(0);
			}
		} while (rommenu_state != FINISHED  || selectedrom != romnumber);
	}
	ui_x=256;
	ui_y=0;
	move_ui();
	setdarknessgs(0);
	rommenu_state = DISABLED;
	//TODO: add back in Auto-Load state feature
	//returning from "rommenu" will call run(1) and start the game, giving an empty stack for the emulator
}

void rommenu_frame(void)
{
	int opt, sel, romz, key, lastselected;
	romz = roms;
	sel = selectedrom;
	//opt = selected_rom_options;
	
	if (rommenu_state == APPEARING)
	{
		EMUinput=0;
		
		//move ui to the left (from 256)
		if (ui_x > 0)
		{
			ui_x -= 32;
			move_ui();
		}
		
		if (ui_x <= 0 || ui_x > 256)
		{
			ui_x = 0;
			move_ui();
			rommenu_state = IN_MENU;
		}
	}
	
	if (rommenu_state == IN_MENU)
	{
		lastselected = sel;
		AGBinput = ~REG_P1;
		
		key=getinput();
		if (key&RIGHT)
		{
			sel+=10;
			if (sel > romz-1) sel = romz-1;
		}
		if (key&LEFT)
		{
			sel-=10;
			if (sel < 0) sel=0;
		}
		if (key&UP)
		{
			sel -= 1;
		}
		if (key&DOWN)
		{
			sel++;
		}
		if (sel < 0) sel += romz;
		if (sel >= romz) sel -= romz;
		
		if (lastselected!=sel)
		{
			opt = drawmenu(sel);
			selected_rom_options = opt;
			selectedrom = sel;
		}
		if (key&(A_BTN + B_BTN + START))
		{
			rommenu_state = RELEASING_BUTTON;
		}
	}
	if (rommenu_state == RELEASING_BUTTON || rommenu_state == DISAPPEARING)
	{
		key = ~REG_P1;
		if (!(key&(A_BTN + B_BTN + START)))
		{
			rommenu_state = DISAPPEARING;
		}
		
		//move UI to the right (to 256)
		if (ui_x < 256)
		{
			int darkness;
			ui_x += 32;
			
			darkness = 7 - ui_x / 32;
			if (darkness < 0)
			{
				darkness = 0;
			}
			
			setdarknessgs(darkness);
			
			move_ui();
		}
		if (ui_x >= 256 || ui_x < 0)
		{
			setdarknessgs(0);
			ui_x = 256;
			move_ui();
			
			if (rommenu_state == DISAPPEARING)
			{
				rommenu_state = FINISHED;
			}
		}
	}
}

//returns options for selected rom
int drawmenu(int sel) {
	int i,j,topline,toprow,romflags=0;
	int top_displayed_line=0;
	u8 *p;
	romheader *ri;

	waitframe();

	if(roms>20) {
		#if !COMPY
		topline=8*(roms-20)*sel/(roms-1);
		#else
		topline = 0;
		#endif
		toprow=topline/8;
		j=(toprow<roms-20)?21:20;
		ui_y=topline%8;
		move_ui();
	} else {
		int ui_row;
		
		ui_row = (160-roms*8)/2;
		ui_row/=4;
		if (ui_row&1)
		{
			ui_y=4;
			move_ui();
			ui_row++;
		}
		ui_row/=2;
		top_displayed_line=ui_row;

		toprow=0;
		j=roms;
	}
	p=findrom(toprow);
	for(i=0;i<j;i++) {
		if(roms>1)drawtext(i+top_displayed_line,(char*)p,i==(sel-toprow)?1:0);
		if(i==sel-toprow) {
			ri=(romheader*)p;
			romflags=(*ri).flags|(*ri).spritefollow<<16;
		}
		p+=*(u32*)(p+32)+48;
		p=find_nes_header(p);
	}
	return (romflags & ~1) | 2 ;
}

int getinput() {
	static int lastdpad,repeatcount=0;
	int dpad;
	int keyhit=(oldinput^AGBinput)&AGBinput;
	oldinput=AGBinput;

	dpad=AGBinput&(UP+DOWN+LEFT+RIGHT);
	if(lastdpad==dpad) {
		repeatcount++;
		if(repeatcount<25 || repeatcount&3)	//delay/repeat
			dpad=0;
	} else {
		repeatcount=0;
		lastdpad=dpad;
	}
	EMUinput=0;	//disable game input
	return dpad|(keyhit&(A_BTN+B_BTN+START));
}

#else
void rommenu()
{
	loadcart(0,emuflags&0x304,1);
	
	ui_x = 256;
	move_ui();
	cls(3);
	get_ready_to_display_text();
}
void rommenu_frame(){}
#endif

u8 *find_nes_header(u8 *base)
{
	//look up to 256 bytes later for an INES header
	u32 *p=(u32*)base;
	
	u32 nes_id=0x1a530000+ne;
	
	int i;
	for (i=0;i<64;i++)
	{
		if (*p==nes_id)
		{
			return ((u8*)p)-48;
		}
		else
		{
			p++;
		}
	}
	return NULL;
}

//return ptr to Nth ROM (including rominfo struct)
u8 *findrom(int n)
{
	u8 *p=find_nes_header(textstart);
	while(p && !pogoshell && n--)
	{
		p+=*(u32*)(p+32)+sizeof(romheader);
		p=find_nes_header(p);
	}
	return p;
}
