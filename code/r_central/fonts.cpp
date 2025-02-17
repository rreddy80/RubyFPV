/*
You can use this C/C++ code however you wish (for example, but not limited to:
     as is, or by modifying it, or by adding new code, or by removing parts of the code;
     in public or private projects, in new free or commercial products) 
     only if you get a priori written consent from Petru Soroaga (petrusoroaga@yahoo.com) for your specific use
     and only if this copyright terms are preserved in the code.
     This code is public for learning and academic purposes.
Also, check the licences folder for additional licences terms.
Code written by: Petru Soroaga, 2021-2023
*/

#include "../base/base.h"
#include "../base/config.h"
#include "../base/ctrl_settings.h"
#include "../base/hw_procs.h"
#include "../base/hdmi.h"
#include "../osd/osd_common.h"

#define MAX_FONTS_SIZES 50

bool s_bFontsInitialized = false;

u32 s_ListOSDFontSizes[50];
int s_iListOSDFontSizesCount = 0;

u32 s_ListMenuFontSizes[50];
int s_iListMenuFontSizesCount = 0;

u32 g_idFontOSD = 0;
u32 g_idFontOSDBig = 0;
u32 g_idFontOSDSmall = 0;
u32 g_idFontOSDExtraSmall = 0;

u32 g_idFontOSDWarnings = 0;
u32 g_idFontStats = 0;
u32 g_idFontStatsSmall = 0;

u32 g_idFontMenu = 0;
u32 g_idFontMenuSmall = 0;
u32 g_idFontMenuLarge = 0;

// Exported to end user
extern u32 s_uRenderEngineUIFontIdSmall;
extern u32 s_uRenderEngineUIFontIdRegular;
extern u32 s_uRenderEngineUIFontIdBig;
  
#include "fonts.h"
#include "shared_vars.h"

u32 _getBestMatchingFontHeight(u32* pFontList, int iFontCount, float fPixelsHeight)
{
   if (NULL == pFontList )
      return 0;

   float hScreen = hdmi_get_current_resolution_height();

   for( int i=0; i<iFontCount-1; i++ )
   {
      float fFontHeightPx = g_pRenderEngine->getFontHeight(pFontList[i]) * hScreen;
      float fFontHeightPxNext = g_pRenderEngine->getFontHeight(pFontList[i+1]) * hScreen;

      if ( fPixelsHeight <= fFontHeightPx )
      {
         log_line("Best font match for %d pixels height: default font %d (h: %d pixels)", (int)fPixelsHeight, i, (int)fFontHeightPx);
         return pFontList[i];
      }
      if ( fPixelsHeight <= (fFontHeightPx + fFontHeightPxNext)/2.0 )
      {
         log_line("Best font match for %d pixels height: default font %d (h: %d pixels)", (int)fPixelsHeight, i, (int)fFontHeightPx);
         return pFontList[i];
      }
   }
   log_line("Best font match for %d pixels height: default font %d", (int)fPixelsHeight, iFontCount-1);
   return pFontList[iFontCount-1];
}

bool _loadFontFamily(const char* szName, u32* pOutputList, int* pCountOutput)
{
   if ( (NULL == szName) || (0 == szName[0]) || (NULL == pOutputList) || (NULL == pCountOutput) )
      return false;

   log_line("Loading font familiy %s ...", szName);

   *pCountOutput = 0;

   char szFontFile[128];
   for( int iSize = 10; iSize < 62; iSize += 2 )
   {
      sprintf(szFontFile, "res/font_%s_%d.dsc", szName, iSize);
      if ( access( szFontFile, R_OK ) == -1 )
         continue;

      int iResult = g_pRenderEngine->loadFont(szFontFile);
      if ( iResult > 0 )
      {
         pOutputList[(*pCountOutput)] = (u32) iResult;
         *pCountOutput = (*pCountOutput) + 1;
      }
      else
         log_line("Can't load font %s, error code: %d", szFontFile, iResult);
   }
   log_line("Loaded %d sizes for font family %s", *pCountOutput, szName);
   return true;
}

bool loadAllFonts(bool bReloadMenuFonts)
{
   float hScreen = hdmi_get_current_resolution_height();
   log_line("Loading fonts for screen height: %d px, reload menu fonts: %s", (int)hScreen, bReloadMenuFonts?"yes":"no");

   if ( s_bFontsInitialized )
   {
      if ( bReloadMenuFonts )
      {
         for( int i=0; i<s_iListMenuFontSizesCount; i++ )
            g_pRenderEngine->freeFont(s_ListMenuFontSizes[i]);
         s_iListMenuFontSizesCount = 0;
      }

      for( int i=0; i<s_iListOSDFontSizesCount; i++ )
         g_pRenderEngine->freeFont(s_ListOSDFontSizes[i]);
      s_iListOSDFontSizesCount = 0;
   }

   s_bFontsInitialized = true;
   
   if ( bReloadMenuFonts )
      _loadFontFamily("raw_bold", s_ListMenuFontSizes, &s_iListMenuFontSizesCount );

   Preferences* p = get_Preferences();
   char szFont[32];
   strcpy(szFont, "raw_bold");
   if ( p->iOSDFont == 0 )
      strcpy(szFont, "raw_bold");
   if ( p->iOSDFont == 1 )
      strcpy(szFont, "rawobold");
   if ( p->iOSDFont == 2 )
      strcpy(szFont, "ariobold");
   if ( p->iOSDFont == 3 )
      strcpy(szFont, "bt_bold");

   _loadFontFamily(szFont, s_ListOSDFontSizes, &s_iListOSDFontSizesCount );

   applyFontScaleChanges();

   log_line("Done loading fonts.");
   return true;
}

void _applyNewFontToExistingPopups(u32 uFontIdOld, u32 uFontIdNew)
{
   for( int i=0; i<popups_get_count(); i++ )
   {
      Popup* p = popups_get_at(i);
      if ( p && ( p->getFontId() == uFontIdOld) )
         p->setFont(uFontIdNew);
      
   }

   for( int i=0; i<popups_get_topmost_count(); i++ )
   {
      Popup* p = popups_get_topmost_at(i);
      if ( p && ( p->getFontId() == uFontIdOld) )
         p->setFont(uFontIdNew);
   }
}

void applyFontScaleChanges()
{
   log_line("Applying font scale updates...");
   Preferences* pP = get_Preferences();
   float hScreen = hdmi_get_current_resolution_height();

   // Percente of screen height
   float fMenuFontSize = 0.026;
   float fOSDFontSize = 0.024;
   float fOSDScale = osd_getScaleOSD();
   float fOSDStatsScale = osd_getScaleOSDStats();

   // Same scaling must be done in menu render width computation
   if ( NULL != pP )
   {
      if ( pP->iScaleMenus < 0 )
         fMenuFontSize *= (1.0 + 0.15*pP->iScaleMenus);
      if ( pP->iScaleMenus > 0 )
         fMenuFontSize *= (1.0 + 0.1*pP->iScaleMenus);
   }

   log_line("Applying menu font size of: %d pixels (screen height: %d pixels)", (int)(hScreen * fMenuFontSize), (int) hScreen);
   u32 uFontId = 0;
   uFontId = _getBestMatchingFontHeight(s_ListMenuFontSizes, s_iListMenuFontSizesCount, hScreen * fMenuFontSize );
   _applyNewFontToExistingPopups(g_idFontMenu, uFontId);
   g_idFontMenu = uFontId;

   uFontId = _getBestMatchingFontHeight(s_ListMenuFontSizes, s_iListMenuFontSizesCount, hScreen * fMenuFontSize * 0.8 );
   _applyNewFontToExistingPopups(g_idFontMenuSmall, uFontId);
   g_idFontMenuSmall = uFontId;

   uFontId = _getBestMatchingFontHeight(s_ListMenuFontSizes, s_iListMenuFontSizesCount, hScreen * fMenuFontSize * 1.24 );
   _applyNewFontToExistingPopups(g_idFontMenuLarge, uFontId);
   g_idFontMenuLarge = uFontId;
   
   log_line("Applying OSD font size of: %d pixels (screen height: %d pixels)", (int)(hScreen * fOSDFontSize), (int) hScreen);

   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDScale );
   _applyNewFontToExistingPopups(g_idFontOSD, uFontId);
   g_idFontOSD = uFontId;
   
   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDScale * 1.25);
   _applyNewFontToExistingPopups(g_idFontOSDBig, uFontId);
   g_idFontOSDBig = uFontId;
   
   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDScale * 0.74);
   _applyNewFontToExistingPopups(g_idFontOSDSmall, uFontId);
   g_idFontOSDSmall = uFontId;
   
   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDScale * 0.5);
   _applyNewFontToExistingPopups(g_idFontOSDExtraSmall, uFontId);
   g_idFontOSDExtraSmall = uFontId;
   

   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDScale );
   _applyNewFontToExistingPopups(g_idFontOSDWarnings, uFontId);
   g_idFontOSDWarnings = uFontId;
   
   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDStatsScale * 0.8 );
   _applyNewFontToExistingPopups(g_idFontStats, uFontId);
   g_idFontStats = uFontId;
   
   uFontId = _getBestMatchingFontHeight(s_ListOSDFontSizes, s_iListOSDFontSizesCount, hScreen * fOSDFontSize * fOSDStatsScale * 0.8 * 0.75);
   _applyNewFontToExistingPopups(g_idFontStatsSmall, uFontId);
   g_idFontStatsSmall = uFontId;
   
   s_uRenderEngineUIFontIdRegular = g_idFontOSD;
   s_uRenderEngineUIFontIdSmall = g_idFontOSDSmall;
   s_uRenderEngineUIFontIdBig = g_idFontOSDBig;

}