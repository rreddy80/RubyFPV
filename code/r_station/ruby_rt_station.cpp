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
#include <pthread.h>
#include "../base/base.h"
#include "../base/config.h"
#include "../base/ctrl_settings.h"
#include "../base/ctrl_interfaces.h"
#include "../base/commands.h"
#include "../base/encr.h"
#include "../base/shared_mem.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/radio_utils.h"
#include "../base/hardware.h"
#include "../base/hw_procs.h"
#include "../base/ruby_ipc.h"
#include "../common/string_utils.h"
#include "../common/radio_stats.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../radio/radiopacketsqueue.h"
#include "../radio/radio_rx.h"
#include "../radio/radio_tx.h"
#include "../radio/radio_duplicate_det.h"
#include "../base/controller_utils.h"
#include "../base/core_plugins_settings.h"
#include "../common/models_connect_frequencies.h"

#include "shared_vars.h"
#include "links_utils.h"
#include "processor_rx_audio.h"
#include "processor_rx_video.h"
#include "rx_video_output.h"
#include "process_radio_in_packets.h"
#include "process_local_packets.h"
#include "packets_utils.h"
#include "video_link_adaptive.h"
#include "video_link_keyframe.h"

#include "timers.h"

int s_iFailedInitRadioInterface = -1;

u8 s_BufferCommands[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeBufferCommands[MAX_PACKET_TOTAL_SIZE];
int s_PipeBufferCommandsPos = 0;

u8 s_BufferMessageFromTelemetry[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeBufferTelemetryUplink[MAX_PACKET_TOTAL_SIZE];
int s_PipeBufferTelemetryUplinkPos = 0;  

u8 s_BufferRCUplink[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeBufferRCUplink[MAX_PACKET_TOTAL_SIZE];
int s_PipeBufferRCUplinkPos = 0;  

t_packet_queue s_QueueRadioPackets;
t_packet_queue s_QueueControlPackets;

u32 s_debugLastFPSTime = 0;
u32 s_debugFramesCount = 0; 

int s_iCountCPULoopOverflows = 0;

int s_iSearchSikAirRate = -1;
int s_iSearchSikECC = -1;
int s_iSearchSikLBT = -1;
int s_iSearchSikMCSTR = -1;

u32 s_uTimeLastReadIPCMessages = 0;

void _broadcast_radio_interface_init_failed(int iInterfaceIndex)
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROLLER_RADIO_INTERFACE_FAILED_TO_INITIALIZE, STREAM_ID_DATA);
   PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
   PH.vehicle_id_dest = iInterfaceIndex;
   PH.total_length = sizeof(t_packet_header);

   u8 buffer[MAX_PACKET_TOTAL_SIZE];
   memcpy(buffer, (u8*)&PH, sizeof(t_packet_header));
   
   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;

   if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, buffer, PH.total_length) )
      log_softerror_and_alarm("No pipe to central to send message to.");

   log_line("Sent message to central that radio interface %d failed to initialize.", iInterfaceIndex+1);
}

void close_and_mark_sik_interfaces_to_reopen()
{
   int iCount = 0;
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      if ( pRadioHWInfo->openedForWrite || pRadioHWInfo->openedForRead )
      if ( (g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex == -1 ) ||
           (g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex == i) )
      {
         radio_rx_pause_interface(i);
         radio_tx_pause_radio_interface(i);
         g_SiKRadiosState.bInterfacesToReopen[i] = true;
         hardware_radio_sik_close(i);
         iCount++;
      }
   }
   log_line("[Router] Closed SiK radio interfaces (%d closed) and marked them for reopening.", iCount);
}

void reopen_marked_sik_interfaces()
{
   int iCount = 0;
   int iCountSikInterfacesOpened = 0;
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      if ( g_SiKRadiosState.bInterfacesToReopen[i] )
      {
         if ( hardware_radio_sik_open_for_read_write(i) <= 0 )
            log_softerror_and_alarm("[Router] Failed to reopen SiK radio interface %d", i+1);
         else
         {
            iCount++;
            iCountSikInterfacesOpened++;
            radio_tx_resume_radio_interface(i);
            radio_rx_resume_interface(i);
         }
      }
      g_SiKRadiosState.bInterfacesToReopen[i] = false;
   }

   log_line("[Router] Reopened SiK radio interfaces (%d reopened).", iCount);
}

void _close_rxtx_radio_interfaces()
{
   log_line("Closing all radio interfaces (rx/tx).");

   radio_tx_mark_quit();
   hardware_sleep_ms(10);
   radio_tx_stop_tx_thread();

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
         hardware_radio_sik_close(i);
   }

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( pRadioHWInfo->openedForWrite )
         radio_close_interface_for_write(i);
   }

   radio_close_interfaces_for_read();

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      g_SM_RadioStats.radio_interfaces[i].openedForRead = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForWrite = 0;
   }
   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   log_line("Closed all radio interfaces (rx/tx)."); 
}

void _open_rxtx_radio_interfaces_for_search( u32 uSearchFreq )
{
   log_line("");
   log_line("OPEN RADIO INTERFACES START =========================================================");
   log_line("Opening RX radio interfaces for search (%s)...", str_format_frequency(uSearchFreq));

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      g_SM_RadioStats.radio_interfaces[i].openedForRead = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForWrite = 0;
   }

   int iCountOpenRead = 0;
   int iCountSikInterfacesOpened = 0;

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      u32 flags = controllerGetCardFlags(pRadioHWInfo->szMAC);
      if ( (flags & RADIO_HW_CAPABILITY_FLAG_DISABLED) || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
         continue;

      if ( 0 == hardware_radio_supports_frequency(pRadioHWInfo, uSearchFreq ) )
         continue;

      if ( flags & RADIO_HW_CAPABILITY_FLAG_CAN_RX )
      if ( flags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA )
      {
         if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
         {
            if ( hardware_radio_sik_open_for_read_write(i) <= 0 )
               s_iFailedInitRadioInterface = i;
            else
            {
               g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;
               iCountOpenRead++;
               iCountSikInterfacesOpened++;
            }
         }
         else if ( radio_open_interface_for_read(i, RADIO_PORT_ROUTER_DOWNLINK) > 0 )
         {
            log_line("Opened radio interface %d for read: USB port %s %s %s", i+1, pRadioHWInfo->szUSBPort, str_get_radio_type_description(pRadioHWInfo->typeAndDriver), pRadioHWInfo->szMAC);
            g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;
            iCountOpenRead++;
         }
         else
            s_iFailedInitRadioInterface = i;
      }
   }
   
   if ( 0 < iCountSikInterfacesOpened )
   {
      radio_tx_set_sik_packet_size(g_pCurrentModel->radioLinksParams.iSiKPacketSize);
      radio_tx_start_tx_thread();
   }

   // While searching, all cards are on same frequency, so a single radio link assigned to them.
   int iRadioLink = 0;
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      if ( g_SM_RadioStats.radio_interfaces[i].openedForRead )
      {
         g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = iRadioLink;
         g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iRadioLink;
         //iRadioLink++;
      }
   }
   
   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   log_line("Opening RX radio interfaces for search complete. %d interfaces opened for RX:", iCountOpenRead);
   
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      if ( g_SM_RadioStats.radio_interfaces[i].openedForRead )
      {
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
         if ( NULL == pRadioHWInfo )
            log_line("   * Radio interface %d, name: %s opened for read.", i+1, "N/A");
         else
            log_line("   * Radio interface %d, name: %s opened for read.", i+1, pRadioHWInfo->szName);
      }
   }
   log_line("OPEN RADIO INTERFACES END =====================================================================");
   log_line("");
}


void _open_rxtx_radio_interfaces()
{
   log_line("");
   log_line("OPEN RADIO INTERFACES START =========================================================");
   log_line("Opening RX/TX radio interfaces for current vehicle ...");

   if ( g_bSearching || (NULL == g_pCurrentModel) )
   {
      log_error_and_alarm("Invalid parameters for opening radio interfaces");
      return;
   }

   int totalCountForRead = 0;
   int totalCountForWrite = 0;

   int countOpenedForReadForRadioLink[MAX_RADIO_INTERFACES];
   int countOpenedForWriteForRadioLink[MAX_RADIO_INTERFACES];
   int iCountSikInterfacesOpened = 0;

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      countOpenedForReadForRadioLink[i] = 0;
      countOpenedForWriteForRadioLink[i] = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForRead = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForWrite = 0;
   }

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);

      if ( controllerIsCardDisabled(pRadioHWInfo->szMAC) )
         continue;

      int nVehicleRadioLinkId = g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId;
      if ( nVehicleRadioLinkId < 0 || nVehicleRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count )
         continue;

      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[nVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;

      // Ignore vehicle's relay radio links
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[nVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
         continue;

      if ( NULL != pRadioHWInfo )
      if ( ((pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_ATHEROS) ||
           ((pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_RALINK) )
      {
         int nRateTx = controllerGetCardDataRate(pRadioHWInfo->szMAC); // Returns 0 if radio link datarate must be used (no custom datarate set for this radio card);
         if ( (0 == nRateTx) && (NULL != g_pCurrentModel) )
         {
            nRateTx = compute_packet_uplink_datarate(nVehicleRadioLinkId, i);
            log_line("Current model uplink radio datarate for vehicle radio link %d (%s): %d, %u, uplink rate type: %d",
               nVehicleRadioLinkId+1, pRadioHWInfo->szName, nRateTx, getRealDataRateFromRadioDataRate(nRateTx),
               g_pCurrentModel->radioLinksParams.uUplinkDataDataRateType[nVehicleRadioLinkId]);
         }
         radio_utils_set_datarate_atheros(NULL, i, nRateTx);
      }

      u32 cardFlags = controllerGetCardFlags(pRadioHWInfo->szMAC);

      if ( cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_RX )
      if ( (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO) ||
           (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA) )
      {
         if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
         {
            if ( hardware_radio_sik_open_for_read_write(i) <= 0 )
               s_iFailedInitRadioInterface = i;
            else
            {
               g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;
               countOpenedForReadForRadioLink[nVehicleRadioLinkId]++;
               totalCountForRead++;

               g_SM_RadioStats.radio_interfaces[i].openedForWrite = 1;
               countOpenedForWriteForRadioLink[nVehicleRadioLinkId]++;
               totalCountForWrite++;
               iCountSikInterfacesOpened++;
            }
         }
         else if ( radio_open_interface_for_read(i, RADIO_PORT_ROUTER_DOWNLINK) > 0 )
         {
            g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;
            countOpenedForReadForRadioLink[nVehicleRadioLinkId]++;
            totalCountForRead++;
         }
         else
            s_iFailedInitRadioInterface = i;
      }

      if ( cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_TX )
      if ( (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO) ||
           (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA) )
      if ( ! hardware_radio_is_sik_radio(pRadioHWInfo) )
      {
         if ( radio_open_interface_for_write(i) > 0 )
         {
            g_SM_RadioStats.radio_interfaces[i].openedForWrite = 1;
            countOpenedForWriteForRadioLink[nVehicleRadioLinkId]++;
            totalCountForWrite++;
         }
         else
            s_iFailedInitRadioInterface = i;
      }
   }

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   log_line("Opening RX/TX radio interfaces complete. %d interfaces opened for RX, %d interfaces opened for TX:", totalCountForRead, totalCountForWrite);

   if ( totalCountForRead == 0 )
   {
      log_error_and_alarm("Failed to find or open any RX interface for receiving data.");
      _close_rxtx_radio_interfaces();
      return;
   }

   if ( 0 == totalCountForWrite )
   {
      log_error_and_alarm("Can't find any TX interfaces for sending data.");
      _close_rxtx_radio_interfaces();
      return;
   }

   for( int i=0; i<g_pCurrentModel->radioLinksParams.links_count; i++ )
   {
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      
      // Ignore vehicle's relay radio links
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
         continue;

      if ( 0 == countOpenedForReadForRadioLink[i] )
         log_error_and_alarm("Failed to find or open any RX interface for receiving data on vehicle's radio link %d.", i+1);
      if ( 0 == countOpenedForWriteForRadioLink[i] )
         log_error_and_alarm("Failed to find or open any TX interface for sending data on vehicle's radio link %d.", i+1);

      //if ( 0 == countOpenedForReadForRadioLink[i] && 0 == countOpenedForWriteForRadioLink[i] )
      //   send_alarm_to_central(ALARM_ID_CONTROLLER_NO_INTERFACES_FOR_RADIO_LINK,i, 0);
   }

   log_line("Opened radio interfaces:");
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
   
      int nVehicleRadioLinkId = g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId;      
      int nLocalRadioLinkId = g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId;      
      char szFlags[128];
      szFlags[0] = 0;
      u32 uFlags = controllerGetCardFlags(pRadioHWInfo->szMAC);
      str_get_radio_capabilities_description(uFlags, szFlags);

      char szType[128];
      strcpy(szType, pRadioHWInfo->szDriver);
      if ( NULL != pCardInfo )
         strcpy(szType, str_get_radio_card_model_string(pCardInfo->cardModel));

      if ( pRadioHWInfo->openedForRead && pRadioHWInfo->openedForWrite )
         log_line(" * Radio Interface %d, %s, %s, %s, local radio link %d, vehicle radio link %d, opened for read/write, flags: %s", i+1, pRadioHWInfo->szName, szType, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), nLocalRadioLinkId+1, nVehicleRadioLinkId+1, szFlags );
      else if ( pRadioHWInfo->openedForRead )
         log_line(" * Radio Interface %d, %s, %s, %s, local radio link %d, vehicle radio link %d, opened for read, flags: %s", i+1, pRadioHWInfo->szName, szType, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), nLocalRadioLinkId+1, nVehicleRadioLinkId+1, szFlags );
      else if ( pRadioHWInfo->openedForWrite )
         log_line(" * Radio Interface %d, %s, %s, %s, local radio link %d, vehicle radio link %d, opened for write, flags: %s", i+1, pRadioHWInfo->szName, szType, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), nLocalRadioLinkId+1, nVehicleRadioLinkId+1, szFlags );
      else
         log_line(" * Radio Interface %d, %s, %s, %s not used. Flags: %s", i+1, pRadioHWInfo->szName, szType, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), szFlags );
   }

   if ( 0 < iCountSikInterfacesOpened )
   {
      radio_tx_set_sik_packet_size(g_pCurrentModel->radioLinksParams.iSiKPacketSize);
      radio_tx_start_tx_thread();
   }

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   log_line("Finished opening RX/TX radio interfaces.");
   log_line("OPEN RADIO INTERFACES END ===========================================================");
   log_line("");
}

void _reinit_radio_interfaces()
{
   char szComm[256];
   
   _close_rxtx_radio_interfaces();
   
   send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE, 0);

   sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
   hw_execute_bash_command(szComm, NULL);

   hw_execute_bash_command("/etc/init.d/udev restart", NULL);
   hardware_sleep_ms(200);
   hw_execute_bash_command("sudo systemctl restart networking", NULL);
   hardware_sleep_ms(200);
   hw_execute_bash_command("sudo ifconfig -a", NULL);

   hardware_sleep_ms(50);

   hw_execute_bash_command("sudo systemctl stop networking", NULL);
   hardware_sleep_ms(200);
   hw_execute_bash_command("sudo ifconfig -a", NULL);

   hardware_sleep_ms(50);

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   char szOutput[4096];
   szOutput[0] = 0;
   hw_execute_bash_command_raw("sudo ifconfig -a | grep wlan", szOutput);

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   log_line("Reinitializing radio interfaces: found interfaces on ifconfig: [%s]", szOutput);
   sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
   hw_execute_bash_command(szComm, NULL);
   
   hw_execute_bash_command("ifconfig wlan0 down", NULL);
   hw_execute_bash_command("ifconfig wlan1 down", NULL);
   hw_execute_bash_command("ifconfig wlan2 down", NULL);
   hw_execute_bash_command("ifconfig wlan3 down", NULL);
   hardware_sleep_ms(200);

   hw_execute_bash_command("ifconfig wlan0 up", NULL);
   hw_execute_bash_command("ifconfig wlan1 up", NULL);
   hw_execute_bash_command("ifconfig wlan2 up", NULL);
   hw_execute_bash_command("ifconfig wlan3 up", NULL);
   
   sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
   hw_execute_bash_command(szComm, NULL);
   
   // Remove radio initialize file flag
   hw_execute_bash_command("rm -rf tmp/ruby/conf_radios", NULL);
   hw_execute_bash_command("./ruby_initradio", NULL);
   
   hardware_sleep_ms(100);
   hardware_reset_radio_enumerated_flag();
   hardware_enumerate_radio_interfaces();

   hardware_save_radio_info();
   hardware_sleep_ms(100);
 
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   log_line("=================================================================");
   log_line("Detected hardware radio interfaces:");
   hardware_log_radio_info();

   _open_rxtx_radio_interfaces();

   send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE, 0);


}

void flag_update_sik_interface(int iInterfaceIndex)
{
   if ( g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex >= 0 )
   {
      log_line("Router was re-flagged to reconfigure SiK radio interface %d.", iInterfaceIndex+1);
      return;
   }
   log_line("Router was flagged to reconfigure SiK radio interface %d.", iInterfaceIndex+1);
   g_SiKRadiosState.uTimeIntervalSiKReinitCheck = 500;
   g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex = iInterfaceIndex;
   g_SiKRadiosState.iThreadRetryCounter = 0;
   close_and_mark_sik_interfaces_to_reopen();

   //send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE, 0);
}


void flag_reinit_sik_interface(int iInterfaceIndex)
{
   // Already flagged ?

   if ( g_SiKRadiosState.bMustReinitSiKInterfaces )
      return;

   log_softerror_and_alarm("Router was flagged to reinit SiK radio interfaces.");
   g_SiKRadiosState.uTimeIntervalSiKReinitCheck = 500;
   g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex = -1;

   close_and_mark_sik_interfaces_to_reopen();

   g_SiKRadiosState.bMustReinitSiKInterfaces = true;
   g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown = (u32) iInterfaceIndex;
   g_SiKRadiosState.iThreadRetryCounter = 0;
   send_alarm_to_central(ALARM_ID_RADIO_INTERFACE_DOWN, g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown, 0);
}

static void * _reinit_sik_thread_func(void *ignored_argument)
{
   log_line("[Router-SiKThread] Reinitializing SiK radio interfaces...");

   // radio serial ports are already closed at this point

   if ( g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex >= 0 )
   {
      log_line("[Router-SiKThread] Must reconfigure and reinitialize SiK radio interface %d...", g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex+1 );
      if ( ! hardware_radio_index_is_sik_radio(g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex) )
         log_softerror_and_alarm("[Router-SiKThread] Radio interface %d is not a SiK radio interface.", g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex+1 );
      else
      {
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex);
         if ( NULL == pRadioHWInfo )
            log_softerror_and_alarm("[Router-SiKThread] Failed to get radio hw info for radio interface %d.", g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex+1);
         else
         {
            ControllerSettings* pCS = get_ControllerSettings();
            u32 uFreqKhz = pRadioHWInfo->uHardwareParamsList[8];
            u32 uDataRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
            u32 uTxPower = pCS->iTXPowerSiK;
            u32 uLBT = 0;
            u32 uECC = 0;
            u32 uMCSTR = 0;

            if ( NULL != g_pCurrentModel )
            {
               int iRadioLink = g_pCurrentModel->radioInterfacesParams.interface_link_id[g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex];
               if ( (iRadioLink >= 0) && (iRadioLink < g_pCurrentModel->radioLinksParams.links_count) )
               {
                  uFreqKhz = g_pCurrentModel->radioLinksParams.link_frequency_khz[iRadioLink];
                  uDataRate = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iRadioLink];
                  uECC = (g_pCurrentModel->radioLinksParams.link_radio_flags[iRadioLink] & RADIO_FLAGS_SIK_ECC)? 1:0;
                  uLBT = (g_pCurrentModel->radioLinksParams.link_radio_flags[iRadioLink] & RADIO_FLAGS_SIK_LBT)? 1:0;
                  uMCSTR = (g_pCurrentModel->radioLinksParams.link_radio_flags[iRadioLink] & RADIO_FLAGS_SIK_MCSTR)? 1:0;
               
                  bool bDataRateOk = false;
                  for( int i=0; i<getSiKAirDataRatesCount(); i++ )
                  {
                     if ( (int)uDataRate == getSiKAirDataRates()[i] )
                     {
                        bDataRateOk = true;
                        break;
                     }
                  }

                  if ( ! bDataRateOk )
                  {
                     log_softerror_and_alarm("[Router-SiKThread] Invalid radio datarate for SiK radio: %d bps. Revert to %d bps.", uDataRate, DEFAULT_RADIO_DATARATE_SIK_AIR);
                     uDataRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
                  }
               }
            }
            int iRes = hardware_radio_sik_set_params(pRadioHWInfo, 
                   uFreqKhz,
                   DEFAULT_RADIO_SIK_FREQ_SPREAD, DEFAULT_RADIO_SIK_CHANNELS,
                   DEFAULT_RADIO_SIK_NETID,
                   uDataRate, uTxPower, 
                   uECC, uLBT, uMCSTR,
                   NULL);
            if ( iRes != 1 )
            {
               log_softerror_and_alarm("[Router-SiKThread] Failed to reconfigure SiK radio interface %d", g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex+1);
               if ( g_SiKRadiosState.iThreadRetryCounter < 3 )
               {
                  log_line("[Router-SiKThread] Will retry to reconfigure radio. Retry counter: %d", g_SiKRadiosState.iThreadRetryCounter);
               }
               else
               {
                  send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED, 0);
                  reopen_marked_sik_interfaces();
  
                  g_SiKRadiosState.bMustReinitSiKInterfaces = false;
                  g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex = -1;
                  g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown = MAX_U32;
               }
               log_line("[Router-SiKThread] Finished.");
               g_SiKRadiosState.bConfiguringSiKThreadWorking = false;
               return NULL;
            }
            else
            {
               log_line("[Router-SiKThread] Updated successfully SiK radio interface %d to txpower %d, airrate: %d bps, ECC/LBT/MCSTR: %d/%d/%d",
                   g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex+1,
                   uTxPower, uDataRate, uECC, uLBT, uMCSTR);
               radio_stats_set_card_current_frequency(&g_SM_RadioStats, g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex, uFreqKhz);
               if ( NULL != g_pSM_RadioStats )
                  memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
            }
         }
      }
   }
   else if ( 1 != hardware_radio_sik_reinitialize_serial_ports() )
   {
      log_line("[Router-SiKThread] Reinitializing of SiK radio interfaces failed (not the same ones are present yet).");
      // Will restart the thread to try again
      log_line("[Router-SiKThread] Finished.");
      g_SiKRadiosState.bConfiguringSiKThreadWorking = false;
      return NULL;
   }
   
   log_line("[Router-SiKThread] Reinitialized SiK radio interfaces successfully.");
   
   reopen_marked_sik_interfaces();

   if ( g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex >= 0 )
      send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE, 0);
   else
      send_alarm_to_central(ALARM_ID_RADIO_INTERFACE_REINITIALIZED, g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown, 0);
   
   g_SiKRadiosState.bMustReinitSiKInterfaces = false;
   g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex = -1;
   g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown = MAX_U32;
   log_line("[Router-SiKThread] Finished.");
   g_SiKRadiosState.bConfiguringSiKThreadWorking = false;
   radio_rx_reset_interfaces_broken_state();

   return NULL;
}

int _check_reinit_sik_interfaces()
{
   if ( g_SiKRadiosState.bConfiguringToolInProgress && (g_SiKRadiosState.uTimeStartConfiguring != 0) )
   if ( g_TimeNow >= g_SiKRadiosState.uTimeStartConfiguring+500 )
   {
      if ( hw_process_exists("ruby_sik_config") )
      {
         g_SiKRadiosState.uTimeStartConfiguring = g_TimeNow - 400;
      }
      else
      {
         int iResult = -1;
         FILE* fd = fopen(FILE_TMP_SIK_CONFIG_FINISHED, "rb");
         if ( NULL != fd )
         {
            if ( 1 != fscanf(fd, "%d", &iResult) )
               iResult = -2;
            fclose(fd);
         }
         log_line("SiK radio configuration tool completed. Result: %d.", iResult);
         char szBuff[256];
         sprintf(szBuff, "rm -rf %s", FILE_TMP_SIK_CONFIG_FINISHED);
         hw_execute_bash_command(szBuff, NULL);
         g_SiKRadiosState.bConfiguringToolInProgress = false;
         reopen_marked_sik_interfaces();
         send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE, 0);
         return 0;
      }
   }
   
   if ( (! g_SiKRadiosState.bMustReinitSiKInterfaces) && (g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex == -1) )
      return 0;

   if ( g_SiKRadiosState.bConfiguringToolInProgress )
      return 0;

   if ( g_SiKRadiosState.bConfiguringSiKThreadWorking )
      return 0;
   
   if ( g_TimeNow < g_SiKRadiosState.uTimeLastSiKReinitCheck + g_SiKRadiosState.uTimeIntervalSiKReinitCheck )
      return 0;

   g_SiKRadiosState.uTimeLastSiKReinitCheck = g_TimeNow;
   g_SiKRadiosState.uTimeIntervalSiKReinitCheck += 200;

   g_SiKRadiosState.bConfiguringSiKThreadWorking = true;
   if ( 0 != pthread_create(&g_SiKRadiosState.pThreadSiKReinit, NULL, &_reinit_sik_thread_func, NULL) )
   {
      log_softerror_and_alarm("[Router] Failed to create worker thread to reinit SiK radio interfaces.");
      g_SiKRadiosState.bConfiguringSiKThreadWorking = false;
      return 0;
   }

   log_line("[Router] Created thread to reinit SiK radio interfaces.");
   if ( 0 == g_SiKRadiosState.iThreadRetryCounter )
      send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE, 0);
   g_SiKRadiosState.iThreadRetryCounter++;
   return 1;
}

void _compute_radio_interfaces_assignment()
{
   log_line("------------------------------------------------------------------");

   if ( g_bSearching || (NULL == g_pCurrentModel) )
   {
      log_error_and_alarm("Invalid parameters for assigning radio interfaces");
      return;
   }

   g_SM_RadioStats.countVehicleRadioLinks = 0;
   if ( NULL != g_pCurrentModel )
      g_SM_RadioStats.countVehicleRadioLinks = g_pCurrentModel->radioLinksParams.links_count;
   else
      g_SM_RadioStats.countVehicleRadioLinks = 1;
 
   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = -1;
      g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = -1;
      g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId = -1;
   }

   // See how many active radio links the vehicle has

   u32 uStoredMainFrequencyForModel = get_model_main_connect_frequency(g_pCurrentModel->vehicle_id);
   int iStoredMainRadioLinkForModel = -1;

   int iCountAssignedVehicleRadioLinks = 0;

   int iCountVehicleActiveUsableRadioLinks = 0;
   u32 uConnectFirstUsableFrequency = 0;
   int iConnectFirstUsableRadioLinkId = 0;

   log_line("Computing radio interfaces assignment to radio links...");
   log_line("Vehicle (%s) main 'connect to' frequency: %s, vehicle has a total of %d radio links.", g_pCurrentModel->getLongName(), str_format_frequency(uStoredMainFrequencyForModel), g_pCurrentModel->radioLinksParams.links_count);

   for( int i=0; i<g_pCurrentModel->radioLinksParams.links_count; i++ )
   {
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
      {
         log_line("Vehicle's radio link %d is disabled. Skipping it.", i+1);
         continue;
      }

      // Ignore vehicle's relay radio links
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      {
         log_line("Vehicle's radio link %d is used for relaying. Skipping it.", i+1);
         continue;
      }

      if ( (g_pCurrentModel->sw_version>>16) >= 45 )
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      {
         log_line("Vehicle's radio link %d is used for relaying. Skipping it.", i+1);
         continue;       
      }      

      log_line("Vehicle's radio link %d is usable, frequency: %s", i+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[i]));
      iCountVehicleActiveUsableRadioLinks++;
      if ( 0 == uConnectFirstUsableFrequency )
      {
         uConnectFirstUsableFrequency = g_pCurrentModel->radioLinksParams.link_frequency_khz[i];
         iConnectFirstUsableRadioLinkId = i;
      }
      if ( g_pCurrentModel->radioLinksParams.link_frequency_khz[i] == uStoredMainFrequencyForModel )
      {
         iStoredMainRadioLinkForModel = i;
         log_line("Vehicle's radio link %d is the main connect to radio link.", i+1);
      }
   }

   log_line("Vehicle has %d active (enabled and not relay) radio links (out of %d radio links)", iCountVehicleActiveUsableRadioLinks, g_pCurrentModel->radioLinksParams.links_count);
      
   if ( 0 == iCountVehicleActiveUsableRadioLinks )
   {
      log_error_and_alarm("Vehicle has no active (enabled and not relay) radio links (out of %d radio links)", g_pCurrentModel->radioLinksParams.links_count);
      return;
   }

   // Begin - Model with a single active radio link

   if ( 1 == iCountVehicleActiveUsableRadioLinks )
   {
      log_line("Computing controller's radio interfaces assignment to vehicle's radio link %d (vehicle has a single active (enabled and not relay) radio link on %s)", iConnectFirstUsableRadioLinkId+1, str_format_frequency(uConnectFirstUsableFrequency));
      
      int iCountInterfacesAssigned = 0;

      for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
      {
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
         if ( controllerIsCardDisabled(pRadioHWInfo->szMAC) )
         {
            log_line("  * Radio interface %d is disabled, do not assign it.", i+1);
            continue;
         }
         if ( ! hardware_radio_supports_frequency(pRadioHWInfo, uConnectFirstUsableFrequency) )
         {
            log_line("  * Radio interface %d does not support %s, do not assign it.", i+1, str_format_frequency(uConnectFirstUsableFrequency));
            continue;
         }
         g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = 0;
         g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iConnectFirstUsableRadioLinkId;
         g_SM_RadioStats.radio_links[0].matchingVehicleRadioLinkId = iConnectFirstUsableRadioLinkId;
         iCountInterfacesAssigned++;
         t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
         if ( NULL != pCardInfo )  
            log_line("  * Assigned radio interface %d (%s) to vehicle radio link %d", i+1, str_get_radio_card_model_string(pCardInfo->cardModel), iConnectFirstUsableRadioLinkId+1);
         else
            log_line("  * Assigned radio interface %d (%s) to vehicle radio link %d", i+1, "Unknown Type", iConnectFirstUsableRadioLinkId+1);
      }
      iCountAssignedVehicleRadioLinks = 1;
      g_SM_RadioStats.countLocalRadioLinks = 1;
      if ( NULL != g_pSM_RadioStats )
         memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
      if ( 0 == iCountAssignedVehicleRadioLinks )
         send_alarm_to_central(ALARM_ID_CONTROLLER_NO_INTERFACES_FOR_RADIO_LINK,iConnectFirstUsableRadioLinkId, 0);
      
      log_line("Controller will have %d radio links to vehicle.", iCountAssignedVehicleRadioLinks);
      log_line("Done computing radio interfaces assignment to radio links.");
      log_line("------------------------------------------------------------------");
      return;
   }

   // End - Model with a single active radio link


   // Begin - Model with a multiple active radio links

   log_line("Computing controller's radio interfaces assignment to vehicle's radio links (vehicle has %d active radio links (enabled and not relay), main controller connect frequency is: %s)", iCountVehicleActiveUsableRadioLinks, str_format_frequency(uStoredMainFrequencyForModel));

   if ( uStoredMainFrequencyForModel <= 0 )
      uStoredMainFrequencyForModel = uConnectFirstUsableFrequency;

   // Check what vehicle radio links are supported by each radio interface.

   bool bInterfaceSupportsVehicleLink[MAX_RADIO_INTERFACES][MAX_RADIO_INTERFACES];
   bool bInterfaceSupportsMainConnectLink[MAX_RADIO_INTERFACES];
   int iInterfaceSupportedLinksCount[MAX_RADIO_INTERFACES];
   
   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      bInterfaceSupportsMainConnectLink[i] = false;
      iInterfaceSupportedLinksCount[i] = 0;
      for( int k=0; k<MAX_RADIO_INTERFACES; k++ )
         bInterfaceSupportsVehicleLink[i][k] = false;
   }

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
      {
         log_softerror_and_alarm("Failed to get controller's radio interface %d hardware info. Skipping it.", i+1);
         continue;
      }
      if ( controllerIsCardDisabled(pRadioHWInfo->szMAC) )
      {
         log_line("Controller's radio interface %d is disabled. Skipping it.", i+1);
         continue;
      }

      u32 cardFlags = controllerGetCardFlags(pRadioHWInfo->szMAC);

      for( int iRadioLink=0; iRadioLink<g_pCurrentModel->radioLinksParams.links_count; iRadioLink++ )
      {
         if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
            continue;
         if ( (g_pCurrentModel->sw_version>>16) >= 45 )
         if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
            continue;

         // Uplink type radio link and RX only radio interface

         if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_CAN_RX )
         if ( ! (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
         if ( ! (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
            continue;

         // Downlink type radio link and TX only radio interface

         if ( ! (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
         if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLink] & RADIO_HW_CAPABILITY_FLAG_CAN_TX )
         if ( ! (cardFlags & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
            continue;

         if ( hardware_radio_supports_frequency(pRadioHWInfo, g_pCurrentModel->radioLinksParams.link_frequency_khz[iRadioLink]) )
         {
            log_line("Controller's radio interface %d does support vehicle's radio link %d (%s).", i+1, iRadioLink+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iRadioLink]));
            bInterfaceSupportsVehicleLink[i][iRadioLink] = true;
            iInterfaceSupportedLinksCount[i]++;

            if ( uStoredMainFrequencyForModel == g_pCurrentModel->radioLinksParams.link_frequency_khz[iRadioLink] )
               bInterfaceSupportsMainConnectLink[i] = true;
         }
      }
   }


   bool bCtrlInterfaceWasAssigned[MAX_RADIO_INTERFACES];
   bool bVehicleLinkWasAssigned[MAX_RADIO_INTERFACES];
   int  iVehicleLinkWasAssignedToControllerLinkIndex[MAX_RADIO_INTERFACES];

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      bCtrlInterfaceWasAssigned[i] = false;
      bVehicleLinkWasAssigned[i] = false;
      iVehicleLinkWasAssignedToControllerLinkIndex[i] = -1;
   }

   // Begin - First, assign the radio interfaces that supports only a single radio link

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
         continue;
      if ( iInterfaceSupportedLinksCount[i] != 1 )
         continue;

      int iSupportedVehicleLinkByInterface = -1;
      for( int k=0; k<MAX_RADIO_INTERFACES; k++ )
      {
         if ( bInterfaceSupportsVehicleLink[i][k] )
         {
            iSupportedVehicleLinkByInterface = k;
            break;
         }
      }
      if ( (-1 == iSupportedVehicleLinkByInterface) || (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iSupportedVehicleLinkByInterface] & RADIO_HW_CAPABILITY_FLAG_DISABLED) )
         continue;
      
      if ( ! bVehicleLinkWasAssigned[iSupportedVehicleLinkByInterface] )
      {
         iVehicleLinkWasAssignedToControllerLinkIndex[iSupportedVehicleLinkByInterface] = iCountAssignedVehicleRadioLinks;
         iCountAssignedVehicleRadioLinks++;
      }
      bVehicleLinkWasAssigned[iSupportedVehicleLinkByInterface] = true;
      bCtrlInterfaceWasAssigned[i] = true;
      
      g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = iVehicleLinkWasAssignedToControllerLinkIndex[iSupportedVehicleLinkByInterface];
      g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iSupportedVehicleLinkByInterface;
      g_SM_RadioStats.radio_links[iVehicleLinkWasAssignedToControllerLinkIndex[iSupportedVehicleLinkByInterface]].matchingVehicleRadioLinkId = iSupportedVehicleLinkByInterface;

      t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
      if ( NULL != pCardInfo )  
         log_line("  * Step A) Assigned controller's radio interface %d (%s) to controller radio link %d, vehicle's radio link %d, %s, as it supports a single radio link from vehicle.", i+1, str_get_radio_card_model_string(pCardInfo->cardModel), g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iSupportedVehicleLinkByInterface+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iSupportedVehicleLinkByInterface]));
      else
         log_line("  * Step A) Assigned controller's radio interface %d (%s) to controller radio link %d, vehicle's radio link %d, %s, as it supports a single radio link from vehicle.", i+1, "Unknown Type", g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iSupportedVehicleLinkByInterface+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iSupportedVehicleLinkByInterface]));
   }

   // End - First, assign the radio interfaces that supports only a single radio link

   // Assign at least one radio interface to the main connect radio link

   if ( (iStoredMainRadioLinkForModel != -1) && (! bVehicleLinkWasAssigned[iStoredMainRadioLinkForModel]) )
   {
      for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
      {
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
         if ( NULL == pRadioHWInfo || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
            continue;
         if ( bCtrlInterfaceWasAssigned[i] )
            continue;
         if ( ! bInterfaceSupportsMainConnectLink[i] )
            continue;

         if ( ! bVehicleLinkWasAssigned[iStoredMainRadioLinkForModel] )
         {
            iVehicleLinkWasAssignedToControllerLinkIndex[iStoredMainRadioLinkForModel] = iCountAssignedVehicleRadioLinks;
            iCountAssignedVehicleRadioLinks++;
         }
         bVehicleLinkWasAssigned[iStoredMainRadioLinkForModel] = true;
         bCtrlInterfaceWasAssigned[i] = true;
         
         g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = iVehicleLinkWasAssignedToControllerLinkIndex[iStoredMainRadioLinkForModel];
         g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iStoredMainRadioLinkForModel;
         g_SM_RadioStats.radio_links[iVehicleLinkWasAssignedToControllerLinkIndex[iStoredMainRadioLinkForModel]].matchingVehicleRadioLinkId = iStoredMainRadioLinkForModel;

         t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
         if ( NULL != pCardInfo )  
            log_line("  * Step B) Assigned controller's radio interface %d (%s) to controller radio link %d, vehicle's main connect radio link %d, %s", i+1, str_get_radio_card_model_string(pCardInfo->cardModel), g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iStoredMainRadioLinkForModel+1, str_format_frequency(uStoredMainFrequencyForModel));
         else
            log_line("  * Step B) Assigned controller's radio interface %d (%s) to controller radio link %d, vehicle's main connect radio link %d, %s", i+1, "Unknown Type", g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iStoredMainRadioLinkForModel+1, str_format_frequency(uStoredMainFrequencyForModel));
         break;
      }
   }

   // Assign alternativelly each remaining radio interfaces to one radio link

   // Assign to the first vehicle's radio link that has no cards assigned to

   int iVehicleRadioLinkIdToAssign = 0;
   int iSafeCounter = 10;
   while ( true && (iSafeCounter > 0) )
   {
      iSafeCounter--;
      if ( ! bVehicleLinkWasAssigned[iVehicleRadioLinkIdToAssign] )
         break;       

      iVehicleRadioLinkIdToAssign++;
      if ( iVehicleRadioLinkIdToAssign >= g_pCurrentModel->radioLinksParams.links_count )
      {
         iVehicleRadioLinkIdToAssign = 0;
         break;
      }
   }

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
         continue;
      if ( iInterfaceSupportedLinksCount[i] < 2 )
         continue;
      if ( bCtrlInterfaceWasAssigned[i] )
         continue;

      int k=0;
      do
      {
         if ( bInterfaceSupportsVehicleLink[i][iVehicleRadioLinkIdToAssign] )
         {
            if ( ! bVehicleLinkWasAssigned[iVehicleRadioLinkIdToAssign] )
            {
               iVehicleLinkWasAssignedToControllerLinkIndex[iVehicleRadioLinkIdToAssign] = iCountAssignedVehicleRadioLinks;
               iCountAssignedVehicleRadioLinks++;
            }
            bVehicleLinkWasAssigned[iVehicleRadioLinkIdToAssign] = true;
            bCtrlInterfaceWasAssigned[i] = true;
            
            g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = iVehicleLinkWasAssignedToControllerLinkIndex[iVehicleRadioLinkIdToAssign];
            g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iVehicleRadioLinkIdToAssign;
            g_SM_RadioStats.radio_links[iVehicleLinkWasAssignedToControllerLinkIndex[iVehicleRadioLinkIdToAssign]].matchingVehicleRadioLinkId = iVehicleRadioLinkIdToAssign;

            t_ControllerRadioInterfaceInfo* pCardInfo = controllerGetRadioCardInfo(pRadioHWInfo->szMAC);
            if ( NULL != pCardInfo )  
               log_line("  * C) Assigned controller's radio interface %d (%s) to controller radio link %d, radio link %d, %s", i+1, str_get_radio_card_model_string(pCardInfo->cardModel), g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iVehicleRadioLinkIdToAssign+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkIdToAssign]));
            else
               log_line("  * C) Assigned controller's radio interface %d (%s) to controller radio link %d, radio link %d, %s", i+1, "Unknown Type", g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId+1, iVehicleRadioLinkIdToAssign+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkIdToAssign]));
         }
         k++;
         iVehicleRadioLinkIdToAssign++;
         if ( iVehicleRadioLinkIdToAssign >= g_pCurrentModel->radioLinksParams.links_count )
            iVehicleRadioLinkIdToAssign = 0;
      }
      while ( (! bCtrlInterfaceWasAssigned[i]) && (k <= MAX_RADIO_INTERFACES) );
   }

   g_SM_RadioStats.countLocalRadioLinks = iCountAssignedVehicleRadioLinks;
   log_line("Assigned %d controller radio links to controller links (vehicle has %d active radio links)", iCountAssignedVehicleRadioLinks, iCountVehicleActiveUsableRadioLinks);
   
   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));

   // Log errors

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
      {
         log_line("  * Radio interface %d is disabled. It was not assigned to any radio link.", i+1 );
         continue;
      }
      if ( iInterfaceSupportedLinksCount[i] == 0 )
      {
         log_line("  * Radio interface %d does not support any radio links.", i+1 );
         continue;
      }
   }

   for( int i=0; i<g_pCurrentModel->radioLinksParams.links_count; i++ )
   {
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;

      // Ignore vehicle's relay radio links
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
         continue;

      if ( ! bVehicleLinkWasAssigned[i] )
      {
         log_softerror_and_alarm("  * No controller radio interfaces where assigned to vehicle radio link %d !", i+1);
         int iCountAssignableRadioInterfaces = controller_count_asignable_radio_interfaces_to_vehicle_radio_link(g_pCurrentModel, i);
         if ( 0 == iCountAssignableRadioInterfaces )
            send_alarm_to_central(ALARM_ID_CONTROLLER_NO_INTERFACES_FOR_RADIO_LINK, (u32)i, 0);
      }
   }
   log_line("Radio links mapping (%d controller links to %d vehicle links:", g_SM_RadioStats.countLocalRadioLinks, g_pCurrentModel->radioLinksParams.links_count);
   for( int i=0; i<g_SM_RadioStats.countLocalRadioLinks; i++ )
      log_line("* Local radio link %d mapped to vehicle radio link %d;", i+1, g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId+1);

   log_line("Done computing radio interfaces assignment to radio links.");
   log_line("------------------------------------------------------------------");
}


bool links_set_cards_frequencies_for_search( u32 uSearchFreq, bool bSiKSearch, int iAirDataRate, int iECC, int iLBT, int iMCSTR )
{
   log_line("Links: Set all cards frequencies for search mode to %s", str_format_frequency(uSearchFreq));
   if ( bSiKSearch )
      log_line("Search SiK mode update. Change all cards frequencies and update SiK params: Airrate: %d bps, ECC/LBT/MCSTR: %d/%d/%d",
         iAirDataRate, iECC, iLBT, iMCSTR);
   else
      log_line("No SiK mode update. Just change all interfaces frequencies");
   
   ControllerSettings* pCS = get_ControllerSettings();
   
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;

      u32 flags = controllerGetCardFlags(pRadioHWInfo->szMAC);
      if ( (flags & RADIO_HW_CAPABILITY_FLAG_DISABLED) || controllerIsCardDisabled(pRadioHWInfo->szMAC) )
      {
         log_line("Links: Radio interface %d is disabled. Skipping it.", i+1);
         continue;
      }

      if ( 0 == hardware_radio_supports_frequency(pRadioHWInfo, uSearchFreq ) )
      {
         log_line("Links: Radio interface %d does not support search frequency %s. Skipping it.", i+1, str_format_frequency(uSearchFreq));
         continue;
      }

      if ( ! (flags & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
      {
         log_line("Links: Radio interface %d is disabled. Skipping it.", i+1);
         continue;
      }

      if ( ! (flags & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA) )
      {
         log_line("Links: Radio interface %d can't be used for Rx. Skipping it.", i+1);
         continue;
      }

      if ( bSiKSearch && hardware_radio_is_sik_radio(pRadioHWInfo) )
      {
         u32 uFreqKhz = uSearchFreq;
         u32 uDataRate = iAirDataRate;
         u32 uTxPower = pCS->iTXPowerSiK;
         u32 uECC = iECC;
         u32 uLBT = iLBT;
         u32 uMCSTR = iMCSTR;

         bool bDataRateOk = false;
         for( int i=0; i<getSiKAirDataRatesCount(); i++ )
         {
            if ( (int)uDataRate == getSiKAirDataRates()[i] )
            {
               bDataRateOk = true;
               break;
            }
         }

         if ( ! bDataRateOk )
         {
            log_softerror_and_alarm("Invalid radio datarate for SiK radio: %d bps. Revert to %d bps.", uDataRate, DEFAULT_RADIO_DATARATE_SIK_AIR);
            uDataRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
         }
         
         int iRetry = 0;
         while ( iRetry < 2 )
         {
            int iRes = hardware_radio_sik_set_params(pRadioHWInfo, 
                   uFreqKhz,
                   DEFAULT_RADIO_SIK_FREQ_SPREAD, DEFAULT_RADIO_SIK_CHANNELS,
                   DEFAULT_RADIO_SIK_NETID,
                   uDataRate, uTxPower, 
                   uECC, uLBT, uMCSTR,
                   NULL);
            if ( iRes != 1 )
            {
               log_softerror_and_alarm("Failed to configure SiK radio interface %d", i+1);
               iRetry++;
            }
            else
            {
               log_line("Updated successfully SiK radio interface %d to txpower %d, airrate: %d bps, ECC/LBT/MCSTR: %d/%d/%d",
                   i+1, uTxPower, uDataRate, uECC, uLBT, uMCSTR);
               radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, uSearchFreq);
               break;
            }
         }
      }
      else
      {
         if ( radio_utils_set_interface_frequency(NULL, i, uSearchFreq, g_pProcessStats) )
            radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, uSearchFreq);
      }
   }

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
   log_line("Links: Set all cards frequencies for search mode to %s. Completed.", str_format_frequency(uSearchFreq));
   return true;
}

bool links_set_cards_frequencies_and_params(int iVehicleLinkId)
{
   if ( g_bSearching || (NULL == g_pCurrentModel) )
   {
      log_error_and_alarm("Invalid parameters for setting radio interfaces frequencies");
      return false;
   }

   if ( (iVehicleLinkId < 0) || (iVehicleLinkId >= hardware_get_radio_interfaces_count()) )
      log_line("Links: Setting all cards frequencies and params according to vehicle radio links...");
   else
      log_line("Links: Setting cards frequencies and params only for vehicle radio link %d", iVehicleLinkId+1);

   ControllerSettings* pCS = get_ControllerSettings();
         
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      
      if ( controllerIsCardDisabled(pRadioHWInfo->szMAC) )
      {
         log_line("Links: Radio interface %d is disabled. Skipping it.", i+1);
         continue;
      }

      int nAssignedVehicleRadioLinkId = g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId;
      if ( nAssignedVehicleRadioLinkId < 0 || nAssignedVehicleRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count )
      {
         log_line("Links: Radio interface %d is not assigned to any vehicle radio link. Skipping it.", i+1);
         continue;
      }

      if ( ( iVehicleLinkId >= 0 ) && (nAssignedVehicleRadioLinkId != iVehicleLinkId) )
         continue;

      if ( 0 == hardware_radio_supports_frequency(pRadioHWInfo, g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId] ) )
      {
         log_line("Links: Radio interface %d does not support vehicle radio link %d frequency %s. Skipping it.", i+1, nAssignedVehicleRadioLinkId+1, str_format_frequency(g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId]));
         continue;
      }

      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      {
         if ( iVehicleLinkId >= 0 )
            flag_update_sik_interface(i);
         else
         {
            u32 uFreqKhz = g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId];
            u32 uDataRate = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[nAssignedVehicleRadioLinkId];
            u32 uTxPower = pCS->iTXPowerSiK;
            u32 uECC = (g_pCurrentModel->radioLinksParams.link_radio_flags[nAssignedVehicleRadioLinkId] & RADIO_FLAGS_SIK_ECC)? 1:0;
            u32 uLBT = (g_pCurrentModel->radioLinksParams.link_radio_flags[nAssignedVehicleRadioLinkId] & RADIO_FLAGS_SIK_LBT)? 1:0;
            u32 uMCSTR = (g_pCurrentModel->radioLinksParams.link_radio_flags[nAssignedVehicleRadioLinkId] & RADIO_FLAGS_SIK_MCSTR)? 1:0;

            bool bDataRateOk = false;
            for( int i=0; i<getSiKAirDataRatesCount(); i++ )
            {
               if ( (int)uDataRate == getSiKAirDataRates()[i] )
               {
                  bDataRateOk = true;
                  break;
               }
            }

            if ( ! bDataRateOk )
            {
               log_softerror_and_alarm("Invalid radio datarate for SiK radio: %d bps. Revert to %d bps.", uDataRate, DEFAULT_RADIO_DATARATE_SIK_AIR);
               uDataRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
            }
            
            int iRetry = 0;
            while ( iRetry < 2 )
            {
               int iRes = hardware_radio_sik_set_params(pRadioHWInfo, 
                      uFreqKhz,
                      DEFAULT_RADIO_SIK_FREQ_SPREAD, DEFAULT_RADIO_SIK_CHANNELS,
                      DEFAULT_RADIO_SIK_NETID,
                      uDataRate, uTxPower, 
                      uECC, uLBT, uMCSTR,
                      NULL);
               if ( iRes != 1 )
               {
                  log_softerror_and_alarm("Failed to configure SiK radio interface %d", i+1);
                  iRetry++;
               }
               else
               {
                  log_line("Updated successfully SiK radio interface %d to txpower %d, airrate: %d bps, ECC/LBT/MCSTR: %d/%d/%d",
                     i+1, uTxPower, uDataRate, uECC, uLBT, uMCSTR);
                  radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId]);
                  break;
               }
            }
         }
      }
      else
      {
         if ( radio_utils_set_interface_frequency(NULL, i, g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId], g_pProcessStats) )
            radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, g_pCurrentModel->radioLinksParams.link_frequency_khz[nAssignedVehicleRadioLinkId]);
      }
   }

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));

   hardware_save_radio_info();

   return true;
}

void reasign_radio_links()
{
   log_line("ROUTER REASIGN LINKS START -----------------------------------------------------");
   log_line("Router: Reasigning radio interfaces to radio links (close, reasign, reopen radio interfaces)...");

   send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_REASIGNING_RADIO_LINKS_START, 0);

   radio_rx_stop_rx_thread();
   _close_rxtx_radio_interfaces();
   _compute_radio_interfaces_assignment();
   links_set_cards_frequencies_and_params(-1);
   _open_rxtx_radio_interfaces();

   radio_duplicate_detection_init();
   radio_rx_start_rx_thread(&g_SM_RadioStats, &g_SM_RadioStatsInterfacesRxGraph, (int)g_bSearching);

   send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_REASIGNING_RADIO_LINKS_END, 0);

   log_line("Router: Reasigning radio interfaces to radio links complete.");
   log_line("ROUTER REASIGN LINKS END -------------------------------------------------------");
}

void broadcast_router_ready()
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROLLER_ROUTER_READY, STREAM_ID_DATA);
   PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
   PH.vehicle_id_dest = 0;
   PH.total_length = sizeof(t_packet_header);

   u8 buffer[MAX_PACKET_TOTAL_SIZE];
   memcpy(buffer, (u8*)&PH, sizeof(t_packet_header));
   
   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;

   if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, buffer, PH.total_length) )
      log_softerror_and_alarm("No pipe to central to broadcast router ready to.");

   if ( -1 != g_fIPCToTelemetry )
      ruby_ipc_channel_send_message(g_fIPCToTelemetry, buffer, PH.total_length);

   log_line("Broadcasted that router is ready.");
}

void _check_for_atheros_datarate_change_command_to_vehicle(u8* pPacketBuffer)
{
   if ( NULL == pPacketBuffer )
      return;
   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;

   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_COMMANDS )
      return;

   if ( pPH->packet_type != PACKET_TYPE_COMMAND )
      return;

   int iParamsLength = pPH->total_length - sizeof(t_packet_header) - sizeof(t_packet_header_command);
   t_packet_header_command* pPHC = (t_packet_header_command*)(pPacketBuffer + sizeof(t_packet_header));

   if ( (pPHC->command_type != COMMAND_ID_SET_RADIO_LINK_FLAGS) ||
         (iParamsLength != (int)(2*sizeof(u32)+2*sizeof(int))) ) 
      return;

   u32* pInfo = (u32*)(pPacketBuffer + sizeof(t_packet_header)+sizeof(t_packet_header_command));
   u32 linkIndex = *pInfo;
   pInfo++;
   u32 linkFlags = *pInfo;
   pInfo++;
   int* piInfo = (int*)pInfo;
   int datarateVideo = *piInfo;
   piInfo++;
   int datarateData = *piInfo;

   log_line("Intercepted Set Radio Links Flags command to vehicle: Link %d, Link flags: %s, Datarate %d/%d", linkIndex+1, str_get_radio_frame_flags_description2(linkFlags), datarateVideo, datarateData);
   g_TimeLastSetRadioFlagsCommandSent = g_TimeNow;
   g_uLastRadioLinkIndexForSentSetRadioLinkFlagsCommand = linkIndex;
   g_iLastRadioLinkDataRateSentForSetRadioLinkFlagsCommand = datarateData;
}


// returns 0 if not ping must be sent now, or the ping interval in ms if it must be inserted
int _must_inject_ping_now()
{
   if ( g_bUpdateInProgress || g_bSearching || (NULL == g_pCurrentModel) || g_pCurrentModel->is_spectator || g_pCurrentModel->b_mustSyncFromVehicle )
      return 0;

   u32 ping_interval_ms = compute_ping_interval_ms(g_pCurrentModel->uModelFlags, g_pCurrentModel->rxtx_sync_type, g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags);

   //if ( g_pCurrentModel->radioLinksParams.links_count > 1 )
   //   ping_interval_ms /= g_pCurrentModel->radioLinksParams.links_count;

   if ( (g_SM_RadioStats.countLocalRadioLinks > 1 ) || (g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0) )
      ping_interval_ms /= 2;

   static u32 sl_uTimeLastPingSent = 0;

   if ( g_TimeNow > sl_uTimeLastPingSent + ping_interval_ms ||
        g_TimeNow < sl_uTimeLastPingSent )
   {
      sl_uTimeLastPingSent = g_TimeNow;   
      return (int)ping_interval_ms;
   }
   return 0;
}

// returns true if it added a packet to the radio queue

bool _check_send_or_queue_ping()
{
   if ( (NULL == g_pCurrentModel) || g_bSearching )
      return false;
   if ( _must_inject_ping_now() <= 0 )
      return false;

   static u8 s_uPingToSendId = 0xFF;
   static u8 s_uPingToSendLocalRadioLinkId = 0;
   static u8 s_uPingToSendVehicleIndex = 0;

   // Send next ping id
   s_uPingToSendId++;

   // Iterate all vehicles present on a controller radio link, then move to next radio link
   
   u8 uCountVehicles = 1;

   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
      uCountVehicles++;

   s_uPingToSendVehicleIndex++;
   if ( s_uPingToSendVehicleIndex >= uCountVehicles )
   {
      s_uPingToSendVehicleIndex = 0;
      s_uPingToSendLocalRadioLinkId++;
      if ( s_uPingToSendLocalRadioLinkId >= g_SM_RadioStats.countLocalRadioLinks )
         s_uPingToSendLocalRadioLinkId = 0;
   }
   
   int iVehicleRadioLinkId = g_SM_RadioStats.radio_links[s_uPingToSendLocalRadioLinkId].matchingVehicleRadioLinkId;
   if ( (iVehicleRadioLinkId < 0) || (iVehicleRadioLinkId >= MAX_RADIO_INTERFACES) ||
      ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY ) )
      return false;

   u32 uDestinationVehicleId = 0;
   u8 uDestinationRelayCapabilities = 0;
   u8 uDestinationRelayMode = 0;

   if ( 0 == s_uPingToSendVehicleIndex )
      uDestinationVehicleId = g_pCurrentModel->vehicle_id;

   if ( 1 == s_uPingToSendVehicleIndex )
      uDestinationVehicleId = g_pCurrentModel->relay_params.uRelayedVehicleId;

   if ( 0 == s_uPingToSendVehicleIndex )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
   {
      uDestinationRelayCapabilities = g_pCurrentModel->relay_params.uRelayCapabilitiesFlags;
      uDestinationRelayMode = g_pCurrentModel->relay_params.uCurrentRelayMode;
      uDestinationRelayMode |= RELAY_MODE_IS_RELAY_NODE;
      uDestinationRelayMode &= ~RELAY_MODE_IS_RELAYED_NODE;
   }

   if ( 1 == s_uPingToSendVehicleIndex )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
   {
      uDestinationRelayCapabilities = g_pCurrentModel->relay_params.uRelayCapabilitiesFlags;
      uDestinationRelayMode = g_pCurrentModel->relay_params.uCurrentRelayMode;
      uDestinationRelayMode &= ~RELAY_MODE_IS_RELAY_NODE;
      uDestinationRelayMode |= RELAY_MODE_IS_RELAYED_NODE;
   }

   // Store info about this ping

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i] != uDestinationVehicleId )
         continue;
      for( int k=2; k>0; k-- )
      {
         g_SM_RouterVehiclesRuntimeInfo.uLastPingIdSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][k] = g_SM_RouterVehiclesRuntimeInfo.uLastPingIdSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][k-1];
         g_SM_RouterVehiclesRuntimeInfo.uTimeLastPingSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][k] = g_SM_RouterVehiclesRuntimeInfo.uTimeLastPingSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][k-1];
      }
      g_SM_RouterVehiclesRuntimeInfo.uLastPingIdSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][0] = s_uPingToSendId;
      g_SM_RouterVehiclesRuntimeInfo.uTimeLastPingSentToVehiclesOnLocalRadioLinks[i][s_uPingToSendLocalRadioLinkId][0] = get_current_timestamp_micros();
   }

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_RUBY_PING_CLOCK, STREAM_ID_DATA);
   PH.vehicle_id_src = g_uControllerId;
   PH.vehicle_id_dest = uDestinationVehicleId;
   if ( (g_pCurrentModel->sw_version>>16) > 79 ) // 7.7
      PH.total_length = sizeof(t_packet_header) + 4*sizeof(u8);
   else
      PH.total_length = sizeof(t_packet_header) + 2*sizeof(u8);

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO
   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK_PARAMS )
   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_ADAPTIVE_VIDEO_LINK_USE_CONTROLLER_INFO_TOO )
   if ( g_TimeNow > g_TimeLastControllerLinkStatsSent + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS/2 )
      PH.total_length += get_controller_radio_link_stats_size();
   #endif

   u8 packet[MAX_PACKET_TOTAL_SIZE];
   // u8 ping id, u8 radio link id, u8 relay flags for destination vehicle
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet+sizeof(t_packet_header), &s_uPingToSendId, sizeof(u8));
   memcpy(packet+sizeof(t_packet_header)+sizeof(u8), &s_uPingToSendLocalRadioLinkId, sizeof(u8));
   if ( (g_pCurrentModel->sw_version>>16) > 79 ) // 7.7
   {
      memcpy(packet+sizeof(t_packet_header)+2*sizeof(u8), &uDestinationRelayCapabilities, sizeof(u8));
      memcpy(packet+sizeof(t_packet_header)+3*sizeof(u8), &uDestinationRelayMode, sizeof(u8));
   }
   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO
   if ( NULL != g_pCurrentModel && (g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK_PARAMS) )
   if ( g_TimeNow > g_TimeLastControllerLinkStatsSent + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS/2 )
   {
      if ( (g_pCurrentModel->sw_version>>16) > 79 ) // 7.7
         add_controller_radio_link_stats_to_buffer(packet+sizeof(t_packet_header)+4*sizeof(u8));
      else
         add_controller_radio_link_stats_to_buffer(packet+sizeof(t_packet_header)+2*sizeof(u8));
      g_TimeLastControllerLinkStatsSent = g_TimeNow;
   }
   #endif

   packets_queue_add_packet(&s_QueueRadioPackets, packet);

   if ( g_bDebugIsPacketsHistoryGraphOn && (!g_bDebugIsPacketsHistoryGraphPaused) )
      add_detailed_history_tx_packets(g_pDebug_SM_RouterPacketsStatsHistory, g_TimeNow % 1000, 0, 0, 1, 0, 0, 0);
   return true;
}

void _process_and_send_packets(int iCountPendingVideoRetransmissionsRequests)
{
   if ( g_bQuit || g_bSearching || NULL == g_pCurrentModel || ( (NULL != g_pCurrentModel) && (g_pCurrentModel->is_spectator)) )
   {
      // Empty queue
      packets_queue_init(&s_QueueRadioPackets);
      return;
   }

   _check_send_or_queue_ping();

   if ( 0 == packets_queue_has_packets(&s_QueueRadioPackets) )
      return;

   Preferences* pP = get_Preferences();
   
   int maxLengthAllowedInRadioPacket = pP->iDebugMaxPacketSize;
   if ( maxLengthAllowedInRadioPacket > MAX_PACKET_PAYLOAD )
      maxLengthAllowedInRadioPacket = MAX_PACKET_PAYLOAD;

   u8 composed_packet[MAX_PACKET_TOTAL_SIZE];
   int composed_packet_length = 0;
   int send_count = 1;
   int countComm = 0;
   int countRC = 0;

   int iMaxPacketsToSend = 4 - iCountPendingVideoRetransmissionsRequests;
   iMaxPacketsToSend += 2; // Allow at least 2 packets that are not retransmissions requests

   // Send retransmissions first, if any

   if( iCountPendingVideoRetransmissionsRequests > 0 )
   {
      for( int i=0; i<packets_queue_has_packets(&s_QueueRadioPackets); i++ )
      {
         int length = 0;
         u8* pData = packets_queue_peek_packet(&s_QueueRadioPackets, i, &length);
         if ( NULL == pData || length <= 0 )
            continue;

         t_packet_header* pPH = (t_packet_header*)pData;
         if ( pPH->packet_flags == PACKET_COMPONENT_VIDEO )
         if ( (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS) || (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2) )
         {
            pPH->vehicle_id_src = g_uControllerId;
            send_packet_to_radio_interfaces(pData, length);
            if ( g_bDebugIsPacketsHistoryGraphOn && (!g_bDebugIsPacketsHistoryGraphPaused) )
               add_detailed_history_tx_packets(g_pDebug_SM_RouterPacketsStatsHistory, g_TimeNow % 1000, 0, 0, 0, 0, 0, 0);

            iCountPendingVideoRetransmissionsRequests--;

            if ( 0 == iCountPendingVideoRetransmissionsRequests )
               break;
         }
      } 
   }

   #ifdef FEATURE_CONCATENATE_SMALL_RADIO_PACKETS
   u32 uDestinationVehicleIdLastPacket = 0;
   u32 uLastPacketType = 0;
   #endif
   
   while ( packets_queue_has_packets(&s_QueueRadioPackets) && iMaxPacketsToSend > 0 )
   {
      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;

      int iPacketLength = -1;
      u8* pPacketBuffer = packets_queue_pop_packet(&s_QueueRadioPackets, &iPacketLength);
      if ( NULL == pPacketBuffer || -1 == iPacketLength )
         break;

      _check_for_atheros_datarate_change_command_to_vehicle(pPacketBuffer);

      t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
      pPH->vehicle_id_src = g_uControllerId;
      
      if ( pPH->packet_flags == PACKET_COMPONENT_VIDEO )
      if ( (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS) || (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2) )
         continue;

      #ifdef FEATURE_CONCATENATE_SMALL_RADIO_PACKETS
      
      bool bSendNow = false;

      if ( (composed_packet_length + iPacketLength > maxLengthAllowedInRadioPacket) )
         bSendNow = true;

      if ( g_bUpdateInProgress )
         bSendNow = true;

      if ( (pPH->packet_type == PACKET_TYPE_RUBY_PING_CLOCK) || (uLastPacketType == PACKET_TYPE_RUBY_PING_CLOCK) )
         bSendNow = true;

      if ( uDestinationVehicleIdLastPacket != pPH->vehicle_id_dest )
      if ( 0 != uDestinationVehicleIdLastPacket )
         bSendNow = true;

      uDestinationVehicleIdLastPacket = pPH->vehicle_id_dest;

      if ( bSendNow && (composed_packet_length > 0) )
      {
         for( int i=0; i<send_count; i++ )
         {
            if ( i != 0 )
               hardware_sleep_ms(2);

            send_packet_to_radio_interfaces(composed_packet, composed_packet_length);
            if ( g_bDebugIsPacketsHistoryGraphOn && (!g_bDebugIsPacketsHistoryGraphPaused) )
               add_detailed_history_tx_packets(g_pDebug_SM_RouterPacketsStatsHistory, g_TimeNow % 1000, 0, countComm, 0, countRC, 0, 0);
         }
         iMaxPacketsToSend--;
         composed_packet_length = 0;
         send_count = 1;
         countComm = 0;
         countRC = 0;
         uDestinationVehicleIdLastPacket = 0;
      }

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
      if ( pPH->packet_type == COMMAND_ID_SET_RADIO_LINK_FREQUENCY )
         send_count = 10;

      if ( (pPH->packet_type == PACKET_TYPE_VEHICLE_RECORDING) )
         send_count = 5;
        
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
      if ( pPH->packet_type == COMMAND_ID_SET_CAMERA_PARAMETERS )
         video_link_adaptive_switch_to_med_level(pPH->vehicle_id_dest);

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
         countComm = 1;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RC )
         countRC = 1;

      memcpy(&composed_packet[composed_packet_length], pPacketBuffer, iPacketLength);
      composed_packet_length += iPacketLength;
      uLastPacketType = pPH->packet_type;
      #else
      
      send_count = 1;
      countComm = 0;
      countRC = 0;

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
      if ( pPH->packet_type == COMMAND_ID_SET_RADIO_LINK_FREQUENCY )
         send_count = 10;

      if ( (pPH->packet_type == PACKET_TYPE_VEHICLE_RECORDING) )
         send_count = 5;

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_COMMANDS )
         countComm = 1;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_RC )
         countRC = 1;

      for( int i=0; i<send_count; i++ )
      {
         if ( i != 0 )
            hardware_sleep_ms(2);

         send_packet_to_radio_interfaces(pPacketBuffer, iPacketLength);
         if ( g_bDebugIsPacketsHistoryGraphOn && (!g_bDebugIsPacketsHistoryGraphPaused) )
            add_detailed_history_tx_packets(g_pDebug_SM_RouterPacketsStatsHistory, g_TimeNow % 1000, 0, countComm, 0, countRC, 0, 0);
      }

      iMaxPacketsToSend--;
      
      composed_packet_length = 0;
      
      #endif
   }

   if ( composed_packet_length > 0 )
   {
      for( int i=0; i<send_count; i++ )
      {
         if ( i != 0 )
            hardware_sleep_ms(2);

         send_packet_to_radio_interfaces(composed_packet, composed_packet_length);

         if ( g_bDebugIsPacketsHistoryGraphOn && (!g_bDebugIsPacketsHistoryGraphPaused) )
            add_detailed_history_tx_packets(g_pDebug_SM_RouterPacketsStatsHistory, g_TimeNow % 1000, 0, countComm, 0, countRC, 0, 0);
      }
   }
}

void _preprocess_radio_out_packet(u8* pPacketBuffer)
{
   if ( NULL == pPacketBuffer )
      return;

   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
      
   if ( pPH->packet_type == PACKET_TYPE_SIK_CONFIG )
   {
      u8 uVehicleLinkId = *(pPacketBuffer + sizeof(t_packet_header));
      u8 uCommandId = *(pPacketBuffer + sizeof(t_packet_header) + sizeof(u8));
      log_line("Received message to send to vehicle to configure SiK vehicle radio link %d, command: %d", (int) uVehicleLinkId+1, (int)uCommandId);
   }
}


void _read_ipc_pipes(u32 uTimeNow)
{
   s_uTimeLastReadIPCMessages = uTimeNow;
   int maxToRead = 10;
   int maxPacketsToRead = maxToRead;

   maxPacketsToRead += DEFAULT_UPLOAD_PACKET_CONFIRMATION_FREQUENCY;
   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(g_fIPCFromCentral, 50, s_PipeBufferCommands, &s_PipeBufferCommandsPos, s_BufferCommands) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferCommands;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferCommands); 
      else
      {
         _preprocess_radio_out_packet(s_BufferCommands);
         packets_queue_add_packet(&s_QueueRadioPackets, s_BufferCommands); 
      }
   }
   if ( maxToRead - maxPacketsToRead > 6 )
      log_line("Read %d messages from central msgqueue.", maxToRead - maxPacketsToRead);

   maxPacketsToRead = maxToRead;
   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(g_fIPCFromTelemetry, 50, s_PipeBufferTelemetryUplink, &s_PipeBufferTelemetryUplinkPos, s_BufferMessageFromTelemetry) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferMessageFromTelemetry;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferMessageFromTelemetry); 
      else
      {
         _preprocess_radio_out_packet(s_BufferMessageFromTelemetry);
         packets_queue_add_packet(&s_QueueRadioPackets, s_BufferMessageFromTelemetry);
      }
   }
   if ( maxToRead - maxPacketsToRead > 6 )
      log_line("Read %d messages from telemetry msgqueue.", maxToRead - maxPacketsToRead);

   maxPacketsToRead = maxToRead;
   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(g_fIPCFromRC, 50, s_PipeBufferRCUplink, &s_PipeBufferRCUplinkPos, s_BufferRCUplink) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferRCUplink;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferRCUplink); 
      else
      {
         _preprocess_radio_out_packet(s_BufferRCUplink);
         packets_queue_add_packet(&s_QueueRadioPackets, s_BufferRCUplink);
      }
   }
   if ( maxToRead - maxPacketsToRead > 6 )
      log_line("Read %d messages from RC msgqueue.", maxToRead - maxPacketsToRead);
}

void init_shared_memory_objects()
{
   g_TimeNow = get_current_timestamp_ms();
   radio_stats_interfaces_rx_graph_reset(&g_SM_RadioStatsInterfacesRxGraph, 10);

   g_pSM_RadioStatsInterfacesRxGraph = shared_mem_controller_radio_stats_interfaces_rx_graphs_open_for_write();
   if ( NULL == g_pSM_RadioStatsInterfacesRxGraph )
      log_softerror_and_alarm("Failed to open controller radio interfaces rx graphs shared memory for write.");
   else
      log_line("Opened controller radio interfaces rx graphs shared memory for write: success.");

   if ( NULL != g_pSM_RadioStatsInterfacesRxGraph )
      memcpy((u8*)g_pSM_RadioStatsInterfacesRxGraph, (u8*)&g_SM_RadioStatsInterfacesRxGraph, sizeof(shared_mem_radio_stats_interfaces_rx_graph));

   g_pSM_RadioStats = shared_mem_radio_stats_open_for_write();
   if ( NULL == g_pSM_RadioStats )
      log_softerror_and_alarm("Failed to open radio stats shared memory for write.");
   else
      log_line("Opened radio stats shared memory for write: success.");


   g_pSM_HistoryRxStats = shared_mem_radio_stats_rx_hist_open_for_write();

   if ( NULL == g_pSM_HistoryRxStats )
      log_softerror_and_alarm("Failed to open history radio rx stats shared memory for write.");
   else
      shared_mem_radio_stats_rx_hist_reset(&g_SM_HistoryRxStats);
  
   if ( NULL == g_pCurrentModel )
      radio_stats_reset(&g_SM_RadioStats, g_pControllerSettings->nGraphRadioRefreshInterval);
   else
      radio_stats_reset(&g_SM_RadioStats, g_pControllerSettings->nGraphRadioRefreshInterval);

   if ( NULL != g_pSM_RadioStats )
      memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));


   if ( (NULL != g_pCurrentModel) && g_pCurrentModel->audio_params.has_audio_device && g_pCurrentModel->audio_params.enabled )
   {
      g_pSM_AudioDecodeStats = shared_mem_controller_audio_decode_stats_open_for_write();
      if ( NULL == g_pSM_AudioDecodeStats )
         log_softerror_and_alarm("Failed to open shared mem audio decode stats for writing: %s", SHARED_MEM_AUDIO_DECODE_STATS);
      else
         log_line("Opened shared mem audio decode stats stats for writing.");
   }
   else
   {
      log_line("No audio present on this vehicle. No audio decode stats shared mem to open.");
      g_pSM_AudioDecodeStats = NULL;
   }

   g_pSM_VideoDecodeStats = shared_mem_video_stream_stats_rx_processors_open(false);
   if ( NULL == g_pSM_VideoDecodeStats )
   {
      log_softerror_and_alarm("Failed to open video decoder stats shared memory for read/write.");
   }
   else
   {
      log_line("Opened video decoder stats shared memory for read/write: success.");
      memset(g_pSM_VideoDecodeStats, 0, sizeof(shared_mem_video_stream_stats_rx_processors));
   }
   memset((u8*)&g_SM_VideoDecodeStats, 0, sizeof(shared_mem_video_stream_stats_rx_processors));


   g_pSM_VideoDecodeStatsHistory = shared_mem_video_stream_stats_history_rx_processors_open(false);
   if ( NULL == g_pSM_VideoDecodeStatsHistory )
   {
      log_softerror_and_alarm("Failed to open video decoder stats history shared memory for read/write.");
   }
   else
   {
      log_line("Opened video decoder stats history shared memory for read/write: success.");
      memset(g_pSM_VideoDecodeStatsHistory, 0, sizeof(shared_mem_video_stream_stats_history_rx_processors));
   }
   memset((u8*)&g_SM_VideoDecodeStatsHistory, 0, sizeof(shared_mem_video_stream_stats_history_rx_processors));


   g_pSM_ControllerRetransmissionsStats = shared_mem_controller_video_retransmissions_stats_open_for_write();
   if ( NULL == g_pSM_ControllerRetransmissionsStats )
   {
      log_softerror_and_alarm("Failed to open video retransmissions stats shared memory for read/write.");
   }
   else
   {
      log_line("Opened video retransmissions stats shared memory for read/write: success.");
      memset(g_pSM_ControllerRetransmissionsStats, 0, sizeof(shared_mem_controller_retransmissions_stats_rx_processors));
   }
   memset((u8*)&g_SM_ControllerRetransmissionsStats, 0, sizeof(shared_mem_controller_retransmissions_stats_rx_processors));

   g_pSM_VideoInfoStats = shared_mem_video_info_stats_open_for_write();
   if ( NULL == g_pSM_VideoInfoStats )
      log_softerror_and_alarm("Failed to open shared mem video info stats for writing: %s", SHARED_MEM_VIDEO_STREAM_INFO_STATS);
   else
      log_line("Opened shared mem video info stats stats for writing.");

   g_pSM_VideoInfoStatsRadioIn = shared_mem_video_info_stats_radio_in_open_for_write();
   if ( NULL == g_pSM_VideoInfoStatsRadioIn )
      log_softerror_and_alarm("Failed to open shared mem video info radio in stats for writing: %s", SHARED_MEM_VIDEO_STREAM_INFO_STATS_RADIO_IN);
   else
      log_line("Opened shared mem video info radio in stats stats for writing.");

   g_pSM_RouterVehiclesRuntimeInfo = shared_mem_router_vehicles_runtime_info_open_for_write();
   if ( NULL == g_pSM_RouterVehiclesRuntimeInfo )
      log_softerror_and_alarm("Failed to open shared mem controller vehicles runtime info for writing: %s", SHARED_MEM_CONTROLLER_ADAPTIVE_VIDEO_INFO);
   else
      log_line("Opened shared mem controller adaptive video info for writing.");

   g_TimeNow = get_current_timestamp_ms();

   memset((u8*)&g_SM_RouterVehiclesRuntimeInfo, 0, sizeof(shared_mem_router_vehicles_runtime_info));
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
      resetVehicleRuntimeInfo(i);

   if ( NULL != g_pCurrentModel )
      g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[0] = g_pCurrentModel->vehicle_id;

   if ( NULL != g_pSM_RouterVehiclesRuntimeInfo )
      memcpy( (u8*)g_pSM_RouterVehiclesRuntimeInfo, (u8*)&g_SM_RouterVehiclesRuntimeInfo, sizeof(shared_mem_router_vehicles_runtime_info));

   g_pSM_VideoLinkStats = shared_mem_video_link_stats_open_for_write();
   if ( NULL == g_pSM_VideoLinkStats )
      log_softerror_and_alarm("Failed to open shared mem video link stats for writing: %s", SHARED_MEM_VIDEO_LINK_STATS);
   else
      log_line("Opened shared mem video link stats stats for writing.");

   g_pSM_VideoLinkGraphs = shared_mem_video_link_graphs_open_for_write();
   if ( NULL == g_pSM_VideoLinkGraphs )
      log_softerror_and_alarm("Failed to open shared mem video link graphs for writing: %s", SHARED_MEM_VIDEO_LINK_GRAPHS);
   else
      log_line("Opened shared mem video link graphs stats for writing.");

   g_pProcessStats = shared_mem_process_stats_open_write(SHARED_MEM_WATCHDOG_ROUTER_RX);
   if ( NULL == g_pProcessStats )
      log_softerror_and_alarm("Failed to open shared mem for video rx process watchdog stats for writing: %s", SHARED_MEM_WATCHDOG_ROUTER_RX);
   else
      log_line("Opened shared mem for video rx process watchdog stats for writing.");

   if ( NULL != g_pProcessStats )
   {
      g_pProcessStats->alarmFlags = 0;
      g_pProcessStats->alarmTime = 0;
      g_pProcessStats->alarmParam[0] = g_pProcessStats->alarmParam[1] = g_pProcessStats->alarmParam[2] = g_pProcessStats->alarmParam[3] = 0;
   }
}

int open_pipes()
{
   g_fIPCFromRC = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_RC_TO_ROUTER);
   if ( g_fIPCFromRC < 0 )
      return -1;

   g_fIPCToRC = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_RC);
   if ( g_fIPCToRC < 0 )
      return -1;
   
   g_fIPCFromCentral = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_CENTRAL_TO_ROUTER);
   if ( g_fIPCFromCentral < 0 )
      return -1;

   g_fIPCToCentral = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_CENTRAL);
   if ( g_fIPCToCentral < 0 )
      return -1;
   
   g_fIPCToTelemetry = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_TELEMETRY);
   if ( g_fIPCToTelemetry < 0 )
      return -1;
   
   g_fIPCFromTelemetry = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_TELEMETRY_TO_ROUTER);
   if ( g_fIPCFromTelemetry < 0 )
      return -1;
   
   if ( NULL == g_pCurrentModel || (!g_pCurrentModel->audio_params.enabled) )
   {
      log_line("Audio is disabled on current vehicle.");
      return 0;
   }
   if ( NULL == g_pCurrentModel || (! g_pCurrentModel->audio_params.has_audio_device) )
   {
      log_line("No audio capture device on current vehicle.");
      return 0;
   }

   return 0;
}

void _check_rx_loop_consistency()
{
   int iAnyBrokeInterface = radio_rx_any_interface_broken();
   if ( iAnyBrokeInterface > 0 )
   {
      if ( hardware_radio_index_is_sik_radio(iAnyBrokeInterface-1) )
         flag_reinit_sik_interface(iAnyBrokeInterface-1);
      else
      {
          send_alarm_to_central(ALARM_ID_RADIO_INTERFACE_DOWN, iAnyBrokeInterface-1, 0); 
         _reinit_radio_interfaces();
         return;
      }
   }

   int iAnyRxTimeouts = radio_rx_any_rx_timeouts();
   if ( iAnyRxTimeouts > 0 )
   {
      static u32 s_uTimeLastAlarmRxTimeout = 0;
      if ( g_TimeNow > s_uTimeLastAlarmRxTimeout + 3000 )
      {
         s_uTimeLastAlarmRxTimeout = g_TimeNow;

         int iCount = radio_rx_get_timeout_count_and_reset(iAnyRxTimeouts-1);
         send_alarm_to_central(ALARM_ID_CONTROLLER_RX_TIMEOUT,(u32)(iAnyRxTimeouts-1), (u32)iCount);
      }
   }

   int iAnyRxErrors = radio_rx_any_interface_bad_packets();
   if ( (iAnyRxErrors > 0) && (!g_bSearching) )
   {
      int iError = radio_rx_get_interface_bad_packets_error_and_reset(iAnyRxErrors-1);
      send_alarm_to_central(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET, (u32)iError, 0);
   }

   u32 uMaxRxLoopTime = radio_rx_get_and_reset_max_loop_time();
   if ( (int)uMaxRxLoopTime > get_ControllerSettings()->iDevRxLoopTimeout )
   {
      static u32 s_uTimeLastAlarmRxLoopOverload = 0;
      if ( g_TimeNow > s_uTimeLastAlarmRxLoopOverload + 10000 )
      {
         s_uTimeLastAlarmRxLoopOverload = g_TimeNow;
         u32 uRead = radio_rx_get_and_reset_max_loop_time_read();
         u32 uQueue = radio_rx_get_and_reset_max_loop_time_queue();
         if ( uRead > 0xFFFF ) uRead = 0xFFFF;
         if ( uQueue > 0xFFFF ) uQueue = 0xFFFF;
         
         send_alarm_to_central(ALARM_ID_CPU_RX_LOOP_OVERLOAD, uMaxRxLoopTime, uRead | (uQueue<<16));
      }
   }
}
      
void _consume_ipc_messages()
{
   int iMaxToConsume = 20;
   u32 uTimeStart = get_current_timestamp_ms();

   while ( packets_queue_has_packets(&s_QueueControlPackets) && (iMaxToConsume > 0) )
   {
      iMaxToConsume--;
      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;

      int length = -1;
      u8* pBuffer = packets_queue_pop_packet(&s_QueueControlPackets, &length);
      if ( NULL == pBuffer || -1 == length )
         break;

      t_packet_header* pPH = (t_packet_header*)pBuffer;
      process_local_control_packet(pPH);
   }

   u32 uTime = get_current_timestamp_ms();
   if ( uTime > uTimeStart+500 )
      log_softerror_and_alarm("Consuming %d IPC messages took too long: %d ms", 20 - iMaxToConsume, uTime - uTimeStart);
}

void _consume_radio_rx_packets()
{
   int iReceivedAnyPackets = radio_rx_has_packets_to_consume();

   if ( iReceivedAnyPackets <= 0 )
      return;

   if ( iReceivedAnyPackets > 10 )
      iReceivedAnyPackets = 10;
   
   u32 uTimeStart = get_current_timestamp_ms();

   for( int i=0; i<iReceivedAnyPackets; i++ )
   {
      if ( g_bQuit )
         break;
      int iPacketLength = 0;
      int iIsShortPacket = 0;
      int iRadioInterfaceIndex = 0;
      u8* pPacket = radio_rx_get_next_received_packet(&iPacketLength, &iIsShortPacket, &iRadioInterfaceIndex);
      if ( (NULL == pPacket) || (iPacketLength <= 0) )
         continue;

      shared_mem_radio_stats_rx_hist_update(&g_SM_HistoryRxStats, iRadioInterfaceIndex, pPacket, g_TimeNow);
      process_received_single_radio_packet(iRadioInterfaceIndex, pPacket, iPacketLength);
      u32 uTime = get_current_timestamp_ms();    
      if ( uTime > uTimeStart + 500 )
      {
         log_softerror_and_alarm("Consuming radio rx packets takes too long (%u ms), read ipc messages.", uTime - uTimeStart);
         uTimeStart = uTime;
         _read_ipc_pipes(uTime);
      }
      if ( uTime > s_uTimeLastReadIPCMessages + 500 )
      {
         log_softerror_and_alarm("Too much time since last ipc messages read (%u ms) while consuming radio messages, read ipc messages.", uTime - s_uTimeLastReadIPCMessages);
         uTimeStart = uTime;
         _read_ipc_pipes(uTime);
      }
   }
}

void _check_send_pairing_requests()
{
   if ( ! g_bFirstModelPairingDone )
      return;
   if ( g_bSearching )
      return;
   if ( (g_SM_RadioStats.radio_links[0].totalRxPackets == 0) && (g_SM_RadioStats.radio_links[1].totalRxPackets == 0) )
      return;

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i] == 0 )
         continue;
      if ( g_SM_RouterVehiclesRuntimeInfo.iPairingDone[i] )
         continue;

      Model* pModel = findModelWithId(g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i]);
      if ( NULL == pModel )
         continue;
      if ( pModel->is_spectator )
         continue;

      bool bExpectedVehicle = false;
      if ( pModel->vehicle_id == g_pCurrentModel->vehicle_id )
         bExpectedVehicle = true;

      if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
      if ( g_pCurrentModel->relay_params.uRelayedVehicleId == pModel->vehicle_id )
         bExpectedVehicle = true;
        
      if ( ! bExpectedVehicle )
         continue;

      if ( g_TimeNow < g_SM_RouterVehiclesRuntimeInfo.uPairingRequestTime[i] + g_SM_RouterVehiclesRuntimeInfo.uPairingRequestInterval[i] )
         continue;

      g_SM_RouterVehiclesRuntimeInfo.uPairingRequestId[i]++;
      g_SM_RouterVehiclesRuntimeInfo.uPairingRequestTime[i] = g_TimeNow;
      if ( g_SM_RouterVehiclesRuntimeInfo.uPairingRequestInterval[i] < 400 )
         g_SM_RouterVehiclesRuntimeInfo.uPairingRequestInterval[i]+=10;

      t_packet_header PH;
      radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_RUBY_PAIRING_REQUEST, STREAM_ID_DATA);
      PH.vehicle_id_src = g_uControllerId;
      PH.vehicle_id_dest = pModel->vehicle_id;
      PH.total_length = sizeof(t_packet_header) + sizeof(u32);
      u8 packet[MAX_PACKET_TOTAL_SIZE];
      memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
      memcpy(packet + sizeof(t_packet_header), &g_SM_RouterVehiclesRuntimeInfo.uPairingRequestId[i], sizeof(u32));
      if ( 0 == send_packet_to_radio_interfaces(packet, PH.total_length) )
      {
         if ( g_SM_RouterVehiclesRuntimeInfo.uPairingRequestId[i] < 2 )
            send_alarm_to_central(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_SENT_PAIRING_REQUEST, PH.vehicle_id_dest);
         if ( (g_SM_RouterVehiclesRuntimeInfo.uPairingRequestId[i] % 5) == 0 )
            log_line("Sent pairing request to vehicle (retry count: %u). CID: %u, VID: %u", g_SM_RouterVehiclesRuntimeInfo.uPairingRequestId[i], PH.vehicle_id_src, PH.vehicle_id_dest);  
      }
   }
}

void _router_periodic_loop()
{
   _check_reinit_sik_interfaces();

   if ( radio_stats_periodic_update(&g_SM_RadioStats, &g_SM_RadioStatsInterfacesRxGraph, g_TimeNow) )
   {
      static u32 s_uTimeLastRadioStatsSharedMemSync = 0;

      if ( g_TimeNow >= s_uTimeLastRadioStatsSharedMemSync + 100 )
      {
         s_uTimeLastRadioStatsSharedMemSync = g_TimeNow;
         if ( NULL != g_pSM_RadioStats )
            memcpy((u8*)g_pSM_RadioStats, (u8*)&g_SM_RadioStats, sizeof(shared_mem_radio_stats));
         if ( NULL != g_pSM_RadioStatsInterfacesRxGraph )
            memcpy((u8*)g_pSM_RadioStatsInterfacesRxGraph, (u8*)&g_SM_RadioStatsInterfacesRxGraph, sizeof(shared_mem_radio_stats_interfaces_rx_graph));
      }

      bool bHasRecentRxData = false;
      for( int i=0; i<g_SM_RadioStats.countLocalRadioLinks; i++ )
      {
         if ( g_TimeNow > 2000 && g_SM_RadioStats.radio_links[i].timeLastRxPacket >= g_TimeNow-1000 )
         {
            bHasRecentRxData = true;
            break;
         }
      }

      if ( bHasRecentRxData )
      {
         if ( g_bIsControllerLinkToVehicleLost )
         {
             log_line("Link to vehicle recovered (received vehicle radio packets)");
             g_bIsControllerLinkToVehicleLost = false;
         }
      }
      else
      {
          if ( ! g_bIsControllerLinkToVehicleLost )
          {
             log_line("Link to vehicle lost (stopped receiving radio packets from vehicle)");
             g_bIsControllerLinkToVehicleLost = true;
          }
      }
   }

   radio_controller_links_stats_periodic_update(&g_PD_ControllerLinkStats, g_TimeNow);
   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastActiveTime = g_TimeNow;

   s_debugFramesCount++;
   if ( g_TimeNow > s_debugLastFPSTime + 500 )
   {
      s_debugLastFPSTime = g_TimeNow;
      s_debugFramesCount = 0;

      if ( g_iGetSiKConfigAsyncResult != 0 )
      {
         char szBuff[256];
         strcpy(szBuff, "SiK config: done.");

         if ( 1 == g_iGetSiKConfigAsyncResult )
         {
            hardware_radio_sik_save_configuration();
            hardware_save_radio_info();
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(g_iGetSiKConfigAsyncRadioInterfaceIndex);
         
            char szTmp[256];
            szTmp[0] = 0;
            for( int i=0; i<16; i++ )
            {
               char szB[32];
               sprintf(szB, "%u\n", pRadioHWInfo->uHardwareParamsList[i]);
               strcat(szTmp, szB);
            }
            strcpy(szBuff, szTmp);
         }
         else
            strcpy(szBuff, "Failed to get SiK configuration from device.");

         t_packet_header PH;
         radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_SIK_CONFIG, STREAM_ID_DATA);
         PH.vehicle_id_src = g_uControllerId;
         PH.vehicle_id_dest = g_uControllerId;
         PH.total_length = sizeof(t_packet_header) + strlen(szBuff)+3*sizeof(u8);

         u8 uCommandId = 0;

         u8 packet[MAX_PACKET_TOTAL_SIZE];
         memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
         memcpy(packet+sizeof(t_packet_header), &g_uGetSiKConfigAsyncVehicleLinkIndex, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + sizeof(u8), &uCommandId, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + 2*sizeof(u8), szBuff, strlen(szBuff)+1);
         if ( ! ruby_ipc_channel_send_message(g_fIPCToCentral, packet, PH.total_length) )
            log_softerror_and_alarm("No pipe to central to send message to.");

         log_line("Send back to central Sik current config for vehicle radio link %d", (int)g_uGetSiKConfigAsyncVehicleLinkIndex+1);

         g_iGetSiKConfigAsyncResult = 0;
      }
   }

   static u32 s_uTimeLastVideoStatsUpdate = 0;
   if ( g_TimeNow >= s_uTimeLastVideoStatsUpdate + 50 )
   {
      s_uTimeLastVideoStatsUpdate = g_TimeNow;
      for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
      {
         if ( g_pVideoProcessorRxList[i] == NULL )
            break;
         memcpy((u8*)&(g_SM_VideoDecodeStats.video_streams[i]), g_pVideoProcessorRxList[i]->getVideoDecodeStats(), sizeof(shared_mem_video_stream_stats));
         memcpy((u8*)&(g_SM_VideoDecodeStatsHistory.video_streams[i]), g_pVideoProcessorRxList[i]->getVideoDecodeStatsHistory(), sizeof(shared_mem_video_stream_stats_history));
         memcpy((u8*)&(g_SM_ControllerRetransmissionsStats.video_streams[i]), g_pVideoProcessorRxList[i]->getControllerRetransmissionsStats(), sizeof(shared_mem_controller_retransmissions_stats));
      }

      memcpy(g_pSM_VideoDecodeStats, &g_SM_VideoDecodeStats, sizeof(shared_mem_video_stream_stats_rx_processors));
      memcpy(g_pSM_VideoDecodeStatsHistory, &g_SM_VideoDecodeStatsHistory, sizeof(shared_mem_video_stream_stats_history_rx_processors));
      memcpy(g_pSM_ControllerRetransmissionsStats, &g_SM_ControllerRetransmissionsStats, sizeof(shared_mem_controller_retransmissions_stats_rx_processors));
   }

   _check_send_pairing_requests();
}

void _synchronize_shared_mems()
{
   if ( g_bQuit )
      return;

   static u32 s_TimeLastVideoStatsUpdate = 0;

   if ( g_TimeNow >= s_TimeLastVideoStatsUpdate+50 )
   {
      s_TimeLastVideoStatsUpdate = g_TimeNow;
      memcpy((u8*)g_pSM_VideoDecodeStats, (u8*)(&g_SM_VideoDecodeStats), sizeof(shared_mem_video_stream_stats_rx_processors));
   }

   if ( NULL != g_pCurrentModel )
   if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_STATS_VIDEO_INFO)
   if ( g_TimeNow >= g_VideoInfoStats.uTimeLastUpdate + 200 )
   {
      update_shared_mem_video_info_stats( &g_VideoInfoStats, g_TimeNow);
      update_shared_mem_video_info_stats( &g_VideoInfoStatsRadioIn, g_TimeNow);

      if ( NULL != g_pSM_VideoInfoStats )
         memcpy((u8*)g_pSM_VideoInfoStats, (u8*)&g_VideoInfoStats, sizeof(shared_mem_video_info_stats));
      if ( NULL != g_pSM_VideoInfoStatsRadioIn )
         memcpy((u8*)g_pSM_VideoInfoStatsRadioIn, (u8*)&g_VideoInfoStatsRadioIn, sizeof(shared_mem_video_info_stats));
   }

   static u32 s_uTimeLastRxHistorySync = 0;

   if ( g_TimeNow >= s_uTimeLastRxHistorySync + 100 )
   {
      s_uTimeLastRxHistorySync = g_TimeNow;
      memcpy((u8*)g_pSM_HistoryRxStats, (u8*)&g_SM_HistoryRxStats, sizeof(shared_mem_radio_stats_rx_hist));
   }
}


void handle_sigint(int sig) 
{ 
   log_line("--------------------------");
   log_line("Caught signal to stop: %d", sig);
   log_line("--------------------------");
   radio_rx_mark_quit();
   g_bQuit = true;
} 
  
int main (int argc, char *argv[])
{
   signal(SIGPIPE, SIG_IGN);
   signal(SIGINT, handle_sigint);
   signal(SIGTERM, handle_sigint);
   signal(SIGQUIT, handle_sigint);
   
   if ( strcmp(argv[argc-1], "-ver") == 0 )
   {
      printf("%d.%d (b%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR/10, SYSTEM_SW_BUILD_NUMBER);
      return 0;
   }
      
   log_init("Router");
   
   g_bSearching = false;
   g_uSearchFrequency = 0;
   if ( argc >= 3 )
   if ( strcmp(argv[1], "-search") == 0 )
   {
      g_bSearching = true;
      g_uSearchFrequency = atoi(argv[2]);
   }
   
   if ( argc >= 8 )
   if ( strcmp(argv[1], "-search") == 0 )
   if ( strcmp(argv[3], "-sik") == 0 )
   {
      g_bSearching = true;
      g_uSearchFrequency = atoi(argv[2]);
      s_iSearchSikAirRate = atoi(argv[4]);
      s_iSearchSikECC = atoi(argv[5]);
      s_iSearchSikLBT = atoi(argv[6]);
      s_iSearchSikMCSTR = atoi(argv[7]);

      log_line ("Searching in Sik Mode: air rate: %d, ECC/LBT/MCSTR: %d/%d/%d",
         s_iSearchSikAirRate, s_iSearchSikECC, s_iSearchSikLBT, s_iSearchSikMCSTR);
   }

   g_bDebug = false;

   if ( argc >= 1 )
   if ( strcmp(argv[argc-1], "-debug") == 0 )
      g_bDebug = true;

   if ( g_bDebug )
      log_enable_stdout();
 
   if ( g_bSearching )
      log_line("Launched router in search mode, search frequency: %s", str_format_frequency(g_uSearchFrequency));

   radio_init_link_structures();
   radio_enable_crc_gen(1);
   hardware_enumerate_radio_interfaces(); 

   init_radio_rx_structures();
   reset_sik_state_info(&g_SiKRadiosState);

   load_Preferences();   
   load_ControllerSettings();
   load_ControllerInterfacesSettings();
   hardware_i2c_load_device_settings();

   controllerRadioInterfacesLogInfo();

   g_pControllerSettings = get_ControllerSettings();
   g_pControllerInterfaces = get_ControllerInterfacesSettings();
   Preferences* pP = get_Preferences();   
   if ( pP->nLogLevel != 0 )
      log_only_errors();
 
   if ( NULL != g_pControllerSettings )
      radio_rx_set_timeout_interval(g_pControllerSettings->iDevRxLoopTimeout);
     
   g_uControllerId = controller_utils_getControllerId();
   log_line("Controller UID: %u", g_uControllerId);

   g_pCurrentModel = NULL;
   if ( ! g_bSearching )
   {
      loadAllModels();

      g_pCurrentModel = getCurrentModel();
      if ( g_pCurrentModel->enc_flags != MODEL_ENC_FLAGS_NONE )
         lpp(NULL, 0);
      g_pCurrentModel->logVehicleRadioInfo();
   }

   if ( NULL != g_pControllerSettings )
      hw_set_priority_current_proc(g_pControllerSettings->iNiceRouter);
 
   rx_video_output_init();
   ProcessorRxVideo::oneTimeInit();

   if ( ! g_bSearching )
      init_processing_audio();

   init_shared_memory_objects();

   log_line("Init shared mem objects: done");

   if ( -1 == open_pipes() )
   {
      radio_link_cleanup();
      log_error_and_alarm("Failed to open required pipes. Exit.");
      return -1;
   }
   
   radio_controller_links_stats_reset(&g_PD_ControllerLinkStats);
   
   u32 delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   if ( NULL != pP )
      delayMs = (u32) pP->iDebugWiFiChangeDelay;
   if ( delayMs<1 || delayMs > 200 )
      delayMs = DEFAULT_DELAY_WIFI_CHANGE;

   hardware_sleep_ms(delayMs);

   g_SM_RadioStats.countLocalRadioInterfaces = hardware_get_radio_interfaces_count();
  
   if ( g_bSearching )
   {
      if ( s_iSearchSikAirRate > 0 )
         links_set_cards_frequencies_for_search(g_uSearchFrequency, true, s_iSearchSikAirRate, s_iSearchSikECC, s_iSearchSikLBT, s_iSearchSikMCSTR );
      else
         links_set_cards_frequencies_for_search(g_uSearchFrequency, false, -1,-1,-1,-1 );
      hardware_save_radio_info();
      _open_rxtx_radio_interfaces_for_search(g_uSearchFrequency);
   }
   else
   {
      _compute_radio_interfaces_assignment();
      links_set_cards_frequencies_and_params(-1);
      _open_rxtx_radio_interfaces();
   }

   packets_queue_init(&s_QueueRadioPackets);
   packets_queue_init(&s_QueueControlPackets);

   log_line("IPC Queues Init Complete.");
   
   g_TimeStart = get_current_timestamp_ms();

   if ( NULL != g_pCurrentModel )
   {
      char szBuffF[128];
      for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
      {
         str_get_radio_frame_flags_description(g_pCurrentModel->radioInterfacesParams.interface_current_radio_flags[i], szBuffF);
         log_line("Radio frame flags for radio interface %d: %u, %s", i+1, g_pCurrentModel->radioInterfacesParams.interface_current_radio_flags[i], szBuffF);
      }
   }

   g_bFirstModelPairingDone = false;
   if ( access( FILE_FIRST_PAIRING_DONE, R_OK ) != -1 )
      g_bFirstModelPairingDone = true;

   if ( g_bSearching )
      log_line("Router started in search mode");
   else if ( g_bFirstModelPairingDone )
      log_line("Router started with a valid user controll/spectator model (first model pairing was already completed)");
   else
      log_line("Router started with the default model (first model pairing was never completed)");

   processor_rx_video_forware_prepare_video_stream_write();
   if ( ! g_bSearching )
   {
      video_link_adaptive_init(g_pCurrentModel->vehicle_id);
      video_link_keyframe_init(g_pCurrentModel->vehicle_id);
   }

   load_CorePlugins(0);

   radio_duplicate_detection_init();
   radio_rx_start_rx_thread(&g_SM_RadioStats, &g_SM_RadioStatsInterfacesRxGraph, (int)g_bSearching);

   log_line("Broadcasting that router is ready.");
   broadcast_router_ready();

   if ( s_iFailedInitRadioInterface >= 0 )
      _broadcast_radio_interface_init_failed(s_iFailedInitRadioInterface);

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      if ( pRadioHWInfo->uExtraFlags & RADIO_HW_EXTRA_FLAG_FIRMWARE_OLD )
         send_alarm_to_central(ALARM_ID_FIRMWARE_OLD, i, 0);
   }

   log_line("");
   log_line("");
   log_line("----------------------------------------------");
   log_line("         Started all ok. Running now.");
   log_line("----------------------------------------------");
   log_line("");
   log_line("");

   u32 uTimeLastMemoryCheck = 0;
   u32 uCountMemoryChecks = 0;

   u32 uMaxLoopTime = DEFAULT_MAX_LOOP_TIME_MILISECONDS;

   while (!g_bQuit) 
   {
      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->uLoopCounter++;
         g_pProcessStats->lastActiveTime = g_TimeNow;
      }

      hardware_sleep_ms(2);
      g_TimeNow = get_current_timestamp_ms();
      g_TimeNowMicros = get_current_timestamp_micros();
      u32 tTime0 = g_TimeNow;

      if ( (0 == uCountMemoryChecks && (g_TimeNow > g_TimeStart+6000)) || (g_TimeNow > uTimeLastMemoryCheck + 60000) )
      {
         uCountMemoryChecks++;
         uTimeLastMemoryCheck = g_TimeNow;
         char szOutput[2048];
         if ( 1 == hw_execute_bash_command_raw("df -m /home/pi/ruby | grep root", szOutput) )
         {
            char szTemp[1024];
            long lb, lu, lMemoryFreeMb;
            sscanf(szOutput, "%s %ld %ld %ld", szTemp, &lb, &lu, &lMemoryFreeMb);
            if ( lMemoryFreeMb < 200 )
               send_alarm_to_central(ALARM_ID_CONTROLLER_LOW_STORAGE_SPACE, (u32)lMemoryFreeMb, 0);
         }
      }

      _router_periodic_loop();
      _synchronize_shared_mems();
      
      _check_rx_loop_consistency();

      u32 tTime1 = get_current_timestamp_ms();

      _read_ipc_pipes(tTime1);
      _consume_ipc_messages();

      u32 tTime2 = get_current_timestamp_ms();

      _consume_radio_rx_packets();

      u32 tTime3 = get_current_timestamp_ms();
      
      int nEndOfVideoBlock = 0;
      /*
      for( int i=0; i<6; i++ )
      {
         if ( receivedAny > 0 )
            nEndOfVideoBlock |= process_received_radio_packets();
         else
            break;
         receivedAny = try_receive_radio_packets(200);
      }
      */
      u32 tTime4 = get_current_timestamp_ms();
      
      if ( g_bSearching )
      {
         u32 tNow = get_current_timestamp_ms();
         if ( tNow > g_TimeNow + uMaxLoopTime )
            log_softerror_and_alarm("Router loop took too long to complete (%d milisec)!!!", tNow - g_TimeNow);
         else
            s_iCountCPULoopOverflows = 0;
         continue;
      }

      for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
      {
         if ( g_pVideoProcessorRxList[i] == NULL )
            break;
         g_pVideoProcessorRxList[i]->periodicLoop(g_TimeNow);
      }
      
      if ( (!g_bSearching) && (NULL != g_pCurrentModel) && g_pCurrentModel->hasCamera() )
      {
         processor_rx_video_forward_loop();
         video_link_adaptive_periodic_loop();
      }

      u32 tTime5 = get_current_timestamp_ms();
      
      int iContainsVideoRequestsCount = 0;
      int iContainsVideoAdjustmentsRequests = 0;
      for( int i=0; i<packets_queue_has_packets(&s_QueueRadioPackets); i++ )
      {
         int length = 0;
         u8* pData = packets_queue_peek_packet(&s_QueueRadioPackets, i, &length);
         if ( NULL == pData || length <= 0 )
            continue;
         t_packet_header* pPH = (t_packet_header*)pData;

         if ( pPH->packet_flags == PACKET_COMPONENT_VIDEO )
         if ( (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS) || (pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2) )
            iContainsVideoRequestsCount++;

         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO )
         if ( pPH->packet_type == PACKET_TYPE_VIDEO_SWITCH_TO_ADAPTIVE_VIDEO_LEVEL )
         {
            u32 uLevel = 0;
            memcpy((u8*)&uLevel, pData + sizeof(t_packet_header), sizeof(u32));

            int iIndex = getVehicleRuntimeIndex(pPH->vehicle_id_dest);
            if ( -1 != iIndex )
            {
               g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndex].iCurrentTargetLevelShift = (int) uLevel;
               iContainsVideoAdjustmentsRequests++;
            }
         }   
      }

      bool bSendNow = false;

      if ( !g_pCurrentModel->hasCamera())
         bSendNow = true;
      if ( g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_NONE )
         bSendNow = true;
      if ( g_bUpdateInProgress || nEndOfVideoBlock )
         bSendNow = true;
      if ( s_QueueRadioPackets.timeFirstPacket < g_TimeNow-100 )
         bSendNow = true;
      if ( iContainsVideoRequestsCount > 0 || iContainsVideoAdjustmentsRequests > 0 )
         bSendNow = true;
 
      if ( bSendNow )
      {
         _process_and_send_packets(iContainsVideoRequestsCount);
      }

      u32 tTime6 = get_current_timestamp_ms();
      if ( (g_TimeNow > g_TimeStart + 10000) && (tTime6 > tTime0 + uMaxLoopTime) )
      {
         log_softerror_and_alarm("Router loop took too long to complete (%d milisec: %u + %u + %u + %u + %u + %u), repeat count: %u!!!", tTime6 - tTime0, tTime1-tTime0, tTime2-tTime1, tTime3-tTime2, tTime4-tTime3, tTime5-tTime4, tTime6-tTime5, s_iCountCPULoopOverflows+1);

         s_iCountCPULoopOverflows++;
         if ( s_iCountCPULoopOverflows > 5 )
         if ( g_TimeNow > g_TimeLastSetRadioFlagsCommandSent + 5000 )
            send_alarm_to_central(ALARM_ID_CONTROLLER_CPU_LOOP_OVERLOAD,(tTime6-tTime0), 0);

         if ( tTime6 >= tTime0 + 300 )
         if ( g_TimeNow > g_TimeLastSetRadioFlagsCommandSent + 5000 )
            send_alarm_to_central(ALARM_ID_CONTROLLER_CPU_LOOP_OVERLOAD,(tTime6-tTime0)<<16, 0);
      }
      else
      {
         s_iCountCPULoopOverflows = 0;
      }

      if ( NULL != g_pProcessStats )
      {
         if ( g_pProcessStats->uMaxLoopTimeMs < tTime6 - tTime0 )
            g_pProcessStats->uMaxLoopTimeMs = tTime6 - tTime0;
         g_pProcessStats->uTotalLoopTime += tTime6 - tTime0;
         if ( 0 != g_pProcessStats->uLoopCounter )
            g_pProcessStats->uAverageLoopTimeMs = g_pProcessStats->uTotalLoopTime / g_pProcessStats->uLoopCounter;
      }
   }

   log_line("Stopping...");

   radio_rx_stop_rx_thread();
   radio_link_cleanup();
   
   unload_CorePlugins();

   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( NULL != g_pVideoProcessorRxList[i] )
      {
         g_pVideoProcessorRxList[i]->uninit();
         delete g_pVideoProcessorRxList[i];
         g_pVideoProcessorRxList[i] = NULL;
      }
   }

   rx_video_output_uninit();

   shared_mem_radio_stats_rx_hist_close(g_pSM_HistoryRxStats);
   shared_mem_controller_radio_stats_interfaces_rx_graphs_close(g_pSM_RadioStatsInterfacesRxGraph);
   shared_mem_controller_audio_decode_stats_close(g_pSM_AudioDecodeStats);
   shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_ROUTER_RX, g_pProcessStats);
   shared_mem_video_stream_stats_rx_processors_close(g_pSM_VideoDecodeStats);
   shared_mem_video_stream_stats_history_rx_processors_close(g_pSM_VideoDecodeStatsHistory);
   shared_mem_controller_video_retransmissions_stats_close(g_pSM_ControllerRetransmissionsStats);
   shared_mem_video_link_stats_close(g_pSM_VideoLinkStats);
   shared_mem_video_link_graphs_close(g_pSM_VideoLinkGraphs);
   shared_mem_radio_stats_close(g_pSM_RadioStats);
   shared_mem_video_info_stats_close(g_pSM_VideoInfoStats);
   shared_mem_video_info_stats_radio_in_close(g_pSM_VideoInfoStatsRadioIn);
   shared_mem_router_vehicles_runtime_info_close(g_pSM_RouterVehiclesRuntimeInfo);

   _close_rxtx_radio_interfaces(); 
  
   ruby_close_ipc_channel(g_fIPCFromCentral);
   ruby_close_ipc_channel(g_fIPCToCentral);
   ruby_close_ipc_channel(g_fIPCFromTelemetry);
   ruby_close_ipc_channel(g_fIPCToTelemetry);
   ruby_close_ipc_channel(g_fIPCFromRC);
   ruby_close_ipc_channel(g_fIPCToRC);

   uninit_processing_audio();

   if ( NULL != g_pCurrentModel )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   {
      g_pCurrentModel->relay_params.uCurrentRelayMode = RELAY_MODE_MAIN | RELAY_MODE_IS_RELAY_NODE;
      saveControllerModel(g_pCurrentModel);
   }

   g_fIPCFromCentral = -1;
   g_fIPCToCentral = -1;
   g_fIPCToTelemetry = -1;
   g_fIPCFromTelemetry = -1;
   g_fIPCToRC = -1;
   g_fIPCFromRC = -1;
   log_line("---------------------------");
   log_line("Execution Finished. Exit.");
   log_line("---------------------------");
 
   return 0;
}
