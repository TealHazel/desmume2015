/*  Copyright (C) 2006 yopyop
    yopyop156@ifrance.com
    yopyop156.ifrance.com

    Copyright (C) 2006 Theo Berkau

    This file is part of DeSmuME

    DeSmuME is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DeSmuME is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DeSmuME; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <string.h>
#include <stdlib.h>

#include "GPU.h"
#include "debug.h"

ARM9_struct ARM9Mem;

extern BOOL click;
Screen MainScreen;
Screen SubScreen;

//#define DEBUG_TRI

u8 GPU_screen[4*256*192];

short sizeTab[4][4][2] =
{
      {{256,256}, {512, 256}, {256, 512}, {512, 512}},
      {{128,128}, {256, 256}, {512, 512}, {1024, 1024}},
//      {{128,128}, {256, 256}, {512, 256}, {512, 512}},
      {{512,1024}, {1024, 512}, {0, 0}, {0, 0}},
      {{0, 0}, {0, 0}, {0, 0}, {0, 0}}
};

size sprSizeTab[4][4] = 
{
     {{8, 8}, {16, 8}, {8, 16}, {8, 8}},
     {{16, 16}, {32, 8}, {8, 32}, {8, 8}},
     {{32, 32}, {32, 16}, {16, 32}, {8, 8}},
     {{64, 64}, {64, 32}, {32, 64}, {8, 8}},
};

s8 mode2type[8][4] = 
{
      {0, 0, 0, 0},
      {0, 0, 0, 1},
      {0, 0, 1, 1},
      {0, 0, 0, 1},
      {0, 0, 1, 1},
      {0, 0, 1, 1},
      {3, 3, 2, 3},
      {0, 0, 0, 0}
};

void lineText(GPU * gpu, u8 num, u16 l, u8 * DST);
void lineRot(GPU * gpu, u8 num, u16 l, u8 * DST);
void lineExtRot(GPU * gpu, u8 num, u16 l, u8 * DST);

void (*modeRender[8][4])(GPU * gpu, u8 num, u16 l, u8 * DST)=
{
     {lineText, lineText, lineText, lineText},     //0
     {lineText, lineText, lineText, lineRot},      //1
     {lineText, lineText, lineRot, lineRot},       //2
     {lineText, lineText, lineText, lineExtRot},   //3
     {lineText, lineText, lineRot, lineExtRot},    //4
     {lineText, lineText, lineExtRot, lineExtRot}, //5
     {lineText, lineText, lineText, lineText},     //6
     {lineText, lineText, lineText, lineText},     //7
};

GPU * GPU_Init(u8 l)
{
     GPU * g;

     if ((g = (GPU *) malloc(sizeof(GPU))) == NULL)
        return NULL;

     GPU_Reset(g, l);

     return g;
}

void GPU_Reset(GPU *g, u8 l)
{
   memset(g, 0, sizeof(GPU));

   g->lcd = l;
   g->core = l;
   g->BGSize[0][0] = g->BGSize[1][0] = g->BGSize[2][0] = g->BGSize[3][0] = 256;
   g->BGSize[0][1] = g->BGSize[1][1] = g->BGSize[2][1] = g->BGSize[3][1] = 256;
   g->dispBG[0] = g->dispBG[1] = g->dispBG[2] = g->dispBG[3] = TRUE;
     
   g->spriteRender = sprite1D;
     
   if(g->core == GPU_SUB)
   {
      g->oam = (OAM *)(ARM9Mem.ARM9_OAM + ADDRESS_STEP_1KB);
      g->sprMem = ARM9Mem.ARM9_BOBJ;
   }
   else
   {
      g->oam = (OAM *)(ARM9Mem.ARM9_OAM);
      g->sprMem = ARM9Mem.ARM9_AOBJ;
   }
}

void GPU_DeInit(GPU * gpu)
{
     free(gpu);
}

/* Sets up LCD control variables for Display Engines A and B for quick reading */
void GPU_setVideoProp(GPU * gpu, u32 p)
{
	gpu->prop = p;

        gpu->dispMode = DISPCNT_DISPLAY_MODE(p,gpu->lcd) ;

        switch (gpu->dispMode)
        {
           case 0: // Display Off
              return;
           case 1: // Display BG and OBJ layers
              break;
           case 2: // Display framebuffer
              gpu->vramBlock = DISPCNT_VRAMBLOCK(p) ;
              return;
           case 3: // Display from Main RAM
              LOG("FIXME: Display Mode 3 not supported(Display from Main RAM)\n");
              return;
        }

	gpu->nbBGActif = 0;
        if(DISPCNT_OBJMAPING1D(p))
	{
		/* 1-d sprite mapping */
		
                gpu->sprBlock = 5 + DISPCNT_TILEOBJ1D_BOUNDARY(p) ;        /* TODO: better comment (and understanding btw 8S) */
                if((gpu->core == GPU_SUB) && (DISPCNT_TILEOBJ1D_BOUNDARY(p) == 3))
		{
			gpu->sprBlock = 7;
		}
		gpu->spriteRender = sprite1D;
	}
	else
	{
		/* 2d sprite mapping */
		gpu->sprBlock = 5;
		gpu->spriteRender = sprite2D;
	}
     
        if(DISPCNT_BMPOBJ1D_BOUNDARY(p) && (gpu->core == GPU_MAIN))
	{
		gpu->sprBMPBlock = 8;
	}
	else
	{
		gpu->sprBMPBlock = 7;
	}

	gpu->sprEnable = DISPCNT_SPRITEENABLE(p) ;
	
	GPU_setBGProp(gpu, 3, T1ReadWord(ARM9Mem.ARM9_REG, gpu->core * ADDRESS_STEP_4KB + 14));
	GPU_setBGProp(gpu, 2, T1ReadWord(ARM9Mem.ARM9_REG, gpu->core * ADDRESS_STEP_4KB + 12));
	GPU_setBGProp(gpu, 1, T1ReadWord(ARM9Mem.ARM9_REG, gpu->core * ADDRESS_STEP_4KB + 10));
	GPU_setBGProp(gpu, 0, T1ReadWord(ARM9Mem.ARM9_REG, gpu->core * ADDRESS_STEP_4KB + 8));
	
        if(DISPCNT_BG3ENABLED(p) && gpu->dispBG[3])
	{
		gpu->ordre[0] = 3;
		gpu->BGIndex[3] = 1;
		gpu->nbBGActif++;
	}
	else
	{
		gpu->BGIndex[3] = 0;
	}
	
        if(DISPCNT_BG2ENABLED(p) && gpu->dispBG[2])
	{
		if(gpu->nbBGActif)
		{
                        if(BGCNT_PRIORITY(gpu->BGProp[2]) > BGCNT_PRIORITY(gpu->BGProp[3]))
			{
				gpu->ordre[0] = 2;
				gpu->BGIndex[2] = 1;
				gpu->ordre[1] = 3;
				gpu->BGIndex[3] = 2;
			}
			else
			{
				gpu->ordre[1] = 2;
				gpu->BGIndex[2] = 2;
			}
		} 
		else
		{
			gpu->ordre[0] = 2;
			gpu->BGIndex[2] = 1;
		}
		
		gpu->nbBGActif++;
		
	}
	else
	{
			gpu->BGIndex[2] = 0;
	}
	
        if(DISPCNT_BG1ENABLED(p) && gpu->dispBG[1])
	{
		if(gpu->nbBGActif == 0)
		{
			gpu->ordre[0] = 1;
			gpu->BGIndex[1] = 1;
		}
		else
		{
			u8 i = 0;
			s8 j;
                        for(; (i < gpu->nbBGActif) && (BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]]) >= BGCNT_PRIORITY(gpu->BGProp[1])); ++i);
			for(j = gpu->nbBGActif-1; j >= i; --j)
			{
				gpu->ordre[j+1] = gpu->ordre[j];
				gpu->BGIndex[gpu->ordre[j]]++;
			}
			gpu->ordre[i] = 1;
			gpu->BGIndex[1] = i+1;
		}
		gpu->nbBGActif++;
	}
	else
	{
		gpu->BGIndex[1] = 0;
	}
	
        if(DISPCNT_BG0ENABLED(p) && (!(p & 0x8)) && gpu->dispBG[0])
	{
		if(gpu->nbBGActif == 0)
		{
			gpu->ordre[0] = 0;
			gpu->BGIndex[0] = 1;
		}
		else
		{
			u8 i = 0;
			s8 j;
                        for(; (i < gpu->nbBGActif) && (BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]]) >= BGCNT_PRIORITY(gpu->BGProp[0])); ++i);
			for(j = gpu->nbBGActif-1; j >= i; --j)
			{
				gpu->ordre[j+1] = gpu->ordre[j];
				gpu->BGIndex[gpu->ordre[j]]++;
			}
			gpu->ordre[i] = 0;
			gpu->BGIndex[0] = i+1;
		}
			gpu->nbBGActif++;
	}
	else
	{
		gpu->BGIndex[0] = 0;
	}
	
	/* FIXME: this debug won't work, obviously ... */
#ifdef DEBUG_TRI
	log::ajouter("------------------");
	for(u8 i = 0; i < gpu->nbBGActif; ++i)
	{
		sprintf(logbuf, "bg %d prio %d", gpu->ordre[i], gpu->BGProp[gpu->ordre[i]]&3);
		log::ajouter(logbuf);
	}
	log::ajouter("_________________");
#endif
}

/* this is writing in BGxCNT */
/* FIXME: all DEBUG_TRI are broken */
void GPU_setBGProp(GPU * gpu, u16 num, u16 p)
{
	u8 index = gpu->BGIndex[num];
	
	if((gpu->nbBGActif != 0) && (index != 0))
	{
		index--;
                if(BGCNT_PRIORITY(gpu->BGProp[num]) < BGCNT_PRIORITY(p))
		{
#ifdef DEBUG_TRI
               sprintf(logbuf, "INF NEW bg %d prio %d %d", num, p&3, index);
               log::ajouter(logbuf);
               for(u8 i = 0; i < gpu->nbBGActif; ++i)
               {
                    sprintf(logbuf, "bg %d prio %d", gpu->ordre[i], gpu->BGProp[gpu->ordre[i]]&3);
                    log::ajouter(logbuf);
               }
#endif
			u8 i = 0;
                        for(; (i < index) && (((BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]]))>(BGCNT_PRIORITY(p))) || (((BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]]))==(BGCNT_PRIORITY(p)))&&(gpu->ordre[i]>num))); ++i);  /* TODO: commenting and understanding */
               
#ifdef DEBUG_TRI
					
               sprintf(logbuf, "new i %d old %d", i, index);
               log::ajouter(logbuf);
#endif

			if(i != index)
			{
				s8 j;
				for(j = index-1; j>=i; --j)
				{
					gpu->ordre[j+1] = gpu->ordre[j];
					++gpu->BGIndex[gpu->ordre[j]];
				}
				gpu->ordre[i] = num;
				gpu->BGIndex[num] = i + 1;
			}
#ifdef DEBUG_TRI
               log::ajouter("");
               for(u8 i = 0; i < gpu->nbBGActif; ++i)
               {
                    sprintf(logbuf, "bg %d prio %d", gpu->ordre[i], gpu->BGProp[gpu->ordre[i]]&3);
                    log::ajouter(logbuf);
               }
               log::ajouter("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
#endif
		}
		else
		{
			if(BGCNT_PRIORITY(gpu->BGProp[num])>BGCNT_PRIORITY(p))
			{
#ifdef DEBUG_TRI
               sprintf(logbuf, "SUP NEW bg %d prio %d", num, p&3);
               log::ajouter(logbuf);
               for(u8 i = 0; i < gpu->nbBGActif; ++i)
               {
                    sprintf(logbuf, "bg %d prio %d", gpu->ordre[i], gpu->BGProp[gpu->ordre[i]]&3);
                    log::ajouter(logbuf);
               }
#endif
				u8 i = gpu->nbBGActif-1;
				for(; (i>index) && ((BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]])<BGCNT_PRIORITY(p)) ||  ((BGCNT_PRIORITY(gpu->BGProp[gpu->ordre[i]])==BGCNT_PRIORITY(p))&&(gpu->ordre[i]<num))); --i);
#ifdef DEBUG_TRI
               sprintf(logbuf, "new i %d old %d", i, index);
               log::ajouter(logbuf);
#endif
				if(i!=index)
				{
					s8 j;
					for(j = index; j<i; ++j)
					{
						gpu->ordre[j] = gpu->ordre[j+1];
						gpu->BGIndex[gpu->ordre[j]]--;
					}
					gpu->ordre[i] = num;
					gpu->BGIndex[num] = i + 1;
				}
#ifdef DEBUG_TRI
               log::ajouter("");
               for(u8 i = 0; i < gpu->nbBGActif; ++i)
               {
                    sprintf(logbuf, "bg %d prio %d", gpu->ordre[i], gpu->BGProp[gpu->ordre[i]]&3);
                    log::ajouter(logbuf);
               }
               log::ajouter("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
#endif
			}
		}
	}
		
	gpu->BGProp[num] = p;
	
	if(gpu->core == GPU_SUB)
	{
                gpu->BG_bmp_ram[num] = ((u8 *)ARM9Mem.ARM9_BBG) + BGCNT_SCREENBASEBLOCK(p) * ADDRESS_STEP_16KB;
                gpu->BG_tile_ram[num] = ((u8 *)ARM9Mem.ARM9_BBG) + BGCNT_CHARBASEBLOCK(p) * ADDRESS_STEP_16KB;
                gpu->BG_map_ram[num] = ARM9Mem.ARM9_BBG + BGCNT_SCREENBASEBLOCK(p) * ADDRESS_STEP_2KB;
	}
	else
	{
                gpu->BG_bmp_ram[num] = ((u8 *)ARM9Mem.ARM9_ABG) + BGCNT_SCREENBASEBLOCK(p) * ADDRESS_STEP_16KB;
                gpu->BG_tile_ram[num] = ((u8 *)ARM9Mem.ARM9_ABG) + BGCNT_CHARBASEBLOCK(p) * ADDRESS_STEP_16KB + ((gpu->prop >> 24) & 0x7) * ADDRESS_STEP_64kB ;
                gpu->BG_map_ram[num] = ARM9Mem.ARM9_ABG + BGCNT_SCREENBASEBLOCK(p) * ADDRESS_STEP_2KB + DISPCNT_SCREENBASEBLOCK(gpu->prop) * ADDRESS_STEP_64kB;
	}

     /*if(!(p&(1<<7)))
          BGExtPalSlot[num] = 0;
     else
          if(!(prop&(1<<30)))
               BGExtPalSlot[num] = 0;
          else*/

    gpu->BGExtPalSlot[num] = BGCNT_EXTPALSLOT(p) * 2 + num ;
                  
     /*if(!(prop&(3<<16)))
     {
          BGSize[num][0] = lcdSizeTab[p>>14][0];
          BGSize[num][1] =  lcdSizeTab[p>>14][1];
          return;
     }*/
                                                                                                      
        gpu->BGSize[num][0] = sizeTab[mode2type[DISPCNT_MODE(gpu->prop)][num]][BGCNT_SCREENSIZE(p)][0];
        gpu->BGSize[num][1] = sizeTab[mode2type[DISPCNT_MODE(gpu->prop)][num]][BGCNT_SCREENSIZE(p)][1];
}

void GPU_remove(GPU * gpu, u8 num)
{
     u8 index;
     //!!!!AJOUTER UN CRITICAL SECTION
     if(index = gpu->BGIndex[num])
     {
	  u8 i;
          --index;
          --gpu->nbBGActif;
          for(i = index; i < gpu->nbBGActif; ++i)
          {
               gpu->ordre[i] = gpu->ordre[i+1];
               --gpu->BGIndex[gpu->ordre[i]];
          }
          gpu->BGIndex[num] = 0;
     }
     gpu->dispBG[num] = !gpu->dispBG[num];
}

void GPU_addBack(GPU * gpu, u8 num)
{
     if((!gpu->BGIndex[num])&&(gpu->prop&((1<<8)<<num)))
     {
          u8 i = 0;
	  s8 j;
          u8 p = gpu->BGProp[num]&3;
          for(; (i<gpu->nbBGActif) && (((gpu->BGProp[gpu->ordre[i]]&3)>p) || (((gpu->BGProp[gpu->ordre[i]]&3)==p)&&(gpu->ordre[i]>num))); ++i);
          for(j = gpu->nbBGActif-1; j >= i; --j)
          {
               gpu->ordre[j+1] = gpu->ordre[j];
               ++gpu->BGIndex[gpu->ordre[j]];
          }
          gpu->ordre[i] = num;
          gpu->BGIndex[num] = i+1;
          ++gpu->nbBGActif;
     }
     gpu->dispBG[num] = !gpu->dispBG[num];
}

void GPU_scrollX(GPU * gpu, u8 num, u16 v)
{
	gpu->BGSX[num] = v;
}

void GPU_scrollY(GPU * gpu, u8 num, u16 v)
{
	gpu->BGSY[num] = v;
}

void GPU_scrollXY(GPU * gpu, u8 num, u32 v)
{
	gpu->BGSX[num] = (v & 0xFFFF);
	gpu->BGSY[num] = (v >> 16);
}

void GPU_setX(GPU * gpu, u8 num, u32 v)
{
	gpu->BGX[num] = (((s32)(v<<4))>>4);
}

void GPU_setXH(GPU * gpu, u8 num, u16 v)
{
	gpu->BGX[num] = (((s32)((s16)(v<<4)))<<12) | (gpu->BGX[num]&0xFFFF);
}

void GPU_setXL(GPU * gpu, u8 num, u16 v)
{
	gpu->BGX[num] = (gpu->BGX[num]&0xFFFF0000) | v;
}

void GPU_setY(GPU * gpu, u8 num, u32 v)
{
	gpu->BGY[num] = (((s32)(v<<4))>>4);
}

void GPU_setYH(GPU * gpu, u8 num, u16 v)
{
	gpu->BGY[num] = (((s32)((s16)(v<<4)))<<12) | (gpu->BGY[num]&0xFFFF);
}

void GPU_setYL(GPU * gpu, u8 num, u16 v)
{
	gpu->BGY[num] = (gpu->BGY[num]&0xFFFF0000) | v;
}

void GPU_setPA(GPU * gpu, u8 num, u16 v)
{
	gpu->BGPA[num] = (s32)v;
}

void GPU_setPB(GPU * gpu, u8 num, u16 v)
{
	gpu->BGPB[num] = (s32)v;
}

void GPU_setPC(GPU * gpu, u8 num, u16 v)
{
	gpu->BGPC[num] = (s32)v;
}

void GPU_setPD(GPU * gpu, u8 num, u16 v)
{
	gpu->BGPD[num] = (s32)v;
}

void GPU_setPAPB(GPU * gpu, u8 num, u32 v)
{
	gpu->BGPA[num] = (s16)v;
	gpu->BGPB[num] = (s16)(v>>16);
}

void GPU_setPCPD(GPU * gpu, u8 num, u32 v)
{
	gpu->BGPC[num] = (s16)v;
	gpu->BGPD[num] = (s16)(v>>16);
}

void GPU_setBLDCNT(GPU *gpu, u16 v)
{
    gpu->BLDCNT = v ;
}

void GPU_setBLDALPHA(GPU *gpu, u16 v)
{
	gpu->BLDALPHA = v ;
}

void GPU_setBLDY(GPU *gpu, u16 v)
{
	gpu->BLDY = v ;
}

INLINE void renderline_setFinalColor(GPU *gpu,u32 passing,u8 bgnum,u8 *dst,u16 color) {
	if (gpu->BLDCNT & (1 << bgnum))   /* the bg to draw has a special color effect */
	{
		switch (gpu->BLDCNT & 0xC0) /* type of special color effect */
		{
			case 0x00:              /* none (plain color passing) */
				T2WriteWord(dst, passing, color) ;
				break ;
			case 0x40:              /* alpha blending */
				{
					#define min(a,b) (((a)<(b))?(a):(b))
					//if (!(color & 0x8000)) return ;
					/* we cant do alpha on an invisible pixel */

					u16 sourceFraction = (gpu->BLDALPHA & 0x1F) ;
					if (!sourceFraction) return ;
					/* no fraction of this BG to be showed, so don't do anything */
					u16 sourceR = ((color & 0x1F) * sourceFraction) >> 4 ;
					/* weighted component from color to draw */
					u16 sourceG = (((color>>5) & 0x1F) * sourceFraction) >> 4 ;
					u16 sourceB = (((color>>10) & 0x1F) * sourceFraction) >> 4 ;
					u16 targetFraction = (gpu->BLDALPHA & 0x1F00) >> 8 ;
					if (targetFraction) {
					/* when we dont take any fraction from existing pixel, we can just draw */
						color = T2ReadWord(dst, passing) ;
					//if (color & 0x8000) {
						/* the existing pixel is not invisible */
							u16 targetR = ((color & 0x1F) * targetFraction) >> 4 ;  /* weighted component from color we draw on */
							u16 targetG = (((color>>5) & 0x1F) * targetFraction) >> 4 ;
							u16 targetB = (((color>>10) & 0x1F) * targetFraction) >> 4 ;
							sourceR = min(0x1F,targetR+sourceR) ;                   /* limit combined components to 31 max */
							sourceG = min(0x1F,targetG+sourceG) ;
							sourceB = min(0x1F,targetB+sourceB) ;
						//}
					}
					color = (sourceR & 0x1F) | ((sourceG & 0x1F) << 5) | ((sourceB & 0x1F) << 10) | 0x8000 ;
					#undef min
				}
				T2WriteWord(dst, passing, color) ;
				break ;
			case 0x80:               /* brightness increase */
				{
					if (gpu->BLDY != 0x0) { /* dont slow down if there is nothing to do */
						u16 modFraction = (gpu->BLDY & 0x1F) ;
						u16 sourceR = (color & 0x1F) ;
						u16 sourceG = ((color>>5) & 0x1F) ;
						u16 sourceB = ((color>>10) & 0x1F) ;
						sourceR += ((31-sourceR) * modFraction) >> 4 ;
						sourceG += ((31-sourceG) * modFraction) >> 4 ;
						sourceB += ((31-sourceB) * modFraction) >> 4 ;
						color = (sourceR & 0x1F) | ((sourceG & 0x1F) << 5) | ((sourceB & 0x1F) << 10) | 0x8000 ;
					} ;
				}
				T2WriteWord(dst, passing, color) ;
				break ;
			case 0xC0:               /* brightness decrease */
				{
					if (gpu->BLDY!=0) { /* dont slow down if there is nothing to do */
						u16 modFraction = (gpu->BLDY & 0x1F) ;
						u16 sourceR = (color & 0x1F) ;
						u16 sourceG = ((color>>5) & 0x1F) ;
						u16 sourceB = ((color>>10) & 0x1F) ;
						sourceR -= ((sourceR) * modFraction) >> 4 ;
						sourceG -= ((sourceG) * modFraction) >> 4 ;
						sourceB -= ((sourceB) * modFraction) >> 4 ;
						color = (sourceR & 0x1F) | ((sourceG & 0x1F) << 5) | ((sourceB & 0x1F) << 10) | 0x8000 ;
					}
				}
				T2WriteWord(dst, passing, color) ;
				break ;
		}
	} else {
		/* when no effect is active */
		T2WriteWord(dst, passing, color) ;
	}
} ;

/* render a text background to the combined pixelbuffer */
INLINE void renderline_textBG(GPU * gpu, u8 num, u8 * DST, u16 X, u16 Y, u16 LG)
{
	u32 bgprop = gpu->BGProp[num];
	u16 lg     = gpu->BGSize[num][0];
	u16 ht     = gpu->BGSize[num][1];
	u16 tmp    = ((Y&(ht-1))>>3);
	u8 * map   = gpu->BG_map_ram[num] + (tmp&31) * 64;
	u8 *dst    = DST;
	u8 *tile;
	u16 xoff   = X;
	u8 * pal;
	u16 yoff;
	u16 x;

	if(tmp>31) 
	{
		map+= ADDRESS_STEP_512B << BGCNT_SCREENSIZE(bgprop) ;
	}
	
	tile = (u8*) gpu->BG_tile_ram[num];
	if((!tile) || (!gpu->BG_map_ram[num])) return; 	/* no tiles or no map*/
	xoff = X;
	if(!BGCNT_256COL(bgprop))    /* color: 16 palette entries */
	{
		yoff = ((Y&7)<<2);
		pal = ARM9Mem.ARM9_VMEM + gpu->core * ADDRESS_STEP_1KB ;
		for(x = 0; x < LG;)
		{
			u8 * mapinfo;
			u16 mapinfovalue;
			u8 *line;
			u16 xfin;
			tmp = ((xoff&(lg-1))>>3);
			mapinfo = map + (tmp&0x1F) * 2;
			if(tmp>31) mapinfo += 32*32*2;
			mapinfovalue = T1ReadWord(mapinfo, 0);

			line = (u8 * )tile + ((mapinfovalue&0x3FF) * 0x20) + (((mapinfovalue)& 0x800 ? (7*4)-yoff : yoff));
			xfin = x + (8 - (xoff&7));
			if (xfin > LG)
				xfin = LG;
			
#define RENDERL(c,m) \
	if (c) renderline_setFinalColor(gpu,0,num,dst,T1ReadWord(pal, ((c) + ((mapinfovalue>>12)&0xF) * m) << 1)) ; \
	dst += 2; x++; xoff++;

			if((mapinfovalue) & 0x400)
			{
				line += 3 - ((xoff&7)>>1);
				for(; x < xfin; ) {
// XXX
					RENDERL(((*line)>>4),0x10)
					RENDERL(((*line)&0xF),0x10)
					line--;
				}
			} else {
				line += ((xoff&7)>>1);
				for(; x < xfin; ) {
// XXX
					RENDERL(((*line)&0xF),0x10)
					RENDERL(((*line)>>4),0x10)
					line++;
				}
			}
		}
		return;
	}
	if(!DISPCNT_USEEXTPAL(gpu->prop))  /* color: no extended palette */
	{
		yoff = ((Y&7)<<3);
		pal = ARM9Mem.ARM9_VMEM + gpu->core * ADDRESS_STEP_1KB ;
		for(x = 0; x < LG;)
		{
			u8 * mapinfo;
			u16 mapinfovalue;
			u8 *line;
			u16 xfin;
			tmp = ((xoff&(lg-1))>>3);
			mapinfo = map + (tmp & 31) * 2;
			mapinfovalue;

			if(tmp > 31) mapinfo += 32*32*2;

			mapinfovalue = T1ReadWord(mapinfo, 0);

			line = (u8 * )tile + ((mapinfovalue&0x3FF)*0x40) + (((mapinfovalue)& 0x800 ? (7*8)-yoff : yoff));
			xfin = x + (8 - (xoff&7));
			if (xfin > LG)
				xfin = LG;
			
			if((mapinfovalue)& 0x400)
			{
				line += (7 - (xoff&7));
				for(; x < xfin; )
				{
					RENDERL((*line),0)
					line--;
				}
			} else
			{
				line += (xoff&7);
				for(; x < xfin; )
				{
					RENDERL((*line),0)
					line++;
				}
			}
		}
		return;
	}
													/* color: extended palette */
	pal = ARM9Mem.ExtPal[gpu->core][gpu->BGExtPalSlot[num]];
	if(!pal) return;

	yoff = ((Y&7)<<3);

	for(x = 0; x < LG;)
	{
		u8 * mapinfo;
		u16 mapinfovalue;
		u8 * line;
		u16 xfin;
		tmp = ((xoff&(lg-1))>>3);
		mapinfo = map + (tmp & 0x1F) * 2;
		mapinfovalue;

		if(tmp>31) mapinfo += 32 * 32 * 2;

		mapinfovalue = T1ReadWord(mapinfo, 0);

		line = (u8 * )tile + ((mapinfovalue&0x3FF)*0x40) + (((mapinfovalue)& 0x800 ? (7*8)-yoff : yoff));
		xfin = x + (8 - (xoff&7));
		if (xfin > LG)
			xfin = LG;
		
		if((mapinfovalue)& 0x400)
		{
			line += (7 - (xoff&7));
			for(; x < xfin;)
			{
				/* this is was adapted */
				RENDERL((*line),0x100)
				line--;
			}
		} else
		{
			line += (xoff&7);
			for(; x < xfin; )
			{
				/* this is was adapted */
				RENDERL((*line),0x100)
				line++;
			}
		}
	}
#undef RENDERL
}

INLINE void rotBG2(GPU * gpu, u8 num, u8 * DST, u16 H, s32 X, s32 Y, s16 PA, s16 PB, s16 PC, s16 PD, u16 LG)
{
     u32 bgprop = gpu->BGProp[num];

     s32 x = X + (s32)PB*(s32)H;
     s32 y = Y + (s32)PD*(s32)H;


     s32 dx = (s32)PA;
     s32 dy = (s32)PC;

     s32 auxX;
     s32 auxY;

     s32 lg = gpu->BGSize[num][0];
     s32 ht = gpu->BGSize[num][1];
     s32 lgmap = (lg>>3);

     u8 * map = gpu->BG_map_ram[num];
     u8 * tile = (u8 *)gpu->BG_tile_ram[num];
     u8 * dst = DST;
     u8 mapinfo;
     u8 coul;
     u8 * pal;
     u32 i;

     if((!tile)||(!map)) return;

     pal = ARM9Mem.ARM9_VMEM + gpu->core * 0x400;
     for(i = 0; i < LG; ++i)
     {
          auxX = x>>8;
          auxY = y>>8;

          if(bgprop&(1<<13))
          {
               auxX &= (lg-1);
               auxY &= (ht-1);
          }

          if ((auxX >= 0) && (auxX < lg) && (auxY >= 0) && (auxY < ht))
          {
               mapinfo = map[(auxX>>3) + ((auxY>>3) * lgmap)];
               coul = tile[mapinfo*64 + ((auxY&7)<<3) + (auxX&7)];
               if(coul)
		    		renderline_setFinalColor(gpu,0,num,dst,T1ReadWord(pal, coul << 1));
          }
          dst += 2;
          x += dx;
          y += dy;
     }
}

INLINE void extRotBG2(GPU * gpu, u8 num, u8 * DST, u16 H, s32 X, s32 Y, s16 PA, s16 PB, s16 PC, s16 PD, s16 LG)
{
	u32 bgprop = gpu->BGProp[num];
	
	s32 x = X + (s32)PB*(s32)H;
	s32 y = Y + (s32)PD*(s32)H;
	
	s32 dx = PA;
	s32 dy = PC;
	
	s32 auxX;
	s32 auxY;
	
	s16 lg = gpu->BGSize[num][0];
	s16 ht = gpu->BGSize[num][1];
	u16 lgmap = (lg>>3);
	
	u8 * tile = (u8 *)gpu->BG_tile_ram[num];
	u8 * dst = DST;
	u16 mapinfo;
	u8 coul;
	
	switch(((bgprop>>2)&1)|((bgprop>>6)&2))
	{
		case 0 :
		case 1 :
		{
		u8 * pal = ARM9Mem.ExtPal[gpu->core][gpu->BGExtPalSlot[num]];
		if(!pal) return;

#define LOOP(c) \
	u16 i; \
	for(i = 0; i < LG; ++i) { \
		auxX = x>>8; auxY = y>>8; \
		if(bgprop&(1<<13)) { \
			auxX &= (lg-1); auxY &= (ht-1); \
		} \
		if ((auxX >= 0) && (auxX < lg) && (auxY >= 0) && (auxY < ht)) c \
		dst += 2; x += dx; y += dy; \
	} 
		u8 * map = gpu->BG_map_ram[num];
		LOOP(
		{
			u16 x1;
			u16 y1;
			mapinfo = T1ReadWord(map, ((auxX>>3) + (auxY>>3) * lgmap) << 1);

			x1 = (mapinfo & 0x400) ? 7 - (auxX&7) : (auxX&7);
			y1 = (mapinfo & 0x800) ? 7 - (auxY&7) : (auxY&7);
			coul = tile[(mapinfo&0x3FF)*64 + x1 + (y1<<3)];
			if(coul)
			renderline_setFinalColor(gpu,0,num,dst, T1ReadWord(pal, (coul + (mapinfo>>12)*0x100) << 1));
		})
	
		}
		return;
	case 2 :
		{
		u8 * pal = ARM9Mem.ARM9_VMEM + gpu->core * 0x400;
		u8 * map = gpu->BG_bmp_ram[num];
		LOOP(
		{
			mapinfo = map[auxX + auxY * lg];
			if(mapinfo)
				renderline_setFinalColor(gpu,0,num,dst, T1ReadWord(pal, mapinfo << 1));
		})
		}
		return;
	case 3 :
		{
		u8 * map = gpu->BG_bmp_ram[num];
		LOOP(
		{
			mapinfo = T1ReadWord(map, (auxX + auxY * lg) << 1);
			if(mapinfo)
			renderline_setFinalColor(gpu,0,num,dst, mapinfo);
		})
		}
		return;
#undef LOOP
     }
}

void lineText(GPU * gpu, u8 num, u16 l, u8 * DST)
{
	renderline_textBG(gpu, num, DST, gpu->BGSX[num], l + gpu->BGSY[num], 256);
}

void lineRot(GPU * gpu, u8 num, u16 l, u8 * DST)
{
     rotBG2(gpu, num, DST, l,
              gpu->BGX[num],
              gpu->BGY[num],
              gpu->BGPA[num],
              gpu->BGPB[num],
              gpu->BGPC[num],
              gpu->BGPD[num],
              256);
}

void lineExtRot(GPU * gpu, u8 num, u16 l, u8 * DST)
{
     extRotBG2(gpu, num, DST, l,
              gpu->BGX[num],
              gpu->BGY[num],
              gpu->BGPA[num],
              gpu->BGPB[num],
              gpu->BGPC[num],
              gpu->BGPD[num],
              256);
}

void textBG(GPU * gpu, u8 num, u8 * DST)
{
	u32 i;
	for(i = 0; i < gpu->BGSize[num][1]; ++i)
	{
		renderline_textBG(gpu, num, DST + i*gpu->BGSize[num][0], 0, i, gpu->BGSize[num][0]);
	}
}

void rotBG(GPU * gpu, u8 num, u8 * DST)
{
     u32 i;
     for(i = 0; i < gpu->BGSize[num][1]; ++i)
          rotBG2(gpu, num, DST + i*gpu->BGSize[num][0], i, 0, 0, 256, 0, 0, 256, gpu->BGSize[num][0]);
}

void extRotBG(GPU * gpu, u8 num, u8 * DST)
{
     u32 i;
     for(i = 0; i < gpu->BGSize[num][1]; ++i)
          extRotBG2(gpu, num, DST + i*gpu->BGSize[num][0], i, 0, 0, 256, 0, 0, 256, gpu->BGSize[num][0]);
}

#define nbShow 128
#define RENDERS_A(a) \
	if((a)&&(prioTab[sprX]>=prio)) \
	{ \
		renderline_setFinalColor(gpu, sprX << 1,4,dst, c); \
		prioTab[sprX] = prio; \
	}
#define RENDERS_B(c) \
	if((c)&&(prioTab[sprX]>=prio)) \
	{ \
		renderline_setFinalColor(gpu, sprX << 1,4,dst, T1ReadWord(pal, (c) << 1)); \
		prioTab[sprX] = prio; \
	}
#define RENDERS_C(c,d) \
	if((c)&&(prioTab[sprX]>=prio)) \
	{ \
		renderline_setFinalColor(gpu, (sprX d) << 1,4,dst, T1ReadWord(pal, ((c)+((aux->attr2>>12)*0x10)) << 1)); \
		prioTab[sprX d] = prio; \
	}


void sprite1D(GPU * gpu, u16 l, u8 * dst, u8 * prioTab)
{
	if (!gpu->sprEnable) return ;
	OAM * aux = gpu->oam + (nbShow-1);// + 127;
	
	u8 block = gpu->sprBlock;
	u16 i;
	
	for(i = 0; i<nbShow; ++i, --aux)
	{
		s32 sprX = aux->attr1 & 0x1FF;
		s32 sprY;
		s32 x = 0;
		u32 lg;
		size sprSize;
		s32 y;
		u8 prio;
		u8 * src;
		u8 * pal;
		u16 j;
	
		sprX = ((s32)(sprX<<23))>>23;
		sprY = aux->attr0 & 0xFF;
	
		sprSize = sprSizeTab[(aux->attr1>>14)][(aux->attr0>>14)];
	
		lg = sprSize.x;

		if(sprY>192)
		sprY = (s32)((s8)(aux->attr0 & 0xFF));
	
		if( ((aux->attr0&(1<<9))&&(!(aux->attr0&(1<<8)))) ||
		    (l<sprY)||(l>=sprY+sprSize.y) ||
		    (sprX==256) )
			continue;
	
		if(sprX<0)
		{
			if(sprX+sprSize.x<=0) continue;
			lg += sprX;
			x = -sprX;
			sprX = 0;
		} else if(sprX+sprSize.x>256)
			lg = 255 - sprX;
	
		y = l - sprY;
		prio = (aux->attr2>>10)&3;
	
		if(aux->attr1&(1<<13)) 
			y = sprSize.y - y -1;
		
		
		if((aux->attr0&(3<<10))==(3<<10))
		{
			u16 i;
			src = (gpu->sprMem) +(aux->attr2&0x3FF)*16 + (y<<gpu->sprBMPBlock);
	
			if(aux->attr1&(1<<12))
			{
				x = sprSize.x -x - 1;
				for(i = 0; i < lg; ++i, --x, ++sprX)
				{
					u8 c = src[x];
					// What's the point in shifting down by 15 when c is 8-bits?
					RENDERS_A(c>>15)
				}
				continue;
			}
			for(i = 0; i < lg; ++i, ++x, ++sprX)
			{
				u16 c = T1ReadWord(src, x << 1);
				RENDERS_A(c>>15)
			}
			continue;
		}
		
		if(aux->attr0&(1<<13))
		{
		u16 i;
		src = gpu->sprMem + ((aux->attr2&0x3FF)<<block) + ((y>>3)*sprSize.x*8) + ((y&0x7)*8);
	
		if(gpu->prop&(1<<31))
			pal = ARM9Mem.ObjExtPal[gpu->core][0]+((aux->attr2>>12)*0x200);
		else
			pal = ARM9Mem.ARM9_VMEM + 0x200 + gpu->core *0x400;
	
		if(aux->attr1&(1<<12))
		{
			x = sprSize.x -x - 1;
			for(i = 0; i < lg; ++i, --x, ++sprX)
			{
				u8 c = src[(x&0x7) + ((x&0xFFF8)<<3)];
				RENDERS_B(c)
			}
			continue;
		}
		for(i = 0; i < lg; ++i, ++x, ++sprX)
		{
			u8 c = src[(x&0x7) + ((x&0xFFF8)<<3)];
			RENDERS_B(c)
		}
		continue;
		}
		src = gpu->sprMem + ((aux->attr2&0x3FF)<<block) + ((y>>3)*sprSize.x*4) + ((y&0x7)*4);
		pal = ARM9Mem.ARM9_VMEM + 0x200 + gpu->core * 0x400;
			
		if(x&1)
		{
			if(aux->attr1&(1<<12))
			{
				s32 x1 = ((sprSize.x-x)>>1);
				u8 c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c&0xF),)
				x1 = ((sprSize.x-x-lg)>>1);
				c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c&0xF),+lg-1)
			} else {
				s32 x1 = (x>>1);
				u8 c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c>>4),)
				x1 = ((x+lg-1)>>1);
				c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c>>4),+lg-1)
		
			}
			++sprX;
			++x;
		}
		lg >>= 1;
		x >>= 1;
	
		if(aux->attr1&(1<<12))
		{
			u16 i;
			x = (sprSize.x>>1) - x -1;
			for(i = 0; i < lg; ++i, --x)
			{
				u8 c = src[(x&0x3) + ((x&0xFFFC)<<3)];
				RENDERS_C((c>>4),)
				++sprX;
				RENDERS_C((c&0xF),)
				++sprX;
			}
			continue;
		}

		for(j = 0; j < lg; ++j, ++x)
		{
			u8 c = src[(x&0x3) + ((x&0xFFFC)<<3)];	
			RENDERS_C((c&0xF),)
			++sprX;
			RENDERS_C((c>>4),)
			++sprX;
		}
	}
}

void sprite2D(GPU * gpu, u16 l, u8 * dst, u8 * prioTab)
{
	if (!gpu->sprEnable) return ;
	u16 i;
	OAM * aux = gpu->oam + (nbShow-1);// + 127;
	
	for(i = 0; i<nbShow; ++i, --aux)
	{
		s32 sprX = aux->attr1 & 0x1FF;
		s32 sprY;
		s32 x = 0;
		size sprSize;
		u32 lg;
		s32 y;
		u8 prio;
		u8 * src;
		u8 * pal;
		u16 j;
	
		sprX = ((s32)(sprX<<23))>>23;
		sprY = aux->attr0 & 0xFF;
	
		sprSize = sprSizeTab[(aux->attr1>>14)][(aux->attr0>>14)];
	
		lg = sprSize.x;
	
		if(sprY>192)
			sprY = (s32)((s8)(aux->attr0 & 0xFF));
	
		if ( ((aux->attr0&(1<<9))&&(!(aux->attr0&(1<<8)))) ||
		     (l<sprY)||(l>=sprY+sprSize.y) ||
		     (sprX==256) )
			continue;
	
		if(sprX<0) {
			if(sprX+sprSize.x<=0) continue;
			lg += sprX;
			x = -sprX;
			sprX = 0;
		} else if(sprX+sprSize.x>256)
			lg = 255 - sprX;
	
		y = l - sprY;
		prio = (aux->attr2>>10)&3;
	
		if(aux->attr1&(1<<13))
			y = sprSize.y - y -1;
		
		if((aux->attr0&(3<<10))==(3<<10))
		{
			u16 i;
			src = (gpu->sprMem) + (((aux->attr2&0x3E0) * 64 + (aux->attr2&0x1F) * 8 + ( y << 8)) << 1);
	
			if(aux->attr1&(1<<12))
			{
				LOG("Using fubared h-flip\n");
		
				x = sprSize.x -x - 1;
				for(i = 0; i < lg; ++i, --x, ++sprX)
				{
					u8 c = src[x << 1];
					// What's the point in shifting down by 15 when c is 8-bits?
					RENDERS_A(c>>15)
				}
				continue;
			}
			for(i = 0; i < lg; ++i, ++x, ++sprX)
			{
				u16 c = T1ReadWord(src, x << 1);
				RENDERS_A(c>>15)
			}//
			continue;
		}
		
		if(aux->attr0&(1<<13))
		{
			u16 i;
			src = gpu->sprMem + ((aux->attr2&0x3FF)<<5) + ((y>>3)<<10) + ((y&0x7)*8);
			pal = ARM9Mem.ARM9_VMEM + 0x200 + gpu->core * 0x400;
		
			if(aux->attr1&(1<<12))
			{
				x = sprSize.x -x - 1;
				for(i = 0; i < lg; ++i, --x, ++sprX)
				{
					u8 c = src[(x&0x7) + ((x&0xFFF8)<<3)];
					RENDERS_B(c)
				}
				continue;
			}
	
			for(i = 0; i < lg; ++i, ++x, ++sprX)
			{
				u8 c = src[(x&0x7) + ((x&0xFFF8)<<3)];
				RENDERS_B(c)
			}
			continue;
		}
		src = gpu->sprMem + ((aux->attr2&0x3FF)<<5) + ((y>>3)<<10) + ((y&0x7)*4);
		pal = ARM9Mem.ARM9_VMEM + 0x200 + gpu->core * 0x400;
		
		if(x&1)
		{
			if(aux->attr1&(1<<12))
			{
				s32 x1 = ((sprSize.x-x)>>1);
				u8 c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];	
				RENDERS_C((c&0xF),)
				x1 = ((sprSize.x-x-lg)>>1);
				c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c&0xF),+lg-1)
			} else {
				s32 x1 = (x>>1);
				u8 c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c>>4),)
				x1 = ((x+lg-1)>>1);
				c = src[(x1&0x3) + ((x1&0xFFFC)<<3)];
				RENDERS_C((c>>4),+lg-1)
			}
			++sprX;
			++x;
		}
		lg >>= 1;
		x >>= 1;
		
		if(aux->attr1&(1<<12))
		{
			u16 i;
			x = (sprSize.x>>1) - x -1;
			for(i = 0; i < lg; ++i, --x)
			{
				u8 c = src[(x&0x3) + ((x&0xFFFC)<<3)];
				RENDERS_C((c>>4),)
				++sprX;
				RENDERS_C((c&0xF),)
				++sprX;
			}
			continue;
		}
		for(j = 0; j < lg; ++j, ++x)
		{
			u8 c = src[(x&0x3) + ((x&0xFFFC)<<3)];
			RENDERS_C((c&0xF),)
			++sprX;
			RENDERS_C((c>>4),)
			++sprX;
		}
	}
}

void Screen_Init(void) {
        MainScreen.gpu = GPU_Init(0);
        SubScreen.gpu = GPU_Init(1);
}

void Screen_Reset(void) {
   GPU_Reset(MainScreen.gpu, 0);
   GPU_Reset(SubScreen.gpu, 1);
}

void Screen_DeInit(void) {
        GPU_DeInit(MainScreen.gpu);
        GPU_DeInit(SubScreen.gpu);
}
