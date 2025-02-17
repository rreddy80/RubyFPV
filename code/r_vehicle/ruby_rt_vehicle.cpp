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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <semaphore.h>
#include <pthread.h>
#include <ctype.h>

#include "../base/base.h"
#include "../base/shared_mem.h"
#include "../base/config.h"
#include "../base/commands.h"
#include "../base/hw_procs.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/radio_utils.h"
#include "../base/encr.h"
#include "../base/ruby_ipc.h"
#include "../base/camera_utils.h"
#include "../base/vehicle_settings.h"
#include "../common/string_utils.h"
#include "../common/radio_stats.h"
#include "../common/relay_utils.h"

#include "shared_vars.h"
#include "timers.h"

#include "processor_tx_audio.h"
#include "processor_tx_video.h"
#include "process_received_ruby_messages.h"
#include "process_radio_in_packets.h"
#include "launchers_vehicle.h"
#include "utils_vehicle.h"

#include "../radio/radiopackets2.h"
#include "../radio/radiolink.h"
#include "../radio/radiopacketsqueue.h"
#include "../radio/radio_rx.h"
#include "../radio/radio_tx.h"
#include "../radio/radio_duplicate_det.h"
#include "../radio/fec.h" 
#include "packets_utils.h"
#include "ruby_rt_vehicle.h"
#include "events.h"
#include "process_local_packets.h"
#include "video_link_auto_keyframe.h"
#include "video_link_stats_overwrites.h"
#include "video_link_check_bitrate.h"
#include "processor_relay.h"


#define MAX_RECV_UPLINK_HISTORY 12
#define SEND_ALARM_MAX_COUNT 5

static int s_iCountCPULoopOverflows = 0;
static u32 s_uTimeLastCheckForRaspiDebugMessages = 0;

u32 s_MinVideoBlocksGapMilisec = 1;


u8 s_BufferCommandsReply[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeTmpBufferCommandsReply[MAX_PACKET_TOTAL_SIZE];
int s_PipeTmpBufferCommandsReplyPos = 0;

u8 s_BufferTelemetryDownlink[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeTmpBufferTelemetryDownlink[MAX_PACKET_TOTAL_SIZE];
int s_PipeTmpBufferTelemetryDownlinkPos = 0;  

u8 s_BufferRCDownlink[MAX_PACKET_TOTAL_SIZE];
u8 s_PipeTmpBufferRCDownlink[MAX_PACKET_TOTAL_SIZE];
int s_PipeTmpBufferRCDownlinkPos = 0;  

t_packet_queue s_QueueRadioPacketsOut;

u32 s_LoopCounter = 0;
u32 s_debugFramesCount = 0;
u32 s_debugVideoBlocksInCount = 0;
u32 s_debugVideoBlocksOutCount = 0;

bool s_bRadioReinitialized = false;

long s_lLastLiveLogFileOffset = -1;

u16 s_countTXVideoPacketsOutPerSec[2];
u16 s_countTXDataPacketsOutPerSec[2];
u16 s_countTXCompactedPacketsOutPerSec[2];

u32 s_uTimeLastLoopOverloadError = 0;
u32 s_LoopOverloadErrorCount = 0;

extern u32 s_uLastAlarms;
extern u32 s_uLastAlarmsFlags1;
extern u32 s_uLastAlarmsFlags2;
extern u32 s_uLastAlarmsTime;
extern u32 s_uLastAlarmsCount;

pthread_t s_pThreadWatchDogVideoCapture;

u32 s_uTimeLastReadIPCMessages = 0;
u32 s_uTimeLastCheckForRelayedVehicleRubyTelemetryAlarm = 0;

int try_read_video_input(bool bDiscard);

bool links_set_cards_frequencies_and_params(int iLinkId)
{
   if ( NULL == g_pCurrentModel )
   {
      log_error_and_alarm("Invalid parameters for setting radio interfaces frequencies");
      return false;
   }

   if ( (iLinkId < 0) || (iLinkId >= hardware_get_radio_interfaces_count()) )
      log_line("Setting all cards frequencies and params according to vehicle radio links...");
   else
      log_line("Setting cards frequencies and params only for vehicle radio link %d", iLinkId+1);

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      
      int nRadioLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[i];
      if ( nRadioLinkId < 0 || nRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count )
         continue;

      if ( ( iLinkId >= 0 ) && (nRadioLinkId != iLinkId) )
        continue;

      u32 uRadioLinkFrequency = g_pCurrentModel->radioLinksParams.link_frequency_khz[nRadioLinkId];
      if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId == nRadioLinkId )
      if ( g_pCurrentModel->relay_params.uRelayFrequencyKhz != 0 )
      {
         uRadioLinkFrequency = g_pCurrentModel->relay_params.uRelayFrequencyKhz;
         log_line("Setting relay frequency of %s for radio interface %d.", str_format_frequency(uRadioLinkFrequency), i+1);
      }
      if ( 0 == hardware_radio_supports_frequency(pRadioHWInfo, uRadioLinkFrequency ) )
      {
         log_softerror_and_alarm("Radio interface %d, %s, %s does not support radio link %d frequency %s. Did not changed the radio interface frequency.",
            i+1, pRadioHWInfo->szName, pRadioHWInfo->szDriver, nRadioLinkId+1, str_format_frequency(uRadioLinkFrequency));
         continue;
      }
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      {
         if ( iLinkId >= 0 )
            flag_update_sik_interface(i);
         else
         {
            u32 uFreqKhz = uRadioLinkFrequency;
            u32 uDataRate = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[nRadioLinkId];
            u32 uTxPower = g_pCurrentModel->radioInterfacesParams.txPowerSiK;
            u32 uECC = (g_pCurrentModel->radioLinksParams.link_radio_flags[nRadioLinkId] & RADIO_FLAGS_SIK_ECC)? 1:0;
            u32 uLBT = (g_pCurrentModel->radioLinksParams.link_radio_flags[nRadioLinkId] & RADIO_FLAGS_SIK_LBT)? 1:0;
            u32 uMCSTR = (g_pCurrentModel->radioLinksParams.link_radio_flags[nRadioLinkId] & RADIO_FLAGS_SIK_MCSTR)? 1:0;

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
                  radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, uFreqKhz);
                  break;
               }
            }
         }
      }
      else
      {
         if ( radio_utils_set_interface_frequency(g_pCurrentModel, i, uRadioLinkFrequency, g_pProcessStats) )
            radio_stats_set_card_current_frequency(&g_SM_RadioStats, i, uRadioLinkFrequency);
      }
   }

   hardware_save_radio_info();

   return true;
}

void close_and_mark_sik_interfaces_to_reopen()
{
   int iCount = 0;
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      if ( pRadioHWInfo->openedForWrite || pRadioHWInfo->openedForRead )
      {
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

void close_radio_interfaces()
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
      radio_hw_info_t* pRadioInfo = hardware_get_radio_info(i);
      if ( pRadioInfo->openedForWrite )
         radio_close_interface_for_write(i);
   }

   radio_close_interfaces_for_read(); 

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      g_SM_RadioStats.radio_interfaces[i].openedForRead = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForWrite = 0;
   }
   log_line("Closed all radio interfaces (rx/tx).");
}

int open_radio_interfaces()
{
   log_line("OPENING INTERFACES BEGIN =========================================================");
   log_line("Opening RX/TX radio interfaces...");
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
      log_line("Relaying is enabled on radio link %d on frequency: %s.", g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId+1, str_format_frequency(g_pCurrentModel->relay_params.uRelayFrequencyKhz));

   //init_radio_in_packets_state();

   int countOpenedForRead = 0;
   int countOpenedForWrite = 0;
   int iCountSikInterfacesOpened = 0;

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      int iRadioLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[i];
      g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId = iRadioLinkId;
      g_SM_RadioStats.radio_interfaces[i].assignedVehicleRadioLinkId = iRadioLinkId;
      g_SM_RadioStats.radio_interfaces[i].openedForRead = 0;
      g_SM_RadioStats.radio_interfaces[i].openedForWrite = 0;
   }

   // Init RX interfaces

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      if ( pRadioHWInfo->lastFrequencySetFailed )
         continue;
      int iRadioLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[i];
      if ( iRadioLinkId < 0 )
      {
         log_softerror_and_alarm("No radio link is assigned to radio interface %d", i+1);
         continue;
      }
      if ( iRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count )
      {
         log_softerror_and_alarm("Invalid radio link (%d of %d) is assigned to radio interface %d", iRadioLinkId+1, g_pCurrentModel->radioLinksParams.links_count, i+1);
         continue;
      }

      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( ! (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
         continue;

      if ( g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( ! (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_RX) )
         continue;

      if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId == iRadioLinkId )
         log_line("Open radio interface %d for radio link %d in relay mode ...", i+1, iRadioLinkId);
      else
         log_line("Open radio interface %d for radio link %d ...", i+1, iRadioLinkId);

      if ( ((pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_ATHEROS) ||
           ((pRadioHWInfo->typeAndDriver & 0xFF) == RADIO_TYPE_RALINK) )
      {
         int nRateTx = g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iRadioLinkId];
         if ( 0 != g_pCurrentModel->radioInterfacesParams.interface_datarate_video_bps[i] )
         if ( getRealDataRateFromRadioDataRate(g_pCurrentModel->radioInterfacesParams.interface_datarate_video_bps[i]) < getRealDataRateFromRadioDataRate(nRateTx) )
            nRateTx = g_pCurrentModel->radioInterfacesParams.interface_datarate_video_bps[i];
         radio_utils_set_datarate_atheros(g_pCurrentModel, i, nRateTx);
      }

      if ( (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO) ||
           (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA) )
      {
         if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
         {
            if ( hardware_radio_sik_open_for_read_write(i) > 0 )
            {
               g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;
               countOpenedForRead++;
               iCountSikInterfacesOpened++;
            }
         }
         else
         {
            if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
               radio_open_interface_for_read(i, RADIO_PORT_ROUTER_DOWNLINK);
            else
               radio_open_interface_for_read(i, RADIO_PORT_ROUTER_UPLINK);

            g_SM_RadioStats.radio_interfaces[i].openedForRead = 1;      
            countOpenedForRead++;
         }
      }
   }

   if ( countOpenedForRead == 0 )
   {
      log_error_and_alarm("Failed to find or open any RX interface for receiving data.");
      close_radio_interfaces();
      return -1;
   }


   // Init TX interfaces

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( pRadioHWInfo->lastFrequencySetFailed )
         continue;
      int iRadioLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[i];
      if ( iRadioLinkId < 0 )
      {
         log_softerror_and_alarm("No radio link is assigned to radio interface %d", i+1);
         continue;
      }
      if ( iRadioLinkId >= g_pCurrentModel->radioLinksParams.links_count )
      {
         log_softerror_and_alarm("Invalid radio link (%d of %d) is assigned to radio interface %d", iRadioLinkId+1, g_pCurrentModel->radioLinksParams.links_count, i+1);
         continue;
      }

      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( ! (g_pCurrentModel->radioLinksParams.link_capabilities_flags[iRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
         continue;

      if ( g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
         continue;
      if ( ! (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_TX) )
         continue;

      if ( (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO) ||
           (g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA) )
      {
         if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
         {
            if ( ! g_SM_RadioStats.radio_interfaces[i].openedForRead )
            {
               if ( hardware_radio_sik_open_for_read_write(i) > 0 )
               {
                 g_SM_RadioStats.radio_interfaces[i].openedForWrite= 1;
                 countOpenedForWrite++;
                 iCountSikInterfacesOpened++;
               }
            }
            else
            {
               g_SM_RadioStats.radio_interfaces[i].openedForWrite= 1;
               countOpenedForWrite++;
            }
         }
         else
         {
            radio_open_interface_for_write(i);
            g_SM_RadioStats.radio_interfaces[i].openedForWrite = 1;
            countOpenedForWrite++;
         }
      }
   }

   if ( 0 < iCountSikInterfacesOpened )
   {
      radio_tx_set_sik_packet_size(g_pCurrentModel->radioLinksParams.iSiKPacketSize);
      radio_tx_start_tx_thread();
   }

   if ( 0 == countOpenedForWrite )
   {
      log_error_and_alarm("Can't find any TX interfaces for video/data or failed to init it.");
      close_radio_interfaces();
      return -1;
   }

   log_line("Opening RX/TX radio interfaces complete. %d interfaces for RX, %d interfaces for TX :", countOpenedForRead, countOpenedForWrite);
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      char szFlags[128];
      szFlags[0] = 0;
      str_get_radio_capabilities_description(g_pCurrentModel->radioInterfacesParams.interface_capabilities_flags[i], szFlags);

      if ( pRadioHWInfo->openedForRead && pRadioHWInfo->openedForWrite )
         log_line(" * Interface %s, %s, %s opened for read/write, flags: %s", pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), szFlags );
      else if ( pRadioHWInfo->openedForRead )
         log_line(" * Interface %s, %s, %s opened for read, flags: %s", pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), szFlags );
      else if ( pRadioHWInfo->openedForWrite )
         log_line(" * Interface %s, %s, %s opened for write, flags: %s", pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), szFlags );
      else
         log_line(" * Interface %s, %s, %s not used. Flags: %s", pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), szFlags );
   }
   log_line("OPENING INTERFACES END ============================================================");

   g_pCurrentModel->logVehicleRadioInfo();

   if ( ! process_data_tx_video_init() )
   {
      log_error_and_alarm("Failed to initialize video data tx processor.");
      close_radio_interfaces();
      return -1;
   }

   send_radio_config_to_controller();
   return 0;
}

void send_radio_config_to_controller()
{
   if ( NULL == g_pCurrentModel )
   {
      log_softerror_and_alarm("Tried to send current radio config to controller but there is no current model.");
      return;
   }
 
   log_line("Notify controller (send a radio message) about current vehicle radio configuration.");

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_RUBY_RADIO_CONFIG_UPDATED, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   PH.vehicle_id_dest = PH.vehicle_id_src;
   PH.total_length = sizeof(t_packet_header) + sizeof(type_relay_parameters) + sizeof(type_radio_interfaces_parameters) + sizeof(type_radio_links_parameters);

   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet + sizeof(t_packet_header), (u8*)&(g_pCurrentModel->relay_params), sizeof(type_relay_parameters));
   memcpy(packet + sizeof(t_packet_header) + sizeof(type_relay_parameters), (u8*)&(g_pCurrentModel->radioInterfacesParams), sizeof(type_radio_interfaces_parameters));
   memcpy(packet + sizeof(t_packet_header) + sizeof(type_relay_parameters) + sizeof(type_radio_interfaces_parameters), (u8*)&(g_pCurrentModel->radioLinksParams), sizeof(type_radio_links_parameters));
   send_packet_to_radio_interfaces(packet, PH.total_length);
}

void send_radio_reinitialized_message()
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_RUBY_RADIO_REINITIALIZED, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   PH.vehicle_id_dest = PH.vehicle_id_src;
   PH.total_length = sizeof(t_packet_header);

   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));

   send_packet_to_radio_interfaces(packet, PH.total_length);
}


void flag_need_video_capture_restart()
{
   if ( 0 != g_TimeToRestartVideoCapture )
      log_line("Router was flagged to restart video capture");
   if ( (NULL != g_pCurrentModel) && (!g_pCurrentModel->hasCamera()) )
   {
      log_line("Vehicle has no camera. Do not flag need for restarting video capture.");
      return;
   }
   g_uRouterState |= ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE;
   g_TimeToRestartVideoCapture = 0; 
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
            u32 uFreqKhz = pRadioHWInfo->uHardwareParamsList[8];
            u32 uDataRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
            u32 uTxPower = DEFAULT_RADIO_SIK_TX_POWER;
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
                  uTxPower = g_pCurrentModel->radioInterfacesParams.txPowerSiK;
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
                  send_alarm_to_controller(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE_FAILED, 0, 10);
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
       send_alarm_to_controller(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE, 0, 10);
   else
       send_alarm_to_controller(ALARM_ID_RADIO_INTERFACE_REINITIALIZED, g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown, 0, 10); 

   g_SiKRadiosState.bMustReinitSiKInterfaces = false;
   g_SiKRadiosState.iMustReconfigureSiKInterfaceIndex = -1;
   g_SiKRadiosState.uSiKInterfaceIndexThatBrokeDown = MAX_U32;
   log_line("[Router-SiKThread] Finished.");
   g_SiKRadiosState.bConfiguringSiKThreadWorking = false;
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
         log_line("SiK radio configuration completed. Result: %d.", iResult);
         char szBuff[256];
         sprintf(szBuff, "rm -rf %s", FILE_TMP_SIK_CONFIG_FINISHED);
         hw_execute_bash_command(szBuff, NULL);
         g_SiKRadiosState.bConfiguringToolInProgress = false;
         reopen_marked_sik_interfaces();
         send_alarm_to_controller(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURED_RADIO_INTERFACE, 0, 10);
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
      send_alarm_to_controller(ALARM_ID_GENERIC_STATUS_UPDATE, ALARM_FLAG_GENERIC_STATUS_RECONFIGURING_RADIO_INTERFACE, 0, 10);
   g_SiKRadiosState.iThreadRetryCounter++;
   return 1;
}

void reinit_radio_interfaces()
{
   if ( g_bQuit )
      return;

   g_bReinitializeRadioInProgress = true;

   char szComm[128];
   log_line("Reinit radio interfaces (PID %d)...", getpid());

   sprintf(szComm, "touch %s", FILE_TMP_ALARM_ON);
   hw_execute_bash_command_silent(szComm, NULL);

   sprintf(szComm, "touch %s", FILE_TMP_REINIT_RADIO_IN_PROGRESS);
   hw_execute_bash_command_silent(szComm, NULL);

   u32 uTimeStart = get_current_timestamp_ms();

   close_radio_interfaces();
   if ( g_bQuit )
   {
      g_bReinitializeRadioInProgress = false;
      return;
   }
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
   hw_execute_bash_command(szComm, NULL);

   vehicle_stop_video_capture(g_pCurrentModel);

   // Clean up video pipe data
   if ( g_pCurrentModel->hasCamera() )
      for( int i=0; i<50; i++ )
         try_read_video_input(true);

   while ( true )
   {
      log_line("Try to do recovery action...");
      hardware_sleep_ms(400);
      if ( get_current_timestamp_ms() > uTimeStart + 20000 )
      {
         g_bReinitializeRadioInProgress = false;
         hw_execute_bash_command("sudo reboot -f", NULL);
         sleep(10);
      }

      if ( NULL != g_pProcessStats )
      {
         g_TimeNow = get_current_timestamp_ms();
         g_pProcessStats->lastActiveTime = g_TimeNow;
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      }

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
      if ( 0 == strlen(szOutput) )
         continue;

      if ( NULL != g_pProcessStats )
      {
         g_TimeNow = get_current_timestamp_ms();
         g_pProcessStats->lastActiveTime = g_TimeNow;
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      }

      log_line("Reinitializing radio interfaces: found interfaces on ifconfig: [%s]", szOutput);
      sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
      hw_execute_bash_command(szComm, NULL);
      hardware_reset_radio_enumerated_flag();
      hardware_enumerate_radio_interfaces();
      if ( 0 < hardware_get_radio_interfaces_count() )
         break;
   }

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }
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

   log_line("=================================================================");
   log_line("Detected hardware radio interfaces:");
   hardware_log_radio_info();

   log_line("Setting all the cards frequencies again...");

   for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
   {
      if ( g_pCurrentModel->radioInterfacesParams.interface_link_id[i] < 0 )
      {
         log_softerror_and_alarm("No radio link is assigned to radio interface %d", i+1);
         continue;
      }
      if ( g_pCurrentModel->radioInterfacesParams.interface_link_id[i] >= g_pCurrentModel->radioLinksParams.links_count )
      {
         log_softerror_and_alarm("Invalid radio link (%d of max %d) is assigned to radio interface %d", i+1, g_pCurrentModel->radioInterfacesParams.interface_link_id[i]+1, g_pCurrentModel->radioLinksParams.links_count);
         continue;
      }
      g_pCurrentModel->radioInterfacesParams.interface_current_frequency_khz[i] = g_pCurrentModel->radioLinksParams.link_frequency_khz[g_pCurrentModel->radioInterfacesParams.interface_link_id[i]];
      radio_utils_set_interface_frequency(g_pCurrentModel, i, g_pCurrentModel->radioInterfacesParams.interface_current_frequency_khz[i], g_pProcessStats );
   }
   log_line("Setting all the cards frequencies again. Done.");
   hardware_save_radio_info();
   hardware_sleep_ms(100);
 
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   open_radio_interfaces();

   log_line("Reinit radio interfaces: completed.");

   s_bRadioReinitialized = true;
   g_TimeRadioReinitialized = get_current_timestamp_ms();

   sprintf(szComm, "rm -rf %s", FILE_TMP_REINIT_RADIO_IN_PROGRESS);
   hw_execute_bash_command_silent(szComm, NULL);

   sprintf(szComm, "rm -rf %s", FILE_TMP_REINIT_RADIO_REQUEST);
   hw_execute_bash_command_silent(szComm, NULL); 

   // wait here so that whatchdog restarts everything

   log_line("Reinit radio interfaces procedure complete. Now waiting for watchdog to restart everything (PID: %d).", getpid());
   int iCount = 0;
   int iSkip = 1;
   while ( !g_bQuit )
   {
      hardware_sleep_ms(50);
      iCount++;
      iSkip--;

      if ( iSkip == 0 )
      {
         send_radio_reinitialized_message();
         iSkip = iCount;
      }
   }
   log_line("Reinit radio interfaces procedure was signaled to quit (PID: %d).", getpid());

   g_bReinitializeRadioInProgress = false;

}

void send_model_settings_to_controller()
{
   log_line("Send model settings to controller. Notify rx_commands to do it.");
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROL_VEHICLE_SEND_MODEL_SETTINGS, STREAM_ID_DATA);
   PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
   PH.total_length = sizeof(t_packet_header);

   ruby_ipc_channel_send_message(s_fIPCRouterToCommands, (u8*)&PH, PH.total_length);

   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   if ( NULL != g_pProcessStats )
      g_pProcessStats->lastActiveTime = get_current_timestamp_ms();
}

void _inject_video_link_dev_stats_packet()
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_TELEMETRY, PACKET_TYPE_RUBY_TELEMETRY_VIDEO_LINK_DEV_STATS, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   PH.vehicle_id_dest = g_pCurrentModel->vehicle_id;
   PH.total_length = sizeof(t_packet_header) + sizeof(shared_mem_video_link_stats_and_overwrites);
   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet+sizeof(t_packet_header), &g_SM_VideoLinkStats, sizeof(shared_mem_video_link_stats_and_overwrites));

   // Add it to the start of the queue, so it's extracted (poped) next
   packets_queue_inject_packet_first(&s_QueueRadioPacketsOut, packet);
}

void _inject_video_link_dev_graphs_packet()
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_TELEMETRY, PACKET_TYPE_RUBY_TELEMETRY_VIDEO_LINK_DEV_GRAPHS, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   PH.vehicle_id_dest = g_pCurrentModel->vehicle_id;
   PH.total_length = sizeof(t_packet_header) + sizeof(shared_mem_video_link_graphs);
   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet+sizeof(t_packet_header), &g_SM_VideoLinkGraphs, sizeof(shared_mem_video_link_graphs));

   // Add it to the start of the queue, so it's extracted (poped) next
   packets_queue_inject_packet_first(&s_QueueRadioPacketsOut, packet);
}

int try_read_video_input(bool bDiscard)
{
   if ( -1 == s_fInputVideoStream )
      return 0;

   fd_set readset;
   FD_ZERO(&readset);
   FD_SET(s_fInputVideoStream, &readset);

   struct timeval timePipeInput;
   timePipeInput.tv_sec = 0;
   timePipeInput.tv_usec = 100; // 0.1 miliseconds timeout

   int selectResult = select(s_fInputVideoStream+1, &readset, NULL, NULL, &timePipeInput);
   if ( selectResult <= 0 )
      return 0;

   if( 0 == FD_ISSET(s_fInputVideoStream, &readset) )
      return 0;

   int count = read(s_fInputVideoStream, process_data_tx_video_get_current_buffer_to_read_pointer(), process_data_tx_video_get_current_buffer_to_read_size());
   if ( count < 0 )
   {
      log_error_and_alarm("Failed to read from video input fifo: %s, returned code: %d, error: %s", FIFO_RUBY_CAMERA1, count, strerror(errno));
      return -1;
   }
   
   if ( bDiscard )
      return 0;

   // We have a full video block ?

   if ( process_data_tx_video_on_data_read_complete(count) )
      s_debugVideoBlocksInCount++;
   return count;
}


void _read_ipc_pipes(u32 uTimeNow)
{
   s_uTimeLastReadIPCMessages = uTimeNow;
   int maxToRead = 20;
   int maxPacketsToRead = maxToRead;

   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(s_fIPCRouterFromCommands, 50, s_PipeTmpBufferCommandsReply, &s_PipeTmpBufferCommandsReplyPos, s_BufferCommandsReply) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferCommandsReply;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferCommandsReply); 
      else
         packets_queue_add_packet(&s_QueueRadioPacketsOut, s_BufferCommandsReply);
   } 
   if ( maxToRead - maxPacketsToRead > 6 )
      log_line("Read %d messages from commands msgqueue.", maxToRead - maxPacketsToRead);

   maxPacketsToRead = maxToRead;
   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(s_fIPCRouterFromTelemetry, 50, s_PipeTmpBufferTelemetryDownlink, &s_PipeTmpBufferTelemetryDownlinkPos, s_BufferTelemetryDownlink) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferTelemetryDownlink;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferTelemetryDownlink); 
      else
      {
         packets_queue_add_packet(&s_QueueRadioPacketsOut, s_BufferTelemetryDownlink); 
         /*
         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_TELEMETRY )
         {
            if ( pPH->packet_type == PACKET_TYPE_TELEMETRY_ALL )
            {
               t_packet_header_fc_telemetry* pH = (t_packet_header_fc_telemetry*)(&s_BufferTelemetryDownlink[0] + sizeof(t_packet_header) + sizeof(t_packet_header_ruby_telemetry));
               log_line("Received from telemetry pipe: %d pitch", pH->pitch/100-180);
            }
            //else
            //   log_line("Received from telemetry pipe: other packet to download, type: %d, size: %d bytes", pPH->packet_type, pPH->total_length);
         }
         */
      }
   }
   if ( maxToRead - maxPacketsToRead > 6 )
      log_line("Read %d messages from telemetry msgqueue.", maxToRead - maxPacketsToRead);

   maxPacketsToRead = maxToRead;
   while ( (maxPacketsToRead > 0) && NULL != ruby_ipc_try_read_message(s_fIPCRouterFromRC, 50, s_PipeTmpBufferRCDownlink, &s_PipeTmpBufferRCDownlinkPos, s_BufferRCDownlink) )
   {
      maxPacketsToRead--;
      t_packet_header* pPH = (t_packet_header*)s_BufferRCDownlink;      
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_LOCAL_CONTROL )
         packets_queue_add_packet(&s_QueueControlPackets, s_BufferRCDownlink); 
      else
         packets_queue_add_packet(&s_QueueRadioPacketsOut, s_BufferRCDownlink); 
   }
   if ( maxToRead - maxPacketsToRead > 3 )
      log_line("Read %d messages from RC msgqueue.", maxToRead - maxPacketsToRead);
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


void process_and_send_packets()
{
   u8 composed_packet[MAX_PACKET_TOTAL_SIZE];
   int composed_packet_length = 0;
   bool bMustInjectVideoDevStats = false;
   bool bMustInjectVideoDevGraphs = false;
   
   #ifdef FEATURE_CONCATENATE_SMALL_RADIO_PACKETS
   bool bLastHasHighCapacityFlag = false;
   bool bLastHasLowCapacityFlag = false;
   u32 uLastPacketType = 0;
   #endif

   while ( packets_queue_has_packets(&s_QueueRadioPacketsOut) )
   {
      u32 uTime = get_current_timestamp_ms();
      if ( uTime > s_uTimeLastReadIPCMessages + 500 )
      {
         log_softerror_and_alarm("Too much time since last ipc messages read (%u ms) while sending radio packets, read ipc messages.", uTime - s_uTimeLastReadIPCMessages);
         _read_ipc_pipes(uTime);
      }

      int iPacketLength = -1;
      u8* pPacketBuffer = packets_queue_pop_packet(&s_QueueRadioPacketsOut, &iPacketLength);
      if ( NULL == pPacketBuffer || -1 == iPacketLength )
         break;

      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCIncomingTime = g_TimeNow;

      bMustInjectVideoDevStats = false;
      bMustInjectVideoDevGraphs = false;

      t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
      pPH->packet_flags &= (~PACKET_FLAGS_BIT_CAN_START_TX);

      #ifdef FEATURE_CONCATENATE_SMALL_RADIO_PACKETS

      bool bHasHighCapacityFlag = false;
      bool bHasLowCapacityFlag = false;
      if ( pPH->packet_flags_extended & PACKET_FLAGS_EXTENDED_BIT_SEND_ON_HIGH_CAPACITY_LINK_ONLY )
         bHasHighCapacityFlag = true;
      if ( pPH->packet_flags_extended & PACKET_FLAGS_EXTENDED_BIT_SEND_ON_LOW_CAPACITY_LINK_ONLY )
         bHasLowCapacityFlag = true;

      if ( ( bLastHasLowCapacityFlag != bHasLowCapacityFlag ) ||
           ( bLastHasHighCapacityFlag != bHasHighCapacityFlag ) ||
           (composed_packet_length + iPacketLength > MAX_PACKET_PAYLOAD) ||
           (pPH->packet_type == PACKET_TYPE_RUBY_PING_CLOCK_REPLY) ||
           (uLastPacketType == PACKET_TYPE_RUBY_PING_CLOCK_REPLY) ||
           (pPH->packet_type == PACKET_TYPE_RUBY_MODEL_SETTINGS) ||
           (uLastPacketType == PACKET_TYPE_RUBY_MODEL_SETTINGS) ||
           (pPH->packet_type == PACKET_TYPE_COMMAND_RESPONSE) ||
           (uLastPacketType == PACKET_TYPE_COMMAND_RESPONSE) )
      if ( composed_packet_length > 0 )
      {
         send_packet_to_radio_interfaces(composed_packet, composed_packet_length);
         composed_packet_length = 0;
      }

      bLastHasHighCapacityFlag = bHasHighCapacityFlag;
      bLastHasLowCapacityFlag = bHasLowCapacityFlag;
      #endif

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_TELEMETRY )
      {
         // Update Ruby telemetry info if we are sending Ruby telemetry to controller
         // Update also the telemetry extended extra info: retransmissions info

         if ( pPH->packet_type == PACKET_TYPE_RUBY_TELEMETRY_EXTENDED )
         {
            if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_ENABLE_VIDEO_LINK_STATS) )
               bMustInjectVideoDevStats = true;
            if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_ADAPTIVE_VIDEO_GRAPH) )
               bMustInjectVideoDevStats = true;
            if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_ENABLE_VIDEO_LINK_GRAPHS) )
               bMustInjectVideoDevGraphs = true;

            t_packet_header_ruby_telemetry_extended_v3* pPHRTE = (t_packet_header_ruby_telemetry_extended_v3*) (pPacketBuffer + sizeof(t_packet_header));
            pPHRTE->downlink_tx_video_bitrate_bps = g_pProcessorTxVideo->getCurrentVideoBitrateAverageLastMs(500);
            pPHRTE->downlink_tx_video_all_bitrate_bps = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverageLastMs(500);
            if ( NULL != g_pProcessorTxAudio )
            {
               pPHRTE->downlink_tx_data_bitrate_bps += g_pProcessorTxAudio->getAverageAudioInputBps();
               pPHRTE->downlink_tx_video_all_bitrate_bps += g_pProcessorTxAudio->getAverageAudioInputBps();
            }
            pPHRTE->downlink_tx_data_bitrate_bps = 0;

            pPHRTE->downlink_tx_video_packets_per_sec = s_countTXVideoPacketsOutPerSec[0] + s_countTXVideoPacketsOutPerSec[1];
            pPHRTE->downlink_tx_data_packets_per_sec = s_countTXDataPacketsOutPerSec[0] + s_countTXDataPacketsOutPerSec[1];
            pPHRTE->downlink_tx_compacted_packets_per_sec = s_countTXCompactedPacketsOutPerSec[0] + s_countTXCompactedPacketsOutPerSec[1];

            for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
            {
               // 0...127 regular datarates, 128+: mcs rates;
               if ( get_last_tx_used_datarate(i, 0) >= 0 )
                  pPHRTE->downlink_datarate_bps[i][0] = 1000*1000*get_last_tx_used_datarate(i, 0);
               else
                  pPHRTE->downlink_datarate_bps[i][0] = get_last_tx_used_datarate(i, 0);

               if ( get_last_tx_used_datarate(i, 1) >= 0 )
                  pPHRTE->downlink_datarate_bps[i][0] = 1000*1000*get_last_tx_used_datarate(i, 1);
               else
                  pPHRTE->downlink_datarate_bps[i][0] = get_last_tx_used_datarate(i, 1);

               pPHRTE->uplink_datarate_bps[i] = 1000*500*g_UplinkInfoRxStats[i].lastReceivedDataRate;
               pPHRTE->uplink_rssi_dbm[i] = g_UplinkInfoRxStats[i].lastReceivedDBM + 200;
               pPHRTE->uplink_link_quality[i] = g_SM_RadioStats.radio_interfaces[i].rxQuality;
            }

            pPHRTE->txTimePerSec = 0;
            if ( NULL != g_pCurrentModel )
               for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
                  pPHRTE->txTimePerSec += g_InterfacesTxMiliSecTimePerSecond[i];

            // Update extra info: retransmissions
            
            if ( pPHRTE->extraSize > 0 )
            if ( pPHRTE->extraSize == sizeof(t_packet_header_ruby_telemetry_extended_extra_info_retransmissions) )
            if ( pPH->total_length == (sizeof(t_packet_header) + sizeof(t_packet_header_ruby_telemetry_extended_v3) + sizeof(t_packet_header_ruby_telemetry_extended_extra_info_retransmissions)) )
            {
                memcpy( pPacketBuffer + sizeof(t_packet_header) + sizeof(t_packet_header_ruby_telemetry_extended_v3), (u8*)&g_PHTE_Retransmissions, sizeof(g_PHTE_Retransmissions));
            }
         }
      }
 
      #ifdef FEATURE_CONCATENATE_SMALL_RADIO_PACKETS

      uLastPacketType = pPH->packet_type;

      memcpy(&composed_packet[composed_packet_length], pPacketBuffer, iPacketLength);
      composed_packet_length += iPacketLength;
      
      if ( bHasLowCapacityFlag )
      {
         send_packet_to_radio_interfaces(composed_packet, composed_packet_length);
         composed_packet_length = 0;
         continue;
      }

      #else

      send_packet_to_radio_interfaces(pPacketBuffer, iPacketLength);
      composed_packet_length = 0;
      
      #endif

      if ( bMustInjectVideoDevStats )
         _inject_video_link_dev_stats_packet();
      if ( bMustInjectVideoDevGraphs )
         _inject_video_link_dev_graphs_packet();
   }

   if ( composed_packet_length > 0 )
   {
      send_packet_to_radio_interfaces(composed_packet, composed_packet_length);
   }
}

void _check_write_filesystem()
{
   static bool s_bRouterCheckedForWriteFileSystem = false;
   static bool s_bRouterWriteFileSystemOk = false;

   if ( ! s_bRouterCheckedForWriteFileSystem )
   {
      log_line("Checking the file system for write access...");
      s_bRouterCheckedForWriteFileSystem = true;
      s_bRouterWriteFileSystemOk = false;

      hw_execute_bash_command("rm -rf tmp/testwrite.txt", NULL);
      FILE* fdTemp = fopen("tmp/testwrite.txt", "wb");
      if ( NULL == fdTemp )
      {
         g_pCurrentModel->alarms |= ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR;
         s_bRouterWriteFileSystemOk = false;
      }
      else
      {
         fprintf(fdTemp, "test1234\n");
         fclose(fdTemp);
         fdTemp = fopen("tmp/testwrite.txt", "rb");
         if ( NULL == fdTemp )
         {
            g_pCurrentModel->alarms |= ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR;
            s_bRouterWriteFileSystemOk = false;
         }
         else
         {
            char szTmp[256];
            if ( 1 != fscanf(fdTemp, "%s", szTmp) )
            {
               g_pCurrentModel->alarms |= ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR;
               s_bRouterWriteFileSystemOk = false;
            }
            else if ( 0 != strcmp(szTmp, "test1234") )
            {
               g_pCurrentModel->alarms |= ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR;
               s_bRouterWriteFileSystemOk = false;
            }
            else
               s_bRouterWriteFileSystemOk = true;
            fclose(fdTemp);
            hw_execute_bash_command("rm -rf tmp/testwrite.txt", NULL);
         }
      }
      if ( ! s_bRouterWriteFileSystemOk )
         log_line("Checking the file system for write access: Failed.");
      else
         log_line("Checking the file system for write access: Succeeded.");
   }
   if ( s_bRouterCheckedForWriteFileSystem )
   {
      if ( ! s_bRouterWriteFileSystemOk )
        send_alarm_to_controller(ALARM_ID_VEHICLE_STORAGE_WRITE_ERRROR, 0, 0, 5);
   }      
}

static void * _thread_watchdog_video_capture(void *ignored_argument)
{
   g_uRouterState &= ~ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE;
   if ( (NULL != g_pCurrentModel) && (! g_pCurrentModel->hasCamera()) )
   {
      log_line("Vehicle has no camera. Stop the watchdog thread for video capture.");
      return NULL;
   }

   int iCount = 0;
   while ( ! g_bQuit )
   {
      hardware_sleep_ms(2000);
      iCount++;

      // If video capture was flagged or is in the process of restarting, do not check video capture running

      if ( (g_uRouterState & ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE) || (g_TimeToRestartVideoCapture != 0) )
         continue;

      // Check video capture program up and running
   
      if ( g_pCurrentModel->hasCamera() && (g_TimeStartRaspiVid > 0) && (g_TimeNow > g_TimeStartRaspiVid+5000) )
      if ( ! g_bVideoPaused )
      //if ( g_TimeNow > g_TimeLastVideoCaptureProgramRunningCheck + 2000 )
      {
         g_TimeLastVideoCaptureProgramRunningCheck = g_TimeNow;

         bool bProgramRunning = false;
         bool bNeedsRestart = false;
         if ( (!bProgramRunning) && hw_process_exists(VIDEO_RECORDER_COMMAND) )
            bProgramRunning = true;
         if ( (!bProgramRunning) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE) )
            bProgramRunning = true;
         if ( (!bProgramRunning) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE307) )
            bProgramRunning = true;
         if ( (!bProgramRunning) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE_SHORT_NAME) )
            bProgramRunning = true;

         if ( ! bProgramRunning )
         {
            log_line("Can't find the running video capture program. Search for it.");
            char szOutput[2048];
            hw_execute_bash_command("pidof ruby_capture_raspi", szOutput);
            log_line("PID of ruby capture: [%s]", szOutput);
            hw_execute_bash_command_raw("ps -aef | grep ruby 2>&1", szOutput);
            log_line("Current running Ruby processes: [%s]", szOutput);
            if ( NULL != strstr(szOutput, "ruby_capture_") )
            {
               log_line("Video capture is still running. Do not restart it.");
            }
            else
            {
               log_softerror_and_alarm("The video capture program has crashed (no process). Restarting it.");
               bNeedsRestart = true;
            }
         }
         if ( bProgramRunning )
         {
            if ( (iCount % 5) == 0 )
               log_line("Video capture watchdog: capture is running ok.");
              
            static int s_iCheckVideoOutputBitrateCounter = 0;
            if ( relay_current_vehicle_must_send_own_video_feeds() &&
                (g_pProcessorTxVideo->getCurrentVideoBitrateAverageLastMs(1000) < 100000) )
            {
               log_softerror_and_alarm("Current output video bitrate is less than 100 kbps!");
               s_iCheckVideoOutputBitrateCounter++;
               if ( s_iCheckVideoOutputBitrateCounter >= 3 )
               {
                  log_softerror_and_alarm("The video capture program has crashed (no video output). Restarting it.");
                  bNeedsRestart = true;
               }
            }
            else
               s_iCheckVideoOutputBitrateCounter = 0;
         }
         if ( bNeedsRestart )
         {
            send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_CAPTURE_RESTARTED,0,0, 5);
            log_line("Signaled router main thread to restart video capture.");
            flag_need_video_capture_restart();
         }
      }
   }
   return NULL;
}

void _periodic_loop_check_ping()
{
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
   if ( g_TimeNow > s_uTimeLastCheckForRelayedVehicleRubyTelemetryAlarm + 200 )
   {
      s_uTimeLastCheckForRelayedVehicleRubyTelemetryAlarm = g_TimeNow;
      
      u32 uLastTimeRecvRubyTelemetry = relay_get_time_last_received_ruby_telemetry_from_relayed_vehicle();
      static u32 sl_uTimeLastSendRubyRelayedTelemetryLostAlarm = 0;
      static u32 sl_uTimeLastSendRubyRelayedTelemetryRecoveredAlarm = 0;
      if ( g_TimeNow > uLastTimeRecvRubyTelemetry + TIMEOUT_TELEMETRY_LOST )
      {
         if ( g_TimeNow > sl_uTimeLastSendRubyRelayedTelemetryLostAlarm + 10000 )
         {
            sl_uTimeLastSendRubyRelayedTelemetryLostAlarm = g_TimeNow;
            sl_uTimeLastSendRubyRelayedTelemetryRecoveredAlarm = 0;
            send_alarm_to_controller(ALARM_ID_GENERIC, ALARM_ID_GENERIC_TYPE_RELAYED_TELEMETRY_LOST, 0, 2);
         }
      }
      else
      {
         if ( g_TimeNow > sl_uTimeLastSendRubyRelayedTelemetryRecoveredAlarm + 10000 )
         {
            sl_uTimeLastSendRubyRelayedTelemetryRecoveredAlarm = g_TimeNow;
            sl_uTimeLastSendRubyRelayedTelemetryLostAlarm = 0;
            send_alarm_to_controller(ALARM_ID_GENERIC, ALARM_ID_GENERIC_TYPE_RELAYED_TELEMETRY_RECOVERED, 0, 2);
         }
      }
   }

   // Vehicle does not need to ping the relayed vehicle. Controller will.
   return;

   /*
   static u32 s_uTimeLastCheckSendPing = 0;
   static u8 s_uLastPingSentId = 0;

   if ( g_TimeNow < s_uTimeLastCheckSendPing+1000 )
      return;

   s_uTimeLastCheckSendPing = g_TimeNow;

   bool bMustSendPing = false;

   if ( g_pCurrentModel->relay_params.uCurrentRelayMode & RELAY_MODE_IS_RELAY_NODE )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
      bMustSendPing = true;

   if ( ! bMustSendPing )
      return;

   s_uLastPingSentId++;
   u8 uRadioLinkId = g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId;
   u8 uDestinationRelayFlags = g_pCurrentModel->relay_params.uRelayCapabilitiesFlags;
   u8 uDestinationRelayMode = g_pCurrentModel->relay_params.uCurrentRelayMode;

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_RUBY_PING_CLOCK, STREAM_ID_DATA);
   PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   PH.vehicle_id_dest = g_pCurrentModel->relay_params.uRelayedVehicleId;
   PH.total_length = sizeof(t_packet_header) + 4*sizeof(u8);
   
   u8 packet[MAX_PACKET_TOTAL_SIZE];
   // u8 ping id, u8 radio link id, u8 relay flags for destination vehicle
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet+sizeof(t_packet_header), &s_uLastPingSentId, sizeof(u8));
   memcpy(packet+sizeof(t_packet_header)+sizeof(u8), &uRadioLinkId, sizeof(u8));
   memcpy(packet+sizeof(t_packet_header)+2*sizeof(u8), &uDestinationRelayFlags, sizeof(u8));
   memcpy(packet+sizeof(t_packet_header)+3*sizeof(u8), &uDestinationRelayMode, sizeof(u8));
   
   relay_send_single_packet_to_relayed_vehicle(packet, PH.total_length);
   */
} 

// returns 1 if needs to stop/exit

int periodic_loop()
{
   s_LoopCounter++;
   s_debugFramesCount++;

   _check_reinit_sik_interfaces();

   if ( ! g_bHasSentVehicleSettingsAtLeastOnce )
   if ( (g_TimeNow > g_TimeStart + 4000) )
   {
      g_bHasSentVehicleSettingsAtLeastOnce = true;

      log_line("Tell rx_commands to generate all model settings to send to controller.");
      t_packet_header PH;
      radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROL_VEHICLE_SEND_MODEL_SETTINGS, STREAM_ID_DATA);
      PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
      PH.total_length = sizeof(t_packet_header);

      ruby_ipc_channel_send_message(s_fIPCRouterToCommands, (u8*)&PH, PH.total_length);

      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastActiveTime = get_current_timestamp_ms();

      _check_write_filesystem();
   }

   if ( radio_stats_periodic_update(&g_SM_RadioStats, NULL, g_TimeNow) )
   {
      // Send them to controller if needed
      bool bSend = false;
      if ( g_pCurrentModel )
      if ( g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_VEHICLE_RADIO_INTERFACES_STATS )
          bSend = true;
      //if ( (NULL != g_pCurrentModel) && g_pCurrentModel->bDeveloperMode )
      //    bSend = true;

      static u32 sl_uLastTimeSentRadioInterfacesStats = 0;
      u32 uSendInterval = g_SM_RadioStats.refreshIntervalMs;
      if ( g_SM_RadioStats.graphRefreshIntervalMs < (int)uSendInterval )
         uSendInterval = g_SM_RadioStats.graphRefreshIntervalMs;

      if ( uSendInterval < 100 )
         uSendInterval = 100;
      if ( g_TimeNow >= sl_uLastTimeSentRadioInterfacesStats + uSendInterval )
      if ( bSend )
      {
         sl_uLastTimeSentRadioInterfacesStats = uSendInterval;
         // Update lastDataRate for SiK radios and MCS links
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            int iLinkId = g_pCurrentModel->radioInterfacesParams.interface_link_id[i];
            if ( (iLinkId < 0) || (iLinkId >= g_pCurrentModel->radioLinksParams.links_count) )
               continue;
            if ( g_pCurrentModel->radioLinkIsSiKRadio(iLinkId) )
            {
               g_SM_RadioStats.radio_interfaces[i].lastDataRate = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iLinkId];
               g_SM_RadioStats.radio_interfaces[i].lastDataRateData = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iLinkId];
               g_SM_RadioStats.radio_interfaces[i].lastDataRateVideo = 0;
            }
            else if ( g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iLinkId] < 0 )
            {
               g_SM_RadioStats.radio_interfaces[i].lastDataRate = g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iLinkId];
               g_SM_RadioStats.radio_interfaces[i].lastDataRateData = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iLinkId];
               g_SM_RadioStats.radio_interfaces[i].lastDataRateVideo = g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iLinkId];
            }
         }

         // Update time now
         for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
         {
            g_SM_RadioStats.radio_interfaces[i].timeNow = g_TimeNow;
         }

         t_packet_header PH;
         radio_packet_init(&PH, PACKET_COMPONENT_TELEMETRY, PACKET_TYPE_RUBY_TELEMETRY_VEHICLE_RX_CARDS_STATS, STREAM_ID_DATA);
         PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
         PH.vehicle_id_dest = g_uControllerId;
         
         u8 packet[MAX_PACKET_TOTAL_SIZE];
         u8* pData = packet + sizeof(t_packet_header) + sizeof(u8);
         
         // Send all in single packet
         
         PH.packet_flags_extended |= PACKET_FLAGS_EXTENDED_BIT_SEND_ON_HIGH_CAPACITY_LINK_ONLY;
         PH.packet_flags_extended &= (~PACKET_FLAGS_EXTENDED_BIT_SEND_ON_LOW_CAPACITY_LINK_ONLY);
         PH.total_length = sizeof(t_packet_header) + sizeof(u8) + g_pCurrentModel->radioInterfacesParams.interfaces_count * sizeof(shared_mem_radio_stats_radio_interface);

         if ( PH.total_length <= MAX_PACKET_PAYLOAD )
         {
            u8 count = g_pCurrentModel->radioInterfacesParams.interfaces_count;
            memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
            memcpy(packet + sizeof(t_packet_header), (u8*)&count, sizeof(u8));
            for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
            {
               memcpy(pData, &(g_SM_RadioStats.radio_interfaces[i]), sizeof(shared_mem_radio_stats_radio_interface));
               pData += sizeof(shared_mem_radio_stats_radio_interface);
            }
            packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);
         }

         // Send rx stats, for each radio interface in individual single packets (to fit in small SiK packets)
         // Send shared_mem_radio_stats_radio_interface_compact
         if ( hardware_radio_has_low_capacity_links() )
         {
            PH.packet_flags_extended |= PACKET_FLAGS_EXTENDED_BIT_SEND_ON_LOW_CAPACITY_LINK_ONLY;
            PH.packet_flags_extended &= (~PACKET_FLAGS_EXTENDED_BIT_SEND_ON_HIGH_CAPACITY_LINK_ONLY);
            PH.total_length = sizeof(t_packet_header) + sizeof(u8) + sizeof(shared_mem_radio_stats_radio_interface_compact);
            
            static u8 uCardIndexRxStatsToSend = 0;
            uCardIndexRxStatsToSend++;
            if ( uCardIndexRxStatsToSend >= g_pCurrentModel->radioInterfacesParams.interfaces_count )
               uCardIndexRxStatsToSend = 0;

            shared_mem_radio_stats_radio_interface_compact statsCompact;
            
            statsCompact.lastDbm = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDbm;
            statsCompact.lastDbmVideo = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDbmVideo;
            statsCompact.lastDbmData = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDbmData;
            statsCompact.lastDataRate = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDataRate;
            statsCompact.lastDataRateVideo = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDataRateVideo;
            statsCompact.lastDataRateData = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].lastDataRateData;

            statsCompact.totalRxBytes = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalRxBytes;
            statsCompact.totalTxBytes = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalTxBytes;
            statsCompact.rxBytesPerSec = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].rxBytesPerSec;
            statsCompact.txBytesPerSec = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].txBytesPerSec;
            statsCompact.totalRxPackets = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalRxPackets;
            statsCompact.totalRxPacketsBad = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalRxPacketsBad;
            statsCompact.totalRxPacketsLost = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalRxPacketsLost;
            statsCompact.totalTxPackets = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].totalTxPackets;
            statsCompact.rxPacketsPerSec = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].rxPacketsPerSec;
            statsCompact.txPacketsPerSec = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].txPacketsPerSec;
            statsCompact.timeLastRxPacket = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].timeLastRxPacket;
            statsCompact.timeLastTxPacket = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].timeLastTxPacket;
            statsCompact.timeNow = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].timeNow;
            statsCompact.rxQuality = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].rxQuality;
            statsCompact.rxRelativeQuality = g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].rxRelativeQuality;

            memcpy(statsCompact.hist_rxPacketsCount, g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].hist_rxPacketsCount, MAX_HISTORY_RADIO_STATS_RECV_SLICES * sizeof(u8));
            memcpy(statsCompact.hist_rxPacketsLostCount, g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].hist_rxPacketsLostCount, MAX_HISTORY_RADIO_STATS_RECV_SLICES * sizeof(u8));
            memcpy(statsCompact.hist_rxGapMiliseconds, g_SM_RadioStats.radio_interfaces[uCardIndexRxStatsToSend].hist_rxGapMiliseconds, MAX_HISTORY_RADIO_STATS_RECV_SLICES * sizeof(u8));

            memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
            memcpy(packet + sizeof(t_packet_header), (u8*)&uCardIndexRxStatsToSend, sizeof(u8));
            memcpy(packet + sizeof(t_packet_header) + sizeof(u8), &(statsCompact), sizeof(shared_mem_radio_stats_radio_interface_compact));
            
            packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);
         }
      }
   }

   _periodic_loop_check_ping();

   if ( g_pCurrentModel->hasCamera() )
   if ( (! g_bDidSentRaspividBitrateRefresh) || (0 == g_TimeStartRaspiVid) )
   if ( g_TimeNow > g_TimeLastVideoCaptureProgramStartCheck + 500 )
   {
      g_TimeLastVideoCaptureProgramStartCheck = g_TimeNow;

      if ( 0 == g_TimeStartRaspiVid )
      {
         log_line("Detecting running video capture program...");
         bool bHasCompatibleCaptureProgram = false;
         if ( (!bHasCompatibleCaptureProgram) && hw_process_exists(VIDEO_RECORDER_COMMAND) )
            bHasCompatibleCaptureProgram = true;
         if ( (!bHasCompatibleCaptureProgram) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE) )
            bHasCompatibleCaptureProgram = true;
         if ( (!bHasCompatibleCaptureProgram) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE307) )
            bHasCompatibleCaptureProgram = true;
         if ( (!bHasCompatibleCaptureProgram) && hw_process_exists(VIDEO_RECORDER_COMMAND_VEYE_SHORT_NAME) )
            bHasCompatibleCaptureProgram = true;

         if ( ! bHasCompatibleCaptureProgram )
         {
            log_line("No compatible video capture program running (for sending video bitrate on the fly).");
            g_bDidSentRaspividBitrateRefresh = true;
         }
         else
         {
            log_line("Compatible video capture program is running (for sending video bitrate on the fly).");
            g_TimeStartRaspiVid = g_TimeNow - 500;
         }
      }

      if ( ! g_bDidSentRaspividBitrateRefresh )
      if ( 0 != g_TimeStartRaspiVid )
      if ( g_TimeNow > g_TimeStartRaspiVid + 2000)
      if ( NULL != g_pCurrentModel )
      {
         log_line("Send initial bitrate (%u bps) to video capture program.", g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate);
         send_control_message_to_raspivid( RASPIVID_COMMAND_ID_VIDEO_BITRATE, g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate/100000 );
         if ( NULL != g_pProcessorTxVideo )
            g_pProcessorTxVideo->setLastSetCaptureVideoBitrate(g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate, true);
         g_bDidSentRaspividBitrateRefresh = true;
      }
   }

#ifdef FEATURE_ENABLE_RC_FREQ_SWITCH
   if ( (s_iPendingFrequencyChangeLinkId >= 0) && (s_uPendingFrequencyChangeTo > 100) &&
        (s_uTimeFrequencyChangeRequest != 0) && (g_TimeNow > s_uTimeFrequencyChangeRequest + VEHICLE_SWITCH_FREQUENCY_AFTER_MS) )
   {
      log_line("Processing pending RC trigger to change frequency to: %s on link: %d", str_format_frequency(s_uPendingFrequencyChangeTo), s_iPendingFrequencyChangeLinkId+1 );
      g_pCurrentModel->compute_active_radio_frequencies(true);

      for( int i=0; i<g_pCurrentModel->nic_count; i++ )
      {
         if ( g_pCurrentModel->nic_flags[i] & NIC_FLAG_DISABLED )
            continue;
         if ( i == s_iPendingFrequencyChangeLinkId )
         {
            radio_utils_set_interface_frequency(g_pCurrentModel, i, s_uPendingFrequencyChangeTo, g_pProcessStats); 
            g_pCurrentModel->nic_frequency[i] = s_uPendingFrequencyChangeTo;
         }
      }
      hardware_save_radio_info();
      g_pCurrentModel->compute_active_radio_frequencies(true);
      saveCurrentModel();
      log_line("Notifying all other components of the new link frequency.");

      t_packet_header PH;
      radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROL_LINK_FREQUENCY_CHANGED, STREAM_ID_DATA);
      PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
      PH.vehicle_id_dest = 0;
      PH.total_length = sizeof(t_packet_header) + 2*sizeof(u32);
   
      u8 buffer[MAX_PACKET_TOTAL_SIZE];
      memcpy(buffer, (u8*)&PH, sizeof(t_packet_header));
      u32* pI = (u32*)((&buffer[0])+sizeof(t_packet_header));
      *pI = (u32)s_iPendingFrequencyChangeLinkId;
      pI++;
      *pI = s_uPendingFrequencyChangeTo;
      
      radio_packet_compute_crc(buffer, PH.total_length);

      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;  

      write(s_fPipeTelemetryUplink, buffer, PH.total_length);
      write(s_fPipeToCommands, buffer, PH.total_length);
      log_line("Done notifying all other components about the frequency change.");
      s_iPendingFrequencyChangeLinkId = -1;
      s_uPendingFrequencyChangeTo = 0;
      s_uTimeFrequencyChangeRequest = 0;
   }
#endif

   int iMaxRxQuality = 0;
   for( int i=0; i<g_pCurrentModel->radioInterfacesParams.interfaces_count; i++ )
      if ( g_SM_RadioStats.radio_interfaces[i].rxQuality > iMaxRxQuality )
         iMaxRxQuality = g_SM_RadioStats.radio_interfaces[i].rxQuality;
        
   if ( g_SM_VideoLinkGraphs.vehicleRXQuality[0] == 255 || (iMaxRxQuality < g_SM_VideoLinkGraphs.vehicleRXQuality[0]) )
      g_SM_VideoLinkGraphs.vehicleRXQuality[0] = iMaxRxQuality;


   if ( g_TimeNow >= g_TimeLastPacketsOutPerSecCalculation + 500 )
   {
      g_TimeLastPacketsOutPerSecCalculation = g_TimeNow;
      s_countTXVideoPacketsOutPerSec[1] = s_countTXVideoPacketsOutPerSec[0] = 0;
      s_countTXDataPacketsOutPerSec[1] = s_countTXDataPacketsOutPerSec[0] = 0;
      s_countTXCompactedPacketsOutPerSec[1] = s_countTXCompactedPacketsOutPerSec[0] = 0;

      s_countTXVideoPacketsOutPerSec[0] = s_countTXVideoPacketsOutTemp;
      s_countTXDataPacketsOutPerSec[0] = s_countTXDataPacketsOutTemp;
      s_countTXCompactedPacketsOutPerSec[0] = s_countTXCompactedPacketsOutTemp;

      s_countTXVideoPacketsOutTemp = 0;
      s_countTXDataPacketsOutTemp = 0;
      s_countTXCompactedPacketsOutTemp = 0;

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
         radio_packet_init(&PH, PACKET_COMPONENT_RUBY, PACKET_TYPE_SIK_CONFIG, STREAM_ID_DATA);
         PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
         PH.vehicle_id_dest = g_uControllerId;
         PH.total_length = sizeof(t_packet_header) + strlen(szBuff)+3*sizeof(u8);

         u8 uCommandId = 0;

         u8 packet[MAX_PACKET_TOTAL_SIZE];
         memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
         memcpy(packet+sizeof(t_packet_header), &g_uGetSiKConfigAsyncVehicleLinkIndex, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + sizeof(u8), &uCommandId, sizeof(u8));
         memcpy(packet+sizeof(t_packet_header) + 2*sizeof(u8), szBuff, strlen(szBuff)+1);
         packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);

         log_line("Send back to radio Sik current config for vehicle radio link %d", (int)g_uGetSiKConfigAsyncVehicleLinkIndex+1);
         g_iGetSiKConfigAsyncResult = 0;
      }
   }


   if ( g_TimeNow >= g_TimeLastDebugFPSComputeTime + 1000 )
   {
      if( access( FILE_TMP_REINIT_RADIO_REQUEST, R_OK ) != -1 )
      {
         log_line("Received signal to reinitialize the radio modules.");
         reinit_radio_interfaces();
         return 1;
      }

      if ( NULL != g_pProcessStats )
         g_pProcessStats->lastActiveTime = g_TimeNow;

      //log_line("Loop FPS: %d", s_debugFramesCount);
      g_TimeLastDebugFPSComputeTime = g_TimeNow;
      s_debugFramesCount = 0;


      if (( g_TimeNow > g_TimeStart+50000 ) || g_bReceivedPairingRequest )
      {
          if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->audio_params.has_audio_device) && (g_pCurrentModel->audio_params.enabled) )
          if ( ! g_pProcessorTxAudio->isAudioStreamOpened() )
          {
             vehicle_launch_audio_capture(g_pCurrentModel);
             g_pProcessorTxAudio->openAudioStream();
          }
      }

      s_MinVideoBlocksGapMilisec = 500/(1+s_debugVideoBlocksInCount);
      if ( s_debugVideoBlocksInCount >= 500 )
         s_MinVideoBlocksGapMilisec = 0;
      if ( s_MinVideoBlocksGapMilisec > 40 )
         s_MinVideoBlocksGapMilisec = 40;

      s_debugVideoBlocksInCount = 0;
      s_debugVideoBlocksOutCount = 0;
   }

   if ( s_bRadioReinitialized )
   {
      if ( g_TimeNow < 5000 || g_TimeNow < g_TimeRadioReinitialized+5000 )
      {
         if ( (g_TimeNow/100)%2 )
            send_radio_reinitialized_message();
      }
      else
      {
         s_bRadioReinitialized = false;
         g_TimeRadioReinitialized = 0;
      }
   }

   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_LIVE_LOG )
   if ( g_TimeNow > g_TimeLastLiveLogCheck + 100 )
   {
      g_TimeLastLiveLogCheck = g_TimeNow;
      FILE* fd = fopen(LOG_FILE_SYSTEM, "rb");
      if ( NULL != fd )
      {
         fseek(fd, 0, SEEK_END);
         long lSize = ftell(fd);
         if ( -1 == s_lLastLiveLogFileOffset )
            s_lLastLiveLogFileOffset = lSize;

         while ( lSize - s_lLastLiveLogFileOffset >= 100 )
         {
            fseek(fd, s_lLastLiveLogFileOffset, SEEK_SET);
            u8 buffer[1024];
            long lRead = fread(buffer, 1, 1023, fd);
            if ( lRead > 0 )
            {
               s_lLastLiveLogFileOffset += lRead;
               send_packet_vehicle_log(buffer, (int)lRead);
            }
            else
               break;
         }
         fclose(fd);
      }
   }

   if ( g_TimeNow >= g_TimeLastHistoryTxComputation + 50 )
   {
      g_TimeLastHistoryTxComputation = g_TimeNow;
      
      // Compute the averate tx gap

      g_PHVehicleTxStats.historyTxGapAvgMiliseconds[0] = 0xFF;
      if ( g_PHVehicleTxStats.tmp_uAverageTxCount > 1 )
         g_PHVehicleTxStats.historyTxGapAvgMiliseconds[0] = (g_PHVehicleTxStats.tmp_uAverageTxSum - g_PHVehicleTxStats.historyTxGapMaxMiliseconds[0])/(g_PHVehicleTxStats.tmp_uAverageTxCount-1);
      else if ( g_PHVehicleTxStats.tmp_uAverageTxCount == 1 )
         g_PHVehicleTxStats.historyTxGapAvgMiliseconds[0] = g_PHVehicleTxStats.historyTxGapMaxMiliseconds[0];

      // Compute average video packets interval        

      g_PHVehicleTxStats.historyVideoPacketsGapAvg[0] = 0xFF;
      if ( g_PHVehicleTxStats.tmp_uVideoIntervalsCount > 1 )
         g_PHVehicleTxStats.historyVideoPacketsGapAvg[0] = (g_PHVehicleTxStats.tmp_uVideoIntervalsSum - g_PHVehicleTxStats.historyVideoPacketsGapMax[0])/(g_PHVehicleTxStats.tmp_uVideoIntervalsCount-1);
      else if ( g_PHVehicleTxStats.tmp_uVideoIntervalsCount == 1 )
         g_PHVehicleTxStats.historyVideoPacketsGapAvg[0] = g_PHVehicleTxStats.historyVideoPacketsGapMax[0];
        

      if ( ! g_bVideoPaused )
      if ( g_pCurrentModel->bDeveloperMode )
      if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_SEND_BACK_VEHICLE_TX_GAP )
      {
         t_packet_header PH;
         radio_packet_init(&PH, PACKET_COMPONENT_TELEMETRY, PACKET_TYPE_RUBY_TELEMETRY_VEHICLE_TX_HISTORY, STREAM_ID_DATA);
         PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
         PH.vehicle_id_dest = 0;
         PH.total_length = sizeof(t_packet_header) + sizeof(t_packet_header_vehicle_tx_history);

         g_PHVehicleTxStats.iSliceInterval = 50;
         g_PHVehicleTxStats.uCountValues = MAX_HISTORY_VEHICLE_TX_STATS_SLICES;
         u8 packet[MAX_PACKET_TOTAL_SIZE];
         memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
         memcpy(packet + sizeof(t_packet_header), (u8*)&g_PHVehicleTxStats, sizeof(t_packet_header_vehicle_tx_history));
         packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);
      }

      for( int i=MAX_HISTORY_RADIO_STATS_RECV_SLICES-1; i>0; i-- )
      {
         g_PHVehicleTxStats.historyTxGapMaxMiliseconds[i] = g_PHVehicleTxStats.historyTxGapMaxMiliseconds[i-1];
         g_PHVehicleTxStats.historyTxGapMinMiliseconds[i] = g_PHVehicleTxStats.historyTxGapMinMiliseconds[i-1];
         g_PHVehicleTxStats.historyTxGapAvgMiliseconds[i] = g_PHVehicleTxStats.historyTxGapAvgMiliseconds[i-1];
         g_PHVehicleTxStats.historyTxPackets[i] = g_PHVehicleTxStats.historyTxPackets[i-1];
         g_PHVehicleTxStats.historyVideoPacketsGapMax[i] = g_PHVehicleTxStats.historyVideoPacketsGapMax[i-1];
         g_PHVehicleTxStats.historyVideoPacketsGapAvg[i] = g_PHVehicleTxStats.historyVideoPacketsGapAvg[i-1];
      }
      g_PHVehicleTxStats.historyTxGapMaxMiliseconds[0] = 0xFF;
      g_PHVehicleTxStats.historyTxGapMinMiliseconds[0] = 0xFF;
      g_PHVehicleTxStats.historyTxGapAvgMiliseconds[0] = 0xFF;
      g_PHVehicleTxStats.historyTxPackets[0] = 0;
      g_PHVehicleTxStats.historyVideoPacketsGapMax[0] = 0xFF;
      g_PHVehicleTxStats.historyVideoPacketsGapAvg[0] = 0xFF;

      g_PHVehicleTxStats.tmp_uAverageTxSum = 0;
      g_PHVehicleTxStats.tmp_uAverageTxCount = 0;
      g_PHVehicleTxStats.tmp_uVideoIntervalsSum = 0;
      g_PHVehicleTxStats.tmp_uVideoIntervalsCount = 0;
   }
  
   if ( ! g_bVideoPaused )
   //if ( g_pCurrentModel->bDeveloperMode )
   if ( g_pCurrentModel->osd_params.osd_flags3[g_pCurrentModel->osd_params.layout] & OSD_FLAG3_SHOW_VIDEO_BITRATE_HISTORY )
   if ( g_TimeNow >= g_SM_DevVideoBitrateHistory.uLastGraphSliceTime + g_SM_DevVideoBitrateHistory.uGraphSliceInterval )
   {
      g_SM_DevVideoBitrateHistory.uLastGraphSliceTime = g_TimeNow;
      
      g_SM_DevVideoBitrateHistory.uQuantizationOverflowValue = video_link_get_oveflow_quantization_value();
      g_SM_DevVideoBitrateHistory.uCurrentTargetVideoBitrate = g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate;

      for( int i=MAX_INTERVALS_VIDEO_BITRATE_HISTORY-1; i>0; i-- )
      {
         g_SM_DevVideoBitrateHistory.uHistMaxVideoDataRateMbps[i] = g_SM_DevVideoBitrateHistory.uHistMaxVideoDataRateMbps[i-1]; 
         g_SM_DevVideoBitrateHistory.uHistVideoQuantization[i] = g_SM_DevVideoBitrateHistory.uHistVideoQuantization[i-1]; 
         g_SM_DevVideoBitrateHistory.uHistVideoBitrateKb[i] = g_SM_DevVideoBitrateHistory.uHistVideoBitrateKb[i-1]; 
         g_SM_DevVideoBitrateHistory.uHistVideoBitrateAvgKb[i] = g_SM_DevVideoBitrateHistory.uHistVideoBitrateAvgKb[i-1]; 
         g_SM_DevVideoBitrateHistory.uHistTotalVideoBitrateAvgKb[i] = g_SM_DevVideoBitrateHistory.uHistTotalVideoBitrateAvgKb[i-1]; 
         g_SM_DevVideoBitrateHistory.uHistoryVideoSwitches[i] = g_SM_DevVideoBitrateHistory.uHistoryVideoSwitches[i-1]; 
      }
      g_SM_DevVideoBitrateHistory.uHistVideoQuantization[0] = g_SM_VideoLinkStats.overwrites.currentH264QUantization;
      g_SM_DevVideoBitrateHistory.uHistMaxVideoDataRateMbps[0] = get_last_tx_video_datarate_mbps();
      g_SM_DevVideoBitrateHistory.uHistVideoBitrateKb[0] = g_pProcessorTxVideo->getCurrentVideoBitrate()/1000;
      g_SM_DevVideoBitrateHistory.uHistVideoBitrateAvgKb[0] = g_pProcessorTxVideo->getCurrentVideoBitrateAverage()/1000;
      g_SM_DevVideoBitrateHistory.uHistTotalVideoBitrateAvgKb[0] = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverage()/1000;
      g_SM_DevVideoBitrateHistory.uHistoryVideoSwitches[0] = g_SM_VideoLinkStats.overwrites.currentProfileShiftLevel | (g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile<<4);
      if ( (0 == g_TimeStartRaspiVid) || (g_TimeNow < g_TimeStartRaspiVid + 3000) )
         g_SM_DevVideoBitrateHistory.uHistVideoQuantization[0] = 0xFF;

      t_packet_header PH;
      radio_packet_init(&PH, PACKET_COMPONENT_TELEMETRY, PACKET_TYPE_RUBY_TELEMETRY_DEV_VIDEO_BITRATE_HISTORY, STREAM_ID_DATA);
      PH.vehicle_id_src = g_pCurrentModel->vehicle_id;
      PH.vehicle_id_dest = 0;
      PH.total_length = sizeof(t_packet_header) + sizeof(shared_mem_dev_video_bitrate_history);

      u8 packet[MAX_PACKET_TOTAL_SIZE];
      memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
      memcpy(packet + sizeof(t_packet_header), (u8*)&g_SM_DevVideoBitrateHistory, sizeof(shared_mem_dev_video_bitrate_history));
      packets_queue_add_packet(&s_QueueRadioPacketsOut, packet);
   }

   // If relay params have changed and we have not processed the notification, do it after one second after the change
   if ( g_TimeLastNotificationRelayParamsChanged != 0 )
   if ( g_TimeNow >= g_TimeLastNotificationRelayParamsChanged+1000 )
   {
      relay_on_relay_params_changed();
      g_TimeLastNotificationRelayParamsChanged = 0;
   }

   return 0;
}


void _synchronize_shared_mems()
{
   if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_STATS_VIDEO_INFO)
   if ( g_TimeNow >= g_VideoInfoStats.uTimeLastUpdate + 200 )
   {
      update_shared_mem_video_info_stats( &g_VideoInfoStats, g_TimeNow);

      if ( NULL != g_pSM_VideoInfoStats )
         memcpy((u8*)g_pSM_VideoInfoStats, (u8*)&g_VideoInfoStats, sizeof(shared_mem_video_info_stats));
      else
      {
        g_pSM_VideoInfoStats = shared_mem_video_info_stats_open_for_write();
        if ( NULL == g_pSM_VideoInfoStats )
           log_error_and_alarm("Failed to open shared mem video info stats for write!");
        else
           log_line("Opened shared mem video info stats for write.");
      }
   }

   if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_STATS_VIDEO_INFO)
   if ( g_TimeNow >= g_VideoInfoStatsRadioOut.uTimeLastUpdate + 200 )
   {
      update_shared_mem_video_info_stats( &g_VideoInfoStatsRadioOut, g_TimeNow);

      if ( NULL != g_pSM_VideoInfoStatsRadioOut )
         memcpy((u8*)g_pSM_VideoInfoStatsRadioOut, (u8*)&g_VideoInfoStatsRadioOut, sizeof(shared_mem_video_info_stats));
      else
      {
        g_pSM_VideoInfoStatsRadioOut = shared_mem_video_info_stats_radio_out_open_for_write();
        if ( NULL == g_pSM_VideoInfoStatsRadioOut )
           log_error_and_alarm("Failed to open shared mem video info stats radio out for write!");
        else
           log_line("Opened shared mem video info stats radio out for write.");
      }
   }

   static u32 s_uTimeLastRxHistorySync = 0;

   if ( g_TimeNow >= s_uTimeLastRxHistorySync + 100 )
   {
      s_uTimeLastRxHistorySync = g_TimeNow;
      memcpy((u8*)g_pSM_HistoryRxStats, (u8*)&g_SM_HistoryRxStats, sizeof(shared_mem_radio_stats_rx_hist));
   }
}

void _check_router_state()
{
   if ( g_uRouterState & ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE )
   {
      if ( ! g_pCurrentModel->hasCamera() )
      {
         g_TimeToRestartVideoCapture = 0;
         g_uRouterState &= ~ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE;
         return;
      }

      if ( 0 == g_TimeToRestartVideoCapture )
      {
         log_line("Periodic loop: flag to restart video capture was set. Stop video capture.");
         vehicle_stop_video_capture(g_pCurrentModel); 

         if ( NULL != g_pSharedMemRaspiVidComm )
            munmap(g_pSharedMemRaspiVidComm, SIZE_OF_SHARED_MEM_RASPIVID_COMM);
         g_pSharedMemRaspiVidComm = NULL; 
         
         g_TimeNow = get_current_timestamp_ms();
         g_TimeToRestartVideoCapture = g_TimeNow + 50;
         if ( g_pCurrentModel->isActiveCameraHDMI() )
         {
            log_line("HDMI camera detected.");
            g_TimeToRestartVideoCapture = g_TimeNow + 1500;
         }
         log_line("Start video capture %u miliseconds from now.", g_TimeToRestartVideoCapture - g_TimeNow);
      }
      else if ( g_TimeNow > g_TimeToRestartVideoCapture )
      {
         vehicle_launch_video_capture(g_pCurrentModel, &(g_SM_VideoLinkStats.overwrites));
         vehicle_check_update_processes_affinities(true);
         send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_CAPTURE_RESTARTED,1,0, 5);

         log_line("Opening video commands pipe write endpoint...");
         g_pSharedMemRaspiVidComm = (u8*)open_shared_mem_for_write(SHARED_MEM_RASPIVIDEO_COMMAND, SIZE_OF_SHARED_MEM_RASPIVID_COMM);
         if ( NULL == g_pSharedMemRaspiVidComm )
            log_error_and_alarm("Failed to open video commands pipe write endpoint!");
         else
            log_line("Opened video commands pipe write endpoint."); 

         g_TimeToRestartVideoCapture = 0;
         g_uRouterState &= ~ROUTER_STATE_NEEDS_RESTART_VIDEO_CAPTURE;
      }
   }
}

void cleanUp()
{
   close_radio_interfaces();

   if ( NULL != g_pProcessorTxAudio )
      g_pProcessorTxAudio->closeAudioStream();

   if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->audio_params.has_audio_device) )
      vehicle_stop_audio_capture(g_pCurrentModel);

   if ( -1 != s_fInputVideoStream )
      close(s_fInputVideoStream);

   s_fInputVideoStream = -1;

   ruby_close_ipc_channel(s_fIPCRouterToCommands);
   ruby_close_ipc_channel(s_fIPCRouterFromCommands);
   ruby_close_ipc_channel(s_fIPCRouterToTelemetry);
   ruby_close_ipc_channel(s_fIPCRouterFromTelemetry);
   ruby_close_ipc_channel(s_fIPCRouterToRC);
   ruby_close_ipc_channel(s_fIPCRouterFromRC);

   s_fIPCRouterToCommands = -1;
   s_fIPCRouterFromCommands = -1;
   s_fIPCRouterToTelemetry = -1;
   s_fIPCRouterFromTelemetry = -1;
   s_fIPCRouterToRC = -1;
   s_fIPCRouterFromRC = -1;

   if ( NULL != g_pSharedMemRaspiVidComm )
      munmap(g_pSharedMemRaspiVidComm, SIZE_OF_SHARED_MEM_RASPIVID_COMM);
   g_pSharedMemRaspiVidComm = NULL;

   process_data_tx_video_uninit();
}
  
int router_open_pipes()
{
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   s_fIPCRouterFromRC = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_RC_TO_ROUTER);
   if ( s_fIPCRouterFromRC < 0 )
      return -1;

   s_fIPCRouterToRC = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_RC);
   if ( s_fIPCRouterToRC < 0 )
      return -1;

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   s_fIPCRouterToCommands = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_COMMANDS);
   if ( s_fIPCRouterToCommands < 0 )
   {
      cleanUp();
      return -1;
   }
   
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }
 
   s_fIPCRouterFromCommands = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_COMMANDS_TO_ROUTER);
   if ( s_fIPCRouterFromCommands < 0 )
   {
      cleanUp();
      return -1;
   }
   
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   s_fIPCRouterFromTelemetry = ruby_open_ipc_channel_read_endpoint(IPC_CHANNEL_TYPE_TELEMETRY_TO_ROUTER);
   if ( s_fIPCRouterFromTelemetry < 0 )
   {
      cleanUp();
      return -1;
   }

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   s_fIPCRouterToTelemetry = ruby_open_ipc_channel_write_endpoint(IPC_CHANNEL_TYPE_ROUTER_TO_TELEMETRY);
   if ( s_fIPCRouterToTelemetry < 0 )
   {
      cleanUp();
      return -1;
   }
   
   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms(); 
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   s_fInputVideoStream = -1;

   if ( g_pCurrentModel->hasCamera() )
   {
      log_line("Opening video input stream: %s", FIFO_RUBY_CAMERA1);
      s_fInputVideoStream = open( FIFO_RUBY_CAMERA1, O_RDONLY | RUBY_PIPES_EXTRA_FLAGS);
      if ( s_fInputVideoStream < 0 )
      {
         log_error_and_alarm("Failed to open video input stream: %s", FIFO_RUBY_CAMERA1);
         cleanUp();
         return -1;
      }
      log_line("Opened video input stream: %s", FIFO_RUBY_CAMERA1);
   }
   else
   {
      log_line("Vehicle with no camera. Do not try to read video stream.");
      s_fInputVideoStream = -1;
   }

   if ( -1 != s_fInputVideoStream )
      log_line("Pipe camera read end flags: %s", str_get_pipe_flags(fcntl(s_fInputVideoStream, F_GETFL)));
   return 0;
}

void _check_loop_consistency(int iStep, u32 uLastTotalTxPackets, u32 uLastTotalTxBytes, u32 tTime0, u32 tTime1, u32 tTime2, u32 tTime3, u32 tTime4, u32 tTime5)
{
   if ( g_TimeNow < g_TimeStart + 10000 )
      return;
   
   if ( g_TimeStartRaspiVid != 0 )
   if ( g_TimeNow < g_TimeStartRaspiVid + 4000 )
      return;

   if ( tTime5 > tTime0 + DEFAULT_MAX_LOOP_TIME_MILISECONDS )
   {
      uLastTotalTxPackets = g_SM_RadioStats.radio_links[0].totalTxPackets - uLastTotalTxPackets;
      uLastTotalTxBytes = g_SM_RadioStats.radio_links[0].totalTxBytes - uLastTotalTxBytes;

      s_iCountCPULoopOverflows++;
      if ( s_iCountCPULoopOverflows > 5 )
      if ( g_TimeNow > g_TimeLastSetRadioFlagsCommandReceived + 5000 )
      {
         log_line("Too many CPU loop overflows. Send alarm to controller (%d overflows, now overflow: %u ms)", s_iCountCPULoopOverflows, tTime5 - tTime0);
         send_alarm_to_controller(ALARM_ID_VEHICLE_CPU_LOOP_OVERLOAD,(tTime5-tTime0), 0, 2);
      }
      if ( tTime5 >= tTime0 + 500 )
      if ( g_TimeNow > g_TimeLastSetRadioFlagsCommandReceived + 5000 )
      {
         log_line("CPU loop overflow is too big (%d ms). Send alarm to controller", tTime5 - tTime0);
         send_alarm_to_controller(ALARM_ID_VEHICLE_CPU_LOOP_OVERLOAD,(tTime5-tTime0)<<16, 0, 2);
      }
      if ( g_TimeNow > s_uTimeLastLoopOverloadError + 3000 )
          s_LoopOverloadErrorCount = 0;
      s_uTimeLastLoopOverloadError = g_TimeNow;
      s_LoopOverloadErrorCount++;
      if ( (s_LoopOverloadErrorCount < 10) || ((s_LoopOverloadErrorCount%20)==0) )
         log_softerror_and_alarm("Router loop(%d) took too long to complete (%u milisec (%u + %u + %u + %u + %u ms)), sent %u packets, %u bytes!!!",
            iStep, tTime5 - tTime0, tTime1-tTime0, tTime2-tTime1, tTime3-tTime2, tTime4-tTime3, tTime5-tTime4, uLastTotalTxPackets, uLastTotalTxBytes);
   }
   else
      s_iCountCPULoopOverflows = 0;

   if ( NULL != g_pProcessStats )
   if ( g_TimeNow > g_TimeLastSetRadioFlagsCommandReceived + 5000 )
   {
      if ( g_pProcessStats->uMaxLoopTimeMs < tTime5 - tTime0 )
         g_pProcessStats->uMaxLoopTimeMs = tTime5 - tTime0;
      g_pProcessStats->uTotalLoopTime += tTime5 - tTime0;
      if ( 0 != g_pProcessStats->uLoopCounter )
         g_pProcessStats->uAverageLoopTimeMs = g_pProcessStats->uTotalLoopTime / g_pProcessStats->uLoopCounter;
   }
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
         reinit_radio_interfaces();
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
         send_alarm_to_controller(ALARM_ID_VEHICLE_RX_TIMEOUT,(u32)(iAnyRxTimeouts-1), (u32)iCount, 1);
      }
   }

   int iAnyRxErrors = radio_rx_any_interface_bad_packets();
   if ( iAnyRxErrors > 0 )
   {
      int iError = radio_rx_get_interface_bad_packets_error_and_reset(iAnyRxErrors-1);
      send_alarm_to_controller(ALARM_ID_RECEIVED_INVALID_RADIO_PACKET, (u32)iError, 0, 1);
   }

   u32 uMaxRxLoopTime = radio_rx_get_and_reset_max_loop_time();
   if ( (int)uMaxRxLoopTime > get_VehicleSettings()->iDevRxLoopTimeout )
   {
      static u32 s_uTimeLastAlarmRxLoopOverload = 0;
      if ( g_TimeNow > s_uTimeLastAlarmRxLoopOverload + 10000 )
      {
         s_uTimeLastAlarmRxLoopOverload = g_TimeNow;
         u32 uRead = radio_rx_get_and_reset_max_loop_time_read();
         u32 uQueue = radio_rx_get_and_reset_max_loop_time_queue();
         if ( uRead > 0xFFFF ) uRead = 0xFFFF;
         if ( uQueue > 0xFFFF ) uQueue = 0xFFFF;

         send_alarm_to_controller(ALARM_ID_CPU_RX_LOOP_OVERLOAD, uMaxRxLoopTime, uRead | (uQueue<<16), 1);
      }
   }
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

   if ( iReceivedAnyPackets <= 0 )
   if ( (NULL != g_pProcessStats) && (0 != g_pProcessStats->lastRadioRxTime) && (g_pProcessStats->lastRadioRxTime < g_TimeNow - TIMEOUT_LINK_TO_CONTROLLER_LOST) )
   {
      if ( g_TimeLastReceivedRadioPacketFromController < g_TimeNow - TIMEOUT_LINK_TO_CONTROLLER_LOST )
      if ( g_bHasLinkToController )
      {
         g_bHasLinkToController = false;
         if ( g_pCurrentModel->osd_params.osd_preferences[g_pCurrentModel->osd_params.layout] & OSD_PREFERENCES_BIT_FLAG_SHOW_CONTROLLER_LINK_LOST_ALARM )
            send_alarm_to_controller(ALARM_ID_LINK_TO_CONTROLLER_LOST, 0, 0, 5);
      }

      if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
      if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
      if ( (g_pCurrentModel->relay_params.uCurrentRelayMode & RELAY_MODE_REMOTE) ||
           (g_pCurrentModel->relay_params.uCurrentRelayMode & RELAY_MODE_PIP_MAIN) ||
           (g_pCurrentModel->relay_params.uCurrentRelayMode & RELAY_MODE_PIP_REMOTE) )
      {
         u32 uOldRelayMode = g_pCurrentModel->relay_params.uCurrentRelayMode;
         g_pCurrentModel->relay_params.uCurrentRelayMode = RELAY_MODE_MAIN | RELAY_MODE_IS_RELAY_NODE;
         saveCurrentModel();
         onEventRelayModeChanged(uOldRelayMode, g_pCurrentModel->relay_params.uCurrentRelayMode, "stop");
      }
   }
}

void _check_for_debug_raspi_messages()
{
   if ( g_TimeNow < s_uTimeLastCheckForRaspiDebugMessages + 2000 )
      return;
   s_uTimeLastCheckForRaspiDebugMessages = g_TimeNow;

   if( access( "tmp.cmd", R_OK ) == -1 )
      return;

   int iCommand = 0;
   int iParam = 0;

   hardware_sleep_ms(20);

   FILE* fd = fopen("tmp.cmd", "r");
   if ( NULL == fd )
      return;
   if ( 2 != fscanf(fd, "%d %d", &iCommand, &iParam) )
   {
      iCommand = 0;
      iParam = 0;
   } 

   fclose(fd);
   hw_execute_bash_command_silent("rm -rf tmp.cmd", NULL);

   if ( iCommand > 0 )
      send_control_message_to_raspivid((u8)iCommand, (u8)iParam);
}

void _check_free_storage_space()
{
   static u32 sl_uCountMemoryChecks = 0;
   static u32 sl_uTimeLastMemoryCheck = 0;

   if ( (0 == sl_uCountMemoryChecks && (g_TimeNow > g_TimeStart+2000)) || (g_TimeNow > sl_uTimeLastMemoryCheck + 60000) )
   {
      sl_uCountMemoryChecks++;
      sl_uTimeLastMemoryCheck = g_TimeNow;
      char szOutput[2048];
      if ( 1 == hw_execute_bash_command_raw("df -m /home/pi/ruby | grep root", szOutput) )
      {
         char szTemp[1024];
         long lb, lu, lMemoryFreeMb;
         sscanf(szOutput, "%s %ld %ld %ld", szTemp, &lb, &lu, &lMemoryFreeMb);
         if ( lMemoryFreeMb < 100 )
         {
            szOutput[0] = 0;
            hw_execute_bash_command_raw("du -h logs/", szOutput);
            for( int i=0; i<(int)strlen(szOutput); i++ )
            {
              if ( isspace(szOutput[i]) )
              {
                 szOutput[i] = 0;
                 break;
              }
            }
            u32 uLogSize = 0;
            int iSize = strlen(szOutput)-1;
            if ( (iSize > 0) && (! isdigit(szOutput[iSize])) )
            {
               if ( szOutput[iSize] == 'M' || szOutput[iSize] == 'm' )
               {
                  sscanf(szOutput, "%u", &uLogSize);
                  uLogSize *= 1000 * 1000;
               }
               if ( szOutput[iSize] == 'K' || szOutput[iSize] == 'k' )
               {
                  sscanf(szOutput, "%u", &uLogSize);
                  uLogSize *= 1000;
               }
            }
      
            send_alarm_to_controller(ALARM_ID_VEHICLE_LOW_STORAGE_SPACE, (u32)lMemoryFreeMb, uLogSize, 5);
         }
      }
   }
}

void _broadcast_router_ready()
{
   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_LOCAL_CONTROL, PACKET_TYPE_LOCAL_CONTROL_VEHICLE_ROUTER_READY, STREAM_ID_DATA);
   PH.vehicle_id_src = PACKET_COMPONENT_RUBY;
   PH.vehicle_id_dest = 0;
   PH.total_length = sizeof(t_packet_header);

   u8 buffer[MAX_PACKET_TOTAL_SIZE];
   memcpy(buffer, (u8*)&PH, sizeof(t_packet_header));
   
   if ( ! ruby_ipc_channel_send_message(s_fIPCRouterToTelemetry, buffer, PH.total_length) )
      log_softerror_and_alarm("No pipe to telemetry to broadcast router ready to.");

   log_line("Broadcasted that router is ready.");
} 

void handle_sigint(int sig) 
{ 
   log_line("--------------------------");
   log_line("Caught signal to stop: %d", sig);
   log_line("--------------------------");
   g_bQuit = true;
   radio_rx_mark_quit();
} 

int main (int argc, char *argv[])
{
   signal(SIGINT, handle_sigint);
   signal(SIGTERM, handle_sigint);
   signal(SIGQUIT, handle_sigint);

   if ( strcmp(argv[argc-1], "-ver") == 0 )
   {
      printf("%d.%d (b%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR/10, SYSTEM_SW_BUILD_NUMBER);
      return 0;
   }

   log_init("Router");

   load_VehicleSettings();

   VehicleSettings* pVS = get_VehicleSettings();
   if ( NULL != pVS )
      radio_rx_set_timeout_interval(pVS->iDevRxLoopTimeout);
   
   hardware_reload_serial_ports();

   hardware_enumerate_radio_interfaces(); 

   //init_radio_in_packets_state();

   reset_sik_state_info(&g_SiKRadiosState);

   g_pProcessStats = shared_mem_process_stats_open_write(SHARED_MEM_WATCHDOG_ROUTER_TX);
   if ( NULL == g_pProcessStats )
      log_softerror_and_alarm("Start sequence: Failed to open shared mem for router process watchdog for writing: %s", SHARED_MEM_WATCHDOG_ROUTER_TX);
   else
      log_line("Start sequence: Opened shared mem for router process watchdog for writing.");

   loadAllModels();
   g_pCurrentModel = getCurrentModel();

   if ( g_pCurrentModel->enc_flags != MODEL_ENC_FLAGS_NONE )
   {
      if ( ! lpp(NULL, 0) )
      {
         g_pCurrentModel->enc_flags = MODEL_ENC_FLAGS_NONE;
         saveCurrentModel();
      }
   }
  
   log_line("Start sequence: Loaded model. Developer flags: live log: %s, enable radio silence failsafe: %s, log only errors: %s, radio config guard interval: %d ms",
         (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_LIVE_LOG)?"yes":"no",
         (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_RADIO_SILENCE_FAILSAFE)?"yes":"no",
         (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_LOG_ONLY_ERRORS)?"yes":"no",
          (int)((g_pCurrentModel->uDeveloperFlags >> 8) & 0xFF) );
   log_line("Start sequence: Model has vehicle developer video link stats flag on: %s/%s", (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_ENABLE_VIDEO_LINK_STATS)?"yes":"no", (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_ENABLE_VIDEO_LINK_GRAPHS)?"yes":"no");


   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_LOG_ONLY_ERRORS )
      log_only_errors();

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   if ( -1 == router_open_pipes() )
      log_error_and_alarm("Start sequence: Failed to open some pipes.");
   
   u32 uRefreshIntervalMs = 100;
   switch ( g_pCurrentModel->m_iRadioInterfacesGraphRefreshInterval )
   {
      case 0: uRefreshIntervalMs = 10; break;
      case 1: uRefreshIntervalMs = 20; break;
      case 2: uRefreshIntervalMs = 50; break;
      case 3: uRefreshIntervalMs = 100; break;
      case 4: uRefreshIntervalMs = 200; break;
      case 5: uRefreshIntervalMs = 500; break;
   }
   radio_stats_reset(&g_SM_RadioStats, uRefreshIntervalMs);
   
   g_SM_RadioStats.countLocalRadioInterfaces = hardware_get_radio_interfaces_count();
   if ( NULL != g_pCurrentModel )
      g_SM_RadioStats.countVehicleRadioLinks = g_pCurrentModel->radioLinksParams.links_count;
   else
      g_SM_RadioStats.countVehicleRadioLinks = 1;

   g_SM_RadioStats.countLocalRadioLinks = g_SM_RadioStats.countVehicleRadioLinks;
   
   for( int i=0; i<g_SM_RadioStats.countLocalRadioLinks; i++ )
      g_SM_RadioStats.radio_links[i].matchingVehicleRadioLinkId = i;
     
   g_TimeToRestartVideoCapture = 0;

   if ( NULL != g_pCurrentModel )
      hw_set_priority_current_proc(g_pCurrentModel->niceRouter);

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   video_stats_overwrites_init();
  
   hardware_sleep_ms(50);
   radio_init_link_structures();
   radio_enable_crc_gen(1);
   fec_init();

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   log_line("Start sequence: Setting radio interface frequencies...");
   links_set_cards_frequencies_and_params(-1);
   log_line("Start sequence: Done setting radio interface frequencies.");

   if ( open_radio_interfaces() < 0 )
   {
      shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_ROUTER_TX, g_pProcessStats);
      radio_link_cleanup();
      return -1;
   }

   if ( NULL != g_pProcessStats )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
   }

   packets_queue_init(&s_QueueRadioPacketsOut);
   packets_queue_init(&s_QueueControlPackets);

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      memset(&g_UplinkInfoRxStats[i], 0, sizeof(type_uplink_rx_info_stats));
      g_UplinkInfoRxStats[i].lastReceivedDBM = 200;
      g_UplinkInfoRxStats[i].lastReceivedDataRate = 0;
      g_UplinkInfoRxStats[i].timeLastLogWrongRxPacket = 0;
   }
   relay_init_and_set_rx_info_stats(&(g_UplinkInfoRxStats[0]));

   s_countTXVideoPacketsOutPerSec[0] = s_countTXVideoPacketsOutPerSec[1] = 0;
   s_countTXDataPacketsOutPerSec[0] = s_countTXDataPacketsOutPerSec[1] = 0;
   s_countTXCompactedPacketsOutPerSec[0] = s_countTXCompactedPacketsOutPerSec[1] = 0;

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      g_InterfacesTxMiliSecTimePerSecond[i] = 0;
      g_InterfacesVideoTxMiliSecTimePerSecond[i] = 0;
   }

   for( int i=0; i<MAX_HISTORY_VEHICLE_TX_STATS_SLICES; i++ )
   {
      g_PHVehicleTxStats.historyTxGapMaxMiliseconds[i] = 0xFF;
      g_PHVehicleTxStats.historyTxGapMinMiliseconds[i] = 0xFF;
      g_PHVehicleTxStats.historyTxGapAvgMiliseconds[i] = 0xFF;
      g_PHVehicleTxStats.historyTxPackets[i] = 0;
      g_PHVehicleTxStats.historyVideoPacketsGapMax[i] = 0xFF;
      g_PHVehicleTxStats.historyVideoPacketsGapAvg[i] = 0xFF;
   }
   g_PHVehicleTxStats.tmp_uAverageTxSum = 0;
   g_PHVehicleTxStats.tmp_uAverageTxCount = 0;
   g_PHVehicleTxStats.tmp_uVideoIntervalsSum = 0;
   g_PHVehicleTxStats.tmp_uVideoIntervalsCount = 0;

   g_TimeLastHistoryTxComputation = get_current_timestamp_ms();
   g_TimeLastTxPacket = get_current_timestamp_ms();

   memset((u8*)&g_SM_DevVideoBitrateHistory, 0, sizeof(shared_mem_dev_video_bitrate_history));
   g_SM_DevVideoBitrateHistory.uGraphSliceInterval = 100;
   g_SM_DevVideoBitrateHistory.uSlices = MAX_INTERVALS_VIDEO_BITRATE_HISTORY;


   g_pSM_HistoryRxStats = shared_mem_radio_stats_rx_hist_open_for_write();

   if ( NULL == g_pSM_HistoryRxStats )
      log_softerror_and_alarm("Start sequence: Failed to open history radio rx stats shared memory for write.");
   else
      shared_mem_radio_stats_rx_hist_reset(&g_SM_HistoryRxStats);
  
   if ( g_pCurrentModel->hasCamera() )
      vehicle_launch_video_capture(g_pCurrentModel, &(g_SM_VideoLinkStats.overwrites));
   else
      log_line("Vehicle has no camera. Video capture not started.");

   g_pProcessorTxVideo = new ProcessorTxVideo(0,0);
         
   if ( NULL != g_pCurrentModel && g_pCurrentModel->hasCamera() && g_pCurrentModel->isActiveCameraCSICompatible() )
   {
      log_line("Start sequence: Opening video commands pipe write endpoint...");
      g_pSharedMemRaspiVidComm = (u8*)open_shared_mem_for_write(SHARED_MEM_RASPIVIDEO_COMMAND, SIZE_OF_SHARED_MEM_RASPIVID_COMM);
      if ( NULL == g_pSharedMemRaspiVidComm )
         log_error_and_alarm("Start sequence: Failed to open video commands pipe write endpoint!");
      else
         log_line("Start sequence: Opened video commands pipe write endpoint.");
   }

   g_pSM_VideoInfoStats = shared_mem_video_info_stats_open_for_write();
   if ( NULL == g_pSM_VideoInfoStats )
      log_error_and_alarm("Start sequence: Failed to open shared mem video info stats for write!");
   else
      log_line("Start sequence: Opened shared mem video info stats for write.");

   g_pSM_VideoInfoStatsRadioOut = shared_mem_video_info_stats_radio_out_open_for_write();
   if ( NULL == g_pSM_VideoInfoStatsRadioOut )
      log_error_and_alarm("Start sequence: Failed to open shared mem video info stats radio out for write!");
   else
      log_line("Start sequence: Opened shared mem video info stats radio out for write.");

   g_pProcessorTxAudio = new ProcessorTxAudio();

   radio_duplicate_detection_init();
   radio_rx_start_rx_thread(&g_SM_RadioStats, NULL, 0);
   
   g_uRouterState = ROUTER_STATE_RUNNING;

   log_line("");
   log_line("");
   log_line("----------------------------------------------"); 
   log_line("         Started all ok. Running now."); 
   log_line("----------------------------------------------"); 
   log_line("");
   log_line("");

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      
      if ( pRadioHWInfo->openedForRead && pRadioHWInfo->openedForWrite )
         log_line(" * Interface %d: %s, %s, %s opened for read/write, radio link %d", i+1, pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), g_pCurrentModel->radioInterfacesParams.interface_link_id[i]+1 );
      else if ( pRadioHWInfo->openedForRead )
         log_line(" * Interface %d: %s, %s, %s opened for read, radio link %d", i+1, pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), g_pCurrentModel->radioInterfacesParams.interface_link_id[i]+1 );
      else if ( pRadioHWInfo->openedForWrite )
         log_line(" * Interface %d: %s, %s, %s opened for write, radio link %d", i+1, pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), g_pCurrentModel->radioInterfacesParams.interface_link_id[i]+1 );
      else
         log_line(" * Interface %d: %s, %s, %s not used, radio link %d", i+1, pRadioHWInfo->szName, pRadioHWInfo->szDriver, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz), g_pCurrentModel->radioInterfacesParams.interface_link_id[i]+1 );
   }

   g_TimeNow = get_current_timestamp_ms();
   g_TimeStart = get_current_timestamp_ms(); 

   if ( NULL != g_pProcessStats )
   {
      g_pProcessStats->lastActiveTime = g_TimeNow;
      g_pProcessStats->lastRadioTxTime = g_TimeNow;
      g_pProcessStats->lastRadioRxTime = g_TimeNow;
      g_pProcessStats->lastIPCIncomingTime = g_TimeNow;
      g_pProcessStats->lastIPCOutgoingTime = g_TimeNow;
   }

   u32 uLastTotalTxPackets = 0;
   u32 uLastTotalTxBytes = 0;
   
   if ( g_pCurrentModel->hasCamera() )
   {
      if ( 0 != pthread_create(&s_pThreadWatchDogVideoCapture, NULL, &_thread_watchdog_video_capture, NULL) )
         log_softerror_and_alarm("[Router] Failed to create thread for watchdog.");
      else
         log_line("[Router] Created thread for watchdog.");
   }
   
   _broadcast_router_ready();


   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      if ( pRadioHWInfo->uExtraFlags & RADIO_HW_EXTRA_FLAG_FIRMWARE_OLD )
         send_alarm_to_controller(ALARM_ID_FIRMWARE_OLD, i, 0, 5);
   }

   //u32 uLoopMicroInterval = 1000;

   while ( !g_bQuit )
   {
      hardware_sleep_ms(1);
      g_TimeNow = get_current_timestamp_ms();
      u32 tTime0 = g_TimeNow;

      if ( NULL != g_pProcessStats )
      {
         g_pProcessStats->uLoopCounter++;
         g_pProcessStats->lastActiveTime = g_TimeNow;
      }
      
      _check_for_debug_raspi_messages();
      _check_free_storage_space();

      uLastTotalTxPackets = g_SM_RadioStats.radio_links[0].totalTxPackets;
      uLastTotalTxBytes = g_SM_RadioStats.radio_links[0].totalTxBytes;

      process_data_tx_video_loop();

      if ( g_bQuit )
         break;

      u32 tTime1 = get_current_timestamp_ms();

      // Try to recv packets for max uEndRecvTimeMicro microsec.
      /*
      int readResult = 0;
      u32 uStartRecvTimeMicro = get_current_timestamp_micros();
      u32 uEndRecvTimeMicro = uStartRecvTimeMicro + uLoopMicroInterval;
      while ( uStartRecvTimeMicro < uEndRecvTimeMicro )
      {
         readResult = try_receive_radio_packets(uEndRecvTimeMicro-uStartRecvTimeMicro);
         if ( readResult < 0 || g_bQuit )
            break; 

         if( readResult > 0 )
            process_received_radio_packets();
         u32 t = get_current_timestamp_micros();
         if ( t < uStartRecvTimeMicro )
            break;
         uStartRecvTimeMicro = t;
      }
      if ( readResult < 0 || g_bQuit )
         break;
      */

      _check_rx_loop_consistency();
      _consume_radio_rx_packets();

      u32 tTime2 = get_current_timestamp_ms();

      _check_router_state();

      if ( periodic_loop() )
         break;

      u32 tTimeD1 = get_current_timestamp_ms();

      video_stats_overwrites_periodic_loop();

      u32 tTimeD2 = get_current_timestamp_ms();

      _synchronize_shared_mems();

      u32 tTimeD3 = get_current_timestamp_ms();

      _read_ipc_pipes(tTimeD3);

      u32 tTimeD4 = get_current_timestamp_ms();

      _consume_ipc_messages();

      u32 tTimeD5 = get_current_timestamp_ms();

      send_pending_alarms_to_controller();

      if ( NULL != g_pProcessorTxAudio )
         g_pProcessorTxAudio->tryReadAudioInputStream();

      u32 tTimeD6 = get_current_timestamp_ms();

      int iReadCameraBytes = 0;
      int iReadCameraCount = 0;
      do
      {
         iReadCameraCount++;
         if ( g_pCurrentModel->hasCamera() )
            iReadCameraBytes = try_read_video_input(false);
         if ( -1 == iReadCameraBytes )
            log_softerror_and_alarm("Failed to read camera stream.");
         if ( iReadCameraBytes < 100 )
            break;
      } while ( (iReadCameraBytes > 0) && (iReadCameraCount < 5) );

      u32 tTime3 = get_current_timestamp_ms();

      if ( tTime3 > (tTime2 + DEFAULT_MAX_LOOP_TIME_MILISECONDS) )
         log_softerror_and_alarm("Router loop (inner part) too long (%u ms): %u + %u + %u + %u + %u + %u + %u",
          tTime3-tTime2, 
          tTimeD1 - tTime2, tTimeD2 - tTimeD1, tTimeD3 - tTimeD2, tTimeD4 - tTimeD3,
          tTimeD5 - tTimeD4, tTimeD6 - tTimeD5, tTime3 - tTimeD6);
      int videoPacketsReadyToSend = process_data_tx_video_has_packets_ready_to_send();
      int hasVideoBlockReadyToSendIndex = process_data_tx_video_has_block_ready_to_send();

      bool bSendPacketsNow = false;

      if ( g_bVideoPaused || (! g_pCurrentModel->hasCamera()) )
         bSendPacketsNow = true;

      if ( 0 != g_uTimeLastCommandSowftwareUpload )
      if ( g_TimeNow < g_uTimeLastCommandSowftwareUpload + 2000 )
         bSendPacketsNow = true;

      if ( g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_BASIC )
      if ( videoPacketsReadyToSend > 0 )
         bSendPacketsNow = true;

      if ( g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_ADV )
      if ( -1 != hasVideoBlockReadyToSendIndex )
         bSendPacketsNow = true;

      if ( packets_queue_has_packets(&s_QueueRadioPacketsOut) )
      if ( (s_QueueRadioPacketsOut.timeFirstPacket < g_TimeNow-100) || (g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_NONE) )
         bSendPacketsNow = true;

      if ( bSendPacketsNow )
         process_and_send_packets();

      if ( NULL != g_pProcessorTxAudio )
         g_pProcessorTxAudio->sendAudioPackets();

      u32 tTime4 = get_current_timestamp_ms();

      if ( g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_NONE ||
           g_pCurrentModel->rxtx_sync_type == RXTX_SYNC_TYPE_BASIC )
      {
         if ( videoPacketsReadyToSend > 0 )
            process_data_tx_video_send_packets_ready_to_send(videoPacketsReadyToSend);

         u32 tTime5 = get_current_timestamp_ms();
         _check_loop_consistency(0, uLastTotalTxPackets, uLastTotalTxBytes, tTime0,tTime1, tTime2, tTime3, tTime4, tTime5);
         continue;
      }

      if ( -1 != hasVideoBlockReadyToSendIndex || g_TimeLastVideoBlockSent <= g_TimeNow - s_MinVideoBlocksGapMilisec )
      {         
         int countBlocksPendingToSend = process_data_tx_video_get_pending_blocks_to_send_count();
         while ( countBlocksPendingToSend > 0 )
         {
            process_data_tx_video_send_first_complete_block(countBlocksPendingToSend==1);
            g_TimeLastVideoBlockSent = get_current_timestamp_ms();
            s_debugVideoBlocksOutCount++;
            countBlocksPendingToSend--;
         }
      }

      u32 tTime5 = get_current_timestamp_ms();
      _check_loop_consistency(1, uLastTotalTxPackets, uLastTotalTxBytes, tTime0,tTime1, tTime2, tTime3, tTime4, tTime5); 
   }

   log_line("Stopping...");
   if ( g_pCurrentModel->hasCamera() )
      pthread_cancel(s_pThreadWatchDogVideoCapture);

   radio_rx_stop_rx_thread();
   radio_link_cleanup();

   cleanUp();
   delete g_pProcessorTxVideo;
   shared_mem_radio_stats_rx_hist_close(g_pSM_HistoryRxStats);
   shared_mem_video_info_stats_close(g_pSM_VideoInfoStats);
   shared_mem_video_info_stats_radio_out_close(g_pSM_VideoInfoStatsRadioOut);
   shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_ROUTER_TX, g_pProcessStats);
   log_line("Stopped.Exit now. (PID %d)", getpid());
   log_line("---------------------\n");
   return 0;
}

