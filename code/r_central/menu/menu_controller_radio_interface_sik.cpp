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
#include "menu_objects.h"
#include "menu_controller_radio_interface_sik.h"
#include "menu_text.h"
#include "menu_txinfo.h"
#include "menu_confirmation.h"
#include "menu_item_section.h"
#include "menu_item_text.h"

#include <time.h>
#include <sys/resource.h>
#include <semaphore.h>

MenuControllerRadioInterfaceSiK::MenuControllerRadioInterfaceSiK(int iInterfaceIndex)
:Menu(MENU_ID_CONTROLLER_RADIO_INTERFACE_SIK, "Controller Radio Interface (SiK) Settings", NULL)
{
   m_Width = 0.3;
   m_xPos = 0.08;
   m_yPos = 0.1;
   m_pPopupProgress = NULL;
   m_iInterfaceIndex = iInterfaceIndex;
   
   ControllerSettings* pCS = get_ControllerSettings();

   load_ControllerInterfacesSettings();

   char szBuff[1024];

   m_IndexName = -1;

   if ( 0 == hardware_get_radio_interfaces_count() )
   {
      addTopLine("No radio interfaces detected on the system!");
      return;
   }

   m_pItemsSelect[2] = new MenuItemSelect("Enabled", "Enables or disables this card.");
   m_pItemsSelect[2]->addSelection("No");
   m_pItemsSelect[2]->addSelection("Yes");
   m_pItemsSelect[2]->setIsEditable();
   m_IndexEnabled = addMenuItem(m_pItemsSelect[2]);

   m_pItemsSelect[6] = new MenuItemSelect("Card Location", "Marks this card as internal or external.");
   m_pItemsSelect[6]->addSelection("External");
   m_pItemsSelect[6]->addSelection("Internal");
   m_pItemsSelect[6]->setIsEditable();
   m_IndexInternal = addMenuItem(m_pItemsSelect[6]);

   m_pItemsEdit[0] = new MenuItemEdit("Custom Name", "Sets a custom name for this unique physical radio interface; This name is to be displayed in the OSD and menus, to easily identify and distinguish each physical radio interface from the others.", "");
   m_pItemsEdit[0]->setMaxLength(48);
   m_IndexName = addMenuItem(m_pItemsEdit[0]);
}

void MenuControllerRadioInterfaceSiK::valuesToUI()
{
   m_ExtraItemsHeight = 0;

   ControllerInterfacesSettings* pCI = get_ControllerInterfacesSettings();
   ControllerSettings* pCS = get_ControllerSettings();

   if ( 0 == hardware_get_radio_interfaces_count() || m_iInterfaceIndex < 0 || m_iInterfaceIndex >= hardware_get_radio_interfaces_count() )
       return;

   radio_hw_info_t* pNIC = hardware_get_radio_info(m_iInterfaceIndex);
      
   t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pNIC->szMAC);
      
   if ( NULL == pCardInfo )
      return;

   int iRadioLinkId = g_SM_RadioStats.radio_interfaces[m_iInterfaceIndex].assignedLocalRadioLinkId;

   m_pItemsSelect[2]->setEnabled(true);
   m_pItemsSelect[2]->setSelection(1);
   if ( controllerIsCardDisabled(pNIC->szMAC) )
      m_pItemsSelect[2]->setSelection(0);

   if ( 1 == hardware_get_radio_interfaces_count() )
   {
      m_pItemsSelect[2]->setSelection(1);
      m_pItemsSelect[2]->setEnabled(false);
      controllerRemoveCardDisabled(pNIC->szMAC);
      controllerSetCardTXRX(pNIC->szMAC);
   }

   m_pItemsSelect[6]->setSelectedIndex(controllerIsCardInternal(pNIC->szMAC));

   if ( NULL != m_pItemsEdit[0] )
      m_pItemsEdit[0]->setCurrentValue(controllerGetCardUserDefinedName(pNIC->szMAC));
}


void MenuControllerRadioInterfaceSiK::onShow()
{
   Menu::onShow();
}

void MenuControllerRadioInterfaceSiK::Render()
{
   RenderPrepare();
   float yTop = RenderFrameAndTitle();
   float y = yTop;

   for( int i=0; i<m_ItemsCount; i++ )
   {
      y += RenderItem(i,y);
   }
   RenderEnd(yTop);
}

void MenuControllerRadioInterfaceSiK::showProgressInfo()
{
   ruby_pause_watchdog();
   m_pPopupProgress = new Popup("Updating SiK Radio Configuration. Please wait...",0.3,0.4, 0.5, 15);
   popups_add_topmost(m_pPopupProgress);

   g_pRenderEngine->startFrame();
   popups_render();
   popups_render_topmost();
   g_pRenderEngine->endFrame();

}

void MenuControllerRadioInterfaceSiK::hideProgressInfo()
{
   popups_remove(m_pPopupProgress);
   m_pPopupProgress = NULL;
}

void MenuControllerRadioInterfaceSiK::onReturnFromChild(int returnValue)
{
   Menu::onReturnFromChild(returnValue);
}

bool MenuControllerRadioInterfaceSiK::checkFlagsConsistency()
{
   radio_hw_info_t* pNIC = hardware_get_radio_info(m_iInterfaceIndex);
   if ( NULL == pNIC )
      return false;

   u32 cardFlags = controllerGetCardFlags(pNIC->szMAC);

   int enabled = m_pItemsSelect[2]->getSelectedIndex();

   if ( 0 == enabled )
   {
      bool anyEnabled = false;
      for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
      if ( i != m_iInterfaceIndex )
      {
         radio_hw_info_t* pNIC2 = hardware_get_radio_info(i);
         if ( NULL == pNIC2 )
            break;
         u32 flags = controllerGetCardFlags(pNIC2->szMAC);
         if ( !(flags & RADIO_HW_CAPABILITY_FLAG_DISABLED) )
         {
            anyEnabled = true;
            break;
         }
      }
      if ( ! anyEnabled )
      {
         addMessage("You can't disable all the radio interfaces!");
         return false;
      }
   }
   return true;
}

bool MenuControllerRadioInterfaceSiK::setCardFlags()
{
   radio_hw_info_t* pNIC = hardware_get_radio_info(m_iInterfaceIndex);
   if ( NULL == pNIC )
      return false;

   if ( ! checkFlagsConsistency() )
      return false;

   u32 cardFlags = controllerGetCardFlags(pNIC->szMAC);

   int enabled = m_pItemsSelect[2]->getSelectedIndex();
   if ( 1 == enabled )
   {
      controllerRemoveCardDisabled(pNIC->szMAC);
      cardFlags &= (~RADIO_HW_CAPABILITY_FLAG_DISABLED);
   }
   else
   {
      controllerSetCardDisabled(pNIC->szMAC);
      cardFlags |= RADIO_HW_CAPABILITY_FLAG_DISABLED;
   }

   cardFlags |= RADIO_HW_CAPABILITY_FLAG_CAN_RX | RADIO_HW_CAPABILITY_FLAG_CAN_TX;
   controllerSetCardTXRX(pNIC->szMAC);
   
   cardFlags |= RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA;
   
   controllerSetCardFlags(pNIC->szMAC, cardFlags);
   save_ControllerInterfacesSettings();
   return true;
}

int MenuControllerRadioInterfaceSiK::onBack()
{
   radio_hw_info_t* pNIC = hardware_get_radio_info(m_iInterfaceIndex);
   if ( NULL != pNIC )
   if ( m_IndexName == m_SelectedIndex )
   if ( m_pItemsEdit[0]->isEditing() )
   {
      m_pItemsEdit[0]->endEdit(false);
      char szBuff[1024];
      strcpy(szBuff, m_pItemsEdit[0]->getCurrentValue());
      controllerSetCardUserDefinedName(pNIC->szMAC, szBuff);
      save_ControllerInterfacesSettings();
      invalidate();
      valuesToUI();
      return 1;
   }

   return Menu::onBack();
}


void MenuControllerRadioInterfaceSiK::onSelectItem()
{
   if ( m_IndexName == m_SelectedIndex )
   {
      m_pItemsEdit[0]->beginEdit();
      return;
   }

   Menu::onSelectItem();

   if ( handle_commands_is_command_in_progress() )
   {
      handle_commands_show_popup_progress();
      return;
   }

   if ( m_pMenuItems[m_SelectedIndex]->isEditing() )
      return;

   char szBuff[256];

   radio_hw_info_t* pNIC = hardware_get_radio_info(m_iInterfaceIndex);
   if ( NULL == pNIC )
      return;

   if ( m_IndexEnabled == m_SelectedIndex )
   {
      if ( setCardFlags() )
      {
         showProgressInfo();
         pairing_stop();
         pairing_start_normal();
         hideProgressInfo();
      }
      else
         valuesToUI();
      return;
   }

   if ( m_IndexInternal == m_SelectedIndex )
   {
      controllerSetCardInternal(pNIC->szMAC, (bool)m_pItemsSelect[6]->getSelectedIndex());
      save_ControllerInterfacesSettings();
      valuesToUI();
      return;
   }
}
