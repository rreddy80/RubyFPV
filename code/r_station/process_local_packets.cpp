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

#include <stdlib.h>
#include <stdio.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/commands.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/encr.h"
#include "../base/radio_utils.h"
#include "../base/hardware.h"
#include "../base/hw_procs.h"
#include "../base/ruby_ipc.h"
#include "../base/controller_utils.h"
#include "../common/string_utils.h"
#include "../common/radio_stats.h"
#include "../common/models_connect_frequencies.h"
#include "../common/relay_utils.h"
#include "../radio/radiolink.h"
#include "../radio/radiopacketsqueue.h"
#include "../radio/radio_rx.h"
#include "../radio/radio_tx.h"
#include "../radio/radio_duplicate_det.h"

#include "shared_vars.h"
#include "links_utils.h"
#include "ruby_rt_station.h"
#include "processor_rx_audio.h"
#include "processor_rx_video.h"
#include "rx_video_output.h"
#include "process_radio_in_packets.h"
#include "packets_utils.h"
#include "video_link_adaptive.h"
#include "video_link_keyframe.h"
#include "timers.h"


bool _switch_to_vehicle_radio_link(int iVehicleRadioLinkId)
{
   if ( (iVehicleRadioLinkId < 0) || (iVehicleRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count) )
      return false;

   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
      return false;
   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      return false;
   if (  !(g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
      return false;
   
   int iCountAssignableRadioInterfaces = controller_count_asignable_radio_interfaces_to_vehicle_radio_link(g_pCurrentModel, iVehicleRadioLinkId);
   if ( iCountAssignableRadioInterfaces < 1 )
      return false;

   // We must iterate local radio interfaces and switch one from a radio link to this radio link, if it supports this link
   // We must also update the radio state to reflect the new assigned radio links to local radio interfaces
   
   bool bSwitched = false;
   for( int iInterface=0; iInterface<hardware_get_radio_interfaces_count(); iInterface++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iInterface);
      if ( NULL == pRadioHWInfo )
         continue;

      t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
      if ( NULL == pCardInfo )
         continue;

      if ( ! hardware_radio_supports_frequency(pRadioHWInfo, g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]) )
         continue;

      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
         continue;

      if ( ! (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
         continue;

      if ( ! (pCardInfo->capabilities_flags & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
         continue;

      if ( pCardInfo->capabilities_flags & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;

      // Change frequency of the card to the new radio link
      u32 uCurrentFrequencyKhz = pRadioHWInfo->uCurrentFrequencyKhz;

      if ( ! radio_utils_set_interface_frequency(NULL, iInterface, g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId], g_pProcessStats) )
      {
         log_softerror_and_alarm("Failed to change local radio interface %d to new frequency %s", iInterface, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]));
         log_line("Switching back local radio interface %d to previouse frequency %s", iInterface, str_format_frequency(uCurrentFrequencyKhz));
         radio_utils_set_interface_frequency(NULL, iInterface, uCurrentFrequencyKhz, g_pProcessStats);
         continue;
      }
            
      radio_stats_set_card_current_frequency(&g_SM_RadioStats, iInterface, g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]);

      int iLocalRadioLinkId = g_SM_RadioStats.radio_interfaces[iInterface].assignedLocalRadioLinkId;
      g_SM_RadioStats.radio_interfaces[iInterface].assignedVehicleRadioLinkId = iVehicleRadioLinkId;
      g_SM_RadioStats.radio_links[iLocalRadioLinkId].matchingVehicleRadioLinkId = iVehicleRadioLinkId;

      // Update controller's main connect frequency too if needed
      if ( uCurrentFrequencyKhz == get_model_main_connect_frequency(g_pCurrentModel->vehicle_id) )
         set_model_main_connect_frequency(g_pCurrentModel->vehicle_id, g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]);
               
      bSwitched = true;
      break;
   }

   if ( ! bSwitched )
      return false;

   // Update the radio state to reflect the new assigned radio links to local radio interfaces

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   return true;
}

void _process_local_notification_model_changed(t_packet_header* pPH, u8 uChangeType, int fromComponentId, int iExtraParam)
{
   type_radio_interfaces_parameters oldRadioInterfacesParams;
   type_radio_links_parameters oldRadioLinksParams;
   audio_parameters_t oldAudioParams;
   type_relay_parameters oldRelayParams;

   memcpy(&oldRadioInterfacesParams, &(g_pCurrentModel->radioInterfacesParams), sizeof(type_radio_interfaces_parameters));
   memcpy(&oldRadioLinksParams, &(g_pCurrentModel->radioLinksParams), sizeof(type_radio_links_parameters));
   memcpy(&oldAudioParams, &(g_pCurrentModel->audio_params), sizeof(audio_parameters_t));
   memcpy(&oldRelayParams, &(g_pCurrentModel->relay_params), sizeof(type_relay_parameters));

   u32 oldEFlags = g_pCurrentModel->enc_flags;
   ControllerSettings* pCS = get_ControllerSettings();
   int iOldTxPowerSiK = pCS->iTXPowerSiK;

   if ( uChangeType == MODEL_CHANGED_RADIO_POWERS )
   {
      log_line("Received local notification that radio powers have changed.");
      load_ControllerSettings();
      if ( iOldTxPowerSiK == pCS->iTXPowerSiK )
      {
         log_line("SiK Tx power is unchanged. Ignoring this notification.");
         return;
      }
      log_line("SiK Tx power changed from %d to %d. Updating SiK radio interfaces params...", iOldTxPowerSiK, pCS->iTXPowerSiK);
      if ( g_SiKRadiosState.bConfiguringToolInProgress )
      {
         log_softerror_and_alarm("Another SiK configuration is in progress. Ignoring this one.");
         return;
      }

      if ( g_SiKRadiosState.bConfiguringSiKThreadWorking )
      {
         log_softerror_and_alarm("SiK reinitialization thread is in progress. Ignoring this notification.");
         return;
      }

      if ( g_SiKRadiosState.bMustReinitSiKInterfaces )
      {
         log_softerror_and_alarm("SiK reinitialization is already flagged. Ignoring this notification.");
         return;
      }

      close_and_mark_sik_interfaces_to_reopen();
      g_SiKRadiosState.bConfiguringToolInProgress = true;
      g_SiKRadiosState.uTimeStartConfiguring = g_TimeNow;
         
      char szCommand[256];
      sprintf(szCommand, "rm -rf %s", FILE_TMP_SIK_CONFIG_FINISHED);
      hw_execute_bash_command(szCommand, NULL);

      sprintf(szCommand, "./ruby_sik_config none 0 -power %d &", pCS->iTXPowerSiK);
      hw_execute_bash_command(szCommand, NULL);

      // Do not need to notify other components. Just return.
      return;
   }

   // Reload new model state
   if ( ! reloadCurrentModel() )
      log_softerror_and_alarm("Failed to load current model.");

   if ( uChangeType == MODEL_CHANGED_SYNCHRONISED_SETTINGS_FROM_VEHICLE )
   {
      g_pCurrentModel->b_mustSyncFromVehicle = false;
      log_line("Model settings where synchronised from vehicle. Reset model must sync flag.");
   }

   if ( g_pCurrentModel->enc_flags != oldEFlags )
      lpp(NULL, 0);

   if ( uChangeType == MODEL_CHANGED_SIK_PACKET_SIZE )
   {
      log_line("Received notification from central that SiK packet size whas changed to: %d bytes", iExtraParam);
      log_line("Current model new SiK packet size: %d", g_pCurrentModel->radioLinksParams.iSiKPacketSize); 
      radio_tx_set_sik_packet_size(g_pCurrentModel->radioLinksParams.iSiKPacketSize);
      return;
   }

   if ( uChangeType == MODEL_CHANGED_ROTATED_RADIO_LINKS )
   {
      log_line("Received notification from central that vehicle radio links have been rotated.");
      int iTmp = 0;

      // Rotate info in radio stats: rotate radio links ids
      for( int i=0; i<g_SM_RadioStats.countLocalRadioLinks; i++ )
      {
         g_SM_RadioStats.radio_links[i].lastTxInterfaceIndex = -1;
         if ( g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId >= 0 )
         {
            iTmp = g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId;
            g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId++;
            if ( g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId >= g_SM_RadioStats.countVehicleRadioLinks )
               g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId = 0;
            log_line("Rotated radio stats local radio link %d from vehicle radio link %d to vehicle radio link %d",
               i+1, iTmp+1, g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId);
         }
      }

      // Rotate info in radio stats: rotate radio interfaces assigned radio links
      for( int i=0; i<g_SM_RadioStats.countLocalRadioInterfaces; i++ )
      {
         if ( g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId >= 0 )
         {
            iTmp = g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId;
            g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId++;
            if ( g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId >= g_SM_RadioStats.countVehicleRadioLinks )
               g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = 0;
            log_line("Rotated radio stats local radio interface %d (assigned to local radio link %d) from vehicle radio link %d to vehicle radio link %d",
               i, g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1,
               iTmp, g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId);
         }
      }

      // Update the radio state to reflect the new radio links
      if ( NULL != g_pSM_RadioStats )
         memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   
      return;
   }

   if ( uChangeType == MODEL_CHANGED_RADIO_LINK_FRAMES_FLAGS )
   {
      int iLink = (pPH->vehicle_id_src >> 16) & 0xFF;
      log_line("Received notification that radio link flags where changed for radio link %d.", iLink+1);
      
      u32 radioFlags = g_pCurrentModel->radioLinksParams.link_radio_flags[iLink];
      log_line("Radio link %d new datarates: %d/%d", iLink+1, g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iLink], g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iLink]);
      log_line("Radio link %d new radio flags: %s", iLink+1, str_get_radio_frame_flags_description2(radioFlags));
   
      if( (NULL != g_pCurrentModel) && g_pCurrentModel->radioLinkIsSiKRadio(iLink) )
      {
         log_line("Radio flags or radio datarates changed on a SiK radio link (link %d).", iLink+1);
         for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
         {
            if ( g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId == iLink )
            if ( hardware_radio_index_is_sik_radio(i) )
               flag_update_sik_interface(i);
         }
      }

      return;
   }

   if ( uChangeType == MODEL_CHANGED_RADIO_DATARATES )
   {
      int iRadioLink = iExtraParam;
      if ( 0 == memcmp((u8*)&oldRadioLinksParams, (u8*)&g_pCurrentModel->radioLinksParams, sizeof(type_radio_links_parameters) ) )
      {
         log_line("Received local notification that radio data rates have changed on radio link %d but no data rate was changed. Ignoring it.", iRadioLink + 1);
         return;
      }
      log_line("Received local notification that radio data rates have changed on radio link %d", iRadioLink+1);
      log_line("New radio data rates for link %d: v: %d, d: %d/%d, u: %d/%d",
            iRadioLink, g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iRadioLink],
            g_pCurrentModel->radioLinksParams.uDownlinkDataDataRateType[iRadioLink],
            g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iRadioLink],
            g_pCurrentModel->radioLinksParams.uUplinkDataDataRateType[iRadioLink],
            g_pCurrentModel->radioLinksParams.uplink_datarate_data_bps[iRadioLink]);

      // If uplink data rate for an Atheros card has changed, update it.
      
      for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
      {
         if ( g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId != iRadioLink )
            continue;
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
         if ( NULL == pRadioHWInfo )
            continue;
         if ( controllerIsCardDisabled(pRadioHWInfo->szMAC) )
            continue;

         int nRateTx = DEFAULT_RADIO_DATARATE_LOWEST;
         if ( g_pCurrentModel->radioLinksParams.uUplinkDataDataRateType[iRadioLink] == FLAG_RADIO_LINK_DATARATE_DATA_TYPE_LOWEST )
            nRateTx = DEFAULT_RADIO_DATARATE_LOWEST;
         if ( g_pCurrentModel->radioLinksParams.uUplinkDataDataRateType[iRadioLink] == FLAG_RADIO_LINK_DATARATE_DATA_TYPE_FIXED )
            nRateTx = g_pCurrentModel->radioLinksParams.uplink_datarate_data_bps[iRadioLink];
         if ( g_pCurrentModel->radioLinksParams.uUplinkDataDataRateType[iRadioLink] == FLAG_RADIO_LINK_DATARATE_DATA_TYPE_SAME_AS_ADAPTIVE_VIDEO)
            nRateTx = g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iRadioLink];
         update_atheros_card_datarate(g_pCurrentModel, i, nRateTx, g_pProcessStats);
         g_TimeNow = get_current_timestamp_ms();
      }
      return;
   }

   if ( uChangeType == MODEL_CHANGED_RELAY_PARAMS )
   {
      log_line("Received notification from central that relay flags and params changed.");

      if ( NULL != g_pCurrentModel )
         radio_duplicate_detection_remove_data_for_all_except(g_pCurrentModel->vehicle_id);
      
      // If relayed vehicle changed, or relay was disabled, remove runtime info for the relayed vehicle

      if ( (g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId < 0) || (oldRelayParams.uRelayedVehicleId != g_pCurrentModel->relay_params.uRelayedVehicleId) )
      {
         // Remove old one
         if ( (oldRelayParams.uRelayedVehicleId != 0) && (oldRelayParams.uRelayedVehicleId != MAX_U32) )
         {
            log_line("Relayed vehicle id changed or relay was disabled. Remove runtimeinfo for old relayed vehicle id %u", oldRelayParams.uRelayedVehicleId);
            logCurrentVehiclesRuntimeInfo();

            bool bRuntimeFound = false;
            bool bProcessorFound = false;
            int iIndex = getVehicleRuntimeIndex(oldRelayParams.uRelayedVehicleId );
            if ( -1 != iIndex )
            {
               removeVehicleRuntimeInfo(iIndex);
               log_line("Removed runtime info at index %d", iIndex);
               bRuntimeFound = true;
            }
            for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
            {
               if ( g_pVideoProcessorRxList[i] != NULL )
               if ( g_pVideoProcessorRxList[i]->m_uVehicleId == oldRelayParams.uRelayedVehicleId )
               {
                  g_pVideoProcessorRxList[i]->uninit();
                  delete g_pVideoProcessorRxList[i];
                  g_pVideoProcessorRxList[i] = NULL;
                  for( int k=i; k<MAX_VIDEO_PROCESSORS-1; k++ )
                     g_pVideoProcessorRxList[k] = g_pVideoProcessorRxList[k+1];
                  log_line("Removed video processor at index %d", i);
                  bProcessorFound = true;
                  break;
               }
            }

            if ( ! bRuntimeFound )
               log_line("Did not find any runtime info for VID %u", oldRelayParams.uRelayedVehicleId);
            if ( ! bProcessorFound )
               log_line("Did not find any video processors for VID %u", oldRelayParams.uRelayedVehicleId);
         }
      }
      else if ( oldRelayParams.isRelayEnabledOnRadioLinkId != g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId )
      {
         log_line("Relay link id was changed (from %d to %d). Reasigning radio interfaces to current vehicle radio links...",
            oldRelayParams.isRelayEnabledOnRadioLinkId, g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId);
         reasign_radio_links();
      }
      return;
   }

   if ( uChangeType == MODEL_CHANGED_OSD_PARAMS )
   {
      log_line("Received notification from central that current model OSD params have changed.");
      return;       
   }
   
   // Signal other components about the model change if it's not from central or if settings where synchronised form vehicle

   if ( (pPH->vehicle_id_src == PACKET_COMPONENT_COMMANDS) ||
        (uChangeType == MODEL_CHANGED_SYNCHRONISED_SETTINGS_FROM_VEHICLE) )
   {
      if ( -1 != g_fIPCToTelemetry )
         ruby_ipc_channel_send_message(g_fIPCToTelemetry, (u8*)pPH, pPH->total_length);
      if ( -1 != g_fIPCToRC )
         ruby_ipc_channel_send_message(g_fIPCToRC, (u8*)pPH, pPH->total_length);
   }
}

void process_local_control_packet(t_packet_header* pPH)
{
   //log_line("Process received local controll packet type: %d", pPH->packet_type);

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROLLER_RELOAD_CORE_PLUGINS )
   {
      log_line("Router received a local message to reload core plugins.");
      refresh_CorePlugins(0);
      log_line("Router finished reloading core plugins.");
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_SIK_CONFIG )
   {
      u8* pPacketBuffer = (u8*)pPH;
      u8 uVehicleLinkId = *(pPacketBuffer + sizeof(t_packet_header));
      u8 uCommandId = *(pPacketBuffer + sizeof(t_packet_header) + sizeof(u8));

      log_line("Received local message to configure SiK radio for vehicle radio link %d, parameter: %d", (int) uVehicleLinkId+1, (int)uCommandId);

      bool bInvalidParams = false;
      int iLocalRadioInterfaceIndex = -1;
      if ( (uVehicleLinkId < 0) || (uVehicleLinkId >= g_pCurrentModel->radioLinksParams.links_count) )
         bInvalidParams = true;

      if ( ! bInvalidParams )
      {
         for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
         {
            if ( g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId == uVehicleLinkId )
            if ( hardware_radio_index_is_sik_radio(i) )
            {
               iLocalRadioInterfaceIndex = i;
               break;
            }
         }
      }

      if ( -1 == iLocalRadioInterfaceIndex )
         bInvalidParams = true;

      if ( bInvalidParams )
      {
         char szBuff[256];
         strcpy(szBuff, "Invalid radio interface selected.");

         t_packet_header PH;
         radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_SIK_CONFIG, STREAM_ID_DATA);
         PH.vehicle_id_src = g_uControllerId;
         PH.vehicle_id_dest = g_uControllerId;
         PH.total_length = sizeof(t_packet_header) + strlen(szBuff)+3*sizeof(u8);

         u8 packet[MAX_PACKET_TOTAL_SIZE];
         memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
         memcpy(packet+sizeof(t_packet_header), &uVehicleLinkId, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + sizeof(u8), &uCommandId, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + 2*sizeof(u8), szBuff, strlen(szBuff)+1);
         if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, packet, PH.total_length) )
            log_softerror_and_alarm("No pipe to central to send message to.");
         return;
      }

      char szBuff[256];
      strcpy(szBuff, "SiK config: done.");

      if ( uCommandId == 1 )
      {
         radio_rx_pause_interface(iLocalRadioInterfaceIndex);
         radio_tx_pause_radio_interface(iLocalRadioInterfaceIndex);
      }

      if ( uCommandId == 2 )
      {
         radio_rx_resume_interface(iLocalRadioInterfaceIndex);
         radio_tx_resume_radio_interface(iLocalRadioInterfaceIndex);
      }

      if ( uCommandId == 0 )
      {
         log_line("Received message to get local SiK radio config for vehicle radio link %d", (int) uVehicleLinkId+1);
         g_iGetSiKConfigAsyncResult = 0;
         g_iGetSiKConfigAsyncRadioInterfaceIndex = iLocalRadioInterfaceIndex;
         g_uGetSiKConfigAsyncVehicleLinkIndex = uVehicleLinkId;
         hardware_radio_sik_get_all_params_async(iLocalRadioInterfaceIndex, &g_iGetSiKConfigAsyncResult);
         return;
      }

      t_packet_header PH;
      radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_SIK_CONFIG, STREAM_ID_DATA);
      PH.vehicle_id_src = g_uControllerId;
      PH.vehicle_id_dest = g_uControllerId;
      PH.total_length = sizeof(t_packet_header) + strlen(szBuff)+3*sizeof(u8);

      u8 packet[MAX_PACKET_TOTAL_SIZE];
      memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
      memcpy(packet+sizeof(t_packet_header), &uVehicleLinkId, sizeof(u8));
      memcpy(packet+sizeof(t_packet_header) + sizeof(u8), &uCommandId, sizeof(u8));
      memcpy(packet+sizeof(t_packet_header) + 2*sizeof(u8), szBuff, strlen(szBuff)+1);
      if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, packet, PH.total_length) )
         log_softerror_and_alarm("No pipe to central to send message to.");

      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROLLER_SEARCH_FREQ_CHANGED )
   {
      if ( ! g_bSearching )
      {
         log_softerror_and_alarm("Received local message to change the search frequency to %s, but no search is in progress!", str_format_frequency(pPH->vehicle_id_dest));
         return;
      }
      log_line("Received local message to change the search frequency to %s", str_format_frequency(pPH->vehicle_id_dest));
      links_set_cards_frequencies_for_search((int)pPH->vehicle_id_dest, false, -1,-1,-1,-1 );
      hardware_save_radio_info();
      log_line("Switched search frequency to %s. Broadcasting that router is ready.", str_format_frequency(pPH->vehicle_id_dest));
      broadcast_router_ready();
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_LINK_FREQUENCY_CHANGED )
   {
      u32* pI = (u32*)(((u8*)(pPH))+sizeof(t_packet_header));
      u32 uLinkId = *pI;
      pI++;
      u32 uNewFreq = *pI;
               
      log_line("Received control packet from central that a vehicle radio link frequency changed (radio link %u new freq: %s)", uLinkId+1, str_format_frequency(uNewFreq));
      if ( NULL != g_pCurrentModel && (uLinkId < (u32)g_pCurrentModel->radioLinksParams.links_count) )
      {
         g_pCurrentModel->radioLinksParams.link_frequency_khz[uLinkId] = uNewFreq;
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( g_pCurrentModel->radioInterfacesParams.interface_link_id[i] == (int)uLinkId )
               g_pCurrentModel->radioInterfacesParams.interface_current_frequency_khz[i] = uNewFreq;
         }
         links_set_cards_frequencies_and_params((int)uLinkId);
         for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
         {
            if ( NULL == g_pVideoProcessorRxList[i] )
               break;
            if ( g_pVideoProcessorRxList[i]->m_uVehicleId == pPH->vehicle_id_src )
            {
               g_pVideoProcessorRxList[i]->resetRetransmissionsStats();
               break;
            }
         }
      }
      hardware_save_radio_info();
            
      log_line("Done processing control packet of frequency change.");
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_PASSPHRASE_CHANGED )
   {
      lpp(NULL,0);
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_SWITCH_RADIO_LINK )
   {
      int iVehicleRadioLinkId = (int) pPH->vehicle_id_dest;
      log_line("Received control message to switch to vehicle radio link %d", iVehicleRadioLinkId+1);

      pPH->vehicle_id_src = iVehicleRadioLinkId;
      if ( _switch_to_vehicle_radio_link(iVehicleRadioLinkId) )
         pPH->vehicle_id_dest = 1;
      else
         pPH->vehicle_id_dest = 0;
      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;

      log_line("Switching to vehicle radio link %d %s. Sending response to central.", iVehicleRadioLinkId+1, (pPH->vehicle_id_dest == 1)?"succeeded":"failed");
      if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, (u8*)pPH, pPH->total_length) )
         log_softerror_and_alarm("No pipe to central to send message to.");
      else
         log_line("Sent response to central.");
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_RELAY_MODE_SWITCHED )
   {
      g_pCurrentModel->relay_params.uCurrentRelayMode = pPH->vehicle_id_src;
      Model* pModel = findModelWithId(g_pCurrentModel->vehicle_id);
      if ( NULL != pModel )
         pModel->relay_params.uCurrentRelayMode = pPH->vehicle_id_src;

      log_line("Received notification from central that relay mode changed to %d (%s)", g_pCurrentModel->relay_params.uCurrentRelayMode, str_format_relay_mode(g_pCurrentModel->relay_params.uCurrentRelayMode));
      int iCurrentFPS = g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].fps;
      int iDefaultKeyframeIntervalMs = DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
      if ( ! relay_controller_must_display_main_video(g_pCurrentModel) )
         iDefaultKeyframeIntervalMs = DEFAULT_VIDEO_KEYFRAME_INTERVAL_WHEN_RELAYING;
      log_line("Set current keyframe to %d ms for FPS %d for current vehicle (VID: %u)", iDefaultKeyframeIntervalMs, iCurrentFPS, g_pCurrentModel->vehicle_id);
      video_link_keyframe_set_current_level_to_request(g_pCurrentModel->vehicle_id, iDefaultKeyframeIntervalMs);
      
      pModel = findModelWithId(g_pCurrentModel->relay_params.uRelayedVehicleId);
      if ( NULL != pModel )
      {
         iDefaultKeyframeIntervalMs = DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
         if ( ! relay_controller_must_display_remote_video(g_pCurrentModel) )
            iDefaultKeyframeIntervalMs = DEFAULT_VIDEO_KEYFRAME_INTERVAL_WHEN_RELAYING;
         log_line("Set current keyframe to %d ms for remote vehicle (VID: %u)", iDefaultKeyframeIntervalMs, pModel->vehicle_id);
         video_link_keyframe_set_current_level_to_request(pModel->vehicle_id, iDefaultKeyframeIntervalMs);
      }

      g_iShowVideoKeyframesAfterRelaySwitch = 5;
      log_line("Done processing notification from central that relay mode changed to %d (%s)", g_pCurrentModel->relay_params.uCurrentRelayMode, str_format_relay_mode(g_pCurrentModel->relay_params.uCurrentRelayMode));
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_MODEL_CHANGED )
   {
      u8 uComponent = (pPH->vehicle_id_src) && 0xFF;
      u8 uChangeType = (pPH->vehicle_id_src >> 8) & 0xFF;
      u8 uExtraParam = (pPH->vehicle_id_src >> 16) & 0xFF;
      log_line("Received local control packet from central to reload model, change type: %u (%s), extra value: %d.", uChangeType, str_get_model_change_type((int)uChangeType), uExtraParam);

      _process_local_notification_model_changed(pPH, uChangeType, (int)uComponent, (int)uExtraParam);
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_CONTROLLER_CHANGED )
   {
      log_line("Received control packet to reload controller settings.");

      hw_serial_port_info_t oldSerialPorts[MAX_SERIAL_PORTS];
      for( int i=0; i<hardware_get_serial_ports_count(); i++ )
      {
         hw_serial_port_info_t* pPort = hardware_get_serial_port_info(i);
         if ( NULL != pPort )
            memcpy(&oldSerialPorts[i], pPort, sizeof(hw_serial_port_info_t));
      }
     
      int iOldAudioDevice = get_ControllerSettings()->iAudioOutputDevice;
      int iOldAudioVolume = get_ControllerSettings()->iAudioOutputVolume;

      hardware_reload_serial_ports();
      load_ControllerSettings();
      g_pControllerSettings = get_ControllerSettings();

      if ( NULL != g_pControllerSettings )
         radio_rx_set_timeout_interval(g_pControllerSettings->iDevRxLoopTimeout);

      if ( (iOldAudioDevice != g_pControllerSettings->iAudioOutputDevice) ||
           (iOldAudioVolume != g_pControllerSettings->iAudioOutputVolume) )
      {
         log_line("Audio settings have changed. Audio output device: %d, audio output volume: %d",
             g_pControllerSettings->iAudioOutputDevice, g_pControllerSettings->iAudioOutputVolume);
         
         hardware_set_audio_output(g_pControllerSettings->iAudioOutputDevice, g_pControllerSettings->iAudioOutputVolume);
         return;
      }

      for( int i=0; i<hardware_get_serial_ports_count(); i++ )
      {
         hw_serial_port_info_t* pPort = hardware_get_serial_port_info(i);
         if ( NULL == pPort )
            continue;

         if ( pPort->iPortUsage == SERIAL_PORT_USAGE_HARDWARE_RADIO )
         if ( pPort->lPortSpeed != oldSerialPorts[i].lPortSpeed )
         {
            log_line("SiK radio interface changed local serial port speed from %d bps to %d bps",
               oldSerialPorts[i].lPortSpeed, pPort->lPortSpeed );

            if ( g_SiKRadiosState.bConfiguringToolInProgress )
            {
               log_softerror_and_alarm("Another SiK configuration is in progress. Ignoring this one.");
               continue;
            }

            if ( g_SiKRadiosState.bConfiguringSiKThreadWorking )
            {
               log_softerror_and_alarm("SiK reinitialization thread is in progress. Ignoring this notification.");
               continue;
            }
            if ( g_SiKRadiosState.bMustReinitSiKInterfaces )
            {
               log_softerror_and_alarm("SiK reinitialization is already flagged. Ignoring this notification.");
               continue;
            }

            close_and_mark_sik_interfaces_to_reopen();
            g_SiKRadiosState.bConfiguringToolInProgress = true;
            g_SiKRadiosState.uTimeStartConfiguring = g_TimeNow;
            
            send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE, 0);

            char szCommand[256];
            sprintf(szCommand, "rm -rf %s", FILE_TMP_SIK_CONFIG_FINISHED);
            hw_execute_bash_command(szCommand, NULL);

            sprintf(szCommand, "./ruby_sik_config %s %d -serialspeed %d &", pPort->szPortDeviceName, (int)oldSerialPorts[i].lPortSpeed, (int)pPort->lPortSpeed);
            hw_execute_bash_command(szCommand, NULL);
         }
      }

      radio_stats_set_graph_refresh_interval(&g_SM_RadioStats, g_pControllerSettings->nGraphRadioRefreshInterval);
      radio_stats_interfaces_rx_graph_reset(&g_SM_RadioStatsInterfacesRxGraph, 10);

      if ( NULL != g_pSM_RadioStats )
         memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));

      if ( g_pCurrentModel->hasCamera() )
      {
         processor_rx_video_forward_check_controller_settings_changed();
      }
      for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
      {
         if ( NULL == g_pVideoProcessorRxList[i] )
            break;
         g_pVideoProcessorRxList[i]->onControllerSettingsChanged();
      }
      
      // Signal other components about the model change
      if ( pPH->vehicle_id_src == PACKET_COMPONENT_LOCAL_CONTROL )
      {
         if ( -1 != g_fIPCToTelemetry )
            ruby_ipc_channel_send_message(g_fIPCToTelemetry, (u8*)pPH, pPH->total_length);
         if ( -1 != g_fIPCToRC )
            ruby_ipc_channel_send_message(g_fIPCToRC, (u8*)pPH, pPH->total_length);
      }
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_STARTED ||
        pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED ||
        pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_FINISHED )
   {
      if ( -1 != g_fIPCToTelemetry )
         ruby_ipc_channel_send_message(g_fIPCToTelemetry, (u8*)pPH, pPH->total_length);
      if ( -1 != g_fIPCToRC )
         ruby_ipc_channel_send_message(g_fIPCToRC, (u8*)pPH, pPH->total_length);

      if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_STARTED )
         g_bUpdateInProgress = true;
      if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_STOPED ||
           pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_UPDATE_FINISHED )
         g_bUpdateInProgress = false;
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_DEBUG_SCOPE_START )
   {
      log_line("Starting debug router packets history graph");
      g_bDebugIsPacketsHistoryGraphOn = true;
      g_bDebugIsPacketsHistoryGraphPaused = false;
      g_pDebug_SM_RouterPacketsStatsHistory = shared_mem_router_packets_stats_history_open_write();
      if ( NULL == g_pDebug_SM_RouterPacketsStatsHistory )
      {
         log_softerror_and_alarm("Failed to open shared mem for write for router packets stats history.");
         g_bDebugIsPacketsHistoryGraphOn = false;
         g_bDebugIsPacketsHistoryGraphPaused = true;
      }
      return;
   }

   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_DEBUG_SCOPE_STOP )
   {
      g_bDebugIsPacketsHistoryGraphOn = false;
      g_bDebugIsPacketsHistoryGraphPaused = true;
      shared_mem_router_packets_stats_history_close(g_pDebug_SM_RouterPacketsStatsHistory);
      return;
   }
   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_DEBUG_SCOPE_PAUSE )
   {
      g_bDebugIsPacketsHistoryGraphPaused = true;
   }
   if ( pPH->packet_type == PACKET_TYPE_LOCAL_CONTROL_DEBUG_SCOPE_RESUME )
   {
      g_bDebugIsPacketsHistoryGraphPaused = false;
   }
}
