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
#include "menu_spectator.h"
#include "menu_search.h"

const char* s_szNoSpectator = "No recently viewed vehicles in spectator mode.";

MenuSpectator::MenuSpectator(void)
:Menu(MENU_ID_SPECTATOR, "Spectator Mode Vehicles", NULL)
{
   addTopLine("Select a recently viewed vehicle to connect to in spectator mode:");
   m_Width = 0.18;
   m_xPos = menu_get_XStartPos(m_Width); m_yPos = 0.25;
}


void MenuSpectator::onShow()
{
   m_Height = 0.0;
   m_ExtraItemsHeight = 0.02*m_sfScaleFactor;
   
   removeAllItems();
   for( int i=0; i<getControllerModelsSpectatorCount(); i++ )
   {
      Model *p = getSpectatorModel(i);
      char szBuff[64];
      sprintf(szBuff, p->getLongName());
      addMenuItem( new MenuItem(szBuff) );
      if ( NULL != g_pCurrentModel && g_pCurrentModel->is_spectator && g_pCurrentModel->vehicle_id == p->vehicle_id )
         m_ExtraItemsHeight += (1+MENU_TEXTLINE_SPACING)*g_pRenderEngine->textHeight(g_idFontMenu);

   }
   if ( 0 == getControllerModelsSpectatorCount() )
      m_ExtraItemsHeight += 1.3*g_pRenderEngine->getMessageHeight(s_szNoSpectator, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenu);
   addMenuItem( new MenuItem("Search for vehicles") );
   Menu::onShow();
}

void MenuSpectator::Render()
{
   RenderPrepare();
   float yTop = RenderFrameAndTitle();
   float y = yTop;

   if ( 0 == getControllerModelsSpectatorCount() )
   {
      g_pRenderEngine->setColors(get_Color_MenuText());
      g_pRenderEngine->drawMessageLines(m_xPos+m_sfMenuPaddingX, y, s_szNoSpectator, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenu);
      y += 1.3*g_pRenderEngine->getMessageHeight(s_szNoSpectator, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenu);
   }
   
   for( int i=0; i<m_ItemsCount; i++ )
   {
      if ( i == m_ItemsCount-1 )
         y += 0.02*m_sfScaleFactor;

      Model *p = getSpectatorModel(i);
      float h = RenderItem(i,y);
      if ( NULL != p && NULL != g_pCurrentModel && g_pCurrentModel->is_spectator && g_pCurrentModel->vehicle_id == p->vehicle_id )
      {
         float maxWidth = m_RenderWidth - 2.0*m_sfMenuPaddingX + 2.0*m_sfSelectionPaddingX;
         float x = m_xPos+ 0.4*m_sfMenuPaddingX;
         float h2 = m_pMenuItems[i]->getItemHeight(0.0) + 0.3*m_sfMenuPaddingY;
   
         g_pRenderEngine->setColors(get_Color_MenuText());
         g_pRenderEngine->setStroke(get_Color_MenuText());
         g_pRenderEngine->setFill(250,250,250,0.05);   
         g_pRenderEngine->drawRoundRect(m_RenderXPos+m_sfMenuPaddingX - m_sfSelectionPaddingX, y-0.07*m_sfMenuPaddingY, maxWidth, h2, MENU_ROUND_MARGIN*m_sfMenuPaddingY);
         g_pRenderEngine->setColors(get_Color_MenuText());
      }
      y += h;
   }
   RenderEnd(yTop);
}

void MenuSpectator::onSelectItem()
{
   if ( m_SelectedIndex == m_ItemsCount-1 )
   {
      MenuSearch* pMenuSearchSpectator = new MenuSearch();
      pMenuSearchSpectator->setSpectatorOnly();
      add_menu_to_stack(pMenuSearchSpectator);
      return;
   }
   Model *pModel = getSpectatorModel(m_SelectedIndex);
   if ( NULL == pModel )
   {
      log_softerror_and_alarm("NULL model for spectator mode vehicle: %s.", m_pMenuItems[m_SelectedIndex]->getTitle());
      return;
   }
   if ( NULL != g_pCurrentModel && g_pCurrentModel->is_spectator && g_pCurrentModel->vehicle_id == pModel->vehicle_id )
   {
      menu_close_all();
      return;
   }
   menu_close_all();
   warnings_remove_all();
   render_all(get_current_timestamp_ms(), true);
   Popup* p = new Popup("Switching vehicles...",0.3,0.64, 0.26, 0.2);
   popups_add_topmost(p);
   render_all(get_current_timestamp_ms(), true);
   pairing_stop();
   hardware_sleep_ms(100);

   saveControllerModel(g_pCurrentModel);

   moveSpectatorModelToTop(m_SelectedIndex);
   g_pCurrentModel = pModel;
   setControllerCurrentModel(pModel->vehicle_id);
   saveControllerModel(g_pCurrentModel);

   ruby_set_active_model_id(g_pCurrentModel->vehicle_id);
   
   onMainVehicleChanged();
   pairing_start_normal(); 
}