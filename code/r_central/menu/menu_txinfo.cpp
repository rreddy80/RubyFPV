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

#include "menu.h"
#include "menu_txinfo.h"
#include "menu_objects.h"
#include "menu_item_text.h"
#include "menu_confirmation.h"
#include "menu_tx_power_max.h"

MenuTXInfo::MenuTXInfo()
:Menu(MENU_ID_TXINFO, "Radio Output Power Levels", NULL)
{
   m_Width = 0.72;
   m_Height = 0.61;
   m_xPos = 0.05;
   m_yPos = 0.16;
   float fSliderWidth = 0.18;

   m_xTable = m_RenderXPos + m_sfMenuPaddingY;
   m_xTable += 0.15*m_sfScaleFactor;
   m_xTableCellWidth = 0.05*m_sfScaleFactor;

   ControllerSettings* pCS = get_ControllerSettings();

   m_bSelectSecond = false;
   m_bShowThinLine = false;
   
   m_bShowVehicle = true;
   m_bShowController = true;
   
   m_bValuesChangedVehicle = false;
   m_bValuesChangedController = false;
}

MenuTXInfo::~MenuTXInfo()
{
}

void MenuTXInfo::onShow()
{
   float fSliderWidth = 0.18;
   ControllerSettings* pCS = get_ControllerSettings();
 
   m_bShowBothOnController = false;
   m_bShowBothOnVehicle = false;
   m_bDisplay24Cards = false;
   m_bDisplay58Cards = false;

   m_bControllerHas24Cards = false;
   m_bControllerHas58Cards = false;
   m_bVehicleHas24Cards = false;
   m_bVehicleHas58Cards = false;

   for( int n=0; n<hardware_get_radio_interfaces_count(); n++ )
   {
      radio_hw_info_t* pNIC = hardware_get_radio_info(n);
      if ( NULL == pNIC )
         continue;
      if ( (pNIC->typeAndDriver & 0xFF) == RADIO_TYPE_ATHEROS )
         m_bControllerHas24Cards = true;
      if ( (pNIC->typeAndDriver & 0xFF) == RADIO_TYPE_RALINK )
         m_bControllerHas24Cards = true;
      else
         m_bControllerHas58Cards = true;
   }

   if ( m_bControllerHas24Cards && m_bControllerHas58Cards )
      m_bShowBothOnController = true;

   if ( NULL != g_pCurrentModel )
   {
      for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( ((g_pCurrentModel->radioInterfacesParams.interface_type_and_driver[i] & 0xFF00) >> 8) == RADIO_HW_DRIVER_ATHEROS )
            m_bVehicleHas24Cards = true;
         if ( ((g_pCurrentModel->radioInterfacesParams.interface_type_and_driver[i] & 0xFF00) >> 8) == RADIO_HW_DRIVER_RALINK )
            m_bVehicleHas24Cards = true;
         else
            m_bVehicleHas58Cards = true;
      }
   }

   if ( m_bVehicleHas24Cards && m_bVehicleHas58Cards )
      m_bShowBothOnVehicle = true;

   if ( m_bShowVehicle && m_bVehicleHas24Cards )
      m_bDisplay24Cards = true;
   if ( m_bShowController && m_bControllerHas24Cards )
      m_bDisplay24Cards = true;
   if ( m_bShowVehicle && m_bVehicleHas58Cards )
      m_bDisplay58Cards = true;
   if ( m_bShowController && m_bControllerHas58Cards )
      m_bDisplay58Cards = true;

   removeAllItems();

   m_IndexPowerController = -1;
   m_IndexPowerControllerAtheros = -1;
   m_IndexPowerControllerRTL = -1;

   m_IndexPowerVehicle = -1;
   m_IndexPowerVehicleAtheros = -1;
   m_IndexPowerVehicleRTL = -1;

   if ( m_bShowVehicle && (NULL != g_pCurrentModel) )
   {
      if ( ! m_bShowBothOnVehicle )
      {
         if ( m_bVehicleHas24Cards )
            m_pItemsSlider[0] = new MenuItemSlider("Vehicle Tx Power", "Sets the radio TX power used on the vehicle. Requires a reboot of the vehicle after change.", 1, g_pCurrentModel->radioInterfacesParams.txMaxPowerAtheros, g_pCurrentModel->radioInterfacesParams.txMaxPowerAtheros/2, fSliderWidth);
         else
            m_pItemsSlider[0] = new MenuItemSlider("Vehicle Tx Power", "Sets the radio TX power used on the vehicle. Requires a reboot of the vehicle after change.", 1, g_pCurrentModel->radioInterfacesParams.txMaxPowerRTL, g_pCurrentModel->radioInterfacesParams.txMaxPowerRTL/2, fSliderWidth);
         m_IndexPowerVehicle = addMenuItem(m_pItemsSlider[0]);
      }
      else
      {
         char szBuff[256];
         char szCards[256];

         szCards[0] = 0;
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( ((g_pCurrentModel->radioInterfacesParams.interface_type_and_driver[i] & 0xFF00) >> 8) == RADIO_HW_DRIVER_ATHEROS )
               strcat(szCards, " Atheros");
            if ( ((g_pCurrentModel->radioInterfacesParams.interface_type_and_driver[i] & 0xFF00) >> 8) == RADIO_HW_DRIVER_RALINK )
               strcat(szCards, " RaLink");
         }

         sprintf(szBuff, "Vehicle Tx Power (2.4Ghz%s)", szCards);
         m_pItemsSlider[1] = new MenuItemSlider(szBuff, "Sets the radio TX power used on the vehicle for Atheros/RaLink cards. Requires a reboot of the vehicle after change.", 1, g_pCurrentModel->radioInterfacesParams.txMaxPowerAtheros, g_pCurrentModel->radioInterfacesParams.txMaxPowerAtheros/2, fSliderWidth);
         m_IndexPowerVehicleAtheros = addMenuItem(m_pItemsSlider[1]);
         m_pItemsSlider[2] = new MenuItemSlider("Vehicle Tx Power (5.8Ghz)", "Sets the radio TX power used on the vehicle for RTL cards. Requires a reboot of the vehicle after change.", 1, g_pCurrentModel->radioInterfacesParams.txMaxPowerRTL, g_pCurrentModel->radioInterfacesParams.txMaxPowerRTL/2, fSliderWidth);
         m_IndexPowerVehicleRTL = addMenuItem(m_pItemsSlider[2]);
      }
   }

   if ( m_bShowController )
   {
      if ( ! m_bShowBothOnController )
      {
         if ( m_bControllerHas24Cards )
            m_pItemsSlider[5] = new MenuItemSlider("Controller Tx Power", "Sets the radio TX power used on the controller. Requires a reboot of the controller after change.", 1, pCS->iMaxTXPowerAtheros, pCS->iMaxTXPowerAtheros/2, fSliderWidth);
         else
            m_pItemsSlider[5] = new MenuItemSlider("Controller Tx Power", "Sets the radio TX power used on the controller. Requires a reboot of the controller after change.", 1, pCS->iMaxTXPowerRTL, pCS->iMaxTXPowerRTL/2, fSliderWidth);
         m_IndexPowerController = addMenuItem(m_pItemsSlider[5]);
      }
      else
      {
         char szBuff[256];
         char szCards[256];

         szCards[0] = 0;
         for( int n=0; n<hardware_get_radio_interfaces_count(); n++ )
         {
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(n);
            if ( NULL == pRadioHWInfo )
               continue;
            if ( (pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_ATHEROS )
               strcat(szCards, " Atheros");
            if ( (pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_RALINK )
               strcat(szCards, " RaLink");
         }

         sprintf(szBuff, "Controller Tx Power (2.4Ghz%s)", szCards);
         m_pItemsSlider[6] = new MenuItemSlider(szBuff, "Sets the radio TX power used on the controller for Atheros/RaLink cards. Requires a reboot of the controller after change.", 1, pCS->iMaxTXPowerAtheros, pCS->iMaxTXPowerAtheros/2, fSliderWidth);
         m_IndexPowerControllerAtheros = addMenuItem(m_pItemsSlider[6]);
         m_pItemsSlider[7] = new MenuItemSlider("Controller Tx Power (5.8Ghz)", "Sets the radio TX power used on the controller for RTL cards. Requires a reboot of the controller after change.", 1, pCS->iMaxTXPowerRTL, pCS->iMaxTXPowerRTL/2, fSliderWidth);
         m_IndexPowerControllerRTL = addMenuItem(m_pItemsSlider[7]);
      }
   }
   m_IndexPowerMax = addMenuItem(new MenuItem("Limit Maximum Radio Power Levels"));
   m_pMenuItems[m_IndexPowerMax]->showArrow();

   m_pItemsSelect[0] = new MenuItemSelect("Show All Card Types", "Shows all card types in the table below, not just the ones detected as present.");  
   m_pItemsSelect[0]->addSelection("No");
   m_pItemsSelect[0]->addSelection("Yes");
   m_pItemsSelect[0]->setIsEditable();
   m_IndexShowAllCards = addMenuItem(m_pItemsSelect[0]);

   addMenuItem( new MenuItemText("Here is a table with aproximative ouput power levels for different cards:"));

   bool bFirstShow = m_bFirstShow;
   Menu::onShow();

   if ( bFirstShow && m_bSelectSecond )
      m_SelectedIndex = 1;
} 
      
void MenuTXInfo::valuesToUI()
{
   m_ExtraItemsHeight = 0;
   ControllerSettings* pCS = get_ControllerSettings();
   Preferences* pP = get_Preferences();

   m_pItemsSelect[0]->setSelectedIndex(1 - pP->iShowOnlyPresentTxPowerCards);

   if ( m_bShowController )
   {
      if ( -1 != m_IndexPowerController )
      {
         if ( m_bControllerHas24Cards )
            m_pItemsSlider[5]->setCurrentValue(pCS->iTXPowerAtheros);
         if ( m_bControllerHas58Cards )
            m_pItemsSlider[5]->setCurrentValue(pCS->iTXPowerRTL);
      }
      if ( -1 != m_IndexPowerControllerAtheros && m_bControllerHas24Cards )
         m_pItemsSlider[6]->setCurrentValue(pCS->iTXPowerAtheros);
      if ( -1 != m_IndexPowerControllerRTL && m_bControllerHas58Cards )
         m_pItemsSlider[7]->setCurrentValue(pCS->iTXPowerRTL);
   }

   if ( (NULL == g_pCurrentModel) || (!m_bShowVehicle) )
      return;

   if ( -1 != m_IndexPowerVehicle )
   {
      if ( m_bVehicleHas24Cards )
         m_pItemsSlider[0]->setCurrentValue(g_pCurrentModel->radioInterfacesParams.txPowerAtheros);
      if ( m_bVehicleHas58Cards )
         m_pItemsSlider[0]->setCurrentValue(g_pCurrentModel->radioInterfacesParams.txPowerRTL);
   }
   if ( -1 != m_IndexPowerVehicleAtheros && m_bVehicleHas24Cards )
      m_pItemsSlider[1]->setCurrentValue(g_pCurrentModel->radioInterfacesParams.txPowerAtheros);
   if ( -1 != m_IndexPowerVehicleRTL && m_bVehicleHas58Cards )
      m_pItemsSlider[2]->setCurrentValue(g_pCurrentModel->radioInterfacesParams.txPowerRTL);
}


void MenuTXInfo::RenderTableLine(int iCardModel, const char* szText, const char** szValues, bool header)
{
   Preferences* pP = get_Preferences();

   float height_text = g_pRenderEngine->textHeight(g_idFontMenuSmall);
   float x = m_RenderXPos + m_sfMenuPaddingX;
   float y = m_yTemp;

   bool bShowCard = true;
   bool bIsPresentOnVehicle = false;
   bool bIsPresentOnController = false;

   if ( (!header) && (0 != iCardModel) && pP->iShowOnlyPresentTxPowerCards )
   {
      bShowCard = false;
      if ( m_bShowVehicle && (NULL != g_pCurrentModel) )
      {
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( g_pCurrentModel->radioInterfacesParams.interface_card_model[i] == iCardModel )
               bShowCard = true;
            if ( g_pCurrentModel->radioInterfacesParams.interface_card_model[i] == -iCardModel )
               bShowCard = true;
         }
      }

      if ( m_bShowController )
      {
         for( int n=0; n<hardware_get_radio_interfaces_count(); n++ )
         {
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(n);
            if ( NULL == pRadioHWInfo )
               continue;

            t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
            if ( NULL == pCardInfo )
               continue;
            if ( iCardModel == pCardInfo->cardModel )
               bShowCard = true;
            if ( iCardModel == -(pCardInfo->cardModel) )
               bShowCard = true;
         }
      }
   }

   if ( (! bShowCard) && (!header) )
      return;

   if ( (0 != iCardModel) && (!pP->iShowOnlyPresentTxPowerCards) )
   {
      if ( m_bShowVehicle && (NULL != g_pCurrentModel) )
      {
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( g_pCurrentModel->radioInterfacesParams.interface_card_model[i] == iCardModel )
               bIsPresentOnVehicle = true;
            if ( g_pCurrentModel->radioInterfacesParams.interface_card_model[i] == -iCardModel )
               bIsPresentOnVehicle = true;
         }
      }

      if ( m_bShowController )
      {
         for( int n=0; n<hardware_get_radio_interfaces_count(); n++ )
         {
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(n);
            if ( NULL == pRadioHWInfo )
               continue;

            t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
            if ( NULL == pCardInfo )
               continue;
            if ( iCardModel == pCardInfo->cardModel )
               bIsPresentOnController = true;
            if ( iCardModel == -(pCardInfo->cardModel) )
               bIsPresentOnController = true;
         }
      }
   }

   if ( 0 != iCardModel )
   if ( ! pP->iShowOnlyPresentTxPowerCards )
   if ( bIsPresentOnController || bIsPresentOnVehicle )
   {
      g_pRenderEngine->setColors(get_Color_MenuText(), 0.2);
      g_pRenderEngine->setFill(250,200,50,0.1);
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      g_pRenderEngine->setStrokeSize(1);
      
      g_pRenderEngine->drawRoundRect(x - m_sfMenuPaddingX*0.4, y - m_sfMenuPaddingY*0.2, m_RenderWidth - 2.0*m_sfMenuPaddingX + 0.8 * m_sfMenuPaddingX, height_text + 0.4*m_sfMenuPaddingY, MENU_ROUND_MARGIN * m_sfMenuPaddingY);

      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      g_pRenderEngine->setStrokeSize(0);
   }

   g_pRenderEngine->drawText(x, y, g_idFontMenuSmall, const_cast<char*>(szText));

   x = m_xTable;
   if ( header )
      x += 0.008*m_sfScaleFactor;

   char szLevel[128];

   float yTop = m_yTemp - height_text*2.0;
   float yBottom = m_yPos+m_RenderHeight - 3.0*m_sfMenuPaddingY - 1.2*height_text;
   for( int i=0; i<11; i++ )
   {
      g_pRenderEngine->drawText(x, y, g_idFontMenuSmall, const_cast<char*>(szValues[i]));
      x += m_xTableCellWidth;

      if ( header )
      if ( i == 2 || i == 5 || i == 7 || i == 10 )
      {
         g_pRenderEngine->setColors(get_Color_MenuText(), 0.5);
         g_pRenderEngine->setStrokeSize(1);
         g_pRenderEngine->drawLine(x-0.018*m_sfScaleFactor, yTop+0.02*m_sfScaleFactor, x-0.018*m_sfScaleFactor, yBottom);
         g_pRenderEngine->setColors(get_Color_MenuText());
         g_pRenderEngine->setStroke(get_Color_MenuBorder());
         g_pRenderEngine->setStrokeSize(0);
      }

      if ( header )
      {
         szLevel[0] = 0;
         if ( i == 2 )
            strcpy(szLevel, "Low Power");
         if ( i == 5 )
            strcpy(szLevel, "Normal Power");
         if ( i == 7 )
            strcpy(szLevel, "Max Power");
         if ( i == 10 )
            strcpy(szLevel, "Experimental");
         if ( 0 != szLevel[0] )
            g_pRenderEngine->drawTextLeft( x-0.026*m_sfScaleFactor, yTop+0.015*m_sfScaleFactor, g_idFontMenuSmall, szLevel);
      }
   }
      
   m_yTemp += (1.0 + MENU_ITEM_SPACING) * g_pRenderEngine->textHeight(g_idFontMenuSmall);
   if ( header )
   {         
      g_pRenderEngine->setColors(get_Color_MenuText(), 0.7);
      g_pRenderEngine->setStrokeSize(1);
      g_pRenderEngine->drawLine(m_xPos + 1.1*m_sfMenuPaddingX,m_yTemp-0.005*m_sfScaleFactor, m_xPos - 1.1*m_sfMenuPaddingX+m_RenderWidth, m_yTemp-0.005*m_sfScaleFactor);
      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      g_pRenderEngine->setStrokeSize(0);
      m_yTemp += MENU_TEXTLINE_SPACING * g_pRenderEngine->textHeight(g_idFontMenuSmall);
   }

   if ( m_bShowThinLine )
   {
      g_pRenderEngine->setColors(get_Color_MenuText(), 0.5);
      g_pRenderEngine->setStrokeSize(1);
      float yLine = y-0.005*m_sfScaleFactor;
      g_pRenderEngine->drawLine(m_xPos + 1.1*m_sfMenuPaddingX, yLine, m_xPos - 1.1*m_sfMenuPaddingX+m_RenderWidth, yLine);
      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->setStroke(get_Color_MenuBorder());
      g_pRenderEngine->setStrokeSize(0);
   }
   m_bShowThinLine = false;
   m_iLine++;
}

void MenuTXInfo::drawPowerLine(const char* szText, float yPos, int value)
{
   const char* szH[11] =          {    "10",    "20",    "30",     "40",     "50",     "54",     "56",       "60",     "63",    "68",       "72"};
   float height_text = g_pRenderEngine->textHeight(g_idFontMenuSmall);
   float xPos = m_RenderXPos + m_sfMenuPaddingX;
   char szBuff[64];

   g_pRenderEngine->drawText( xPos, yPos, g_idFontMenuSmall, szText);

   float x = m_xTable;
   float xEnd = x;
   for( int i=0; i<11; i++ )
   {
      if ( value <= atoi(szH[i]) )
         break;
      float width = m_xTableCellWidth;
      if ( i<10 && value < atoi(szH[i+1]) )
         width *= 0.5;
      g_pRenderEngine->setColors(get_Color_MenuText(), 0.7);
      g_pRenderEngine->setStrokeSize(1);
      g_pRenderEngine->drawLine(x, yPos+0.6*height_text, x+width,yPos+0.6*height_text);
      xEnd = x+width;
      g_pRenderEngine->setColors(get_Color_MenuText());
      x += m_xTableCellWidth;
   }

   sprintf(szBuff, "%d", value);
   if ( value <= 0 )
      sprintf(szBuff, "N/A");
   g_pRenderEngine->drawText( xEnd+0.2*height_text, yPos, g_idFontMenuSmall, szBuff);
}


void MenuTXInfo::Render()
{
   Preferences* pP = get_Preferences();
   RenderPrepare();
   float height_text = g_pRenderEngine->textHeight(g_idFontMenuSmall);

   float yTop = RenderFrameAndTitle();
   float y = yTop;

   m_xTable = m_RenderXPos + m_sfMenuPaddingX;
   m_xTable += 0.15*m_sfScaleFactor;
   m_xTableCellWidth = 0.05*m_sfScaleFactor;
   
   for( int i=0; i<=m_IndexPowerMax+1; i++ )
      y += RenderItem(i,y);
   
   y += 1.0*height_text;

   m_yTopRender = y + 0.01*m_sfScaleFactor;
   m_yTemp = y+0.01*m_sfScaleFactor + 1.0*height_text;
   if ( m_bShowVehicle || m_bShowController )
      m_yTemp += 1.3 * height_text;
   if ( m_bShowController && m_bShowVehicle )
      m_yTemp += 1.3 * height_text;
   if ( m_bShowVehicle && m_bShowBothOnVehicle )
      m_yTemp += 1.4*height_text;
   if ( m_bShowController && m_bShowBothOnController )
      m_yTemp += 1.4*height_text;

   const char* szH[11] =          {    "10",    "20",    "30",      "40",     "50",     "54",     "56",       "60",     "63",    "68",       "72"};
   //const char* sz722N[11] =     {"0.5 mW",  "2 mW",  "5 mW",   "17 mW",  "50 mW",  "80 mW", "115 mW",  " 310 mW",   "- mW",  "- mW",     "- mW"};
   const char* sz722N[11] =       {"0.3 mW",  "1 mW","2.5 mW",   "10 mW",  "35 mW",  "60 mW",  "80 mW",   " 90 mW",   "- mW",  "- mW",     "- mW"};
   const char* sz722N2W[11] =     {  "6 mW", "20 mW", "70 mW",  "205 mW", "650 mW", "900 mW",    "1 W",    "1.9 W",    "2 W",   "2 W",      "2 W"};
   const char* sz722N4W[11] =     { "10 mW", "60 mW", "200 mW", "450 mW",  "1.2 W",  "1.9 W",  "2.1 W",    "2.1 W",  "2.1 W", "2.1 W",    "2.1 W"};
   const char* szBlueStick[11]  = {  "2 mW",  "4 mW",  "8 mW",   "28 mW",  "80 mW", "110 mW", "280 mW",      "1 W",   "? mW",    "? mW",   "? mW"};
   const char* szGreenStick[11] = {  "2 mW",  "5 mW", "15 mW",   "60 mW",  "75 mW",  "75 mW",   "- mW",     "- mW",   "- mW",    "- mW",   "- mW"};
   const char* szAWUS036NH[11] =  { "10 mW", "20 mW", "30 mW",   "40 mW",  "60 mW",   "- mW",   "- mW",     "- mW",   "- mW",  "- mW",     "- mW"};
   const char* szAWUS036NHA[11] = {"0.5 mW",  "2 mW",  "6 mW",   "17 mW", "120 mW", "180 mW", "215 mW",   "310 mW", "460 mW",  "- mW",     "- mW"};
   
   //const char* szH[11] =        {    "10",    "20",    "30",      "40",     "50",     "54",     "56",       "60",     "63",    "68",       "72"};
   const char* szGeneric[11] =    {"0.3 mW",  "1 mW","2.5 mW",   "10 mW",  "35 mW",  "60 mW",  "80 mW",   " 90 mW",   "- mW",  "- mW",     "- mW"};
   const char* szAWUS036ACH[11] = {  "2 mW",  "5 mW", "20 mW",   "50 mW", "160 mW", "250 mW", "300 mW",   "420 mW", "500 mW",  "- mW",     "- mW"};
   const char* szASUSUSB56[11]  = {  "2 mW",  "9 mW", "30 mW",   "80 mW", "200 mW", "250 mW", "300 mW",   "340 mW", "370 mW",  "370 mW", "150 mW"};
   const char* szRTLDualAnt[11] = {  "5 mW", "17 mW", "50 mW",  "150 mW", "190 mW", "210 mW", "261 mW",   "310 mW", "310 mW",  "310 mW", "310 mW"};
   const char* szAli1W[11]  =     {  "1 mW",  "2 mW",  "5 mW",   "10 mW",  "30 mW",  "50 mW",  "100 mW",   "300 mW", "450 mW",  "450 mW", "450 mW"};
   const char* szA6100[11] =      {  "1 mW",  "3 mW",  "9 mW",   "17 mW",  "30 mW",  "30 mW",  "35 mW",   " 40 mW",   "- mW",  "- mW",     "- mW"};
   const char* szAWUS036ACS[11] = {"0.1 mW",  "1 mW",  "3 mW",   "10 mW",  "35 mW",  "50 mW",  "60 mW",   " 90 mW",   "110 mW",  "- mW",     "- mW"};
   
   RenderTableLine(0, "Card / Power Level", szH, true);
   
   m_yTemp += 0.3 * height_text;
   if ( pP->iShowOnlyPresentTxPowerCards )
      m_yTemp += height_text;

   m_iLine = 0;

   if ( m_bDisplay24Cards )
   {
      RenderTableLine(CARD_MODEL_TPLINK722N, "TPLink WN722N", sz722N, false);
      RenderTableLine(CARD_MODEL_TPLINK722N, "WN722N + 2W Booster", sz722N2W, false);
      RenderTableLine(CARD_MODEL_TPLINK722N, "WN722N + 4W Booster", sz722N4W, false);
      RenderTableLine(CARD_MODEL_BLUE_STICK, "Blue Stick 2.4Ghz AR9271", szBlueStick, false);
      //RenderTableLine(CARD_MODEL_TPLINK722N, "Green Stick 2.4Ghz AR9271", szGreenStick, false);
      RenderTableLine(CARD_MODEL_ALFA_AWUS036NH, "Alfa AWUS036NH", szAWUS036NH, false);
      RenderTableLine(CARD_MODEL_ALFA_AWUS036NHA, "Alfa AWUS036NHA", szAWUS036NHA, false);
   }
   if ( m_iLine != 0 )
   if ( m_bDisplay24Cards && m_bDisplay58Cards )
      m_bShowThinLine = true;
   
   if ( m_bDisplay58Cards )
   {
      RenderTableLine(CARD_MODEL_RTL8812AU_DUAL_ANTENNA, "RTLDualAntenna 5.8Ghz", szRTLDualAnt, false);
      RenderTableLine(CARD_MODEL_ASUS_AC56, "ASUS AC-56", szASUSUSB56, false);
      RenderTableLine(CARD_MODEL_ALFA_AWUS036ACH, "Alfa AWUS036ACH", szAWUS036ACH, false);
      RenderTableLine(CARD_MODEL_ALFA_AWUS036ACS, "Alfa AWUS036ACS", szAWUS036ACS, false);
      RenderTableLine(CARD_MODEL_ZIPRAY, "1 Watt 5.8Ghz", szAli1W, false);
      RenderTableLine(CARD_MODEL_NETGEAR_A6100, "Netgear A6100", szA6100, false);
      RenderTableLine(CARD_MODEL_TENDA_U12, "Tenda U12", szGeneric, false);
      RenderTableLine(CARD_MODEL_ARCHER_T2UPLUS, "Archer T2U Plus", szGeneric, false);
      RenderTableLine(CARD_MODEL_RTL8814AU, "RTL8814AU", szGeneric, false);
   }

   height_text = g_pRenderEngine->textHeight(g_idFontMenuSmall);
   float xPos = m_xPos + 1.1*m_sfMenuPaddingX;

   if ( (NULL != g_pCurrentModel) && m_bShowVehicle )
   {
      if ( m_bShowBothOnVehicle )
      {
         drawPowerLine("Vehicle TX Power (2.4 Ghz band):", m_yTopRender, g_pCurrentModel->radioInterfacesParams.txPowerAtheros );
         drawPowerLine("Vehicle TX Power (5.8 Ghz band):", m_yTopRender, g_pCurrentModel->radioInterfacesParams.txPowerRTL );
      }
      else
      {
         if ( m_bVehicleHas24Cards )
            drawPowerLine("Vehicle TX Power:", m_yTopRender, g_pCurrentModel->radioInterfacesParams.txPowerAtheros );
         else
            drawPowerLine("Vehicle TX Power:", m_yTopRender, g_pCurrentModel->radioInterfacesParams.txPowerRTL );
      }
      m_yTopRender += 1.4*height_text;   
   }

   ControllerSettings* pCS = get_ControllerSettings();

   if ( m_bShowController )
   {
      if ( m_bShowBothOnController )
      {
         drawPowerLine("Controller TX Power (2.4 Ghz band):", m_yTopRender, pCS->iTXPowerAtheros );
         m_yTopRender += 1.4*height_text;      
         drawPowerLine("Controller TX Power (5.8 Ghz band):", m_yTopRender, pCS->iTXPowerRTL );
      }
      else
      {
         if ( m_bControllerHas24Cards )
            drawPowerLine("Controller TX Power:", m_yTopRender, pCS->iTXPowerAtheros );
         else
            drawPowerLine("Controller TX Power:", m_yTopRender, pCS->iTXPowerRTL );
      }
      m_yTopRender += 1.4*height_text;   
   }

   g_pRenderEngine->drawText(m_xPos + Menu::getMenuPaddingX(), m_yPos+m_RenderHeight - 3.0*m_sfMenuPaddingY - height_text, g_idFontMenu, "* Power levels are measured at 5805 Mhz. Lower frequencies do increase the power a little bit.");
}


void MenuTXInfo::sendPowerToVehicle(int tx, int txAtheros, int txRTL)
{
   u8 buffer[10];
   memset(&(buffer[0]), 0, 10);
   buffer[0] = tx;
   buffer[1] = txAtheros;
   buffer[2] = txRTL;
   buffer[3] = g_pCurrentModel->radioInterfacesParams.txMaxPower;
   buffer[4] = g_pCurrentModel->radioInterfacesParams.txMaxPowerAtheros;
   buffer[5] = g_pCurrentModel->radioInterfacesParams.txMaxPowerRTL;

   if ( ! handle_commands_send_to_vehicle(COMMAND_ID_SET_TX_POWERS, 0, buffer, 8) )
       valuesToUI();
}


int MenuTXInfo::onBack()
{
   if ( m_bValuesChangedController )
   {
      m_iConfirmationId = 2;
      MenuConfirmation* pMC = new MenuConfirmation("Restart Required","You need to restart the controller for the power changes to take effect.", m_iConfirmationId);
      pMC->m_yPos = 0.3;
      pMC->addTopLine("");

      if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->rc_params.rc_enabled) )
      {
         pMC->addTopLine("Warning! You have RC link enabled. RC link will be lost while the controller restarts.");
      }
      pMC->addTopLine("Do you want to restart your controller now?");
      add_menu_to_stack(pMC);
      return 1;
   }

   if ( m_bValuesChangedVehicle )
   {
      m_iConfirmationId = 3;
      MenuConfirmation* pMC = new MenuConfirmation("Restart Required","You need to restart the vehicle for the power changes to take effect.", m_iConfirmationId);
      pMC->m_yPos = 0.3;
      pMC->addTopLine("");

      if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->rc_params.rc_enabled) )
      {
         pMC->addTopLine("Warning! You have RC link enabled. RC link will be lost while the vehicle restarts.");
      }
      pMC->addTopLine("Do you want to restart your vehicle now?");
      add_menu_to_stack(pMC);
      return 1;
   }
   return Menu::onBack();
}

void MenuTXInfo::onReturnFromChild(int returnValue)
{
   Menu::onReturnFromChild(returnValue);

   if ( 2 == m_iConfirmationId )
   {
      m_bValuesChangedController = false;
      if ( 1 == returnValue )
      {
         m_iConfirmationId = 0;
         onEventReboot();
         hw_execute_bash_command("sudo reboot -f", NULL);
         return;
      }
      menu_stack_pop();
      return;
   }

   if ( 3 == m_iConfirmationId )
   {
      m_bValuesChangedVehicle = false;
      if ( 1 == returnValue )
      {
         handle_commands_send_to_vehicle(COMMAND_ID_REBOOT, 0, NULL, 0);
         valuesToUI();
         m_iConfirmationId = 0;
         menu_close_all();
         return;
      }
      return;
   }

   valuesToUI();
   m_iConfirmationId = 0;
}

void MenuTXInfo::onSelectItem()
{
   Menu::onSelectItem();
   
   if ( m_pMenuItems[m_SelectedIndex]->isEditing() )
      return;

   if ( handle_commands_is_command_in_progress() )
   {
      handle_commands_show_popup_progress();
      return;
   }

   ControllerSettings* pCS = get_ControllerSettings();
   Preferences* pP = get_Preferences();
   char szBuff[1024];

   if ( m_IndexShowAllCards == m_SelectedIndex )
   {
      pP->iShowOnlyPresentTxPowerCards = 1- m_pItemsSelect[0]->getSelectedIndex();
      save_Preferences();
      valuesToUI();
      invalidate();
      return;
   }

   if ( m_IndexPowerController == m_SelectedIndex )
   {
      if ( m_bControllerHas24Cards )
      {
         if ( pCS->iTXPowerAtheros != m_pItemsSlider[5]->getCurrentValue() )
            m_bValuesChangedController = true;
         pCS->iTXPowerAtheros = m_pItemsSlider[5]->getCurrentValue();
      }
      if ( m_bControllerHas58Cards )
      {
         if ( pCS->iTXPowerRTL != m_pItemsSlider[5]->getCurrentValue() )
            m_bValuesChangedController = true;
         pCS->iTXPowerRTL = m_pItemsSlider[5]->getCurrentValue();
      }
      if ( m_bControllerHas24Cards )
      {
         hardware_set_radio_tx_power_atheros(pCS->iTXPowerAtheros);
      }
      if ( m_bControllerHas58Cards )
      {
         hardware_set_radio_tx_power_rtl(pCS->iTXPowerRTL);
      }
      save_ControllerSettings();

      if ( m_pItemsSlider[5]->getCurrentValue() > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }

      return;
   }

   if ( m_IndexPowerControllerAtheros == m_SelectedIndex )
   {
      if ( pCS->iTXPowerAtheros != m_pItemsSlider[6]->getCurrentValue() )
            m_bValuesChangedController = true;
      pCS->iTXPowerAtheros = m_pItemsSlider[6]->getCurrentValue();
      hardware_set_radio_tx_power_atheros(pCS->iTXPowerAtheros);
      save_ControllerSettings();

      if ( m_pItemsSlider[6]->getCurrentValue() > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }
      return;
   }

   if ( m_IndexPowerControllerRTL == m_SelectedIndex )
   {
      if ( pCS->iTXPowerRTL != m_pItemsSlider[7]->getCurrentValue() )
         m_bValuesChangedController = true;
      pCS->iTXPowerRTL = m_pItemsSlider[7]->getCurrentValue();
      hardware_set_radio_tx_power_rtl(pCS->iTXPowerRTL);
      save_ControllerSettings();

      if ( m_pItemsSlider[7]->getCurrentValue() > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }

      return;
   }


   if ( (m_IndexPowerVehicle == m_SelectedIndex) && menu_check_current_model_ok_for_edit() )
   {
      int val = m_pItemsSlider[0]->getCurrentValue();
      int tx = g_pCurrentModel->radioInterfacesParams.txPower;
      int txAtheros = g_pCurrentModel->radioInterfacesParams.txPowerAtheros;
      int txRTL = g_pCurrentModel->radioInterfacesParams.txPowerRTL;

      if ( m_bVehicleHas24Cards )
      {
         if ( val != txAtheros )
            m_bValuesChangedVehicle = true;
         txAtheros = val;
      }
      if ( m_bVehicleHas58Cards )
      {
         if ( val != txRTL )
            m_bValuesChangedVehicle = true;
         txRTL = val;
      }
      sendPowerToVehicle(tx, txAtheros, txRTL);

      if ( val > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }

      return;
   }
   if ( (m_IndexPowerVehicleAtheros == m_SelectedIndex) && menu_check_current_model_ok_for_edit() )
   {
      int tx = g_pCurrentModel->radioInterfacesParams.txPower;
      int txAtheros = m_pItemsSlider[1]->getCurrentValue();
      if ( g_pCurrentModel->radioInterfacesParams.txPowerAtheros != txAtheros )
         m_bValuesChangedVehicle = true;

      int txRTL = g_pCurrentModel->radioInterfacesParams.txPowerRTL;
      sendPowerToVehicle(tx, txAtheros, txRTL);

      if ( txAtheros > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }
      return;
   }

   if ( (m_IndexPowerVehicleRTL == m_SelectedIndex) && menu_check_current_model_ok_for_edit() )
   {
      int tx = g_pCurrentModel->radioInterfacesParams.txPower;
      int txAtheros = g_pCurrentModel->radioInterfacesParams.txPowerAtheros;
      int txRTL = m_pItemsSlider[2]->getCurrentValue();
      if ( g_pCurrentModel->radioInterfacesParams.txPowerRTL != txRTL )
         m_bValuesChangedVehicle = true;
      sendPowerToVehicle(tx, txAtheros, txRTL);

      if ( txRTL > 59 )
      {
         m_iConfirmationId = 1;
         MenuConfirmation* pMC = new MenuConfirmation("High Power Levels","Setting a card to a very high power level can fry it if it does not have proper cooling.", m_iConfirmationId, true);
         pMC->m_yPos = 0.3;
         pMC->addTopLine("");
         pMC->addTopLine("Proceed with caution!");
         add_menu_to_stack(pMC);
      }

      return;
   }

   if ( m_IndexPowerMax == m_SelectedIndex )
      add_menu_to_stack(new MenuTXPowerMax());

}