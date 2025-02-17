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
#include "menu_vehicle_instruments_general.h"
#include "menu_vehicle_osd_plugins.h"
#include "menu_item_select.h"
#include "menu_item_slider.h"
#include "../osd/osd_common.h"

MenuVehicleInstrumentsGeneral::MenuVehicleInstrumentsGeneral(void)
:Menu(MENU_ID_VEHICLE_INSTRUMENTS_GENERAL, "Intruments/Gauges General Settings", NULL)
{
   m_Width = 0.28;
   m_xPos = menu_get_XStartPos(m_Width); m_yPos = 0.30;
   float fSliderWidth = 0.10;

   for( int i=0; i<20; i++ )
      m_pItemsSelect[i] = NULL;

   m_pItemsSelect[10] = new MenuItemSelect("Display Units", "Changes how the OSD displays data: in metric system or imperial system.");  
   m_pItemsSelect[10]->addSelection("Metric (km/h)");
   m_pItemsSelect[10]->addSelection("Metric (m/s)");
   m_pItemsSelect[10]->addSelection("Imperial (mi/h)");
   m_pItemsSelect[10]->addSelection("Imperial (ft/s)");
   m_pItemsSelect[10]->setIsEditable();
   m_IndexUnits = addMenuItem(m_pItemsSelect[10]);

   m_pItemsSelect[1] = new MenuItemSelect("Instruments Size", "Increase/decrease instruments sizes.");  
   m_pItemsSelect[1]->addSelection("Smallest");
   m_pItemsSelect[1]->addSelection("Smaller");
   m_pItemsSelect[1]->addSelection("Small");
   m_pItemsSelect[1]->addSelection("Normal");
   m_pItemsSelect[1]->addSelection("Large");
   m_pItemsSelect[1]->addSelection("Larger");
   m_pItemsSelect[1]->addSelection("Largest");
   m_pItemsSelect[1]->addSelection("XLarge");
   m_IndexAHISize = addMenuItem(m_pItemsSelect[1]);
 
   m_pItemsSelect[2] = new MenuItemSelect("Instruments Line Thickness", "Sets the thickness of instruments elements");
   m_pItemsSelect[2]->addSelection("Smallest");
   m_pItemsSelect[2]->addSelection("Small");
   m_pItemsSelect[2]->addSelection("Normal");
   m_pItemsSelect[2]->addSelection("Big");
   m_pItemsSelect[2]->addSelection("Biggest");
   m_IndexAHIStrokeSize = addMenuItem(m_pItemsSelect[2]);
   
   m_pItemsSelect[3] = new MenuItemSelect("Altitude type", "Shows the vehicle altitude relative to sea level or relative to the starting position.");  
   m_pItemsSelect[3]->addSelection("Absolute");
   m_pItemsSelect[3]->addSelection("Relative");
   m_pItemsSelect[3]->setIsEditable();
   m_IndexAltitudeType = addMenuItem(m_pItemsSelect[3]);

   m_pItemsSelect[6] = new MenuItemSelect("Vertical Speed", "Shows the vertical speed reported by the flight controller or the one computed locally on the controller based on altitude variations.");
   m_pItemsSelect[6]->addSelection("Reported by FC");
   m_pItemsSelect[6]->addSelection("Computed");
   m_pItemsSelect[6]->setIsEditable();
   m_IndexLocalVertSpeed = addMenuItem(m_pItemsSelect[6]);

   m_pItemsSelect[4] = new MenuItemSelect("Revert Pitch", "Reverses the rotation direction of the pitch axis. Impacts only on the AHI gauge.");  
   m_pItemsSelect[4]->addSelection("No");
   m_pItemsSelect[4]->addSelection("Yes");
   m_pItemsSelect[4]->setUseMultiViewLayout();
   m_IndexRevertPitch = addMenuItem(m_pItemsSelect[4]);

   m_pItemsSelect[5] = new MenuItemSelect("Revert Roll", "Reverses the rotation direction of the roll axis. Impacts only on the AHI gauge.");  
   m_pItemsSelect[5]->addSelection("No");
   m_pItemsSelect[5]->addSelection("Yes");
   m_pItemsSelect[5]->setUseMultiViewLayout();
   m_IndexRevertRoll = addMenuItem(m_pItemsSelect[5]);

   m_pItemsSelect[7] = new MenuItemSelect("Main Speed", "Shows the air speed or ground speed as the main speed indicator.");  
   m_pItemsSelect[7]->addSelection("Ground");
   m_pItemsSelect[7]->addSelection("Air");
   m_pItemsSelect[7]->setUseMultiViewLayout();
   m_IndexAHIShowAirSpeed = addMenuItem(m_pItemsSelect[7]);

   m_pItemsSelect[8] = new MenuItemSelect("Battery Cells", "How many cells (in series) does the vehicle battery has.");  
   m_pItemsSelect[8]->addSelection("Auto Detect");
   m_pItemsSelect[8]->addSelection("1 cell");
   m_pItemsSelect[8]->addSelection("2 cells");
   m_pItemsSelect[8]->addSelection("3 cells");
   m_pItemsSelect[8]->addSelection("4 cells");
   m_pItemsSelect[8]->addSelection("5 cells");
   m_pItemsSelect[8]->addSelection("6 cells");
   m_pItemsSelect[8]->addSelection("7 cells");
   m_pItemsSelect[8]->addSelection("8 cells");
   m_pItemsSelect[8]->addSelection("9 cells");
   m_pItemsSelect[8]->addSelection("10 cells");
   m_pItemsSelect[8]->addSelection("11 cells");
   m_pItemsSelect[8]->addSelection("12 cells");
   m_pItemsSelect[8]->setIsEditable();
   m_IndexBatteryCells = addMenuItem(m_pItemsSelect[8]);
}

MenuVehicleInstrumentsGeneral::~MenuVehicleInstrumentsGeneral()
{
}

void MenuVehicleInstrumentsGeneral::valuesToUI()
{
   Preferences* p = get_Preferences();
   m_nOSDIndex = g_pCurrentModel->osd_params.layout;

   if ( p->iUnits == prefUnitsMetric )
      m_pItemsSelect[10]->setSelection(0);
   if ( p->iUnits == prefUnitsMeters )
      m_pItemsSelect[10]->setSelection(1);
   if ( p->iUnits == prefUnitsImperial )
      m_pItemsSelect[10]->setSelection(2);
   if ( p->iUnits == prefUnitsFeets )
      m_pItemsSelect[10]->setSelection(3);

   //log_dword("start: osd flags", g_pCurrentModel->osd_params.osd_flags[m_nOSDIndex]);
   //log_dword("start: instruments flags", g_pCurrentModel->osd_params.instruments_flags[m_nOSDIndex]);
   //log_line("current layout: %d, show instr: %d", (g_pCurrentModel->osd_params.osd_flags[m_nOSDIndex] & OSD_FLAG_AHI_STYLE_MASK)>>3, g_pCurrentModel->osd_params.instruments_flags[m_nOSDIndex] & INSTRUMENTS_FLAG_SHOW_INSTRUMENTS);

   m_pItemsSelect[1]->setSelection(p->iScaleAHI+3);
   m_pItemsSelect[2]->setSelection(p->iAHIStrokeSize+2);
   m_pItemsSelect[3]->setSelection((g_pCurrentModel->osd_params.osd_flags2[m_nOSDIndex] & OSD_FLAG2_RELATIVE_ALTITUDE)?1:0);
   m_pItemsSelect[6]->setSelection((g_pCurrentModel->osd_params.osd_flags2[m_nOSDIndex] & OSD_FLAG2_SHOW_LOCAL_VERTICAL_SPEED)?1:0);

   m_pItemsSelect[4]->setSelection(((g_pCurrentModel->osd_params.osd_flags[m_nOSDIndex]) & OSD_FLAG_REVERT_PITCH)?1:0);
   m_pItemsSelect[5]->setSelection(((g_pCurrentModel->osd_params.osd_flags[m_nOSDIndex]) & OSD_FLAG_REVERT_ROLL)?1:0);

   m_pItemsSelect[7]->setSelection(((g_pCurrentModel->osd_params.osd_flags[m_nOSDIndex]) & OSD_FLAG_AIR_SPEED_MAIN)?1:0);

   m_pItemsSelect[8]->setSelection(g_pCurrentModel->osd_params.battery_cell_count);
}


void MenuVehicleInstrumentsGeneral::Render()
{
   RenderPrepare();
   float yTop = RenderFrameAndTitle();
   float y = yTop;
   for( int i=0; i<m_ItemsCount; i++ )
      y += RenderItem(i,y);

   RenderEnd(yTop);
}

void MenuVehicleInstrumentsGeneral::onItemValueChanged(int itemIndex)
{
   Menu::onItemValueChanged(itemIndex);
}

void MenuVehicleInstrumentsGeneral::onSelectItem()
{
   Menu::onSelectItem();

   if ( m_pMenuItems[m_SelectedIndex]->isEditing() )
      return;

   if ( handle_commands_is_command_in_progress() )
   {
      handle_commands_show_popup_progress();
      return;
   }

   m_nOSDIndex = g_pCurrentModel->osd_params.layout;

   Preferences* p = get_Preferences();
   osd_parameters_t params;
   memcpy(&params, &(g_pCurrentModel->osd_params), sizeof(osd_parameters_t));
   bool sendToVehicle = false;


   if ( m_IndexUnits == m_SelectedIndex )
   {
      int nSel = m_pItemsSelect[10]->getSelectedIndex();
      if ( 0 == nSel )
         p->iUnits = prefUnitsMetric;
      if ( 1 == nSel )
         p->iUnits = prefUnitsMeters;
      if ( 2 == nSel )
         p->iUnits = prefUnitsImperial;
      if ( 3 == nSel )
         p->iUnits = prefUnitsFeets;

      save_Preferences();
      valuesToUI();
   }

   if ( m_IndexAHISize == m_SelectedIndex )
   {
      p->iScaleAHI = m_pItemsSelect[1]->getSelectedIndex()-3;
      save_Preferences();
      valuesToUI();
      osd_apply_preferences();
   }

   if ( m_IndexAHIStrokeSize == m_SelectedIndex )
   {
      p->iAHIStrokeSize = m_pItemsSelect[2]->getSelectedIndex()-2;
      save_Preferences();
      valuesToUI();
      osd_apply_preferences();
   }

   if ( m_IndexAltitudeType == m_SelectedIndex )
   {
      if ( 0 == m_pItemsSelect[3]->getSelectedIndex() )
      {
         params.altitude_relative = false;
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags2[i] &= ~OSD_FLAG2_RELATIVE_ALTITUDE;
      }
      else
      {
         params.altitude_relative = true;
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags2[i] |= OSD_FLAG2_RELATIVE_ALTITUDE;
      }
      sendToVehicle = true;
   }
   
   if ( m_IndexLocalVertSpeed == m_SelectedIndex )
   {
      if ( 0 == m_pItemsSelect[6]->getSelectedIndex() )
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags2[i] &= ~OSD_FLAG2_SHOW_LOCAL_VERTICAL_SPEED;
      else
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags2[i] |= OSD_FLAG2_SHOW_LOCAL_VERTICAL_SPEED;
      sendToVehicle = true;
   }

   if ( m_IndexRevertPitch == m_SelectedIndex )
   {
      u32 idx = m_pItemsSelect[4]->getSelectedIndex();
      for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
         params.osd_flags[i] &= ~OSD_FLAG_REVERT_PITCH;
      if ( idx > 0 )
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags[i] |= OSD_FLAG_REVERT_PITCH;
      sendToVehicle = true;
   }

   if ( m_IndexRevertRoll == m_SelectedIndex )
   {
      u32 idx = m_pItemsSelect[5]->getSelectedIndex();
      for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
         params.osd_flags[i] &= ~OSD_FLAG_REVERT_ROLL;
      if ( idx > 0 )
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags[i] |= OSD_FLAG_REVERT_ROLL;
      sendToVehicle = true;
   } 

   if ( m_IndexAHIShowAirSpeed == m_SelectedIndex )
   {
      u32 idx = m_pItemsSelect[7]->getSelectedIndex();
      for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
         params.osd_flags[i] &= ~OSD_FLAG_AIR_SPEED_MAIN;
      if ( idx > 0 )
         for( int i=0; i<MODEL_MAX_OSD_PROFILES; i++ )
            params.osd_flags[i] |= OSD_FLAG_AIR_SPEED_MAIN;
      sendToVehicle = true;
   }

   if ( m_IndexBatteryCells == m_SelectedIndex )
   {
      params.battery_cell_count = m_pItemsSelect[8]->getSelectedIndex();
      sendToVehicle = true;
   }

   if ( g_pCurrentModel->is_spectator )
   {
      memcpy(&(g_pCurrentModel->osd_params), &params, sizeof(osd_parameters_t));
      saveControllerModel(g_pCurrentModel);
   }
   else if ( sendToVehicle )
   {
      if ( ! handle_commands_send_to_vehicle(COMMAND_ID_SET_OSD_PARAMS, 0, (u8*)&params, sizeof(osd_parameters_t)) )
         valuesToUI();
      return;
   }
}