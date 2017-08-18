//=========================================================================
//
// Copyright (C) 2013 James Haley et al.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
// haleyjd 11/14/00
//
// Description:  Handles new frame-linked animation processes
//               Currently performs lightning in sky sectors
//
//               11/14/00: updates scroll offsets for hexen-style
//                         skies
//
//=========================================================================

#include "z_zone.h"
#include "doomstat.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "m_collection.h"
#include "m_random.h"
#include "p_setup.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_anim.h"
#include "p_info.h"
#include "a_small.h"
#include "xl_animdefs.h"

int NextLightningFlash;
int LightningFlash;
int LevelSky;
int LevelTempSky;

enum
{
   ANIM_FLAT = 0,
   ANIM_TEXTURE = 1,
};

//
// Hexen style frame definition
//
struct hframedef_t
{
   int index;
   int tics;
   int ticsbase;
   int ticsmod;   // just use all the space
};

//
// Hexen style animation definition
//
struct hanimdef_t
{
   int type;
   int index;
   int tics;
   int currentFrameDef;
   int startFrameDef;
   int endFrameDef;
};

static PODCollection<hanimdef_t> AnimDefs;
static PODCollection<hframedef_t> FrameDefs;

static void P_LightningFlash();

//
// Initializes Hexen animations
//
void P_InitHexenAnims()
{
   for(const XLAnimDef &xad: xldefs)
   {
      // Validate count
      if(xad.count < 2)
         continue;   // must be at least 2

      hanimdef_t &had = AnimDefs.addNew();
      had.type = xad.type == xlanim_flat ? ANIM_FLAT : ANIM_TEXTURE;
      if(had.type == ANIM_FLAT)
      {
         if(R_CheckForFlat(xad.picname.constPtr()) == -1)
            continue;
         had.index = R_FindFlat(xad.picname.constPtr());
      }
      else
      {
         if(R_CheckForWall(xad.picname.constPtr()) == -1)
            continue;
         had.index = R_FindWall(xad.picname.constPtr());
      }
      had.startFrameDef = static_cast<int>(FrameDefs.getLength());

      for(int i = xad.index; i < xad.index + xad.count; ++i)
      {
         const xlpicdef_t &xfd = xlpics[i];
         hframedef_t &hfd = FrameDefs.addNew();
         hfd.index = had.index + xfd.offset - 1;
         if(hfd.index < 0 || hfd.index >= texturecount)
            hfd.index = 0; // prevent overflow
         if(!xfd.isRandom)
            hfd.tics = xfd.tics;
         else
         {
            hfd.ticsbase = xfd.ticsmin;
            hfd.ticsmod = xfd.ticsmax - hfd.ticsbase;
         }
      }
      had.endFrameDef = static_cast<int>(FrameDefs.getLength()) - 1;
      had.currentFrameDef = had.endFrameDef;
      had.tics = 1;
   }
}

//
// P_AnimateSurfaces
//
// Called every tic in P_Ticker
//
void P_AnimateSurfaces()
{
   // update sky scroll offsets
   //   haleyjd: stored as regular ints in the mapinfo so we need 
   //   to transform these to fixed point values :)
   skyflat_t *sky1 = R_SkyFlatForIndex(0);
   skyflat_t *sky2 = R_SkyFlatForIndex(1);

   if(sky1)
      sky1->columnoffset += LevelInfo.skyDelta  << 8;
   if(sky2)
      sky2->columnoffset += LevelInfo.sky2Delta << 8;
   
   if(LevelInfo.hasLightning)
   {
      if(!NextLightningFlash || LightningFlash)
         P_LightningFlash();
      else
         NextLightningFlash--;
   }
}

static void P_LightningFlash()
{
   int i;
   sector_t *tempSec;
   static PointThinker thunderSndOrigin;
   skyflat_t *sky1 = R_SkyFlatForIndex(0);

   if(!sky1)
      return;

   if(LightningFlash)
   {
      LightningFlash--;

      if(LightningFlash)
      {
         for(i = 0; i < numsectors; i++)
         {
            tempSec = &sectors[i];

            if(tempSec->intflags & SIF_SKY &&
               tempSec->oldlightlevel < tempSec->lightlevel - 4)
            {
               tempSec->lightlevel -= 4;
            }
         }
      }
      else
      {
         for(i = 0; i < numsectors; i++)
         {
            tempSec = &sectors[i];

            if(tempSec->intflags & SIF_SKY)
               tempSec->lightlevel = tempSec->oldlightlevel;
         }

         if(LevelSky != -1 && LevelTempSky != -1)
            sky1->texture = LevelSky;
      }
   }
   else
   {
      LightningFlash = (P_Random(pr_lightning) & 7) + 8;
      int flashLight     = 200 + (P_Random(pr_lightning) & 31);
      
      bool foundSec = false;

      for(i = 0; i < numsectors; i++)
      {
         tempSec = &sectors[i];

         if(tempSec->intflags & SIF_SKY)
         {
            tempSec->oldlightlevel = tempSec->lightlevel;
            tempSec->lightlevel    = flashLight;

            if(tempSec->lightlevel < tempSec->oldlightlevel)
               tempSec->lightlevel = tempSec->oldlightlevel;

            foundSec = true;
         }
      }

      if(foundSec)
      {
         if(LevelSky != -1 && LevelTempSky != -1)
            sky1->texture = LevelTempSky;

         S_StartSoundAtVolume(&thunderSndOrigin, sfx_thundr,
                              127, ATTN_NONE, CHAN_AUTO);
      }

      if(!NextLightningFlash)
      {
         if(M_Random() < 50)
            NextLightningFlash = (M_Random() & 15) + 16;
         else
         {
            if(M_Random() < 128 && !(leveltime & 32))
               NextLightningFlash = ((M_Random() & 7) + 2) * 35;
            else
               NextLightningFlash = ((M_Random() & 15) + 5) * 35;
         }
      }
   }
}

void P_ForceLightning()
{
   NextLightningFlash = 0;
}

//
// P_InitLightning(void)
//
// Called from P_SetupLevel
//
void P_InitLightning()
{  
   if(!LevelInfo.hasLightning)
      LightningFlash = 0;
   else
   {
      skyflat_t *sky1 = R_SkyFlatForIndex(0);

      if(!sky1)
         return;

      LevelSky = sky1->texture;
      
      if(LevelInfo.altSkyName)
         LevelTempSky = R_FindWall(LevelInfo.altSkyName);
      else
         LevelTempSky = -1;
      
      LightningFlash = 0;      
      NextLightningFlash = ((M_Random() & 15) + 5) * 35;
   }
}

#if 0
static cell AMX_NATIVE_CALL sm_lightning(AMX *amx, cell *params)
{
   if(gamestate != GS_LEVEL)
   {
      amx_RaiseError(amx, SC_ERR_GAMEMODE | SC_ERR_MASK);
      return -1;
   }

   P_ForceLightning();
   return 0;
}

AMX_NATIVE_INFO panim_Natives[] =
{
   { "_ForceLightning", sm_lightning },
   { NULL, NULL }
};
#endif

// EOF

