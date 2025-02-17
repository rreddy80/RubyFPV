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
#include "../base/ctrl_interfaces.h"
#include "../common/string_utils.h"
#include "ui_alarms.h"
#include "colors.h"
#include "osd/osd_common.h"
#include "osd/osd_stats_radio.h"
#include "popup.h"
#include "link_watch.h"
#include "shared_vars.h"
#include "timers.h"
#include "warnings.h"
#include "menu/menu.h"
#include "menu/menu_confirmation_delete_logs.h"

u32 g_uPersistentAllAlarmsVehicle = 0;
u32 g_uPersistentAllAlarmsLocal = 0;

u32 s_TimeLastVehicleAlarm = 0;
u16 s_uLastVehicleAlarms = 0;

u32 s_TimeLastLocalAlarm = 0;
u16 s_uLastLocalAlarms = 0;

u32 s_TimeLastCPUOverloadAlarmVehicle = 0;
u32 s_TimeLastCPUOverloadAlarmController = 0;

bool s_bAlarmVehicleLowSpaceMenuShown = false;

#define MAX_POPUP_ALARMS 5
Popup* s_pPopupAlarms[MAX_POPUP_ALARMS];
// 0 is on top, count or MAX_POPUP_ALARMS is on bottom

void alarms_reset()
{
   for( int i=0; i<MAX_POPUP_ALARMS; i++ )
      s_pPopupAlarms[i] = NULL;
}

void alarms_reset_vehicle()
{
   s_bAlarmVehicleLowSpaceMenuShown = false;
}

void alarms_remove_all()
{
   for( int i=0; i<MAX_POPUP_ALARMS; i++ )
   {
      if ( popups_has_popup(s_pPopupAlarms[i]) )
         popups_remove(s_pPopupAlarms[i]);
      s_pPopupAlarms[i] = NULL;
   }
}

void alarms_render()
{
}

Popup* _get_next_available_alarm_popup(const char* szTitle, int timeout)
{
   float dY = 0.06 + g_pRenderEngine->textHeight(g_idFontMenuLarge);

   int iPosFree = -1;
   for( int i=0; i<MAX_POPUP_ALARMS; i++ )
   {
      if ( ! popups_has_popup(s_pPopupAlarms[i]) )
      {
         iPosFree = i;
         break;
      }
   }
   
   // No more room? remove one
   if ( iPosFree == -1 )
   {
      popups_remove(s_pPopupAlarms[0]);

      for( int i=0; i<MAX_POPUP_ALARMS-1; i++ )
         s_pPopupAlarms[i] = s_pPopupAlarms[i+1];

      s_pPopupAlarms[MAX_POPUP_ALARMS-1] = NULL;
      iPosFree = MAX_POPUP_ALARMS-1;
   }

   // Move all up on screen
   for( int i=0; i<iPosFree; i++ )
   {
      float fY = s_pPopupAlarms[i]->getBottomMargin();
      s_pPopupAlarms[i]->setBottomMargin(fY+dY);
   }

   Popup* pNew = new Popup(true, szTitle, timeout);
   pNew->setBottomAlign(true);
   pNew->setMaxWidth(0.76);
   pNew->setBottomMargin(0.0);

   s_pPopupAlarms[iPosFree] = pNew;   
   log_line("Add alarm popup on position %d", iPosFree);

   return pNew;
}

void alarms_add_from_vehicle(u32 uVehicleId, u32 uAlarms, u32 uFlags1, u32 uFlags2)
{
   g_uPersistentAllAlarmsVehicle |= uAlarms;

   Preferences* p = get_Preferences();
   if ( p->iDebugShowFullRXStats )
      return;

   char szAlarmPrefix[128];

   int iCountVehicles = 0;
   int iRuntimeIndex = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_VehiclesRuntimeInfo[i].uVehicleId == uVehicleId )
         iRuntimeIndex = i;

      if ( g_VehiclesRuntimeInfo[i].uVehicleId != 0 )
         iCountVehicles++;
   }

   szAlarmPrefix[0] = 0;

   if ( (iRuntimeIndex != -1) && (iCountVehicles > 1) && (NULL != g_VehiclesRuntimeInfo[iRuntimeIndex].pModel) )
      sprintf(szAlarmPrefix, "%s:", g_VehiclesRuntimeInfo[iRuntimeIndex].pModel->getShortName());
   else if ( NULL != g_pCurrentModel )
      sprintf(szAlarmPrefix, "%s:", g_pCurrentModel->getShortName());

   bool bShowAsWarning = false;
   u32 uIconId = g_idIconAlarm;
   char szAlarmText[256];
   char szAlarmText2[256];
   char szAlarmText3[256];

   char szAlarmDesc[2048];
   alarms_to_string(uAlarms, uFlags1, uFlags2, szAlarmDesc);
   sprintf(szAlarmText, "%s Generated alarm (%s)", szAlarmPrefix, szAlarmDesc);
   szAlarmText2[0] = 0;
   szAlarmText3[0] = 0;

   if ( g_bUpdateInProgress )
      return;
  
   if ( ! (uAlarms & ALARM_ID_VEHICLE_LOW_STORAGE_SPACE) )
   if ( ! (p->uEnabledAlarms & uAlarms) )
   {
      log_line(szAlarmText);
      log_line("This alarm is disabled. Do not show it in UI.");
      return;
   }

   if ( uAlarms & ALARM_ID_GENERIC )
   {
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_SWAP_RADIO_INTERFACES_NOT_POSSIBLE )
      {
         uIconId = g_idIconRadio;
         strcpy(szAlarmText, "Swapping vehicle's radio interfaces not possible with current radio configuration.");
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_SWAP_RADIO_INTERFACES_FAILED )
      {
         uIconId = g_idIconRadio;
         strcpy(szAlarmText, "Swapping vehicle's radio interfaces faile");
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_SWAPPED_RADIO_INTERFACES )
      {
         uIconId = g_idIconRadio;
         strcpy(szAlarmText, "Swapped vehicle's radio interfaces");
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_RELAYED_TELEMETRY_LOST )
      {
         warnings_add(uVehicleId, "Telemetry from relayed vehicle lost!", g_idIconCPU, get_Color_IconError());
         return;
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_RELAYED_TELEMETRY_RECOVERED )
      {
         warnings_add(uVehicleId, "Telemetry from relayed vehicle recovered", g_idIconCPU, get_Color_IconError());
         return;
      }
   }

   if ( uAlarms & ALARM_ID_FIRMWARE_OLD )
   {
      uIconId = g_idIconRadio;
      int iInterfaceIndex = uFlags1;
      sprintf(szAlarmText, "%s Radio interface %d firmware is too old!", szAlarmPrefix, iInterfaceIndex+1);
      if ( hardware_radio_index_is_sik_radio(iInterfaceIndex) )
         strcpy(szAlarmText2, "Update the SiK radio firmware to version 2.2");
   }

   if ( uAlarms & ALARM_ID_CPU_RX_LOOP_OVERLOAD )
   {
      uIconId = g_idIconRadio;
      sprintf(szAlarmText, "%s Radio Rx process had a spike of %d ms (read: %u micros, queue: %u micros)", szAlarmPrefix, uFlags1, (uFlags2 & 0xFFFF), (uFlags2>>16));
   }

   if ( uAlarms & ALARM_ID_RADIO_INTERFACE_DOWN )
   {
       if ( (NULL != g_pCurrentModel) && (uFlags1 < g_pCurrentModel->radioInterfacesParams.interfaces_count) )
       {
          const char* szCardModel = str_get_radio_card_model_string(g_pCurrentModel->radioInterfacesParams.interface_card_model[uFlags1]);
          if ( (NULL != szCardModel) && (0 != szCardModel[0]) )
             sprintf(szAlarmText, "%s Radio interface %u (%s) is not working", szAlarmPrefix, uFlags1, szCardModel);
          else
             sprintf(szAlarmText, "%s Radio interface %u (generic) is not working", szAlarmPrefix, uFlags1);
       }
       else
          sprintf(szAlarmText, "%s A radio interface is not working ", szAlarmPrefix);     
       strcpy(szAlarmText2, "Will try automatic recovery to restore it.");
   }

   if ( uAlarms & ALARM_ID_RADIO_INTERFACE_REINITIALIZED )
   {
       if ( (NULL != g_pCurrentModel) && (uFlags1 < g_pCurrentModel->radioInterfacesParams.interfaces_count) )
       {
          const char* szCardModel = str_get_radio_card_model_string(g_pCurrentModel->radioInterfacesParams.interface_card_model[uFlags1]);
          if ( (NULL != szCardModel) && (0 != szCardModel[0]) )
             sprintf(szAlarmText, "%s Radio interface %u (%s) was reinitialized", szAlarmPrefix, uFlags1, szCardModel);
          else
             sprintf(szAlarmText, "%s Radio interface %u (generic) was reinitialized", szAlarmPrefix, uFlags1);
       }
       else
          sprintf(szAlarmText, "%s A radio interface was reinitialized on the vehicle.", szAlarmPrefix);     
   }

   if ( uAlarms & ALARM_ID_GENERIC_STATUS_UPDATE )
   {
      if ( link_is_reconfiguring_radiolink() && g_bConfiguringRadioLinkWaitVehicleReconfiguration )
      {
         log_line("Received vehicle alarm with status update while configuring radio link %d", g_iConfiguringRadioLinkIndex);
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE )
            warnings_add_configuring_radio_link_line("Reconfiguring vehicle radio interfaces");
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE )
         {
            warnings_add_configuring_radio_link_line("Reconfigured vehicle radio interface");
            link_set_received_change_confirmation(false, true, false);
            if ( ! link_reconfiguring_is_waiting_for_confirmation() )
            {
               warnings_remove_configuring_radio_link(true);
               link_reset_reconfiguring_radiolink();
            }
         }
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED )
         {
            warnings_add_configuring_radio_link_line("Failed to reconfigure vehicle radio interfaces");
            link_set_received_change_confirmation(false, true, false);
            if ( ! link_reconfiguring_is_waiting_for_confirmation() )
            {
               warnings_remove_configuring_radio_link(false);
               link_reset_reconfiguring_radiolink();
            }
         }
         return;
      }
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE )
         sprintf(szAlarmText, "%s Reconfiguring radio interface...", szAlarmPrefix);
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE )
         sprintf(szAlarmText, "%s Reconfigured radio interface. Completed.", szAlarmPrefix);
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED )
         sprintf(szAlarmText, "%s Reconfiguring radio interface failed!", szAlarmPrefix);
   }

   if ( uAlarms & ALARM_ID_VEHICLE_VIDEO_CAPTURE_RESTARTED )
   {
      sprintf(szAlarmText, "%s The video capture was restarted", szAlarmPrefix);
      if ( 0 == uFlags1 )
         sprintf(szAlarmText2, "%s There was a malfunctioning of the capture program", szAlarmPrefix);
      else if ( 1 == uFlags1 )
      {
         sprintf(szAlarmText2, "%s There where parameters changes that required restart of the capture program", szAlarmPrefix);
         warnings_add( uVehicleId, "Video capture was restarted due to changed parameters", g_idIconCamera);
         return;
      }
      else
         sprintf(szAlarmText2, "%s There was a unknown malfunctioning of the capture program", szAlarmPrefix);
   
   }

   if ( uAlarms & ALARM_ID_VEHICLE_VIDEO_TX_BITRATE_TOO_LOW )
   {
       sprintf(szAlarmText, "%s The Tx radio bitrate is too low (less than %.1f Mbps)", szAlarmPrefix, 2.0);  
       strcpy(szAlarmText2, "Try to increase your video bitrate.");     
   }

   if ( uAlarms & ALARM_ID_VEHICLE_LOW_STORAGE_SPACE )
   {
       sprintf(szAlarmText, "%s Is running low on free storage space. %u Mb free", szAlarmPrefix, uFlags1);  
       strcpy(szAlarmText2, "Try to delete your vehicle logs or check your SD card on the vehicle."); 
       if ( ! s_bAlarmVehicleLowSpaceMenuShown )
       {
          s_bAlarmVehicleLowSpaceMenuShown = true;
          MenuConfirmationDeleteLogs* pMenu = new MenuConfirmationDeleteLogs(uFlags1, uFlags2);
          add_menu_to_stack(pMenu);
       }
   }

   if ( uAlarms & ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR )
   {
       sprintf(szAlarmText, "%s The SD card has write errors", szAlarmPrefix);  
       strcpy(szAlarmText2, "It's recommended you replace the SD card."); 
   }

   if ( uAlarms & ALARM_ID_RECEIVED_INVALID_RADIO_PACKET )
   {
      sprintf(szAlarmText, "%s Invalid unrecoverable data detected over the radio links! Some radio interfaces are not functioning correctly!", szAlarmPrefix);
      strcpy(szAlarmText2, "Try to: swap the radio interfaces, change the USB ports or replace the radio interfaces!");
   }

   if ( uAlarms & ALARM_ID_RECEIVED_INVALID_RADIO_PACKET_RECONSTRUCTED )
   {
      sprintf(szAlarmText, "%s Invalid but recovered data detected over the radio links! Some radio interfaces are not functioning correctly!", szAlarmPrefix);
      if ( uAlarms & ALARM_ID_RECEIVED_INVALID_RADIO_PACKET )
         sprintf(szAlarmText, "%s Both invalid and recoverable data detected over the radio links! Some radio interfaces are not functioning correctly!", szAlarmPrefix);
      strcpy(szAlarmText2, "Try to: swap the radio interfaces, change the USB ports or replace the radio interfaces!");
   }

   if ( uAlarms & ALARM_ID_VEHICLE_VIDEO_DATA_OVERLOAD )
   {
      uIconId = g_idIconCamera;
      g_bHasVideoDataOverloadAlarm = true;
      g_TimeLastVideoDataOverloadAlarm = g_TimeNow;

      int kbpsSent = (int)(uFlags1 & 0xFFFF);
      int kbpsMax = (int)(uFlags1 >> 16);
      if ( NULL != g_pCurrentModel && (! g_pCurrentModel->osd_params.show_overload_alarm) )
         return;
      sprintf(szAlarmText, "%s Video link is overloaded (sending %.1f Mbps, over the safe %.1f Mbps limit for current radio rate)", szAlarmPrefix, ((float)kbpsSent)/1000.0, ((float)kbpsMax)/1000.0);
      strcpy(szAlarmText2, "Video bitrate was decreased temporarly.");
      strcpy(szAlarmText3, "Lower your video bitrate.");
   }

   if ( uAlarms & ALARM_ID_RADIO_LINK_DATA_OVERLOAD )
   {
      if ( g_TimeNow > g_uTimeLastRadioLinkOverloadAlarm + 5000 )
      {
         uIconId = g_idIconRadio;
         g_uTimeLastRadioLinkOverloadAlarm = g_TimeNow;

         int iRadioInterface = (int)(uFlags1 >> 24);
         int bpsSent = (int)(uFlags1 & 0xFFFFFF);
         int bpsMax = (int)(uFlags2);
         sprintf(szAlarmText, "%s Vehicle radio link %d is overloaded (sending %.1f kbytes/sec, max air rate for this link is %.1f kbytes/sec).",
            szAlarmPrefix, iRadioInterface+1, ((float)bpsSent)/1000.0, ((float)bpsMax)/1000.0);
         strcpy(szAlarmText2, "Lower the amount of data you send on this radio link.");
      }
      else
         return;
   }

   if ( uAlarms & ALARM_ID_VEHICLE_VIDEO_TX_OVERLOAD )
   {
      if ( g_TimeNow < g_TimeLastVideoTxOverloadAlarm + 5000 )
         return;
      if ( g_TimeNow < g_RouterIsReadyTimestamp + 2000 )
         return;
      uIconId = g_idIconCamera;
      g_bHasVideoTxOverloadAlarm = true;
      g_TimeLastVideoTxOverloadAlarm = g_TimeNow;

      int ms = (int)(uFlags1 & 0xFFFF);
      int msMax = (int)(uFlags1 >> 16);
      if ( NULL != g_pCurrentModel && (! g_pCurrentModel->osd_params.show_overload_alarm) )
         return;
      if ( uFlags2 )
         sprintf(szAlarmText, "%s Video link transmission had an Tx overload spike", szAlarmPrefix);
      else
      {
         sprintf(szAlarmText, "%s Video link transmission is overloaded (transmission took %d ms/sec, max safe limit is %d ms/sec)", szAlarmPrefix, ms, msMax);
         osd_stats_set_last_tx_overload_time_ms(ms);
      }
      strcpy(szAlarmText2, "Video bitrate was decreased temporarly.");
      strcpy(szAlarmText3, "Lower your video bitrate or switch frequencies.");
   }

   if ( uAlarms & ALARM_ID_VEHICLE_CPU_LOOP_OVERLOAD )
   if ( !g_bUpdateInProgress )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      bool bShow = false;
      if ( pCS->iDeveloperMode )
         bShow = true;
      else
      {
         u32 timeMax = uFlags1 >> 16;
         if ( timeMax > 100 )
            bShow = true;
      }

      if ( bShow && (g_TimeNow > s_TimeLastCPUOverloadAlarmVehicle + 10000) )
      {
         s_TimeLastCPUOverloadAlarmVehicle = g_TimeNow;
         u32 timeAvg = uFlags1 & 0xFFFF;
         u32 timeMax = uFlags1 >> 16;
         if ( timeMax > 0 )
         {
            sprintf(szAlarmText, "%s The CPU had a spike of %u miliseconds in it's loop processing", szAlarmPrefix, timeMax );
            sprintf(szAlarmText2, "If this is recurent, increase your CPU overclocking speed or reduce your data processing load (video bitrate).");
         }
         if ( timeAvg > 0 )
         {
            sprintf(szAlarmText, "%s The CPU had an overload of %u miliseconds in it's loop processing", szAlarmPrefix, timeAvg );
            sprintf(szAlarmText2, "Increase your CPU overclocking speed or reduce your data processing (video bitrate).");
         }
      }
      else
         return;
   }

   if ( uAlarms & ALARM_ID_VEHICLE_RX_TIMEOUT )
   {
      if ( g_bUpdateInProgress )
         return;
      sprintf(szAlarmText, "%s Radio interface %d timed out reading a radio packet %d times", szAlarmPrefix, 1+(int)uFlags1, (int)uFlags2 );
      bShowAsWarning = true;
   }

   if ( (g_TimeNow > s_TimeLastVehicleAlarm + 8000) || ((s_uLastVehicleAlarms != uAlarms) && (uAlarms != 0)) )
   {
      log_line("Adding UI for alarm from vehicle: [%s]", szAlarmText);
      s_TimeLastVehicleAlarm = g_TimeNow;
      s_uLastVehicleAlarms = uAlarms;

      if ( (uAlarms & ALARM_ID_VEHICLE_VIDEO_TX_OVERLOAD) || (uAlarms & ALARM_ID_VEHICLE_VIDEO_DATA_OVERLOAD) )
         bShowAsWarning = true;
      
      if ( bShowAsWarning )
         warnings_add(0, szAlarmText, uIconId);
      else
      {
         Popup* p = _get_next_available_alarm_popup(szAlarmText, 7);
         p->setFont(g_idFontOSDSmall);
         p->setIconId(uIconId, get_Color_IconNormal());
         
         if ( 0 != szAlarmText2[0] )
            p->addLine(szAlarmText2);
         if ( 0 != szAlarmText3[0] )
            p->addLine(szAlarmText3);
         popups_add_topmost(p);

         if ( (uAlarms & ALARM_ID_VEHICLE_VIDEO_TX_OVERLOAD) || (uAlarms & ALARM_ID_VEHICLE_VIDEO_DATA_OVERLOAD) )
            g_pPopupVideoOverloadAlarm = p;
      }
   }
}

void alarms_add_from_local(u32 uAlarms, u32 uFlags1, u32 uFlags2)
{
   g_uPersistentAllAlarmsLocal |= uAlarms;

   Preferences* p = get_Preferences();
   if ( p->iDebugShowFullRXStats )
      return;

   if ( (uAlarms & ALARM_ID_CONTROLLER_CPU_LOOP_OVERLOAD) || (uAlarms & ALARM_ID_CONTROLLER_IO_ERROR) )
      g_nTotalControllerCPUSpikes++;

   bool bShowAsWarning = false;
   u32 uIconId = g_idIconAlarm;
   char szAlarmText[256];
   char szAlarmText2[256];

   char szAlarmDesc[2048];
   alarms_to_string(uAlarms, uFlags1, uFlags2, szAlarmDesc);
   sprintf(szAlarmText, "Received alarm (%s) from the vehicle", szAlarmDesc);

   sprintf(szAlarmText, "Triggered alarm(%s) on the controller", szAlarmDesc);
   szAlarmText2[0] = 0;

   if ( g_bUpdateInProgress )
      return;

   if ( uAlarms != ALARM_ID_CONTROLLER_IO_ERROR )
   if ( uAlarms != ALARM_ID_CONTROLLER_LOW_STORAGE_SPACE )
   if ( ! (p->uEnabledAlarms & uAlarms) )
   {
      log_line(szAlarmText);
      log_line("This alarm is disabled. Do not show it in UI.");
      return;
   }

   if ( uAlarms & ALARM_ID_FIRMWARE_OLD )
   {
      uIconId = g_idIconRadio;
      int iInterfaceIndex = uFlags1;
      sprintf(szAlarmText, "Controller radio interface %d firmware is too old!", iInterfaceIndex+1);
      if ( hardware_radio_index_is_sik_radio(iInterfaceIndex) )
         strcpy(szAlarmText2, "Update the SiK radio firmware to version 2.2");
   }

   if ( uAlarms & ALARM_ID_CPU_RX_LOOP_OVERLOAD )
   {
      uIconId = g_idIconRadio;
      sprintf(szAlarmText, "Controller radio Rx process had a spike of %d ms (read: %u micros, queue: %u micros)", uFlags1, (uFlags2 & 0xFFFF), (uFlags2>>16));
   }

   if ( uAlarms & ALARM_ID_GENERIC )
   {
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_UNKNOWN_VIDEO )
      {
         uIconId = g_idIconCamera;
         sprintf(szAlarmText, "Receiving video stream from unknown vehicle (Id %u)",uFlags2);
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_RECEIVED_DEPRECATED_MESSAGE )
      {
         static u32 s_uTimeLastAlarmDeprecatedMessage = 0;
         if ( g_TimeNow > s_uTimeLastAlarmDeprecatedMessage + 10000 )
         {
            s_uTimeLastAlarmDeprecatedMessage = g_TimeNow;
            sprintf(szAlarmText, "Received deprecated message from vehicle. Message type: %u", uFlags2);
         }
         else
            return;
      }
      if ( uFlags1 == ALARM_ID_GENERIC_TYPE_ADAPTIVE_VIDEO_LEVEL_MISSMATCH )
      {
         uIconId = g_idIconCamera;

         int iProfile = g_pCurrentModel->get_video_profile_from_total_levels_shift((int)(uFlags2>>16));
         char szTmp1[64];
         char szTmp2[64];
         strcpy(szTmp1, str_get_video_profile_name(uFlags2 & 0xFFFF));
         strcpy(szTmp2, str_get_video_profile_name(iProfile));
               
         sprintf(szAlarmText, "Received video stream profile (%s) is different than the last requested one (%s, level shift %u). Requesting it again.", szTmp1, szTmp2, uFlags2 >> 16);
      }
   }

   if ( uAlarms & ALARM_ID_RADIO_INTERFACE_DOWN )
   {  
       if ( uFlags1 < hardware_get_radio_interfaces_count() )
       {
          radio_hw_info_t* pRadioInfo = hardware_get_radio_info(uFlags1);
          
          char szCardName[128];
          szCardName[0] = 0;
          controllerGetCardUserDefinedNameOrType(pRadioInfo, szCardName);
          if ( 0 == szCardName[0] )
             strcpy(szCardName, "generic");
                
          if ( (NULL != szCardName) && (0 != szCardName[0]) )
             sprintf(szAlarmText, "Radio interface %u (%s) on the controller is not working.", uFlags1+1, szCardName);
          else
             sprintf(szAlarmText, "Radio interface %u (generic) on the controller is not working.", uFlags1+1);
       }
       else
          strcpy(szAlarmText, "A radio interface on the controller is not working.");     
   }

   if ( uAlarms & ALARM_ID_RADIO_INTERFACE_REINITIALIZED )
   {  
       if ( uFlags1 < hardware_get_radio_interfaces_count() )
       {
          radio_hw_info_t* pRadioInfo = hardware_get_radio_info(uFlags1);
          
          char szCardName[128];
          controllerGetCardUserDefinedNameOrType(pRadioInfo, szCardName);
          if ( 0 == szCardName[0] )
             strcpy(szCardName, "generic");
                
          if ( (NULL != szCardName) && (0 != szCardName[0]) )
             sprintf(szAlarmText, "Radio interface %u (%s) was reinitialized on the controller.", uFlags1+1, szCardName);
          else
             sprintf(szAlarmText, "Radio interface %u (generic) was reinitialized on the controller.", uFlags1+1);
       }
       else
          strcpy(szAlarmText, "A radio interface was reinitialized on the controller.");     
   }

   if ( uAlarms & ALARM_ID_GENERIC_STATUS_UPDATE )
   {
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_REASIGNING_RADIO_LINKS_START )
      {
         strcpy(szAlarmText, "Reconfiguring radio links to vehicle...");
         g_bReconfiguringRadioLinks = true;
      }
      else if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_REASIGNING_RADIO_LINKS_END )
      {
         strcpy(szAlarmText, "Finished reconfiguring radio links to vehicle.");
         g_bReconfiguringRadioLinks = false;
      }
      else if ( link_is_reconfiguring_radiolink() && g_bConfiguringRadioLinkWaitControllerReconfiguration )
      {
         log_line("Received controller alarm with status update while configuring radio link %d", g_iConfiguringRadioLinkIndex);
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE )
            warnings_add_configuring_radio_link_line("Reconfiguring controller radio interfaces");
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE )
         {
            warnings_add_configuring_radio_link_line("Reconfigured controller radio interface");
            link_set_received_change_confirmation(false, false, true);
            if ( ! link_reconfiguring_is_waiting_for_confirmation() )
            {
               warnings_remove_configuring_radio_link(true);
               link_reset_reconfiguring_radiolink();
            }
         }
         if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED )
         {
            warnings_add_configuring_radio_link_line("Failed to reconfigure controller radio interfaces");
            link_set_received_change_confirmation(false, false, true);
            if ( ! link_reconfiguring_is_waiting_for_confirmation() )
            {
               warnings_remove_configuring_radio_link(false);
               link_reset_reconfiguring_radiolink();
            }
         }
         return;
      }
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE )
         strcpy(szAlarmText, "Reconfiguring radio interface...");
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE )
         strcpy(szAlarmText, "Reconfigured radio interface. Completed.");
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED )
         strcpy(szAlarmText, "Reconfiguring radio interface failed!");
      if ( uFlags1 == ALARM_FLAG_GENERIC_STATUS_SENT_PAIRING_REQUEST )
      {
         Model* pModel = findModelWithId(uFlags2);
         t_structure_vehicle_info* pRuntimeInfo = get_vehicle_runtime_info_for_vehicle_id(uFlags2);
         if ( (NULL == pModel) || (NULL == pRuntimeInfo) )
         {
            static bool s_bWarningUnknownVehiclePairing = false;
            if ( ! s_bWarningUnknownVehiclePairing )
               warnings_add(0, "Pairing with unknown vehicle...", g_idIconController, get_Color_IconWarning());
            s_bWarningUnknownVehiclePairing = true;
         }
         else if ( ! pRuntimeInfo->bNotificationPairingRequestSent )
         {
            pRuntimeInfo->bNotificationPairingRequestSent = true;
            char szBuff[256];
            sprintf(szBuff, "Pairing with %s...", pModel->getLongName());
            warnings_add(0, szBuff, g_idIconController);
         }
         return;
      }
   }

   if ( uAlarms & ALARM_ID_RADIO_LINK_DATA_OVERLOAD )
   {
      if ( g_TimeNow > g_uTimeLastRadioLinkOverloadAlarm + 5000 )
      {
         uIconId = g_idIconRadio;
         g_uTimeLastRadioLinkOverloadAlarm = g_TimeNow;

         int iRadioInterface = (int)(uFlags1 >> 24);
         int bpsSent = (int)(uFlags1 & 0xFFFFFF);
         int bpsMax = (int)(uFlags2);
         sprintf(szAlarmText, "Controller radio interface %d is overloaded (sending %.1f kbytes/sec, max air rate for this interface is %.1f kbytes/sec).",
            iRadioInterface+1, ((float)bpsSent)/1000.0, ((float)bpsMax)/1000.0);
         strcpy(szAlarmText2, "Lower the amount of data you send on this radio interface.");
      }
      else
         return;
   }

   if ( uAlarms & ALARM_ID_CONTROLLER_LOW_STORAGE_SPACE )
   {
       sprintf(szAlarmText, "Controller is running low on free storage space. %u Mb free.", uFlags1);  
       strcpy(szAlarmText2, "Try to delete your controller logs or some media files or check your SD card."); 
   }

   if ( uAlarms & ALARM_ID_CONTROLLER_STORAGE_WRITE_ERRROR )
   {
       strcpy(szAlarmText, "The SD card on the controller has write errors.");  
       strcpy(szAlarmText2, "It's recommended you replace the SD card."); 
   }

   if ( uAlarms & ALARM_ID_RECEIVED_INVALID_RADIO_PACKET )
   {
      strcpy(szAlarmText, "Invalid data detected over the radio links! Some radio interfaces are not functioning correctly on the controller!");
      strcpy(szAlarmText2, "Try to: swap the radio interfaces, change the USB ports or replace the radio interfaces!");
   }

   static u32 s_uLastTimeAlarmNoRadioInterfacesForRadioLink = 0;
   static u32 s_uLastAlarmNoRadioInterfacesForRadioLinkId = 0;
   
   if ( uAlarms & ALARM_ID_CONTROLLER_NO_INTERFACES_FOR_RADIO_LINK )
   {
      if ( s_uLastAlarmNoRadioInterfacesForRadioLinkId == uFlags1 )
      if ( g_TimeNow < s_uLastTimeAlarmNoRadioInterfacesForRadioLink + 1000 )
         return;

      s_uLastAlarmNoRadioInterfacesForRadioLinkId = uFlags1;
      s_uLastTimeAlarmNoRadioInterfacesForRadioLink = g_TimeNow;
      sprintf(szAlarmText, "No radio interfaces on this controller can handle the vehicle's radio link %d.", uFlags1+1);
      sprintf(szAlarmText2, "Add more radio interfaces to your controller or change radio links settings.");
   }
   
   if ( uAlarms & ALARM_ID_CONTROLLER_CPU_LOOP_OVERLOAD )
   {
      if ( g_bUpdateInProgress )
         return;

      ControllerSettings* pCS = get_ControllerSettings();
      bool bShow = false;
      if ( pCS->iDeveloperMode )
         bShow = true;
      else
      {
         u32 timeMax = uFlags1 >> 16;
         if ( timeMax > 100 )
            bShow = true;
      }

      if ( g_TimeNow > s_TimeLastCPUOverloadAlarmController + 10000 )
      {
         s_TimeLastCPUOverloadAlarmController = g_TimeNow;
         u32 timeAvg = uFlags1 & 0xFFFF;
         u32 timeMax = uFlags1 >> 16;
         if ( timeMax > 0 )
         {
            sprintf(szAlarmText, "Your controller CPU had a spike of %u miliseconds in it's loop processing.", timeMax );
            sprintf(szAlarmText2, "If this is recurent, increase your CPU overclocking speed or reduce your data processing load (video bitrate).");
         }
         if ( timeAvg > 0 )
         {
            sprintf(szAlarmText, "Your controller CPU had an overload of %u miliseconds in it's loop processing.", timeAvg );
            sprintf(szAlarmText2, "Increase your CPU overclocking speed or reduce your data processing load (video bitrate).");
         }
      }
      else
         return;
   }

   if ( uAlarms & ALARM_ID_CONTROLLER_RX_TIMEOUT )
   {
      if ( g_bUpdateInProgress )
         return;
      sprintf(szAlarmText, "Controller radio interface %d timed out reading a radio packet %d times.", 1+(int)uFlags1, (int)uFlags2 );
      bShowAsWarning = true;
   }

   if ( uAlarms & ALARM_ID_CONTROLLER_IO_ERROR )
   {
      if ( g_uLastControllerAlarmIOFlags == uFlags1 )
         return;

      g_uLastControllerAlarmIOFlags = uFlags1;

      if ( 0 == uFlags1 )
      {
         warnings_add(0, "Controller IO Error Alarm cleared.");
         return;
      }
      strcpy(szAlarmText, "Controller IO Error!");
      szAlarmText2[0] = 0;
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_USB_OUTPUT )
         strcpy(szAlarmText2, "Video USB output error.");
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_USB_OUTPUT_TRUNCATED )
         strcpy(szAlarmText2, "Video USB output truncated.");
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_USB_OUTPUT_WOULD_BLOCK )
         strcpy(szAlarmText2, "Video USB output full.");
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_PLAYER_OUTPUT )
         strcpy(szAlarmText2, "Video player output error.");
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_PLAYER_OUTPUT_TRUNCATED )
         strcpy(szAlarmText2, "Video player output truncated.");
      if ( uFlags1 & ALARM_FLAG_IO_ERROR_VIDEO_PLAYER_OUTPUT_WOULD_BLOCK )
         strcpy(szAlarmText2, "Video player output full.");
   }

   if ( g_TimeNow > s_TimeLastLocalAlarm + 10000 || ((s_uLastLocalAlarms != uAlarms) && (uAlarms != 0)) )
   {
      log_line("Adding UI for alarm from controller: [%s]", szAlarmText);
      s_TimeLastLocalAlarm = g_TimeNow;
      s_uLastLocalAlarms = uAlarms;

      if ( uAlarms & ALARM_ID_CONTROLLER_PAIRING_COMPLETED )
      {
         log_line("Adding warning for pairing with vehicle id %u ...", uFlags1);
         Model* pModel = findModelWithId(uFlags1);
         if ( NULL == pModel )
            warnings_add(0, "Paired with a unknown vehicle.", g_idIconController);
         else
         {
            log_line("Paired with %s, VID: %u", pModel->getLongName(), pModel->vehicle_id);
            char szBuff[256];
            sprintf(szBuff, "Paired with %s", pModel->getLongName());
            warnings_add(0, szBuff, g_idIconController);

            int iIndexRuntime = -1;
            for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
            {
               if ( g_VehiclesRuntimeInfo[i].uVehicleId == pModel->vehicle_id )
               {
                  iIndexRuntime = i;
                  break;
               }
            }
            bool bHasCamera = false;
            log_line("Vehicle has %d cameras.", pModel->iCameraCount);
            if ( (pModel->sw_version >>16) < 79 ) // 7.7
            {
               bHasCamera = true;
               log_line("Vehicle (VID %u) has software older than 7.7. Assume camera present.");
            }
            else if ( pModel->iCameraCount > 0 )
            {
               bHasCamera = true;
               log_line("Vehicle has %d cameras.", pModel->iCameraCount);
            }

            if ( -1 != iIndexRuntime )
            if ( (pModel->sw_version >>16) > 79 ) // 7.7
            if ( g_VehiclesRuntimeInfo[iIndexRuntime].bGotRubyTelemetryInfo )
            {
               if ( !( g_VehiclesRuntimeInfo[iIndexRuntime].headerRubyTelemetryExtended.flags & FLAG_RUBY_TELEMETRY_VEHICLE_HAS_CAMERA) )
               {
                  bHasCamera = false;
                  log_line("Vehicle has camera flag not set in received telemetry.");
               }
               else
               {
                  bHasCamera = true;
                  log_line("Vehicle has camera flag set in received telemetry.");
               }
            }
            if ( ! bHasCamera )
               warnings_add(pModel->vehicle_id, "This vehicle has no camera or video streams.", g_idIconCamera, get_Color_IconError());
         }
         return;
      }

      if ( bShowAsWarning )
         warnings_add(0, szAlarmText, uIconId);
      else
      {
         Popup* p = _get_next_available_alarm_popup(szAlarmText, 15);
         p->setFont(g_idFontOSDSmall);
         p->setIconId(uIconId, get_Color_IconNormal());
         if ( 0 != szAlarmText2[0] )
            p->addLine(szAlarmText2);
         popups_add_topmost(p);
      }
   }
}
