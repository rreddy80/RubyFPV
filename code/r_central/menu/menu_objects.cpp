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

#include "menu_objects.h"
#include "menu_search.h"
#include "menu_item_section.h"

#include "../../base/utils.h"
#include "../../radio/radiolink.h"
#include "../osd/osd_common.h"
#include "menu.h"
#include <math.h>
#include "menu_preferences_buttons.h"
#include "menu_preferences_ui.h"
#include "menu_confirmation.h"
#include "../process_router_messages.h"
#include "../render_commands.h"
#include "../keyboard.h"

#define MENU_ALFA_WHEN_IN_BG 0.5

#define MENU_MARGINS 0.8
// As percentage of current font height


float MENU_TEXTLINE_SPACING = 0.1; // percent of item text height
float MENU_ITEM_SPACING = 0.28;  // percent of item text height

static bool s_bMenuObjectsRenderEndItems = false;

float Menu::m_sfMenuPaddingX = 0.0;
float Menu::m_sfMenuPaddingY = 0.0;
float Menu::m_sfSelectionPaddingX = 0.0;
float Menu::m_sfSelectionPaddingY = 0.0;
float Menu::m_sfScaleFactor = 1.0;

Menu::Menu(int id, const char* title, const char* subTitle)
{
   m_pParent = NULL;
   m_MenuId = id;
   m_MenuDepth = 0;
   m_iColumnsCount = 1;
   m_bEnableColumnSelection = true;
   m_bIsSelectingInsideColumn = false;
   m_bFullWidthSelection = false;
   m_bFirstShow = true;
   m_ItemsCount = 0;
   m_SelectedIndex = 0;
   m_Height = 0.0;
   m_ExtraItemsHeight = 0;
   m_iExtraItemsHeightPositionIndex = -1;
   m_RenderTotalHeight = 0;
   m_RenderHeight = 0;
   m_RenderWidth = 0;
   m_RenderHeaderHeight = 0;
   m_RenderTitleHeight = 0.0;
   m_RenderSubtitleHeight = 0.0;
   m_RenderFooterHeight = 0.0;
   m_RenderTooltipHeight = 0.0;
   m_RenderMaxFooterHeight = 0.0;
   m_RenderMaxTooltipHeight = 0.0;
   m_fRenderScrollBarsWidth = 0.015;

   m_fRenderItemsTotalHeight = 0.0;
   m_fRenderItemsVisibleHeight = 0.0;
   m_fRenderItemsStartYPos = 0.0;
   m_fRenderItemsStopYPos = 0.0;

   m_bEnableScrolling = true;
   m_bHasScrolling = false;
   m_iIndexFirstVisibleItem = 0;
   m_fSelectionWidth = 0;

   m_bDisableStacking = false;
   m_bDisableBackgroundAlpha = false;
   m_fAlfaWhenInBackground = MENU_ALFA_WHEN_IN_BG;
   Preferences* pP = get_Preferences();
   if ( (NULL != pP) && (! pP->iMenusStacked) )
      m_fAlfaWhenInBackground = MENU_ALFA_WHEN_IN_BG*1.1;

   m_szCurrentTooltip[0] = 0;
   m_szTitle[0] = 0;
   m_szSubTitle[0] = 0;
   if ( NULL != title )
      strcpy( m_szTitle, title );
   if ( NULL != subTitle )
      strcpy( m_szSubTitle, subTitle );

   m_TopLinesCount = 0;
   m_bInvalidated = true;
   m_uIconId = 0;
   m_fIconSize = 0.0;

   m_uOnShowTime = g_TimeNow;
   m_uOnChildAddTime = 0;
   m_uOnChildCloseTime = 0;
   m_bHasChildMenuActive = false;
   m_iConfirmationId = 0;
   m_bHasConfirmationOnTop = false;

   m_bIsAnimationInProgress = false;
   m_uAnimationStartTime = 0;
   m_uAnimationLastStepTime = 0;
   m_uAnimationTotalDuration = 200;
   m_fAnimationStartXPos = m_xPos;
   m_fAnimationTargetXPos = -10.0;
   m_RenderXPos = -10.0;
}

Menu::~Menu()
{
   m_bHasChildMenuActive = false;
   removeAllItems();
   removeAllTopLines();
}

float Menu::getMenuPaddingX()
{
   return m_sfMenuPaddingX;
}

float Menu::getMenuPaddingY()
{
   return m_sfMenuPaddingY;
}

float Menu::getSelectionPaddingX()
{
   return m_sfSelectionPaddingX;
}

float Menu::getSelectionPaddingY()
{
   return m_sfSelectionPaddingY;
}

float Menu::getScaleFactor()
{
   return m_sfScaleFactor;
}

void Menu::setParent(Menu* pParent)
{
   m_pParent = pParent;
}

void Menu::invalidate()
{
   valuesToUI();
   m_bInvalidated = true;
   for( int i=0; i<m_ItemsCount; i++ )
      m_pMenuItems[i]->invalidate();
}

void Menu::disableScrolling()
{
   m_bEnableScrolling = false;
   m_bHasScrolling = false;
   invalidate();
}

void Menu::setTitle(const char* szTitle)
{
   if ( NULL == szTitle )
      m_szTitle[0] = 0;
   else
      strcpy(m_szTitle, szTitle);
   m_bInvalidated = true;
}

void Menu::setSubTitle(const char* szSubTitle)
{
   if ( NULL == szSubTitle )
      m_szSubTitle[0] = 0;
   else
      strcpy(m_szSubTitle, szSubTitle);
   m_bInvalidated = true;
}

void Menu::removeAllTopLines()
{
   for( int i=0; i<m_TopLinesCount; i++ )
   {
      if ( NULL != m_szTopLines[i] )
         free(m_szTopLines[i]);
   }
   m_TopLinesCount = 0;
   m_bInvalidated = true;
}

void Menu::addTopLine(const char* szLine, float dx)
{
   if ( m_TopLinesCount >= MENU_MAX_TOP_LINES-1 || NULL == szLine )
      return;
   m_fTopLinesDX[m_TopLinesCount] = dx;
   m_szTopLines[m_TopLinesCount] = (char*)malloc(strlen(szLine)+1);
   strcpy(m_szTopLines[m_TopLinesCount], szLine);

   if ( strlen(m_szTopLines[m_TopLinesCount]) > 1 )
   if ( m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] == 10 || 
        m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] == 13 )
      m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] = 0;
   if ( strlen(m_szTopLines[m_TopLinesCount]) > 1 )
   if ( m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] == 10 || 
        m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] == 13 )
      m_szTopLines[m_TopLinesCount][strlen(m_szTopLines[m_TopLinesCount])-1] = 0;

   m_TopLinesCount++;
   m_bInvalidated = true;
}

void Menu::setColumnsCount(int count)
{
   m_iColumnsCount = count;
}

void Menu::enableColumnSelection(bool bEnable)
{
   m_bEnableColumnSelection = bEnable;
}

void Menu::removeAllItems()
{
   for( int i=0; i<m_ItemsCount; i++ )
      if ( NULL != m_pMenuItems[i] )
         delete m_pMenuItems[i];
   m_ItemsCount = 0;
   m_iIndexFirstVisibleItem = 0;
   m_bInvalidated = true;
}

void Menu::removeMenuItem(MenuItem* pItem)
{
   if ( NULL == pItem )
      return;

   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( m_pMenuItems[i] == pItem )
      {
         for( int k=i; k<m_ItemsCount-1; k++ )
         {
            m_pMenuItems[k] = m_pMenuItems[k+1];
            m_bHasSeparatorAfter[k] = m_bHasSeparatorAfter[k+1];   
         }
         m_ItemsCount--;
         if ( m_SelectedIndex >= m_ItemsCount )
            m_SelectedIndex--;

         m_iIndexFirstVisibleItem = 0;
         m_bInvalidated = true;
         return;
      }
   }
}

int Menu::addMenuItem(MenuItem* item)
{
   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( m_pMenuItems[i] == item )
         return -1;
   }

   m_pMenuItems[m_ItemsCount] = item;
   m_bHasSeparatorAfter[m_ItemsCount] = false;
   m_pMenuItems[m_ItemsCount]->m_pMenu = this;
   m_ItemsCount++;
   m_bInvalidated = true;
   if ( -1 == m_SelectedIndex )
   if ( item->isEnabled() )
      m_SelectedIndex = 0;
   return m_ItemsCount-1;
}

void Menu::addSeparator()
{
   if ( 0 == m_ItemsCount )
      return;
   m_bHasSeparatorAfter[m_ItemsCount-1] = true;
}

int Menu::addSection(const char* szSectionName)
{
   if ( (NULL == szSectionName) || (0 == szSectionName[0]) )
      return -1;
   return addMenuItem(new MenuItemSection(szSectionName));
}

void Menu::setTooltip(const char* szTooltip)
{
   if ( NULL == szTooltip || 0 == szTooltip[0] )
      m_szCurrentTooltip[0] = 0;
   else
      strcpy(m_szCurrentTooltip, szTooltip);
   m_bInvalidated = true;
}

void Menu::setIconId(u32 uIconId)
{
   m_uIconId = uIconId;
   m_bInvalidated = true;
}

void Menu::enableMenuItem(int index, bool enable)
{
   if ( (index < 0) || (index >= m_ItemsCount) )
      return;
   if ( NULL != m_pMenuItems[index] )
        m_pMenuItems[index]->setEnabled(enable);
   m_bInvalidated = true;
}

int Menu::getConfirmationId()
{
   return m_iConfirmationId;
}

u32 Menu::getOnShowTime()
{
   return m_uOnShowTime;
}

u32 Menu::getOnChildAddTime()
{
   return m_uOnChildAddTime;
}

u32 Menu::getOnReturnFromChildTime()
{
   return m_uOnChildCloseTime;
}

float Menu::getRenderWidth()
{
   return m_RenderWidth;
}

float Menu::getRenderXPos()
{
   if ( (m_RenderXPos < -8.0) || m_bDisableStacking )
     m_RenderXPos = m_xPos;
   return m_RenderXPos;
}

void Menu::onShow()
{
   //log_line("Menu [%s] on show (%s): xPos: %.2f, xRenderPos: %.2f", m_szTitle, m_bFirstShow? "first show":"not first show", m_xPos, m_RenderXPos);
   if ( m_bDisableStacking )
      m_RenderXPos = m_xPos;

   m_bFirstShow = false;
   if ( 0 == m_uOnShowTime )
      m_uOnShowTime = g_TimeNow;

   if ( m_MenuId == MENU_ID_SIMPLE_MESSAGE )
   {
      removeAllItems();
      addMenuItem(new MenuItem("OK",""));
   }
   valuesToUI();

   m_SelectedIndex = 0;
   if ( 0 == m_ItemsCount )
      m_SelectedIndex = -1;
   else
   {
      while ( m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) )
         m_SelectedIndex++;
   }
   if ( m_SelectedIndex >= m_ItemsCount )
      m_SelectedIndex = -1;
   onFocusedItemChanged();
   m_bInvalidated = true;
}

bool Menu::periodicLoop()
{
   return false;
}

float Menu::getSelectionWidth()
{
   return m_fSelectionWidth;
}

float Menu::getUsableWidth()
{
   m_fIconSize = 0.0;

   m_RenderWidth = m_Width;
   Preferences* pP = get_Preferences();
   m_sfScaleFactor = 1.0;
   // Same scaling must be done in font scaling computation
   if ( NULL != pP )
   {
      if ( pP->iScaleMenus < 0 )
         m_sfScaleFactor = (1.0 + 0.15*pP->iScaleMenus);
      if ( pP->iScaleMenus > 0 )
         m_sfScaleFactor = (1.0 + 0.1*pP->iScaleMenus);
   }
   m_RenderWidth *= m_sfScaleFactor;
   m_fRenderScrollBarsWidth = 0.015 * m_sfScaleFactor;

   if ( (m_iColumnsCount > 1) && (!m_bEnableColumnSelection) )
      return (m_RenderWidth - m_fIconSize)/(float)(m_iColumnsCount) - 2*m_sfMenuPaddingX;
   else
      return (m_RenderWidth - m_fIconSize - 2*m_sfMenuPaddingX);
}

void Menu::computeRenderSizes()
{
   m_fIconSize = 0.0;

   m_RenderYPos = m_yPos;
   getUsableWidth(); // Computes m_RenderWidth
   m_RenderHeight = m_Height;
   m_RenderTotalHeight = m_RenderHeight;
   m_RenderHeaderHeight = 0.0;
   m_RenderTitleHeight = 0.0;
   m_RenderSubtitleHeight = 0.0;

   m_RenderFooterHeight = 0.0;
   m_RenderTooltipHeight = 0.0;
   m_RenderMaxFooterHeight = 0.0;
   m_RenderMaxTooltipHeight = 0.0;


   m_sfMenuPaddingY = g_pRenderEngine->textHeight(g_idFontMenu) * MENU_MARGINS;
   m_sfMenuPaddingX = 1.2*m_sfMenuPaddingY / g_pRenderEngine->getAspectRatio();

   m_sfSelectionPaddingY = 0.54 * MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
   m_sfSelectionPaddingX = 0.3* m_sfMenuPaddingX;

   if ( 0 != m_szTitle[0] )
   {
      m_RenderTitleHeight = g_pRenderEngine->textHeight(g_idFontMenu);
      m_RenderHeaderHeight += m_RenderTitleHeight;
      m_RenderHeaderHeight += m_sfMenuPaddingY;
   }
   if ( 0 != m_szSubTitle[0] )
   {
      m_RenderSubtitleHeight = g_pRenderEngine->textHeight(g_idFontMenu);
      m_RenderHeaderHeight += m_RenderSubtitleHeight;
      m_RenderHeaderHeight += 0.4*m_sfMenuPaddingY;
   }

   if ( 0 != m_szCurrentTooltip[0] )
   {
       m_RenderTooltipHeight = g_pRenderEngine->getMessageHeight(m_szCurrentTooltip, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenuSmall);
       m_RenderMaxTooltipHeight = m_RenderTooltipHeight;
   }

   m_RenderTotalHeight = m_RenderHeaderHeight;
   m_RenderTotalHeight += 0.5*m_sfMenuPaddingY;

   for (int i = 0; i<m_TopLinesCount; i++)
   {
       if ( 0 == strlen(m_szTopLines[i]) )
          m_RenderTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu)*(1.0 + MENU_TEXTLINE_SPACING);
       else
       {
          m_RenderTotalHeight += g_pRenderEngine->getMessageHeight(m_szTopLines[i], MENU_TEXTLINE_SPACING, getUsableWidth() - m_fTopLinesDX[i], g_idFontMenu);
          m_RenderTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu)*MENU_TEXTLINE_SPACING;
       }
   }

   m_RenderTotalHeight += 0.3*m_sfMenuPaddingY;
   if ( 0 < m_TopLinesCount )
      m_RenderTotalHeight += 0.3*m_sfMenuPaddingY;

   m_fRenderItemsStartYPos = m_RenderYPos + m_RenderTotalHeight;
   
   m_fRenderItemsTotalHeight = m_ExtraItemsHeight;
   m_RenderTotalHeight += m_ExtraItemsHeight;
   
   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( NULL == m_pMenuItems[i] )
         continue;
      float fItemTotalHeight = 0.0;
      if ( m_iColumnsCount < 2 )
      {
         fItemTotalHeight += m_pMenuItems[i]->getItemHeight(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[i] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }
      else if ( (i%m_iColumnsCount) == 0 )
      {
         fItemTotalHeight += m_pMenuItems[i]->getItemHeight(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[i] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }

      m_RenderTotalHeight += fItemTotalHeight;
      m_fRenderItemsTotalHeight += fItemTotalHeight;

      if ( NULL != m_pMenuItems[i]->getTooltip() )
      {
         float fHTooltip = g_pRenderEngine->getMessageHeight(m_pMenuItems[i]->getTooltip(), MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenu);
         if ( fHTooltip > m_RenderMaxTooltipHeight )
            m_RenderMaxTooltipHeight = fHTooltip;
      }
   }
   m_RenderTotalHeight += 0.5*m_sfMenuPaddingY;
   
   m_fRenderItemsVisibleHeight = m_fRenderItemsTotalHeight;
   m_fRenderItemsStopYPos = m_RenderYPos + m_RenderTotalHeight;

   // End: compute items total height and render total height

   m_RenderFooterHeight = 0.6*m_sfMenuPaddingY;
   m_RenderFooterHeight += m_RenderTooltipHeight;
   m_RenderFooterHeight += 0.2*m_sfMenuPaddingY;

   m_RenderMaxFooterHeight = 0.6*m_sfMenuPaddingY;
   m_RenderMaxFooterHeight += m_RenderMaxTooltipHeight;
   m_RenderMaxFooterHeight += 0.2*m_sfMenuPaddingY;

   m_RenderTotalHeight += 0.4*m_sfMenuPaddingY;
   m_RenderTotalHeight += m_RenderMaxFooterHeight;
   
   m_bHasScrolling = false;
   m_RenderHeight = m_RenderTotalHeight;
   

   // Detect if scrolling is needed
    
   if ( m_Height < 0.001 )
   {
       if ( m_bEnableScrolling )
       if ( (m_iColumnsCount < 2) && (m_RenderYPos + m_RenderTotalHeight > 0.9) )
       {
          if ( m_TopLinesCount > 10 )
          {
             float fDelta = m_RenderYPos - 0.1;
             m_RenderYPos -= fDelta;
             m_fRenderItemsStartYPos -= fDelta;
             m_fRenderItemsStopYPos -= fDelta;
          }
          else
             m_bHasScrolling = true;
       }

       float fOverflowHeight = (m_RenderYPos + m_RenderTotalHeight) - 0.9;
          
       // Just move up if possible

       if ( m_bHasScrolling )
       if ( m_RenderYPos >= 0.14 )
       if ( fOverflowHeight <= (m_RenderYPos-0.14) )
       {
          m_RenderYPos -= fOverflowHeight;
          m_fRenderItemsStartYPos -= fOverflowHeight;
          m_fRenderItemsStopYPos -= fOverflowHeight;
          m_bHasScrolling = false;
       }

       // Move up as much as possible

       if ( m_bHasScrolling )
       {
          if ( m_RenderYPos > 0.16 )
          {
             float dMoveUp = m_RenderYPos - 0.16;
             m_RenderYPos -= dMoveUp;
             m_fRenderItemsStartYPos -= dMoveUp;
             m_fRenderItemsStopYPos -= dMoveUp;
             
             fOverflowHeight -= dMoveUp;
          }
          if ( fOverflowHeight <= 0.001 )
             m_bHasScrolling = false;
      }

      if ( m_bHasScrolling )
      {
         // Reduce the menu height to fit the screen and enable scrolling
         float fDeltaH = m_RenderHeight - (0.9 - m_RenderYPos);
         m_RenderHeight -= fDeltaH;
         m_fRenderItemsStopYPos -= fDeltaH;
      }
   }
   else
      m_RenderHeight = m_Height * m_sfScaleFactor;

   m_RenderHeight -= m_RenderMaxFooterHeight;
   m_RenderHeight += m_RenderFooterHeight;
   
   m_fSelectionWidth = 0;

   for( int i=0; i<m_ItemsCount; i++ )
   {
      float w = 0;
      if ( NULL != m_pMenuItems[i] )
      {
         if ( m_pMenuItems[i]->isSelectable() )
            w = m_pMenuItems[i]->getTitleWidth(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         else
            m_pMenuItems[i]->getTitleWidth(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         m_pMenuItems[i]->getValueWidth(getUsableWidth());
      }
      if ( m_fSelectionWidth < w )
         m_fSelectionWidth = w;
   }
   if ( m_bFullWidthSelection )
      m_fSelectionWidth = getUsableWidth();
   m_bInvalidated = false;
}


void Menu::RenderPrepare()
{
   if ( m_bInvalidated )
   {
      computeRenderSizes();
      m_bInvalidated = false;
   }

   if ( (m_RenderXPos < -8.0) || m_bDisableStacking )
      m_RenderXPos = m_xPos;

   // Adjust render x position if needed

   Preferences* pP = get_Preferences();
   if ( (NULL == pP) || (! pP->iMenusStacked) )
      return;

   if ( ! m_bIsAnimationInProgress )
      return;
   
   if ( m_bHasConfirmationOnTop || m_bDisableStacking )
      return;

   if ( menu_has_menu_confirmation_above(this) )
   {
      m_bHasConfirmationOnTop = true;
      return;
   }

   float f = (g_TimeNow-m_uAnimationStartTime)/(float)m_uAnimationTotalDuration;
   if ( f < 0.0 ) f = 0.0;
   if ( f < 1.0 )
      m_RenderXPos = m_fAnimationStartXPos +(m_fAnimationTargetXPos - m_fAnimationStartXPos)*f;
   else
   {
      f = 1.0;
      m_bIsAnimationInProgress = false;
      log_line("Menu: Finished animation for menu id %d", m_MenuId);
      m_RenderXPos = m_fAnimationTargetXPos;
      m_fAnimationTargetXPos = -10.0;
      if ( m_uOnChildCloseTime > m_uOnChildAddTime )
         m_uOnChildCloseTime = 0;
   }
}

void Menu::Render()
{
   RenderPrepare();

   float yTop = RenderFrameAndTitle();
   float yPos = yTop;

   s_bMenuObjectsRenderEndItems = false;
   for( int i=0; i<m_ItemsCount; i++ )
      yPos += RenderItem(i,yPos);

   RenderEnd(yTop);
   g_pRenderEngine->setColors(get_Color_MenuText());
}

void Menu::RenderEnd(float yPos)
{
   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( m_pMenuItems[i]->isEditing() )
      {
         s_bMenuObjectsRenderEndItems = true;
         RenderItem(i,m_pMenuItems[i]->m_RenderLastY, m_pMenuItems[i]->m_RenderLastX - (m_RenderXPos + m_sfMenuPaddingX) - m_pMenuItems[i]->m_fMarginX);
         s_bMenuObjectsRenderEndItems = false;
      }
   }
}

float Menu::RenderFrameAndTitle()
{
   if ( m_bInvalidated )
   {
      computeRenderSizes();
      m_bInvalidated = false;
   }
   static double topTitleBgColor[4] = {90,2,20,0.45};

   g_pRenderEngine->setColors(get_Color_MenuBg());
   g_pRenderEngine->setStroke(get_Color_MenuBorder());

   float fExtraWidth = 0.0;
   if ( m_bEnableScrolling && m_bHasScrolling )
      fExtraWidth = m_fRenderScrollBarsWidth;
   g_pRenderEngine->drawRoundRect(m_RenderXPos, m_RenderYPos, m_RenderWidth + fExtraWidth, m_RenderHeight, MENU_ROUND_MARGIN*m_sfMenuPaddingY);
   g_pRenderEngine->drawLine(m_RenderXPos, m_RenderYPos + m_RenderHeaderHeight, m_RenderXPos + m_RenderWidth + fExtraWidth, m_RenderYPos + m_RenderHeaderHeight);

   g_pRenderEngine->setColors(topTitleBgColor);
   g_pRenderEngine->drawRoundRect(m_RenderXPos, m_RenderYPos, m_RenderWidth + fExtraWidth, m_RenderHeaderHeight, MENU_ROUND_MARGIN*m_sfMenuPaddingY);

   g_pRenderEngine->setColors(get_Color_MenuText());
   g_pRenderEngine->setStrokeSize(MENU_OUTLINEWIDTH);

   float yPos = m_RenderYPos + 0.56*m_sfMenuPaddingY;
   g_pRenderEngine->drawText(m_RenderXPos+m_sfMenuPaddingX, yPos, g_idFontMenu, m_szTitle);
   yPos += m_RenderTitleHeight;

   if ( 0 != m_szSubTitle[0] )
   {
      yPos += 0.4*m_sfMenuPaddingY;
      g_pRenderEngine->drawText(m_RenderXPos+m_sfMenuPaddingX, yPos, g_idFontMenu, m_szSubTitle);
      yPos += m_RenderSubtitleHeight;
   }

   yPos = m_RenderYPos + m_RenderHeaderHeight + m_sfMenuPaddingY;

   for (int i=0; i<m_TopLinesCount; i++)
   {
      if ( 0 == strlen(m_szTopLines[i]) )
        yPos += g_pRenderEngine->textHeight(g_idFontMenu)*(1.0 + MENU_TEXTLINE_SPACING);
      else
      {
         yPos += g_pRenderEngine->drawMessageLines(m_RenderXPos + m_sfMenuPaddingX + m_fIconSize + m_fTopLinesDX[i], yPos, m_szTopLines[i], MENU_TEXTLINE_SPACING, getUsableWidth()-m_fTopLinesDX[i], g_idFontMenu);
         yPos += g_pRenderEngine->textHeight(g_idFontMenu)*MENU_TEXTLINE_SPACING;
      }
   }

   yPos += 0.3*m_sfMenuPaddingY;
   if ( 0 < m_TopLinesCount )
      yPos += 0.3*m_sfMenuPaddingY;

   if ( m_bEnableScrolling && m_bHasScrolling )
   {
      float wPixel = g_pRenderEngine->getPixelWidth();
      float fScrollBarHeight = m_fRenderItemsStopYPos - m_fRenderItemsStartYPos - m_sfMenuPaddingY;
      float fScrollButtonHeight = fScrollBarHeight * (fScrollBarHeight/m_fRenderItemsTotalHeight);
      float xPos = m_RenderXPos + m_RenderWidth + fExtraWidth - fExtraWidth*0.8;
      
      float yButton = 0.0;
      for( int k=0; k<m_iIndexFirstVisibleItem; k++ )
      {
         float fItemTotalHeight = 0.0;
         if ( m_iColumnsCount < 2 )
         {
            fItemTotalHeight += m_pMenuItems[k]->getItemHeight(getUsableWidth() - m_pMenuItems[k]->m_fMarginX);
            fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
            if ( m_bHasSeparatorAfter[k] )
               fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
         }
         else if ( (k%m_iColumnsCount) == 0 )
         {
            fItemTotalHeight += m_pMenuItems[k]->getItemHeight(getUsableWidth() - m_pMenuItems[k]->m_fMarginX);
            fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
            if ( m_bHasSeparatorAfter[k] )
               fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
         }
         yButton += fItemTotalHeight;
      }

      yButton = yPos + yButton*(fScrollBarHeight/m_fRenderItemsTotalHeight);
      
      if ( yButton + fScrollButtonHeight > m_fRenderItemsStopYPos )
         yButton = m_fRenderItemsStopYPos - fScrollButtonHeight;

      double* pC = get_Color_MenuText();
      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStrokeSize(MENU_OUTLINEWIDTH);
      g_pRenderEngine->setFill(pC[0], pC[1], pC[2], 0.18);
      g_pRenderEngine->drawRect( xPos - wPixel*2.0, yPos, wPixel*4.0, fScrollBarHeight);

      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStrokeSize(MENU_OUTLINEWIDTH);
      g_pRenderEngine->setFill(pC[0], pC[1], pC[2], 0.5);
      
      g_pRenderEngine->drawRoundRect(xPos - 0.24*fExtraWidth, yButton, fExtraWidth*0.48, fScrollButtonHeight, MENU_ROUND_MARGIN*m_sfMenuPaddingY);
      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStrokeSize(MENU_OUTLINEWIDTH);
   }

   if ( 0 != m_szCurrentTooltip[0] )
   {
      static double bottomTooltipBgColor[4] = {250,30,50,0.1};
      g_pRenderEngine->setColors(bottomTooltipBgColor);
      g_pRenderEngine->drawRoundRect(m_RenderXPos, m_RenderYPos + m_RenderHeight - m_RenderFooterHeight, m_RenderWidth + fExtraWidth, m_RenderFooterHeight, MENU_ROUND_MARGIN*m_sfMenuPaddingY);

      float yTooltip = m_RenderYPos+m_RenderHeight-m_RenderFooterHeight;
      yTooltip += 0.4 * m_sfMenuPaddingY;

      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->drawMessageLines( m_RenderXPos+m_sfMenuPaddingX, yTooltip, m_szCurrentTooltip, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenuSmall);

      g_pRenderEngine->setColors(get_Color_MenuBg());
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      g_pRenderEngine->drawLine(m_RenderXPos, m_RenderYPos + m_RenderHeight - m_RenderFooterHeight, m_RenderXPos + m_RenderWidth + fExtraWidth, m_RenderYPos + m_RenderHeight - m_RenderFooterHeight);
   }


   bool bShowDebug = false;
   if ( bShowDebug )
   {
      g_pRenderEngine->setColors(get_Color_MenuBg(), 0.0);
      g_pRenderEngine->setStroke(get_Color_IconError());
      g_pRenderEngine->drawRect( m_RenderXPos - 6.0 * g_pRenderEngine->getPixelWidth(),
          yPos - 6.0*g_pRenderEngine->getPixelHeight(),
          m_RenderWidth + 12.0 * g_pRenderEngine->getPixelWidth(),
          m_fRenderItemsTotalHeight  + 12.0*g_pRenderEngine->getPixelHeight()
          );

      double col[4] = {255,255,0,1};
      g_pRenderEngine->setColors(get_Color_MenuBg(), 0.0);
      g_pRenderEngine->setStroke(col);
      g_pRenderEngine->drawRect( m_RenderXPos - 4.0 * g_pRenderEngine->getPixelWidth(),
          yPos - 4.0*g_pRenderEngine->getPixelHeight(),
          m_RenderWidth + 8.0 * g_pRenderEngine->getPixelWidth(),
          m_fRenderItemsVisibleHeight + 8.0*g_pRenderEngine->getPixelHeight()
          );

      double col2[4] = {50,105,255,1};
      g_pRenderEngine->setColors(get_Color_MenuBg(), 0.0);
      g_pRenderEngine->setStroke(col2);
      g_pRenderEngine->drawRect( m_RenderXPos - 2.0 * g_pRenderEngine->getPixelWidth(),
          m_fRenderItemsStartYPos - 2.0*g_pRenderEngine->getPixelHeight(),
          m_RenderWidth + 4.0 * g_pRenderEngine->getPixelWidth(),
          (m_fRenderItemsStopYPos - m_fRenderItemsStartYPos) + 4.0*g_pRenderEngine->getPixelHeight()
          );

      g_pRenderEngine->setColors(get_Color_MenuBg());
      g_pRenderEngine->setStroke(get_Color_MenuText());
   }
   return yPos;
}

float Menu::RenderItem(int index, float yPos, float dx)
{
   if ( NULL == m_pMenuItems[index] )
      return 0.0;

   dx += m_pMenuItems[index]->m_fMarginX;
   m_pMenuItems[index]->setLastRenderPos(m_RenderXPos + m_sfMenuPaddingX + dx, yPos);
   
   if ( m_bHasScrolling )
   if ( index < m_iIndexFirstVisibleItem )
      return 0.0;

   float fHeightFont = g_pRenderEngine->textHeight(g_idFontMenu);
   float hItem = m_pMenuItems[index]->getItemHeight(getUsableWidth() - m_pMenuItems[index]->m_fMarginX);
   
   float fTotalHeight = hItem;
   if ( m_bHasScrolling )
   if ( yPos + fTotalHeight > m_fRenderItemsStopYPos + 0.001 )
      return 0.0;

   fTotalHeight += fHeightFont * MENU_ITEM_SPACING;
   if ( m_bHasSeparatorAfter[index] )
      fTotalHeight += fHeightFont * MENU_SEPARATOR_HEIGHT;

   /*
   if ( m_bEnableScrolling && m_bHasScrolling )
   {
      if ( yPos < m_fRenderItemsStartYPos-0.01 )
         return fTotalHeight;
      if ( yPos + m_pMenuItems[index]->getItemHeight(getUsableWidth() - m_pMenuItems[index]->m_fMarginX) > m_fRenderItemsStopYPos + 0.001 )
         return fTotalHeight;
   }
   */

   bool bShowDebug = false;
   if ( bShowDebug )
   {
      g_pRenderEngine->setColors(get_Color_MenuBg(), 0.0);
      g_pRenderEngine->setStroke(get_Color_IconSucces());
      g_pRenderEngine->drawRect( m_RenderXPos + m_sfMenuPaddingX + dx + g_pRenderEngine->getPixelWidth(),
          yPos + g_pRenderEngine->getPixelHeight(),
          getUsableWidth() - 2.0 * g_pRenderEngine->getPixelWidth(), hItem - 2.0*g_pRenderEngine->getPixelHeight()
          );

      g_pRenderEngine->setColors(get_Color_MenuBg(), 0.0);
      g_pRenderEngine->setStroke(get_Color_IconError());
      g_pRenderEngine->drawRect( m_RenderXPos + m_sfMenuPaddingX + dx + 2.0*g_pRenderEngine->getPixelWidth(),
          yPos + 2.0*g_pRenderEngine->getPixelHeight(),
          m_fSelectionWidth - 4.0 * g_pRenderEngine->getPixelWidth(), hItem - 4.0*g_pRenderEngine->getPixelHeight()
          );

      g_pRenderEngine->setStroke(get_Color_MenuBorder());
   }

   m_pMenuItems[index]->Render(m_RenderXPos + m_sfMenuPaddingX + dx, yPos, index == m_SelectedIndex, m_fSelectionWidth);
   
   if ( m_bHasSeparatorAfter[index] && (!s_bMenuObjectsRenderEndItems) )
   {
      g_pRenderEngine->setColors(get_Color_MenuBg());
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      float yLine = yPos+hItem+0.55*(fHeightFont * MENU_ITEM_SPACING + fHeightFont * MENU_SEPARATOR_HEIGHT);
      g_pRenderEngine->drawLine(m_RenderXPos+m_sfMenuPaddingX+dx, yLine, m_RenderXPos+m_RenderWidth - m_sfMenuPaddingX, yLine);

      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStrokeSize(0);
   }

   g_pRenderEngine->setColors(get_Color_MenuText());

   return fTotalHeight;
}

void Menu::updateScrollingOnSelectionChange()
{
   if ( (! m_bEnableScrolling) || (! m_bHasScrolling) )
      return;

   int countItemsDisabledJustBeforeSelection = 0;
   while ( m_SelectedIndex - countItemsDisabledJustBeforeSelection - 1 >= 0 )
   {
      if ( m_pMenuItems[m_SelectedIndex - countItemsDisabledJustBeforeSelection - 1]->m_bEnabled )
         break;
      countItemsDisabledJustBeforeSelection++;
   }

   if ( m_SelectedIndex < m_iIndexFirstVisibleItem )
   {
      m_iIndexFirstVisibleItem = m_SelectedIndex;
      m_iIndexFirstVisibleItem -= countItemsDisabledJustBeforeSelection;
      if ( m_iIndexFirstVisibleItem < 0 )
         m_iIndexFirstVisibleItem = 0;
      return;
   }

   float fTotalItemsHeight = 0.0;
   for( int i=m_iIndexFirstVisibleItem; i<=m_SelectedIndex; i++ )
   {
      float fItemTotalHeight = 0.0;
      if ( m_iColumnsCount < 2 )
      {
         fItemTotalHeight += m_pMenuItems[i]->getItemHeight(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[i] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }
      else if ( (i%m_iColumnsCount) == 0 )
      {
         fItemTotalHeight += m_pMenuItems[i]->getItemHeight(getUsableWidth() - m_pMenuItems[i]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[i] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }
      if ( -1 != m_iExtraItemsHeightPositionIndex )
      if ( m_iExtraItemsHeightPositionIndex == i )
         fItemTotalHeight += m_ExtraItemsHeight;

      fTotalItemsHeight += fItemTotalHeight;
   }

   // End - computed total items height up to, including, selected item

   // Move displayed items range down untill the selected item becomes visible

   while ( fTotalItemsHeight >= m_fRenderItemsStopYPos - m_fRenderItemsStartYPos - 0.01 * m_sfScaleFactor)
   {
      float fItemTotalHeight = 0.0;
      if ( m_iColumnsCount < 2 )
      {
         fItemTotalHeight += m_pMenuItems[m_iIndexFirstVisibleItem]->getItemHeight(getUsableWidth() - m_pMenuItems[m_iIndexFirstVisibleItem]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[m_iIndexFirstVisibleItem] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }
      else if ( (m_iIndexFirstVisibleItem%m_iColumnsCount) == 0 )
      {
         fItemTotalHeight += m_pMenuItems[m_iIndexFirstVisibleItem]->getItemHeight(getUsableWidth() - m_pMenuItems[m_iIndexFirstVisibleItem]->m_fMarginX);
         fItemTotalHeight += MENU_ITEM_SPACING * g_pRenderEngine->textHeight(g_idFontMenu);
         if ( m_bHasSeparatorAfter[m_iIndexFirstVisibleItem] )
            fItemTotalHeight += g_pRenderEngine->textHeight(g_idFontMenu) * MENU_SEPARATOR_HEIGHT;
      }

      if ( -1 != m_iExtraItemsHeightPositionIndex )
      if ( m_iExtraItemsHeightPositionIndex == m_iIndexFirstVisibleItem )
         fItemTotalHeight += m_ExtraItemsHeight;

      fTotalItemsHeight -= fItemTotalHeight;
      m_iIndexFirstVisibleItem++;
   }
}


int Menu::onBack()
{
   if ( m_MenuId == 0 ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return 1;
   }

   for( int i=0; i<m_ItemsCount; i++ )
      if ( m_pMenuItems[i]->isSelectable() && m_pMenuItems[i]->isEditing() )
      {
         m_pMenuItems[i]->endEdit(true);
         onItemValueChanged(i);
         onItemEndEdit(i);
         return 1;
      }

   if ( m_iColumnsCount > 1 && m_bEnableColumnSelection && m_bIsSelectingInsideColumn )
   {
      m_bIsSelectingInsideColumn = false;
      m_SelectedIndex = ((int)(m_SelectedIndex/m_iColumnsCount))*m_iColumnsCount;
      onFocusedItemChanged();
      return 1;
   }
   return 0;
}

void Menu::onSelectItem()
{
   if ( m_MenuId == MENU_ID_SIMPLE_MESSAGE ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return;
   }
   if ( m_SelectedIndex < 0 || m_SelectedIndex >= m_ItemsCount )
      return;
   if ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() )
      return;

   if ( m_pMenuItems[m_SelectedIndex]->isEditable() )
   {
      MenuItem* pSelectedItem = m_pMenuItems[m_SelectedIndex];
      if ( pSelectedItem->isEditing() )
      {
         pSelectedItem->endEdit(false);
         onItemEndEdit(m_SelectedIndex);
      }
      else
         pSelectedItem->beginEdit();
   }
   else
   {
      if ( m_iColumnsCount > 1 && m_bEnableColumnSelection && (!m_bIsSelectingInsideColumn) )
      {
         m_bIsSelectingInsideColumn = true;
         m_SelectedIndex++;
         onFocusedItemChanged();
      }
      else
         m_pMenuItems[m_SelectedIndex]->onClick();
   }
}


void Menu::onMoveUp(bool bIgnoreReversion)
{
   Preferences* p = get_Preferences();
   if ( m_MenuId == 0 ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return;
   }
   if ( 0 == m_ItemsCount )
      return;

   // Check for edit first
   for( int i=0; i<m_ItemsCount; i++ )
      if ( m_pMenuItems[i]->isSelectable() && m_pMenuItems[i]->isEditing() )
      {
         if ( bIgnoreReversion || p->iSwapUpDownButtonsValues == 0 )
         {
            m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
            if ( isKeyMinusLongPressed() )
               for(int k=0; k<0; k++ )
                  m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
            if ( isKeyMinusLongLongPressed() )
               for(int k=0; k<1; k++ )
                  m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
         }
         else
         {
            m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
            if ( isKeyMinusLongPressed() )
               for(int k=0; k<0; k++ )
                  m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
            if ( isKeyMinusLongLongPressed() )
               for(int k=0; k<1; k++ )
                  m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
         }
         onItemValueChanged(i);
         return;
      }

   // Check if current selected items wants to preprocess up/down/left/right
   if ( m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] && m_pMenuItems[m_SelectedIndex]->isEnabled() )
   if ( m_pMenuItems[m_SelectedIndex]->preProcessKeyUp() )
   {
      onItemValueChanged(m_SelectedIndex);
      return;
   }

   int loopCount = 0;

   if ( m_iColumnsCount > 1 && m_bEnableColumnSelection && m_bIsSelectingInsideColumn )
   {
      int indexStartColumn = 1 + ((int)(m_SelectedIndex/m_iColumnsCount))*m_iColumnsCount;
      int indexEndColumn = indexStartColumn + m_iColumnsCount - 2;
      do
      {
      loopCount++;
      if ( m_SelectedIndex > indexStartColumn )
         m_SelectedIndex--;
      else
         m_SelectedIndex = indexEndColumn;
      }
      while ( (loopCount < (m_iColumnsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
   }
   else
   {
      if ( m_bEnableColumnSelection )
      {
         do
         {
            loopCount++;
            if ( m_SelectedIndex >= m_iColumnsCount )
               m_SelectedIndex -= m_iColumnsCount;
            else
            {
               m_SelectedIndex = m_ItemsCount-1;
               m_SelectedIndex = ((int)(m_SelectedIndex/m_iColumnsCount))*m_iColumnsCount;
            }
         }
         while ( (loopCount < (m_ItemsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
      }
      else
      {
         do
         {
            loopCount++;
            if ( m_SelectedIndex >= 1 )
               m_SelectedIndex--;
            else
               m_SelectedIndex = m_ItemsCount-1;
         }
         while ( (loopCount < (m_ItemsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
      }
   }
   if ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() )
      m_SelectedIndex = -1;
   onFocusedItemChanged();
}

void Menu::onMoveDown(bool bIgnoreReversion)
{
   Preferences* p = get_Preferences();
   if ( m_MenuId == 0 ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return;
   }
   if ( 0 == m_ItemsCount )
      return;

   // Check for edit first

   for( int i=0; i<m_ItemsCount; i++ )
      if ( m_pMenuItems[i]->isSelectable() && m_pMenuItems[i]->isEditing() )
      {
         if ( bIgnoreReversion || p->iSwapUpDownButtonsValues == 0 )
         {
            m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
            if ( isKeyPlusLongPressed() )
               for(int k=0; k<0; k++ )
                  m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
            if ( isKeyPlusLongLongPressed() )
               for(int k=0; k<1; k++ )
                  m_pMenuItems[i]->onKeyDown(bIgnoreReversion);
         }
         else
         {
            m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
            if ( isKeyPlusLongPressed() )
               for(int k=0; k<0; k++ )
                  m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
            if ( isKeyPlusLongLongPressed() )
               for(int k=0; k<1; k++ )
                  m_pMenuItems[i]->onKeyUp(bIgnoreReversion);
         }
         onItemValueChanged(i);
         return;
      }

   // Check if current selected items wants to preprocess up/down/left/right
   if ( m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] && m_pMenuItems[m_SelectedIndex]->isEnabled() )
   if ( m_pMenuItems[m_SelectedIndex]->preProcessKeyDown() )
   {
      onItemValueChanged(m_SelectedIndex);
      return;
   }

   int loopCount = 0;

   if ( m_iColumnsCount > 1 && m_bEnableColumnSelection && m_bIsSelectingInsideColumn )
   {
      int indexStartColumn = 1 + ((int)(m_SelectedIndex/m_iColumnsCount))*m_iColumnsCount;
      int indexEndColumn = indexStartColumn + m_iColumnsCount - 2;
      do
      {
      loopCount++;
      if ( m_SelectedIndex < indexEndColumn )
         m_SelectedIndex++;
      else
         m_SelectedIndex = indexStartColumn;
      }
      while ( (loopCount < (m_iColumnsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
   }
   else
   {
      if ( m_bEnableColumnSelection )
      {
         do
         {
            loopCount++;
            if ( m_SelectedIndex < m_ItemsCount-m_iColumnsCount )
               m_SelectedIndex += m_iColumnsCount;
            else
               m_SelectedIndex = 0;
         }
         while ( (loopCount < (m_ItemsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
      }
      else
      {
         do
         {
            loopCount++;
            if ( m_SelectedIndex < m_ItemsCount-1 )
               m_SelectedIndex++;
            else
               m_SelectedIndex = 0;
         }
         while ( (loopCount < (m_ItemsCount+2)*2) && NULL != m_pMenuItems[m_SelectedIndex] && ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() ) );
      }

   }
   if ( ! m_pMenuItems[m_SelectedIndex]->isSelectable() )
      m_SelectedIndex = -1;
   onFocusedItemChanged();
}

void Menu::onMoveLeft(bool bIgnoreReversion)
{
   if ( m_MenuId == 0 ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return;
   }
   if ( 0 == m_ItemsCount )
      return;

   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( m_pMenuItems[i]->isSelectable() && m_pMenuItems[i]->isEditing() )
      {
         m_pMenuItems[i]->onKeyLeft(bIgnoreReversion);
         onItemValueChanged(i);
         return;
      }
   }

   // Check if current selected items wants to preprocess up/down/left/right
   if ( m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] && m_pMenuItems[m_SelectedIndex]->isEnabled() )
   if ( m_pMenuItems[m_SelectedIndex]->preProcessKeyLeft() )
   {
      onItemValueChanged(m_SelectedIndex);
      return;
   }

}

void Menu::onMoveRight(bool bIgnoreReversion)
{
   if ( m_MenuId == 0 ) // simple message menu? just pop it and return.
   {
      menu_stack_pop();
      return;
   }
   if ( 0 == m_ItemsCount )
      return;

   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( m_pMenuItems[i]->isSelectable() && m_pMenuItems[i]->isEditing() )
      {
         m_pMenuItems[i]->onKeyRight(bIgnoreReversion);
         onItemValueChanged(i);
         return;
      }
   }

   // Check if current selected items wants to preprocess up/down/left/right
   if ( m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] && m_pMenuItems[m_SelectedIndex]->isEnabled() )
   if ( m_pMenuItems[m_SelectedIndex]->preProcessKeyRight() )
   {
      onItemValueChanged(m_SelectedIndex);
      return;
   }

}


void Menu::onFocusedItemChanged()
{
   if ( 0 < m_ItemsCount && m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] )
      setTooltip( m_pMenuItems[m_SelectedIndex]->getTooltip() );

   updateScrollingOnSelectionChange();
}

void Menu::onItemValueChanged(int itemIndex)
{
}

void Menu::onItemEndEdit(int itemIndex)
{
}

void Menu::onReturnFromChild(int returnValue)
{
   if ( ! m_bHasChildMenuActive )
      return;
   m_bHasChildMenuActive = false;
   m_uOnChildCloseTime = g_TimeNow;
   m_uOnChildAddTime = 0;
   m_bInvalidated = true;
   m_bHasConfirmationOnTop = false;
   
   m_bIsAnimationInProgress = false;
   if ( m_RenderXPos != m_xPos )
   {
      Preferences* pP = get_Preferences();
      if ( (NULL == pP) || (! pP->iMenusStacked) )
         return;
      if ( m_bDisableStacking )
         return;

      menu_startAnimationOnChildMenuClosed(this);
   }
}

// It is called just before the new child menu is added to the stack.
// So it's not yet actually present in the stack.

void Menu::onChildMenuAdd()
{
   if ( menu_has_menu_confirmation_above(this) )
      m_bHasConfirmationOnTop = true;

   if ( m_bHasChildMenuActive )
      return;

   m_bHasChildMenuActive = true;
   m_uOnChildAddTime = g_TimeNow;
   m_uOnChildCloseTime = 0;
   m_bInvalidated = true;
   
   if ( m_bHasConfirmationOnTop )
      return;

   // Animate menus
   
   m_bIsAnimationInProgress = false;

   Preferences* pP = get_Preferences();
   if ( (NULL == pP) || (! pP->iMenusStacked) )
      return;

   if ( m_bDisableStacking )
      return;

   menu_startAnimationOnChildMenuAdd(this);
}

void Menu::startAnimationOnChildMenuAdd()
{
   if ( m_RenderXPos < -8.0 )
      m_RenderXPos = m_xPos;

   m_bIsAnimationInProgress = true;
   m_uAnimationStartTime = g_TimeNow;
   m_uAnimationLastStepTime = g_TimeNow;
   m_uAnimationTotalDuration = 280;

   m_fAnimationStartXPos = m_RenderXPos;
   if ( m_fAnimationTargetXPos < -8.0 )
      m_fAnimationTargetXPos = m_RenderXPos;
   m_fAnimationTargetXPos -= m_RenderWidth*0.4;

   log_line("Menu: Start open menu animation for menu id %d, from xpos %f to xpos %f (original pos: %f, render pos now: %f)",
      m_MenuId, m_fAnimationStartXPos, m_fAnimationTargetXPos,
      m_xPos, m_RenderXPos);
}

void Menu::startAnimationOnChildMenuClosed()
{
   if ( m_RenderXPos < -8.0 )
      m_RenderXPos = m_xPos;

   m_bIsAnimationInProgress = true;
   m_uAnimationStartTime = g_TimeNow;
   m_uAnimationLastStepTime = g_TimeNow;
   m_uAnimationTotalDuration = 200;

   m_fAnimationStartXPos = m_RenderXPos;

   if ( menu_is_menu_on_top(this) )
      m_fAnimationTargetXPos = m_xPos;
   else
   {
      if ( m_fAnimationTargetXPos < -8.0 )
         m_fAnimationTargetXPos = m_RenderXPos;
      m_fAnimationTargetXPos += m_RenderWidth*0.4;
   }

   log_line("Menu: Start close menu animation for menu id %d, from xpos %f to xpos %f (original pos: %f, render pos now: %f)",
      m_MenuId, m_fAnimationStartXPos, m_fAnimationTargetXPos,
      m_xPos, m_RenderXPos);

}
     
bool Menu::checkIsArmed()
{
   if ( g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].bGotFCTelemetry )
   if ( g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].headerFCTelemetry.flags & FC_TELE_FLAGS_ARMED )
   {
      Popup* p = new Popup("Your vehicle is armed, you can't perform this operation now. Please stop/disarm your vehicle first.", 0.3, 0.3, 0.5, 6 );
      p->setCentered();
      p->setIconId(g_idIconError, get_Color_IconError());
      popups_add_topmost(p);
      return true;
   }
   return false;
}

void Menu::addMessageWithTitle(const char* szTitle, const char* szMessage)
{
   m_iConfirmationId = 0;
   Menu* pm = new Menu(MENU_ID_SIMPLE_MESSAGE,szTitle,NULL);
   pm->m_xPos = 0.32; pm->m_yPos = 0.4;
   pm->m_Width = 0.36;
   pm->addTopLine(szMessage);
   add_menu_to_stack(pm); 
}

void Menu::addMessage(const char* szMessage)
{
   m_iConfirmationId = 0;
   Menu* pm = new Menu(MENU_ID_SIMPLE_MESSAGE,"Info",NULL);
   pm->m_xPos = 0.32; pm->m_yPos = 0.4;
   pm->m_Width = 0.36;
   pm->addTopLine(szMessage);
   add_menu_to_stack(pm);
}

void Menu::addMessage2(const char* szMessage, const char* szLine2)
{
   m_iConfirmationId = 0;
   Menu* pm = new Menu(MENU_ID_SIMPLE_MESSAGE,"Info",NULL);
   pm->m_xPos = 0.32; pm->m_yPos = 0.4;
   pm->m_Width = 0.36;
   pm->addTopLine(szMessage);
   pm->addTopLine(szLine2);
   add_menu_to_stack(pm);
}

bool Menu::checkCancelUpload()
{
   int iCount = 5;
   bool bBackPressed = false;
   while ( iCount > 0 )
   {
      int iEvent = keyboard_get_next_input_event();
      if ( iEvent == 0 )
         break;
      if ( iEvent == INPUT_EVENT_PRESS_BACK )
         bBackPressed = true; 
      iCount--;
   }
   
   if ( ! bBackPressed )
      return false;

   g_bUpdateInProgress = false;
   log_line("The software update was canceled by user.");
   hardware_sleep_ms(50);
   addMessage("Update was canceled.");
   g_nFailedOTAUpdates++;
   return true;
}

bool Menu::uploadSoftware()
{
   ruby_pause_watchdog();
   g_bUpdateInProgress = true;
   render_commands_set_progress_percent(0, true);
   g_pRenderEngine->startFrame();
   popups_render();
   render_commands();
   popups_render_topmost();
   g_pRenderEngine->endFrame();


   // See what we have available to upload:
   // Either the latest update zip if present, either the binaries present on the controller
   // If we had failed uploads, use the binaries on the controller

   char szComm[256];
   char szBuff[1024];
   char szArchiveToUpload[256];
   szArchiveToUpload[0] = 0;
   int iUpdateType = -1;

   //bool bForceUseBinaries = false;
   //if ( (((g_pCurrentModel->sw_version)>>8) & 0xFF) == 6 )
   //if ( ((g_pCurrentModel->sw_version) & 0xFF) < 30 )
   //   bForceUseBinaries = true;
   bool bForceUseBinaries = true;

   if ( bForceUseBinaries )
      log_line("Using controller binaries for update.");
   else
   {
      if ( (g_nFailedOTAUpdates == 0) && (g_nSucceededOTAUpdates == 0) )
      {
         sprintf(szComm, "find updates/ruby_update_%d.%d.zip 2>/dev/null", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR/10);
         hw_execute_bash_command(szComm, szBuff);
         if ( 0 < strlen(szBuff) && NULL != strstr(szBuff, "ruby_update") )
         {
            log_line("Found zip update archive to upload to vehicle: [%s]", szBuff);
            strcpy(szArchiveToUpload, szBuff);
            iUpdateType = 0;
         }
         else
            log_line("Zip update archive to upload to vehicle not found. Using regular update of binary files.");
      }
      else
         log_line("There are previous updates done or failed. Using regular update of binary files.");
   }

   // Always will do this
   if ( iUpdateType == -1 )
   {
      strcpy(szArchiveToUpload, "tmp/sw.tar");
      iUpdateType = 1;

      // Add update info file
      if( access( FILE_INFO_LAST_UPDATE, R_OK ) == -1 )
      {
         sprintf(szComm, "cp %s %s 2>/dev/null", FILE_INFO_VERSION, FILE_INFO_LAST_UPDATE);
         hw_execute_bash_command(szComm, NULL);
      }

      sprintf(szComm, "rm -rf %s 2>/dev/null", szArchiveToUpload);
      hw_execute_bash_command(szComm, NULL);

      g_TimeNow = get_current_timestamp_ms();
      ruby_signal_alive();

      hw_execute_bash_command("cp -rf ruby_update ruby_update_vehicle", NULL);
      hw_execute_bash_command("chmod 777 ruby_update_vehicle", NULL);
      sprintf(szComm, "tar -czf %s ruby_* 2>&1", szArchiveToUpload);
      hw_execute_bash_command(szComm, NULL);
      g_TimeNow = get_current_timestamp_ms();
      ruby_signal_alive();

      log_line("Save last generated update archive...");
      hw_execute_bash_command("rm -rf last_uploaded_archive.tar 2>&1", NULL);
      sprintf(szComm, "cp -rf %s last_uploaded_archive.tar", szArchiveToUpload);
      hw_execute_bash_command(szComm, NULL);
      hw_execute_bash_command("chmod 777 last_uploaded_archive.tar 2>&1", NULL);
   }
   log_line("Generated update archive to upload to vehicle.");

   if ( ! _uploadVehicleUpdate(iUpdateType, szArchiveToUpload) )
   {
      ruby_resume_watchdog();
      g_bUpdateInProgress = false;
      addMessage("There was an error updating your vehicle.");
      return false;
   }

   g_nSucceededOTAUpdates++;
   render_commands_set_progress_percent(-1, true);
   log_line("Successfully sent software package to vehicle.");

   if ( NULL != g_pCurrentModel )
   {
      for( int i=0; i<4; i++ )
      {
         hardware_sleep_ms(500);
         ruby_signal_alive();
      }
      g_pCurrentModel->b_mustSyncFromVehicle = true;
      g_pCurrentModel->sw_version = (SYSTEM_SW_VERSION_MAJOR*256+SYSTEM_SW_VERSION_MINOR) | (SYSTEM_SW_BUILD_NUMBER << 16);
      saveControllerModel(g_pCurrentModel);
   }

   send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);
   ruby_resume_watchdog();
   g_bUpdateInProgress = false;

   return true;
}

MenuItemSelect* Menu::createMenuItemCardModelSelector(const char* szName)
{
   MenuItemSelect* pItem = new MenuItemSelect(szName, "Sets the radio interface model.");
   pItem->addSelection("Generic");
   for( int i=1; i<50; i++ )
   {
      const char* szDesc = str_get_radio_card_model_string(i);
      if ( NULL != szDesc && 0 != szDesc[0] )
      if ( NULL == strstr(szDesc, "NONAME") )
      if ( NULL == strstr(szDesc, "Generic") )
         pItem->addSelection(szDesc);
   }
   pItem->setIsEditable();
   return pItem;
}

bool Menu::_uploadVehicleUpdate(int iUpdateType, const char* szArchiveToUpload)
{
   command_packet_sw_package cpswp_cancel;
   cpswp_cancel.type = iUpdateType;
   cpswp_cancel.total_size = 0;
   cpswp_cancel.file_block_index = MAX_U32;
   cpswp_cancel.last_block = false;
   cpswp_cancel.block_length = 0;

   g_bUpdateInProgress = true;

   long lSize = 0;
   FILE* fd = fopen(szArchiveToUpload, "rb");
   if ( NULL != fd )
   {
      fseek(fd, 0, SEEK_END);
      lSize = ftell(fd);
      fseek(fd, 0, SEEK_SET);
      fclose(fd);
   }

   log_line("Sending to vehicle the update archive (method 6.3): [%s], size: %d bytes", szArchiveToUpload, (int)lSize);

   fd = fopen(szArchiveToUpload, "rb");
   if ( NULL == fd )
   {
      addMessage("There was an error generating the software package.");
      g_bUpdateInProgress = false;
      return false;
   }

   u32 blockSize = 1100;
   u32 nPackets = ((u32)lSize) / blockSize;
   if ( lSize > nPackets * blockSize )
      nPackets++;

   u8** pPackets = (u8**) malloc(nPackets*sizeof(u8*));
   if ( NULL == pPackets )
   {
      addMessage("There was an error generating the upload package.");
      g_bUpdateInProgress = false;
      return false;
   }

   // Read packets in memory

   u32 nTotalPackets = 0;
   for( long l=0; l<lSize; )
   {
      u8* pPacket = (u8*) malloc(1500);
      if ( NULL == pPacket )
      {
         //free(pPackets);
         addMessage("There was an error generating the upload package.");
         g_bUpdateInProgress = false;
         return false;
      }
      command_packet_sw_package* pcpsp = (command_packet_sw_package*)pPacket;

      int nRead = fread(pPacket+sizeof(command_packet_sw_package), 1, blockSize, fd);
      if ( nRead < 0 )
      {
         //free((u8*)pPackets);
         addMessage("There was an error generating the upload package.");
         g_bUpdateInProgress = false;
         return false;
      }

      pcpsp->block_length = nRead;
      l += nRead;
      pcpsp->total_size = (u32)lSize;
      pcpsp->file_block_index = nTotalPackets;
      pcpsp->last_block = ((l == lSize)?true:false);
      pcpsp->type = iUpdateType;
      pPackets[nTotalPackets] = pPacket;
      nTotalPackets++;
   }

   log_line("Uploading %d sw segments", nTotalPackets);
   ruby_signal_alive();

   send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STARTED,0);

   g_TimeNow = get_current_timestamp_ms();
   g_TimeNowMicros = get_current_timestamp_micros();
   u32 uTimeLastRender = 0;

   int iLastAcknowledgedPacket = -1;
   int iPacketToSend = 0;
   int iCountMaxRetriesForCurrentSegments = 10;
   while ( iPacketToSend < nTotalPackets )
   {
      u8* pPacket = pPackets[iPacketToSend];
      command_packet_sw_package* pcpsp = (command_packet_sw_package*)pPacket;

      bool bWaitAck = true;
      if ( (! pcpsp->last_block) && ((iPacketToSend % DEFAULT_UPLOAD_PACKET_CONFIRMATION_FREQUENCY) !=0 ) )
         bWaitAck = false;
      
      if ( ! bWaitAck )
      {
         log_line("Send sw package block %d of %d", iPacketToSend + 1, nTotalPackets);

         g_TimeNow = get_current_timestamp_ms();
         g_TimeNowMicros = get_current_timestamp_micros();
         ruby_signal_alive();

         for( int k=0; k<2; k++ )
            handle_commands_send_single_oneway_command(0, COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, bWaitAck, pPacket, pcpsp->block_length+sizeof(command_packet_sw_package));
         hardware_sleep_ms(2);
         iPacketToSend++;
         continue;
      }

      u32 commandUID = handle_commands_increment_command_counter();
      u8 resendCounter = 0;
      int waitReplyTime = 60;
      bool gotResponse = false;
      bool responseOk = false;

      // Send packet and wait for acknoledge

      if ( iPacketToSend == nTotalPackets - 1 )
         log_line("Send last sw package with ack, segment %d of %d", iPacketToSend + 1, nTotalPackets);
      else
         log_line("Send sw package with ack, segment %d of %d", iPacketToSend + 1, nTotalPackets);

      do
      {
         g_TimeNow = get_current_timestamp_ms();
         g_TimeNowMicros = get_current_timestamp_micros();
         ruby_signal_alive();

         if ( ! handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, resendCounter, bWaitAck, pPacket, pcpsp->block_length+sizeof(command_packet_sw_package)) )
         {
            addMessage("There was an error uploading the software package.");
            fclose(fd);
            g_nFailedOTAUpdates++;
            for( int i=0; i<5; i++ )
            {
               handle_commands_increment_command_counter();
               handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, 0, 0, (u8*)&cpswp_cancel, sizeof(command_packet_sw_package));
               hardware_sleep_ms(20);
            }
            send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);
            //for( int i=0; i<nTotalPackets; i++ )
            //   free((u8*)pPackets[i]);
            //free((u8*)pPackets);
            g_bUpdateInProgress = false;
            return false;
         }

         resendCounter++;
         gotResponse = false;
         u32 timeToWaitReply = g_TimeNow + waitReplyTime;
         log_line("Waiting for ACK for SW package segment %d, for %d ms, on retry: %d", iPacketToSend + 1, waitReplyTime, resendCounter-1);

         while ( g_TimeNow < timeToWaitReply && (! gotResponse) )
         {
            if ( checkCancelUpload() )
            {
               fclose(fd);
               for( int i=0; i<5; i++ )
               {
                  handle_commands_increment_command_counter();
                  handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, 0, 0, (u8*)&cpswp_cancel, sizeof(command_packet_sw_package));
                  hardware_sleep_ms(20);
               }
               send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);
               //for( int i=0; i<nTotalPackets; i++ )
               //   free((u8*)pPackets[i]);
               //free((u8*)pPackets);
               g_bUpdateInProgress = false;
               return false;
            }

            g_TimeNow = get_current_timestamp_ms();
            g_TimeNowMicros = get_current_timestamp_micros();
            if ( try_read_messages_from_router(waitReplyTime) )
            {
               if ( handle_commands_get_last_command_id_response_received() == commandUID )
               {
                  gotResponse = true;
                  responseOk = handle_commands_last_command_succeeded();
                  log_line("Did got an ACK. Succeded: %s", responseOk?"yes":"no");
                  break;
               }
            }
            log_line("Did not get an ACK from vehicle. Keep waiting...");
         } // finised waiting for ACK

         if ( ! gotResponse )
         {
            log_line("Did not get an ACK from vehicle. Increase wait time.");
            waitReplyTime += 50;
            if ( waitReplyTime > 500 )
               waitReplyTime = 500;
         }
      }
      while ( (resendCounter < 15) && (! gotResponse) );

      // Finished sending and waiting for packet with ACK wait

      g_TimeNow = get_current_timestamp_ms();
      g_TimeNowMicros = get_current_timestamp_micros();
      ruby_signal_alive();

      if ( ! gotResponse )
      {
         log_softerror_and_alarm("Did not get a confirmation from vehicle about the software upload.");
         g_nFailedOTAUpdates++;
         fclose(fd);
         for( int i=0; i<5; i++ )
         {
            handle_commands_increment_command_counter();
            handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, 0, 0, (u8*)&cpswp_cancel, sizeof(command_packet_sw_package));
            hardware_sleep_ms(20);
         }
         send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);

         //for( int i=0; i<nTotalPackets; i++ )
         //   free((u8*)pPackets[i]);
         //free((u8*)pPackets);
         g_bUpdateInProgress = false;
         return false;
      }

      if ( gotResponse && (!responseOk) )
      {
         // Restart from last confirmed packet
         log_softerror_and_alarm("The software package block (segment index %d) was rejected by the vehicle.", iPacketToSend+1);
         iPacketToSend = iLastAcknowledgedPacket; // will be +1 down below
         iCountMaxRetriesForCurrentSegments--;
         if ( iCountMaxRetriesForCurrentSegments < 0 )
         {
            g_nFailedOTAUpdates++;
            fclose(fd);
            for( int i=0; i<5; i++ )
            {
               handle_commands_increment_command_counter();
               handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, 0, 0, (u8*)&cpswp_cancel, sizeof(command_packet_sw_package));
               hardware_sleep_ms(20);
            }
            send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);

            g_bUpdateInProgress = false;
            return false;
         }
      }

      if ( gotResponse && responseOk )
      {
         iCountMaxRetriesForCurrentSegments = 10;
         iLastAcknowledgedPacket = iPacketToSend;
         log_line("Got ACK for segment %d", iPacketToSend+1);
      }
      int percent = pcpsp->file_block_index*100/(pcpsp->total_size/blockSize);

      if ( checkCancelUpload() )
      {
         fclose(fd);
         for( int i=0; i<5; i++ )
         {
            handle_commands_increment_command_counter();
            handle_commands_send_single_command_to_vehicle(COMMAND_ID_UPLOAD_SW_TO_VEHICLE63, 0, 0, (u8*)&cpswp_cancel, sizeof(command_packet_sw_package));
            hardware_sleep_ms(20);
         }
         send_control_message_to_router(PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED,0);
         //for( int i=0; i<nTotalPackets; i++ )
         //   free((u8*)pPackets[i]);
         //free((u8*)pPackets);
         g_bUpdateInProgress = false;
         return false;
      }

      if ( g_TimeNow > (uTimeLastRender+100) )
      {
         uTimeLastRender = g_TimeNow;
         render_commands_set_progress_percent(percent, true);
         g_pRenderEngine->startFrame();
         //osd_render();
         popups_render();
         //menu_render();
         render_commands();
         popups_render_topmost();
         g_pRenderEngine->endFrame();
      }
      iPacketToSend++;
   }

   //for( int i=0; i<nTotalPackets; i++ )
   //   free((u8*)pPackets[i]);
   //free((u8*)pPackets);

   g_bUpdateInProgress = false;
   return true;
}

void Menu::addMessageNeedsVehcile(const char* szMessage, int iConfirmationId)
{
   m_iConfirmationId = iConfirmationId;
   Menu* pm = new Menu(MENU_ID_SIMPLE_MESSAGE,"Not connected",NULL);
   pm->m_xPos = 0.4; pm->m_yPos = 0.4;
   pm->m_Width = 0.36;
   pm->addTopLine(szMessage);
   add_menu_to_stack(pm);
}

void Menu::addMessageVideoBitrate(Model* pModel)
{
   if ( menu_has_menu(MENU_ID_SIMPLE_MESSAGE) )
      return;
   if ( NULL == pModel )
      return;

   int iProfile = pModel->video_params.user_selected_video_link_profile;
   u32 uMaxVideoRate = utils_get_max_allowed_video_bitrate_for_profile(pModel, iProfile);
   if ( pModel->video_link_profiles[iProfile].bitrate_fixed_bps <= uMaxVideoRate )
      return;

   Menu* pm = new Menu(MENU_ID_SIMPLE_MESSAGE,"Video Bitrate Warning",NULL);
   pm->m_xPos = m_xPos-0.05; pm->m_yPos = m_yPos+0.05;
   pm->m_Width = 0.5;
   
   char szLine1[256];
   char szLine2[256];
   char szBRVideo[256];
   char szBRRadio[256];

   str_format_bitrate(pModel->video_link_profiles[iProfile].bitrate_fixed_bps, szBRVideo);
   str_format_bitrate(uMaxVideoRate, szBRRadio);
   
   sprintf(szLine1, "Your current video bitrate of %s is bigger than %d%% of your current radio links datarates capacity.",
       szBRVideo, DEFAULT_VIDEO_LINK_MAX_LOAD_PERCENT);
   strcpy(szLine2, "Lower your set video bitrate or increase the radio datarates on your radio links, otherways you will experience delays in the video stream.");
   
   // If a custom data rate was set for this video profile and it's too small, show warning

   if ( 0 != pModel->video_link_profiles[iProfile].radio_datarate_video_bps )
   {
      uMaxVideoRate = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[iProfile].radio_datarate_video_bps);
      if ( uMaxVideoRate < pModel->video_link_profiles[iProfile].bitrate_fixed_bps )
      {
          str_format_bitrate(uMaxVideoRate, szBRRadio);
          sprintf(szLine1, "You set a custom radio datarate for this video profile of %s which is smaller than what is optimum for your desired video bitrate %s.", szBRRadio, szBRVideo );
          sprintf(szLine2, "Disable the custom radio datarate for this video profile or decrease the desired video bitrate. Should be lower than %d%% of the set radio datarate.", DEFAULT_VIDEO_LINK_MAX_LOAD_PERCENT);
      }
   }

   /*
/////////////////////////
   // First get the maximum radio datarate set on radio links

   for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
   {
      if ( ! (pModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY) )
      if ( getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i]) < 2000000)
         continue;

      if ( 0 == uMaxRadioDataRateBPS )
         uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->radioLinksParams.link_datarate_video_bps[i]);

      if ( 0 == iMaxRadioDataRate )
         iMaxRadioDataRate = pModel->radioLinksParams.link_datarate_video_bps[i];
   }

   // If the video profile has a set radio datarate, use it

   if ( 0 != pModel->video_link_profiles[iProfile].radio_datarate_video_bps )
   {
      uMaxRadioDataRateBPS = getRealDataRateFromRadioDataRate(pModel->video_link_profiles[iProfile].radio_datarate_video_bps);
      iMaxRadioDataRate = pModel->video_link_profiles[iProfile].radio_datarate_video_bps;
   }
   */
///////////////////
   
   pm->addTopLine(szLine1);
   pm->addTopLine(szLine2);
   
   pm->m_fAlfaWhenInBackground = 1.0;
   add_menu_to_stack(pm);
}
