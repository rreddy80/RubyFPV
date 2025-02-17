#include "../base/base.h"
#include "../base/config.h"
#include "../base/models_list.h"
#include "../base/commands.h"
#include "../base/hardware.h"
#include "../base/hw_procs.h"
#include "../base/radio_utils.h"
#include "../base/ruby_ipc.h"
#include "../common/radio_stats.h"
#include "../common/string_utils.h"
#include "../radio/radiolink.h"
#include "../radio/radio_rx.h"
#include "../radio/radio_tx.h"

#include "ruby_rt_vehicle.h"
#include "packets_utils.h"
#include "processor_tx_video.h"
#include "process_received_ruby_messages.h"
#include "video_link_auto_keyframe.h"
#include "video_link_stats_overwrites.h"
#include "launchers_vehicle.h"
#include "processor_relay.h"
#include "shared_vars.h"
#include "timers.h"

extern t_packet_queue s_QueueRadioPacketsOut;

fd_set s_ReadSetRXRadio;

bool s_bRCLinkDetected = false;

u32 s_uLastCommandChangeDataRateTime = 0;
u32 s_uLastCommandChangeDataRateCounter = MAX_U32;

u32 s_uTotalBadPacketsReceived = 0;

//u32 s_StreamsMaxReceivedPacketIndex[MAX_RADIO_STREAMS];

void _mark_link_from_controller_present()
{
   g_bHadEverLinkToController = true;
   g_TimeLastReceivedRadioPacketFromController = g_TimeNow;
   
   if ( ! g_bHasLinkToController )
   {
      g_bHasLinkToController = true;
      log_line("[Router] Link to controller recovered (received packets from controller).");

      if ( g_pCurrentModel->osd_params.osd_preferences[g_pCurrentModel->osd_params.layout] & OSD_PREFERENCES_BIT_FLAG_SHOW_CONTROLLER_LINK_LOST_ALARM )
         send_alarm_to_controller(ALARM_ID_LINK_TO_CONTROLLER_RECOVERED, 0, 0, 10);
   }
}


/*
void init_radio_in_packets_state()
{
   for( int i=0; i<MAX_RADIO_STREAMS; i++ )
      s_StreamsMaxReceivedPacketIndex[i] = MAX_U32;
}
*/

void _try_decode_controller_links_stats_from_packet(u8* pPacketData, int packetLength)
{
   t_packet_header* pPH = (t_packet_header*)pPacketData;

   bool bHasControllerData = false;
   u8* pData = NULL;
   //int dataLength = 0;

   if ( ((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RUBY ) && (pPH->packet_type == PACKET_TYPE_RUBY_PING_CLOCK) )
   if ( pPH->total_length > sizeof(t_packet_header) + 4*sizeof(u8) )
   {
      bHasControllerData = true;
      pData = pPacketData + sizeof(t_packet_header) + 4*sizeof(u8);
   }
   if ( ((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO ) && (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS) )
   {
      pData = pPacketData + sizeof(t_packet_header);
      u8 countR = *(pData+1);
      if ( pPH->total_length > sizeof(t_packet_header) + 2*sizeof(u8) + countR * (sizeof(u32) + 2*sizeof(u8)) )
      {
         bHasControllerData = true;
         pData = pPacketData + sizeof(t_packet_header) + 2*sizeof(u8) + countR * (sizeof(u32) + 2*sizeof(u8));
      }
   }
   if ( ((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO ) && (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2) )
   {
      pData = pPacketData + sizeof(t_packet_header);
      pData += sizeof(u32); // Skip: Retransmission request unique id
      u8 countR = *(pData+1);
      if ( pPH->total_length > sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8) + countR * (sizeof(u32) + 2*sizeof(u8)) )
      {
         bHasControllerData = true;
         pData = pPacketData + sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8) + countR * (sizeof(u32) + 2*sizeof(u8));
      }
   }
   if ( (! bHasControllerData ) || NULL == pData )
      return;

   g_SM_VideoLinkStats.timeLastReceivedControllerLinkInfo = g_TimeNow;

   //u8 flagsAndVersion = *pData;
   pData++;
   u32 timestamp = 0;
   memcpy(&timestamp, pData, sizeof(u32));
   pData += sizeof(u32);

   if ( g_PD_LastRecvControllerLinksStats.lastUpdateTime == timestamp )
   {
      //log_line("Discarded duplicate controller links info.");
      return;
   }

   g_PD_LastRecvControllerLinksStats.lastUpdateTime = timestamp;
   u8 srcSlicesCount = *pData;
   pData++;
   g_PD_LastRecvControllerLinksStats.radio_interfaces_count = *pData;
   pData++;
   g_PD_LastRecvControllerLinksStats.video_streams_count = *pData;
   pData++;
   for( int i=0; i<g_PD_LastRecvControllerLinksStats.radio_interfaces_count; i++ )
   {
      memcpy(&(g_PD_LastRecvControllerLinksStats.radio_interfaces_rx_quality[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
   }
   for( int i=0; i<g_PD_LastRecvControllerLinksStats.video_streams_count; i++ )
   {
      memcpy(&(g_PD_LastRecvControllerLinksStats.radio_streams_rx_quality[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
      memcpy(&(g_PD_LastRecvControllerLinksStats.video_streams_blocks_clean[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
      memcpy(&(g_PD_LastRecvControllerLinksStats.video_streams_blocks_reconstructed[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
      memcpy(&(g_PD_LastRecvControllerLinksStats.video_streams_blocks_max_ec_packets_used[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
      memcpy(&(g_PD_LastRecvControllerLinksStats.video_streams_requested_retransmission_packets[i][0]), pData, srcSlicesCount);
      pData += srcSlicesCount;
   }

   // Update dev video links graphs stats using controller received video links info

   u32 receivedTimeInterval = CONTROLLER_LINK_STATS_HISTORY_MAX_SLICES * CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS;
   int sliceDestIndex = 0;

   for( u32 sampleTimestamp = VIDEO_LINK_STATS_REFRESH_INTERVAL_MS/2; sampleTimestamp <= receivedTimeInterval; sampleTimestamp += VIDEO_LINK_STATS_REFRESH_INTERVAL_MS )
   {
      u32 sliceSrcIndex = sampleTimestamp/CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS;
      if ( sliceSrcIndex >= CONTROLLER_LINK_STATS_HISTORY_MAX_SLICES )
         break;

      for( int i=0; i<g_PD_LastRecvControllerLinksStats.radio_interfaces_count; i++ )
         g_SM_VideoLinkGraphs.controller_received_radio_interfaces_rx_quality[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.radio_interfaces_rx_quality[i][sliceSrcIndex];

      for( int i=0; i<g_PD_LastRecvControllerLinksStats.video_streams_count; i++ )
      {
         g_SM_VideoLinkGraphs.controller_received_radio_streams_rx_quality[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.radio_streams_rx_quality[i][sliceSrcIndex];
         g_SM_VideoLinkGraphs.controller_received_video_streams_blocks_clean[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.video_streams_blocks_clean[i][sliceSrcIndex];
         g_SM_VideoLinkGraphs.controller_received_video_streams_blocks_reconstructed[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.video_streams_blocks_reconstructed[i][sliceSrcIndex];
         g_SM_VideoLinkGraphs.controller_received_video_streams_blocks_max_ec_packets_used[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.video_streams_blocks_max_ec_packets_used[i][sliceSrcIndex];
         g_SM_VideoLinkGraphs.controller_received_video_streams_requested_retransmission_packets[i][sliceDestIndex] = g_PD_LastRecvControllerLinksStats.video_streams_requested_retransmission_packets[i][sliceSrcIndex];
      }
      sliceDestIndex++;

      // Update only the last 3 intervals
      if ( sliceDestIndex > 3 || sliceSrcIndex > 3 )
         break;
   }
}

void _check_update_atheros_datarates(u32 linkIndex, int datarateVideoBPS)
{
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioInfo )
         continue;
      if ( ((pRadioInfo->typeAndDriver & 0xFF) != RADIO_TYPE_ATHEROS) &&
           ((pRadioInfo->typeAndDriver & 0xFF) != RADIO_TYPE_RALINK) )
         continue;
      if ( g_pCurrentModel->radioInterfacesParams.interface_link_id[i] != (int)linkIndex )
         continue;
      if ( datarateVideoBPS == pRadioInfo->iCurrentDataRate )
         continue;

      radio_rx_pause_interface(i);
      radio_tx_pause_radio_interface(i);

      bool bWasOpenedForWrite = false;
      bool bWasOpenedForRead = false;
      log_line("Vehicle has Atheros cards. Setting the datarates for it.");
      if ( pRadioInfo->openedForWrite )
      {
         bWasOpenedForWrite = true;
         radio_close_interface_for_write(i);
      }
      if ( pRadioInfo->openedForRead )
      {
         bWasOpenedForRead = true;
         radio_close_interface_for_read(i);
      }
      g_TimeNow = get_current_timestamp_ms();
      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->lastActiveTime = g_TimeNow;
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      }

      radio_utils_set_datarate_atheros(g_pCurrentModel, i, datarateVideoBPS);
      
      pRadioInfo->iCurrentDataRate = datarateVideoBPS;

      g_TimeNow = get_current_timestamp_ms();
      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->lastActiveTime = g_TimeNow;
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      }

      if ( bWasOpenedForRead )
         radio_open_interface_for_read(i, RADIO_PORT_ROUTER_UPLINK);

      if ( bWasOpenedForWrite )
         radio_open_interface_for_write(i);

      radio_rx_resume_interface(i);
      radio_tx_resume_radio_interface(i);
      
      g_TimeNow = get_current_timestamp_ms();
      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->lastActiveTime = g_TimeNow;
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      }
   }
}

// returns the packet length if it was reconstructed
int _handle_received_packet_error(int iInterfaceIndex, u8* pData, int nDataLength, int* pCRCOk)
{
   s_uTotalBadPacketsReceived++;

   int iRadioError = get_last_processing_error_code();

   // Try reconstruction
   if ( iRadioError == RADIO_PROCESSING_ERROR_CODE_INVALID_CRC_RECEIVED )
   {
      t_packet_header* pPH = (t_packet_header*)pData;
      pPH->vehicle_id_src = 0;
      int nPacketLength = packet_process_and_check(iInterfaceIndex, pData, nDataLength, pCRCOk);

      if ( nPacketLength > 0 )
      {
         if ( g_TimeNow > g_UplinkInfoRxStats[iInterfaceIndex].timeLastLogWrongRxPacket + 10000 )
         {
            char szBuff[256];
            alarms_to_string(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET_RECONSTRUCTED, 0,0, szBuff);
            log_line("Reconstructed invalid radio packet received. Sending alarm to controller. Alarms: %s, repeat count: %d", szBuff, (int)s_uTotalBadPacketsReceived);
            send_alarm_to_controller(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET_RECONSTRUCTED, 0, 0, 1);
            g_UplinkInfoRxStats[iInterfaceIndex].timeLastLogWrongRxPacket = g_TimeNow;
            s_uTotalBadPacketsReceived = 0;
         }
         return nPacketLength;
      }
   }

   if ( iRadioError == RADIO_PROCESSING_ERROR_CODE_INVALID_CRC_RECEIVED )
   if ( g_TimeNow > g_UplinkInfoRxStats[iInterfaceIndex].timeLastLogWrongRxPacket + 10000 )
   {
      char szBuff[256];
      alarms_to_string(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET, 0,0, szBuff);
      log_line("Invalid radio packet received (bad CRC) (error: %d). Sending alarm to controller. Alarms: %s, repeat count: %d", iRadioError, szBuff, (int)s_uTotalBadPacketsReceived);
      send_alarm_to_controller(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET, 0, 0, 1);
      s_uTotalBadPacketsReceived = 0;
   }

   if ( g_TimeNow > g_UplinkInfoRxStats[iInterfaceIndex].timeLastLogWrongRxPacket + 10000 )
   {
      g_UplinkInfoRxStats[iInterfaceIndex].timeLastLogWrongRxPacket = g_TimeNow;
      log_softerror_and_alarm("Received %d invalid packet(s). Error: %d", s_uTotalBadPacketsReceived, iRadioError );
      s_uTotalBadPacketsReceived = 0;
   } 
   return 0;
}

void process_received_single_radio_packet(int iRadioInterface, u8* pData, int dataLength )
{
   t_packet_header* pPH = (t_packet_header*)pData;

   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastRadioRxTime = g_TimeNow;

   int iRadioLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[iRadioInterface];
   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iRadioInterface);

   g_UplinkInfoRxStats[iRadioInterface].lastReceivedDBM = pRadioHWInfo->monitor_interface_read.radioInfo.nDbm;
   g_UplinkInfoRxStats[iRadioInterface].lastReceivedDataRate = pRadioHWInfo->monitor_interface_read.radioInfo.nRate;

   if ( dataLength <= 0 )
   {
      int bCRCOk = 0;
      dataLength = _handle_received_packet_error(iRadioInterface, pData, dataLength, &bCRCOk);
      if ( dataLength <= 0 )
         return;
   }

   bool bIsPacketFromRelayRadioLink = false;
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId == iRadioLinkId )
      bIsPacketFromRelayRadioLink = true;

   if ( g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[iRadioInterface] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      bIsPacketFromRelayRadioLink = true;

   // Detect if it's a relayed packet from relayed vehicle to controller

   if ( bIsPacketFromRelayRadioLink )
   {
      static bool s_bFirstTimeReceivedDataOnRelayLink = true;

      if ( s_bFirstTimeReceivedDataOnRelayLink )
      {
         log_line("[Relay-RadioIn] Started for first time to receive data on the relay link (radio link %d, expected relayed VID: %u), from VID: %u, packet type: %s, current relay mode: %s",
            iRadioLinkId+1, g_pCurrentModel->relay_params.uRelayedVehicleId,
            pPH->vehicle_id_src, str_get_packet_type(pPH->packet_type), str_format_relay_mode(g_pCurrentModel->relay_params.uCurrentRelayMode));
         s_bFirstTimeReceivedDataOnRelayLink = false;
      }

      if ( pPH->vehicle_id_src != g_pCurrentModel->relay_params.uRelayedVehicleId )
         return;

      // Process packets received on relay links separately
      relay_process_received_radio_packet_from_relayed_vehicle(iRadioLinkId, iRadioInterface, pData, dataLength);
      return;
   }

   // Detect if it's a relayed packet from controller to relayed vehicle
   
   if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0) )
   if ( pPH->vehicle_id_dest == g_pCurrentModel->relay_params.uRelayedVehicleId )
   {
      _mark_link_from_controller_present();
  
      relay_process_received_single_radio_packet_from_controller_to_relayed_vehicle(iRadioInterface, pData, pPH->total_length);
      return;
   }

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO
   if ( (((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RUBY ) && (pPH->packet_type == PACKET_TYPE_RUBY_PING_CLOCK)) ||
        (((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO ) && (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS)) ||
        (((pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO ) && (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2)) )
      _try_decode_controller_links_stats_from_packet(pData, dataLength);
   #endif
     
   if ( (0 == pPH->vehicle_id_src) || (MAX_U32 == pPH->vehicle_id_src) )
      log_softerror_and_alarm("Received invalid radio packet: Invalid source vehicle id: %u (vehicle id dest: %u, packet type: %s)",
         pPH->vehicle_id_src, pPH->vehicle_id_dest, str_get_packet_type(pPH->packet_type));

   if ( pPH->vehicle_id_src == g_uControllerId )
      _mark_link_from_controller_present();

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RC )
   {
      if ( ! s_bRCLinkDetected )
      {
         char szBuff[64];
         snprintf(szBuff, 63, "touch %s", FILE_TMP_RC_DETECTED);
         hw_execute_bash_command(szBuff, NULL);
      }
      s_bRCLinkDetected = true;
   }

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RUBY )
   {
      if ( pPH->vehicle_id_dest == g_pCurrentModel->vehicle_id )
         process_received_ruby_message(iRadioInterface, pData);
      return;
   }

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
   {
      if ( pPH->packet_type == PACKET_TYPE_COMMAND )
      {
         int iParamsLength = pPH->total_length - sizeof(t_packet_header) - sizeof(t_packet_header_command);
         t_packet_header_command* pPHC = (t_packet_header_command*)(pData + sizeof(t_packet_header));
         if ( pPHC->command_type == COMMAND_ID_UPLOAD_SW_TO_VEHICLE || pPHC->command_type == COMMAND_ID_UPLOAD_SW_TO_VEHICLE63 )
            g_uTimeLastCommandSowftwareUpload = g_TimeNow;

         if ( pPHC->command_type == COMMAND_ID_SET_RADIO_LINK_FLAGS )
         if ( pPHC->command_counter != s_uLastCommandChangeDataRateCounter || (g_TimeNow > s_uLastCommandChangeDataRateTime + 4000) )
         if ( iParamsLength == (int)(2*sizeof(u32)+2*sizeof(int)) )
         {
            s_uLastCommandChangeDataRateTime = g_TimeNow;
            s_uLastCommandChangeDataRateCounter = pPHC->command_counter;
            g_TimeLastSetRadioFlagsCommandReceived = g_TimeNow;
            u32* pInfo = (u32*)(pData + sizeof(t_packet_header)+sizeof(t_packet_header_command));
            u32 linkIndex = *pInfo;
            pInfo++;
            u32 linkFlags = *pInfo;
            pInfo++;
            int* piInfo = (int*)pInfo;
            int datarateVideo = *piInfo;
            piInfo++;
            int datarateData = *piInfo;
            log_line("Received new Set Radio Links Flags command. Link %d, Link flags: %s, Datarate %d/%d", linkIndex+1, str_get_radio_frame_flags_description2(linkFlags), datarateVideo, datarateData);
            if ( datarateVideo != g_pCurrentModel->radioLinksParams.link_datarate_video_bps[linkIndex] ) 
               _check_update_atheros_datarates(linkIndex, datarateVideo);
         }
      }
      //log_line("Received a command: packet type: %d, module: %d, length: %d", pPH->packet_type, pPH->packet_flags & PACKET_FLAGS_MASK_MODULE, pPH->total_length);
      ruby_ipc_channel_send_message(s_fIPCRouterToCommands, pData, dataLength);
      return;
   }

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_TELEMETRY )
   {
      //log_line("Received an uplink telemetry packet: packet type: %d, module: %d, length: %d", pPH->packet_type, pPH->packet_flags & PACKET_FLAGS_MASK_MODULE, pPH->total_length);
      ruby_ipc_channel_send_message(s_fIPCRouterToTelemetry, pData, dataLength);
      return;
   }

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RC )
   {
      ruby_ipc_channel_send_message(s_fIPCRouterToRC, pData, dataLength);
      return;
   }

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO )
   {
      if ( pPH->packet_type == PACKET_TYPE_VIDEO_SWITCH_TO_ADAPTIVE_VIDEO_LEVEL )
      {
         if ( pPH->total_length < sizeof(t_packet_header) + sizeof(u32) )
            return;

         u32 uAdaptiveLevel = 0;
         u8 uVideoStreamIndex = 0;
         memcpy( &uAdaptiveLevel, pData + sizeof(t_packet_header), sizeof(u32));
         if ( pPH->total_length >= sizeof(t_packet_header) + sizeof(u32) + sizeof(u8) )
            memcpy( &uVideoStreamIndex, pData + sizeof(t_packet_header) + sizeof(u32), sizeof(u8));
      
         u32 uAdaptiveLevelResponse = MAX_U32;

         // If video from this vehicle is not active, then we must reply back with acknowledgment
         if ( relay_current_vehicle_must_send_own_video_feeds() )
            uAdaptiveLevelResponse = uAdaptiveLevel;

         t_packet_header PH;
         radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_VIDEO_SWITCH_TO_ADAPTIVE_VIDEO_LEVEL_ACK, STREAM_ID_DATA);
         PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
         PH.vehicle_id_dest = pPH->vehicle_id_src;
         PH.total_length = sizeof(t_packet_header) + sizeof(u32);
         u8 packet[MAX_PACKET_TOTAL_SIZE];
         memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
         memcpy(packet+sizeof(t_packet_header), &uAdaptiveLevelResponse, sizeof(u32));
         packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);

         int iAdaptiveLevel = uAdaptiveLevel;

         if ( NULL != g_pProcessorTxVideo )
            g_pProcessorTxVideo->setLastRequestedAdaptiveVideoLevelFromController(iAdaptiveLevel);
         int iLevelsHQ = g_pCurrentModel->get_video_profile_total_levels(g_pCurrentModel->video_params.user_selected_video_link_profile);
         int iLevelsMQ = g_pCurrentModel->get_video_profile_total_levels(VIDEO_PROFILE_MQ);
         int iLevelsLQ = g_pCurrentModel->get_video_profile_total_levels(VIDEO_PROFILE_LQ);

         int iTargetProfile = g_pCurrentModel->video_params.user_selected_video_link_profile;
         int iTargetShiftLevel = 0;

         if ( iAdaptiveLevel < iLevelsHQ )
         {
            iTargetProfile = g_pCurrentModel->video_params.user_selected_video_link_profile;
            iTargetShiftLevel = iAdaptiveLevel;
         }
         else if ( iAdaptiveLevel < iLevelsHQ + iLevelsMQ )
         {
            iTargetProfile = VIDEO_PROFILE_MQ;
            iTargetShiftLevel = iAdaptiveLevel - iLevelsHQ;
         }
         else if ( iAdaptiveLevel < iLevelsHQ + iLevelsMQ + iLevelsLQ )
         {
            iTargetProfile = VIDEO_PROFILE_LQ;
            iTargetShiftLevel = iAdaptiveLevel - iLevelsHQ - iLevelsMQ;
         }
         else
         {
            iTargetProfile = VIDEO_PROFILE_LQ;
            iTargetShiftLevel = iLevelsHQ + iLevelsMQ + iLevelsLQ - 1;           
         }
         
         if ( iTargetProfile == g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile )
         if ( iTargetShiftLevel == g_SM_VideoLinkStats.overwrites.currentProfileShiftLevel )
         {
            return;
         }
         video_stats_overwrites_switch_to_profile_and_level(iTargetProfile, iTargetShiftLevel);
         return;
      }
      
      if ( pPH->packet_type == PACKET_TYPE_VIDEO_SWITCH_VIDEO_KEYFRAME_TO_VALUE )
      {
         if ( pPH->total_length < sizeof(t_packet_header) + sizeof(u32) )
            return;

         // Discard requests if we are on fixed keyframe
         //if ( g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].keyframe_ms > 0 )
         //   return;
         
         u32 uNewKeyframeValueMs = 0;
         u8 uVideoStreamIndex = 0;
         memcpy( &uNewKeyframeValueMs, pData + sizeof(t_packet_header), sizeof(u32));
         if ( pPH->total_length >= sizeof(t_packet_header) + sizeof(u32) + sizeof(u8) )
            memcpy( &uVideoStreamIndex, pData + sizeof(t_packet_header) + sizeof(u32), sizeof(u8));

         if ( g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs != uNewKeyframeValueMs )
            log_line("[KeyFrame] Recv request from controller for keyframe: %u ms (previous active was: %u ms)", uNewKeyframeValueMs, g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs);
         else
            log_line("[KeyFrame] Recv again request from controller for keyframe: %u ms", uNewKeyframeValueMs);          
         
         // If video is not sent from this vehicle to controller, then we must reply back with acknowledgment
         if ( ! relay_current_vehicle_must_send_own_video_feeds() )
         {
            t_packet_header PH;
            radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_VIDEO_SWITCH_VIDEO_KEYFRAME_TO_VALUE_ACK, STREAM_ID_DATA);
            PH.packet_flags = PACKET_COMPONENT_VIDEO;
            PH.packet_type =  PACKET_TYPE_VIDEO_SWITCH_VIDEO_KEYFRAME_TO_VALUE_ACK;
            PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
            PH.vehicle_id_dest = pPH->vehicle_id_src;
            PH.total_length = sizeof(t_packet_header) + sizeof(u32);
            u8 packet[MAX_PACKET_TOTAL_SIZE];
            memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
            memcpy(packet+sizeof(t_packet_header), &uNewKeyframeValueMs, sizeof(u32));
            packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);
         }

         video_link_auto_keyframe_set_controller_requested_value((int) uVideoStreamIndex, (int)uNewKeyframeValueMs);
         return;
      }

      if ( g_pCurrentModel->hasCamera() )
         process_data_tx_video_command(iRadioInterface, pData);
   }
}
