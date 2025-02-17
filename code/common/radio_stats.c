/*
You can use this C/C++ code however you wish (for example, but not limited to:
     as is, or by modifying it, or by adding new code, or by removing parts of the code;
     in public or private projects, in free or commercial products) 
     only if you get a priori written consent from Petru Soroaga (petrusoroaga@yahoo.com) for your specific use
     and only if this copyright terms are preserved in the code.
     This code is public for learning and academic purposes.
Also, check the licences folder for additional licences terms.
Code written by: Petru Soroaga, 2021-2023
*/

#include "../base/base.h"
#include "../base/config.h"
#include "../base/hardware.h"
#include "../common/string_utils.h"
#include "../radio/radioflags.h"
#include "../radio/radiopackets2.h"
#include "../radio/radiopackets_short.h"
#include "../radio/radio_duplicate_det.h"
#include "radio_stats.h"

static u32 s_uControllerLinkStats_tmpRecv[MAX_RADIO_INTERFACES];
static u32 s_uControllerLinkStats_tmpRecvBad[MAX_RADIO_INTERFACES];
static u32 s_uControllerLinkStats_tmpRecvLost[MAX_RADIO_INTERFACES];

#define MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY 5
static u32 s_uLastLinkRTDelayValues[MAX_RADIO_INTERFACES][MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY];
static u32 s_uLastCommandsRTDelayValues[5];

static u32 s_uLastTimeDebugPacketRecvOnNoLink = 0;
static int s_iRadioStatsEnableHistoryMonitor = 0;


void shared_mem_radio_stats_rx_hist_reset(shared_mem_radio_stats_rx_hist* pStats)
{
   if ( NULL == pStats )
      return;
   for(int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      for( int k=0; k<MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES; k++ )
      {
         pStats->interfaces_history[i].uHistPacketsTypes[k] = 0;
         pStats->interfaces_history[i].uHistPacketsCount[k] = 0;
      }
      pStats->interfaces_history[i].iCurrentSlice = 0;
      pStats->interfaces_history[i].uTimeLastUpdate = 0;
   }
}

void shared_mem_radio_stats_rx_hist_update(shared_mem_radio_stats_rx_hist* pStats, int iInterfaceIndex, u8* pPacket, u32 uTimeNow)
{
   if ( (NULL == pStats) || (NULL == pPacket) )
      return;
   if ( (iInterfaceIndex < 0) || (iInterfaceIndex >= MAX_RADIO_INTERFACES) )
      return;

   t_packet_header* pPH = (t_packet_header*)pPacket;
   
   // Ignore video packets
   if ( pPH->packet_type == PACKET_TYPE_VIDEO_DATA_FULL )
      return;

   // First ever packet ?

   if ( pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate == 0 )
   if ( pStats->interfaces_history[iInterfaceIndex].iCurrentSlice == 0 )
   if ( pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[0] == 0 )
   {
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[0] = pPH->packet_type;
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[0] = 1;
      pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate = uTimeNow;
      return;
   }

   int iCurrentIndex = pStats->interfaces_history[iInterfaceIndex].iCurrentSlice;
   
   // Different second?

   if ( (u32)(pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate/1000) != (u32)(uTimeNow/1000) )
   {
      iCurrentIndex++;
      if ( iCurrentIndex >= MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES )
         iCurrentIndex = 0;

      pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iCurrentIndex] = 0xFF;
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex] = 0xFF;
      
      iCurrentIndex++;
      if ( iCurrentIndex >= MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES )
         iCurrentIndex = 0;

      pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iCurrentIndex] = pPH->packet_type;
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex] = 1;
      pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate = uTimeNow;
      pStats->interfaces_history[iInterfaceIndex].iCurrentSlice = iCurrentIndex;
      return;
   }

   // Different packet type from last one
   
   if ( pPH->packet_type != pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iCurrentIndex] )
   {
      iCurrentIndex++;
      if ( iCurrentIndex >= MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES )
         iCurrentIndex = 0;

      pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iCurrentIndex] = pPH->packet_type;
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex] = 1;
      pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate = uTimeNow;
      pStats->interfaces_history[iInterfaceIndex].iCurrentSlice = iCurrentIndex;
      return;
   }

   // Now it's the same packet type as the last one

   int iPrevIndex = iCurrentIndex;
   iPrevIndex--;
   if ( iPrevIndex < 0 )
      iPrevIndex = MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES - 1;

   // Add it if it's only the first time that it's duplicate

   if ( pPH->packet_type != pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iPrevIndex] )
   {
      iCurrentIndex++;
      if ( iCurrentIndex >= MAX_RADIO_STATS_INTERFACE_RX_HISTORY_SLICES )
         iCurrentIndex = 0;

      pStats->interfaces_history[iInterfaceIndex].uHistPacketsTypes[iCurrentIndex] = pPH->packet_type;
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex] = 1;
      pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate = uTimeNow;
      pStats->interfaces_history[iInterfaceIndex].iCurrentSlice = iCurrentIndex;
      return;
   }

   // Same packet as lasts ones, just increase the counter

   if ( pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex] < 255 )
      pStats->interfaces_history[iInterfaceIndex].uHistPacketsCount[iCurrentIndex]++;
   pStats->interfaces_history[iInterfaceIndex].uTimeLastUpdate = uTimeNow;
}


void radio_stats_interfaces_rx_graph_reset(shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, int iGraphSliceDuration)
{
   if ( NULL == pSMRXStats )
      return;

   memset((u8*)pSMRXStats, 0, sizeof(shared_mem_radio_stats_interfaces_rx_graph));

   pSMRXStats->iCurrentSlice = 0;
   pSMRXStats->uTimeSliceDurationMs = iGraphSliceDuration;
   pSMRXStats->uTimeStartCurrentSlice = 0;
}

void radio_stats_reset(shared_mem_radio_stats* pSMRS, int graphRefreshInterval)
{
   if ( NULL == pSMRS )
      return;

   for( int iLink=0; iLink<MAX_RADIO_INTERFACES; iLink++ )
   for( int i=0; i<MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY; i++ )
      s_uLastLinkRTDelayValues[iLink][i] = 1000;

   for( int i=0; i<sizeof(s_uLastCommandsRTDelayValues)/sizeof(s_uLastCommandsRTDelayValues[0]); i++ )
      s_uLastCommandsRTDelayValues[i] = 1000;

   pSMRS->refreshIntervalMs = 350;
   pSMRS->graphRefreshIntervalMs = graphRefreshInterval;

   pSMRS->countLocalRadioLinks = 0;
   pSMRS->countVehicleRadioLinks = 0;
   pSMRS->countLocalRadioInterfaces = 0;
   
   pSMRS->lastComputeTime = 0;
   pSMRS->lastComputeTimeGraph = 0;

   pSMRS->all_downlinks_tx_time_per_sec = 0;
   pSMRS->tmp_all_downlinks_tx_time_per_sec = 0;

   pSMRS->uAverageCommandRoundtripMiliseconds = MAX_U32;
   pSMRS->uMaxCommandRoundtripMiliseconds = MAX_U32;
   pSMRS->uMinCommandRoundtripMiliseconds = MAX_U32;

   pSMRS->iMaxRxQuality = 0;

   // Init streams

   for( int k=0; k<MAX_CONCURENT_VEHICLES; k++)
   for( int i=0; i<MAX_RADIO_STREAMS; i++ )
   {
      pSMRS->radio_streams[k][i].uVehicleId = 0;
      pSMRS->radio_streams[k][i].totalRxBytes = 0;
      pSMRS->radio_streams[k][i].totalTxBytes = 0;
      pSMRS->radio_streams[k][i].rxBytesPerSec = 0;
      pSMRS->radio_streams[k][i].txBytesPerSec = 0;
      pSMRS->radio_streams[k][i].totalRxPackets = 0;
      pSMRS->radio_streams[k][i].totalTxPackets = 0;
      pSMRS->radio_streams[k][i].totalLostPackets = 0;
      pSMRS->radio_streams[k][i].rxPacketsPerSec = 0;
      pSMRS->radio_streams[k][i].txPacketsPerSec = 0;
      pSMRS->radio_streams[k][i].timeLastRxPacket = 0;
      pSMRS->radio_streams[k][i].timeLastTxPacket = 0;

      pSMRS->radio_streams[k][i].tmpRxBytes = 0;
      pSMRS->radio_streams[k][i].tmpTxBytes = 0;
      pSMRS->radio_streams[k][i].tmpRxPackets = 0;
      pSMRS->radio_streams[k][i].tmpTxPackets = 0;
   }

   // Init radio interfaces

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      pSMRS->radio_interfaces[i].assignedLocalRadioLinkId = -1;
      pSMRS->radio_interfaces[i].assignedVehicleRadioLinkId = -1;
      pSMRS->radio_interfaces[i].lastDbm = 200;
      pSMRS->radio_interfaces[i].lastDbmVideo = 200;
      pSMRS->radio_interfaces[i].lastDbmData = 200;
      pSMRS->radio_interfaces[i].lastDataRate = 0;
      pSMRS->radio_interfaces[i].lastDataRateVideo = 0;
      pSMRS->radio_interfaces[i].lastDataRateData = 0;
      pSMRS->radio_interfaces[i].lastReceivedRadioLinkPacketIndex = MAX_U32;

      pSMRS->radio_interfaces[i].totalRxBytes = 0;
      pSMRS->radio_interfaces[i].totalTxBytes = 0;
      pSMRS->radio_interfaces[i].rxBytesPerSec = 0;
      pSMRS->radio_interfaces[i].txBytesPerSec = 0;
      pSMRS->radio_interfaces[i].totalRxPackets = 0;
      pSMRS->radio_interfaces[i].totalRxPacketsBad = 0;
      pSMRS->radio_interfaces[i].totalRxPacketsLost = 0;
      pSMRS->radio_interfaces[i].totalTxPackets = 0;
      pSMRS->radio_interfaces[i].rxPacketsPerSec = 0;
      pSMRS->radio_interfaces[i].txPacketsPerSec = 0;
      pSMRS->radio_interfaces[i].timeLastRxPacket = 0;
      pSMRS->radio_interfaces[i].timeLastTxPacket = 0;

      pSMRS->radio_interfaces[i].tmpRxBytes = 0;
      pSMRS->radio_interfaces[i].tmpTxBytes = 0;
      pSMRS->radio_interfaces[i].tmpRxPackets = 0;
      pSMRS->radio_interfaces[i].tmpTxPackets = 0;

      pSMRS->radio_interfaces[i].rxQuality = 0;
      pSMRS->radio_interfaces[i].rxRelativeQuality = 0;

      pSMRS->radio_interfaces[i].uSlicesUpdated = 0;
      for( int k=0; k<MAX_HISTORY_RADIO_STATS_RECV_SLICES; k++ )
      {
         pSMRS->radio_interfaces[i].hist_rxPacketsCount[k] = 0;
         pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[k] = 0;
         pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[k] = 0;
         pSMRS->radio_interfaces[i].hist_rxGapMiliseconds[k] = 0xFF;
      }
      pSMRS->radio_interfaces[i].hist_tmp_rxPacketsCount = 0;
      pSMRS->radio_interfaces[i].hist_tmp_rxPacketsBadCount = 0;
      pSMRS->radio_interfaces[i].hist_tmp_rxPacketsLostCount = 0;
   }

   // Init radio links

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      pSMRS->radio_links[i].matchingVehicleRadioLinkId = -1;
      pSMRS->radio_links[i].refreshIntervalMs = 500;
      pSMRS->radio_links[i].lastComputeTimePerSec = 0;
      pSMRS->radio_links[i].totalRxBytes = 0;
      pSMRS->radio_links[i].totalTxBytes = 0;
      pSMRS->radio_links[i].rxBytesPerSec = 0;
      pSMRS->radio_links[i].txBytesPerSec = 0;
      pSMRS->radio_links[i].totalRxPackets = 0;
      pSMRS->radio_links[i].totalTxPackets = 0;
      pSMRS->radio_links[i].rxPacketsPerSec = 0;
      pSMRS->radio_links[i].txPacketsPerSec = 0;
      pSMRS->radio_links[i].totalTxPacketsUncompressed = 0;
      pSMRS->radio_links[i].txUncompressedPacketsPerSec = 0;

      pSMRS->radio_links[i].timeLastRxPacket = 0;
      pSMRS->radio_links[i].timeLastTxPacket = 0;

      pSMRS->radio_links[i].tmpRxBytes = 0;
      pSMRS->radio_links[i].tmpTxBytes = 0;
      pSMRS->radio_links[i].tmpRxPackets = 0;
      pSMRS->radio_links[i].tmpTxPackets = 0;
      pSMRS->radio_links[i].tmpUncompressedTxPackets = 0;

      pSMRS->radio_links[i].linkDelayRoundtripMsLastTime = 0;
      pSMRS->radio_links[i].linkDelayRoundtripMs = MAX_U32;
      pSMRS->radio_links[i].linkDelayRoundtripMinimMs = MAX_U32;
      pSMRS->radio_links[i].lastTxInterfaceIndex = -1;
      pSMRS->radio_links[i].downlink_tx_time_per_sec = 0;
      pSMRS->radio_links[i].tmp_downlink_tx_time_per_sec = 0;
   }

   radio_duplicate_detection_remove_data_for_all_except(0);

   log_line("[RadioStats] Reset radio stats: %d ms refresh interval; %d ms refresh graph interval, total radio stats size: %d bytes", pSMRS->refreshIntervalMs, pSMRS->graphRefreshIntervalMs, sizeof(shared_mem_radio_stats));
}

void radio_stats_set_graph_refresh_interval(shared_mem_radio_stats* pSMRS, int graphRefreshInterval)
{
   if ( NULL == pSMRS )
      return;
   pSMRS->graphRefreshIntervalMs = graphRefreshInterval;
   log_line("[RadioStats] Set radio stats graph refresh interval: %d ms", pSMRS->graphRefreshIntervalMs);
}

void radio_stats_enable_history_monitor(int iEnable)
{
   s_iRadioStatsEnableHistoryMonitor = iEnable;
}

void radio_stats_log_info(shared_mem_radio_stats* pSMRS, u32 uTimeNow)
{
   static int sl_iEnableRadioStatsLog = 0;
   static u32 sl_uLastTimeLoggedRadioStats = 0;

   if ( 0 == sl_iEnableRadioStatsLog )
      return;

   if ( uTimeNow < sl_uLastTimeLoggedRadioStats + 5000 )
      return;

   sl_uLastTimeLoggedRadioStats = uTimeNow;

   char szBuff[512];
   char szBuff2[64];

   log_line("----------------------------------------");
   log_line("Radio RX stats (refresh at %d ms, graphs at %d ms), %d local radio interfaces, %d local radio links, %d vehicle radio links:", pSMRS->refreshIntervalMs, pSMRS->graphRefreshIntervalMs, pSMRS->countLocalRadioInterfaces, pSMRS->countLocalRadioLinks, pSMRS->countVehicleRadioLinks);
   
   for( int k=0; k<MAX_CONCURENT_VEHICLES; k++ )
   {
      if ( pSMRS->radio_streams[k][0].uVehicleId == 0 )
         continue;
      sprintf(szBuff, "Vehicle ID %u Streams total packets: ", pSMRS->radio_streams[k][0].uVehicleId);
      for( int i=0; i<MAX_RADIO_STREAMS; i++ )
      {
         sprintf(szBuff2, "%u, ", pSMRS->radio_streams[k][i].totalRxPackets);
         strcat(szBuff, szBuff2);
      }
      log_line(szBuff);
   }

   int iCountLinks = pSMRS->countLocalRadioLinks;
   strcpy(szBuff, "Radio Links current packets indexes: ");
   for( int i=0; i<iCountLinks; i++ )
   {
      sprintf(szBuff2, "%u, ", pSMRS->radio_links[i].totalRxPackets);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   strcpy(szBuff, "Radio Interf total recv/bad/lost packets: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%u/%u/%u, ", pSMRS->radio_interfaces[i].totalRxPackets, pSMRS->radio_interfaces[i].totalRxPacketsBad, pSMRS->radio_interfaces[i].totalRxPacketsLost );
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   strcpy(szBuff, "Radio Interf current packets indexes: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%u, ", pSMRS->radio_interfaces[i].totalRxPackets);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   strcpy(szBuff, "Radio Interf total rx bytes: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%u, ", pSMRS->radio_interfaces[i].totalRxBytes);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   strcpy(szBuff, "Radio Interf total rx bytes/sec: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%u, ", pSMRS->radio_interfaces[i].rxBytesPerSec);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   log_line("Radio Interfaces Max Rx Quality: %d%%", pSMRS->iMaxRxQuality);

   strcpy(szBuff, "Radio Interf current rx quality: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%d%%, ", pSMRS->radio_interfaces[i].rxQuality);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   strcpy(szBuff, "Radio Interf current relative rx quality: ");
   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      sprintf(szBuff2, "%d%%, ", pSMRS->radio_interfaces[i].rxRelativeQuality);
      strcat(szBuff, szBuff2);
   }
   log_line(szBuff);

   
   log_line( "Radio streams throughput (global):");
   for( int k=0; k<MAX_CONCURENT_VEHICLES; k++ )
   {
      szBuff[0] = 0;
      for( int i=0; i<MAX_RADIO_STREAMS; i++ )
      {
         if ( pSMRS->radio_streams[k][i].uVehicleId != 0 )
         if ( (0 < pSMRS->radio_streams[k][i].rxBytesPerSec) || (0 < pSMRS->radio_streams[k][i].rxPacketsPerSec) )
         {
            sprintf(szBuff2, "VID %u, Stream %d: %u bps / %u pckts/s, ", pSMRS->radio_streams[k][i].uVehicleId, i, pSMRS->radio_streams[k][i].rxBytesPerSec*8, pSMRS->radio_streams[k][i].rxPacketsPerSec);
            strcat(szBuff, szBuff2);
         }
      }
      if ( 0 != szBuff[0] )
         log_line(szBuff);
   }
}

void _radio_stats_update_kbps_values(shared_mem_radio_stats* pSMRS, u32 uDeltaTime)
{
   if ( NULL == pSMRS )
      return;

   // Update radio streams

   for( int k=0; k<MAX_CONCURENT_VEHICLES; k++ )
   {
      if ( 0 == pSMRS->radio_streams[k][0].uVehicleId )
         continue;
      for( int i=0; i<MAX_RADIO_STREAMS; i++ )
      {
         pSMRS->radio_streams[k][i].rxBytesPerSec = (pSMRS->radio_streams[k][i].tmpRxBytes * 1000) / uDeltaTime;
         pSMRS->radio_streams[k][i].txBytesPerSec = (pSMRS->radio_streams[k][i].tmpTxBytes * 1000) / uDeltaTime;
         pSMRS->radio_streams[k][i].tmpRxBytes = 0;
         pSMRS->radio_streams[k][i].tmpTxBytes = 0;

         pSMRS->radio_streams[k][i].rxPacketsPerSec = (pSMRS->radio_streams[k][i].tmpRxPackets * 1000) / uDeltaTime;
         pSMRS->radio_streams[k][i].txPacketsPerSec = (pSMRS->radio_streams[k][i].tmpTxPackets * 1000) / uDeltaTime;
         pSMRS->radio_streams[k][i].tmpRxPackets = 0;
         pSMRS->radio_streams[k][i].tmpTxPackets = 0;
      }
   }
   // Update radio interfaces

   for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
   {
      pSMRS->radio_interfaces[i].rxBytesPerSec = (pSMRS->radio_interfaces[i].tmpRxBytes * 1000) / uDeltaTime;
      pSMRS->radio_interfaces[i].txBytesPerSec = (pSMRS->radio_interfaces[i].tmpTxBytes * 1000) / uDeltaTime;
      pSMRS->radio_interfaces[i].tmpRxBytes = 0;
      pSMRS->radio_interfaces[i].tmpTxBytes = 0;

      pSMRS->radio_interfaces[i].rxPacketsPerSec = (pSMRS->radio_interfaces[i].tmpRxPackets * 1000) / uDeltaTime;
      pSMRS->radio_interfaces[i].txPacketsPerSec = (pSMRS->radio_interfaces[i].tmpTxPackets * 1000) / uDeltaTime;
      pSMRS->radio_interfaces[i].tmpRxPackets = 0;
      pSMRS->radio_interfaces[i].tmpTxPackets = 0;
   }

   pSMRS->all_downlinks_tx_time_per_sec = (pSMRS->tmp_all_downlinks_tx_time_per_sec * 1000) / uDeltaTime;
   pSMRS->tmp_all_downlinks_tx_time_per_sec = 0;
   // Transform from microsec to milisec
   pSMRS->all_downlinks_tx_time_per_sec /= 1000;
}

void _radio_stats_update_kbps_values_radio_links(shared_mem_radio_stats* pSMRS, int iRadioLinkId, u32 uDeltaTimeMs)
{
   if ( NULL == pSMRS )
      return;

   int iCountRadioLinks = pSMRS->countVehicleRadioLinks;
   if ( pSMRS->countLocalRadioLinks > iCountRadioLinks )
      iCountRadioLinks = pSMRS->countLocalRadioLinks;
   if ( (iRadioLinkId < 0) || (iRadioLinkId >= iCountRadioLinks) )
      return;

   // Update radio link kbps

   pSMRS->radio_links[iRadioLinkId].rxBytesPerSec = (pSMRS->radio_links[iRadioLinkId].tmpRxBytes * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].txBytesPerSec = (pSMRS->radio_links[iRadioLinkId].tmpTxBytes * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].tmpRxBytes = 0;
   pSMRS->radio_links[iRadioLinkId].tmpTxBytes = 0;

   pSMRS->radio_links[iRadioLinkId].rxPacketsPerSec = (pSMRS->radio_links[iRadioLinkId].tmpRxPackets * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].txPacketsPerSec = (pSMRS->radio_links[iRadioLinkId].tmpTxPackets * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].tmpRxPackets = 0;
   pSMRS->radio_links[iRadioLinkId].tmpTxPackets = 0;

   pSMRS->radio_links[iRadioLinkId].txUncompressedPacketsPerSec = (pSMRS->radio_links[iRadioLinkId].tmpUncompressedTxPackets * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].tmpUncompressedTxPackets = 0;

   pSMRS->radio_links[iRadioLinkId].downlink_tx_time_per_sec = (pSMRS->radio_links[iRadioLinkId].tmp_downlink_tx_time_per_sec * 1000) / uDeltaTimeMs;
   pSMRS->radio_links[iRadioLinkId].tmp_downlink_tx_time_per_sec = 0;

   // Transform from microsec to milisec
   pSMRS->radio_links[iRadioLinkId].downlink_tx_time_per_sec /= 1000;
}

// returns 1 if it was updated, 0 if unchanged

int radio_stats_periodic_update(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow)
{
   if ( NULL == pSMRS )
      return 0;
   int iReturn = 0;

   int iCountRadioLinks = pSMRS->countLocalRadioLinks;
   for( int i=0; i<iCountRadioLinks; i++ )
   {
      if ( (timeNow >= pSMRS->radio_links[i].lastComputeTimePerSec + 1000) || (timeNow < pSMRS->radio_links[i].lastComputeTimePerSec) )
      {
         u32 uDeltaTime = timeNow - pSMRS->radio_links[i].lastComputeTimePerSec;
         if ( timeNow < pSMRS->radio_links[i].lastComputeTimePerSec )
            uDeltaTime = 500;
         _radio_stats_update_kbps_values_radio_links(pSMRS, i, uDeltaTime );
         pSMRS->radio_links[i].lastComputeTimePerSec = timeNow;
         iReturn = 1;
      }
   }

   if ( (timeNow >= pSMRS->lastComputeTime + pSMRS->refreshIntervalMs) || (timeNow < pSMRS->lastComputeTime) )
   {
      pSMRS->lastComputeTime = timeNow;
      iReturn = 1;

      static u32 sl_uTimeLastUpdateRadioInterfaceskbpsValues = 0;

      if ( timeNow >= sl_uTimeLastUpdateRadioInterfaceskbpsValues + 500 )
      {
         u32 uDeltaTime = timeNow - sl_uTimeLastUpdateRadioInterfaceskbpsValues;
         sl_uTimeLastUpdateRadioInterfaceskbpsValues = timeNow;
         _radio_stats_update_kbps_values(pSMRS, uDeltaTime);
      }
  
      // Update RX quality for each radio interface

      int iIntervalsToUse = 2000 / pSMRS->graphRefreshIntervalMs;
      if ( iIntervalsToUse < 3 )
         iIntervalsToUse = 3;
      if ( iIntervalsToUse >= sizeof(pSMRS->radio_interfaces[0].hist_rxPacketsCount)/sizeof(pSMRS->radio_interfaces[0].hist_rxPacketsCount[0]) )
         iIntervalsToUse = sizeof(pSMRS->radio_interfaces[0].hist_rxPacketsCount)/sizeof(pSMRS->radio_interfaces[0].hist_rxPacketsCount[0]) - 1;

      pSMRS->iMaxRxQuality = 0;
      for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
      {
         u32 totalRecv = 0;
         u32 totalRecvBad = 0;
         u32 totalRecvLost = 0;
         for( int k=0; k<iIntervalsToUse; k++ )
         {
            totalRecv += pSMRS->radio_interfaces[i].hist_rxPacketsCount[k];
            totalRecvBad += pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[k];
            totalRecvLost += pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[k];
         }
         if ( 0 == totalRecv )
            pSMRS->radio_interfaces[i].rxQuality = 0;
         else
            pSMRS->radio_interfaces[i].rxQuality = 100 - (100*(totalRecvLost+totalRecvBad))/(totalRecv+totalRecvLost);
      
         if ( pSMRS->radio_interfaces[i].rxQuality > pSMRS->iMaxRxQuality )
            pSMRS->iMaxRxQuality = pSMRS->radio_interfaces[i].rxQuality;
      }

      // Update relative RX quality for each radio interface

      for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
      {
         u32 totalRecv = 0;
         u32 totalRecvBad = 0;
         u32 totalRecvLost = 0;
         for( int i=0; i<iIntervalsToUse; i++ )
         {
            totalRecv += pSMRS->radio_interfaces[i].hist_rxPacketsCount[i];
            totalRecvBad += pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[i];
            totalRecvLost += pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[i];
         }

         totalRecv += pSMRS->radio_interfaces[i].hist_tmp_rxPacketsCount;
         totalRecvBad += pSMRS->radio_interfaces[i].hist_tmp_rxPacketsBadCount;
         totalRecvLost += pSMRS->radio_interfaces[i].hist_tmp_rxPacketsLostCount;

         pSMRS->radio_interfaces[i].rxRelativeQuality = pSMRS->radio_interfaces[i].rxQuality;
         if ( pSMRS->radio_interfaces[i].lastDbm > 0 )
            pSMRS->radio_interfaces[i].rxRelativeQuality -= pSMRS->radio_interfaces[i].lastDbm;
         else
            pSMRS->radio_interfaces[i].rxRelativeQuality += pSMRS->radio_interfaces[i].lastDbm;

         if ( pSMRS->radio_interfaces[i].lastDbm < -100 )
            pSMRS->radio_interfaces[i].rxRelativeQuality -= 10000;

         pSMRS->radio_interfaces[i].rxRelativeQuality -= totalRecvLost;
         pSMRS->radio_interfaces[i].rxRelativeQuality += (totalRecv-totalRecvBad);
      }

      radio_stats_log_info(pSMRS, timeNow);
   }

   // Update rx graphs

   if ( timeNow >= pSMRS->lastComputeTimeGraph + pSMRS->graphRefreshIntervalMs || timeNow < pSMRS->lastComputeTimeGraph )
   {
      pSMRS->lastComputeTimeGraph = timeNow;
      iReturn = 1;

      for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
      {
         pSMRS->radio_interfaces[i].uSlicesUpdated++;
         for( int k=MAX_HISTORY_RADIO_STATS_RECV_SLICES-1; k>0; k-- )
         {
            pSMRS->radio_interfaces[i].hist_rxPacketsCount[k] = pSMRS->radio_interfaces[i].hist_rxPacketsCount[k-1];
            pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[k] = pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[k-1];
            pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[k] = pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[k-1];
            pSMRS->radio_interfaces[i].hist_rxGapMiliseconds[k] = pSMRS->radio_interfaces[i].hist_rxGapMiliseconds[k-1];
         }

         if ( pSMRS->radio_interfaces[i].hist_tmp_rxPacketsCount < 255 )
            pSMRS->radio_interfaces[i].hist_rxPacketsCount[0] = pSMRS->radio_interfaces[i].hist_tmp_rxPacketsCount;
         else
            pSMRS->radio_interfaces[i].hist_rxPacketsCount[0] = 0xFF;

         if ( pSMRS->radio_interfaces[i].hist_tmp_rxPacketsBadCount < 255 )
            pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[0] = pSMRS->radio_interfaces[i].hist_tmp_rxPacketsBadCount;
         else
            pSMRS->radio_interfaces[i].hist_rxPacketsBadCount[0] = 0xFF;

         if ( pSMRS->radio_interfaces[i].hist_tmp_rxPacketsLostCount < 255 )
            pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[0] = pSMRS->radio_interfaces[i].hist_tmp_rxPacketsLostCount;
         else
            pSMRS->radio_interfaces[i].hist_rxPacketsLostCount[0] = 0xFF;

         pSMRS->radio_interfaces[i].hist_rxGapMiliseconds[0] = 0xFF;
         pSMRS->radio_interfaces[i].hist_tmp_rxPacketsCount = 0;
         pSMRS->radio_interfaces[i].hist_tmp_rxPacketsBadCount = 0;
         pSMRS->radio_interfaces[i].hist_tmp_rxPacketsLostCount = 0;
      }
   }


   // Update rx graphs controller

   if ( NULL != pSMRXStats )
   if ( (timeNow >= pSMRXStats->uTimeStartCurrentSlice + pSMRXStats->uTimeSliceDurationMs) || (timeNow < pSMRXStats->uTimeStartCurrentSlice) )
   {
      pSMRXStats->uTimeStartCurrentSlice = timeNow;
      iReturn = 1;

      for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
      {
         if ( pSMRXStats->interfaces[i].tmp_rxPackets < 255 )
            pSMRXStats->interfaces[i].rxPackets[pSMRXStats->iCurrentSlice] = pSMRXStats->interfaces[i].tmp_rxPackets;
         else
            pSMRXStats->interfaces[i].rxPackets[pSMRXStats->iCurrentSlice] = 0xFF;

         if ( pSMRXStats->interfaces[i].tmp_rxPacketsBad < 255 )
            pSMRXStats->interfaces[i].rxPacketsBad[pSMRXStats->iCurrentSlice] = pSMRXStats->interfaces[i].tmp_rxPacketsBad;
         else
            pSMRXStats->interfaces[i].rxPacketsBad[pSMRXStats->iCurrentSlice] = 0xFF;

         if ( pSMRXStats->interfaces[i].tmp_rxPacketsLost < 255 )
            pSMRXStats->interfaces[i].rxPacketsLost[pSMRXStats->iCurrentSlice] = pSMRXStats->interfaces[i].tmp_rxPacketsLost;
         else
            pSMRXStats->interfaces[i].rxPacketsLost[pSMRXStats->iCurrentSlice] = 0xFF;

         pSMRXStats->interfaces[i].rxGapMiliseconds[pSMRXStats->iCurrentSlice] = 0xFF;
         pSMRXStats->interfaces[i].tmp_rxPackets = 0;
         pSMRXStats->interfaces[i].tmp_rxPacketsBad = 0;
         pSMRXStats->interfaces[i].tmp_rxPacketsLost = 0;
      }

      pSMRXStats->iCurrentSlice++;
      if ( pSMRXStats->iCurrentSlice >= MAX_RX_GRAPH_SLICES )
         pSMRXStats->iCurrentSlice = 0;

   }

   if ( iReturn == 1 )
   {
      static int s_iLogRadioRxTxThroughput = 0;
      static u32 s_uTimeLastLogRadioRxTxThroughput = 0;

      if ( s_iLogRadioRxTxThroughput )
      if ( timeNow >= s_uTimeLastLogRadioRxTxThroughput + 5000 )
      {
         s_uTimeLastLogRadioRxTxThroughput = timeNow;
         for( int i=0; i<pSMRS->countLocalRadioLinks; i++ )
         {
            if ( (0 == pSMRS->radio_links[i].txPacketsPerSec) && (0 == pSMRS->radio_links[i].rxPacketsPerSec) )
               log_line("* Local radio link %d: rx/tx packets/sec: %u / %u (total rx/tx packets: %u/%u)", i+1, pSMRS->radio_links[i].rxPacketsPerSec, pSMRS->radio_links[i].txPacketsPerSec, pSMRS->radio_links[i].totalRxPackets, pSMRS->radio_links[i].totalTxPackets);
            else
               log_line("* Local radio link %d: rx/tx packets/sec: %u / %u", i+1, pSMRS->radio_links[i].rxPacketsPerSec, pSMRS->radio_links[i].txPacketsPerSec);
         }
         for( int i=0; i<pSMRS->countLocalRadioInterfaces; i++ )
         {
            if ( (0 == pSMRS->radio_interfaces[i].txPacketsPerSec) && (0 == pSMRS->radio_interfaces[i].rxPacketsPerSec) )
               log_line("* Local radio interface %d: rx/tx packets/sec: %u / %u (total rx/tx packets: %u/%u)", i+1, pSMRS->radio_interfaces[i].rxPacketsPerSec, pSMRS->radio_interfaces[i].txPacketsPerSec, pSMRS->radio_interfaces[i].totalRxPackets, pSMRS->radio_interfaces[i].totalTxPackets);
            else
               log_line("* Local radio interface %d: rx/tx packets/sec: %u / %u", i+1, pSMRS->radio_interfaces[i].rxPacketsPerSec, pSMRS->radio_interfaces[i].txPacketsPerSec);
         }
         for( int iVehicle=0; iVehicle<MAX_CONCURENT_VEHICLES; iVehicle++)
         for( int i=0; i<MAX_RADIO_STREAMS; i++ )
         {
            if ( pSMRS->radio_streams[iVehicle][i].uVehicleId != 0 )
            if ( (pSMRS->radio_streams[iVehicle][i].totalRxPackets > 0 ) || (pSMRS->radio_streams[iVehicle][i].totalTxPackets > 0) )
            {
               char szVID[32];
               if ( pSMRS->radio_streams[iVehicle][i].uVehicleId == MAX_U32 )
                  strcpy(szVID, "BROADCAST");
               else
                  sprintf(szVID, "%u", pSMRS->radio_streams[iVehicle][i].uVehicleId);
               if ( (0 == pSMRS->radio_streams[iVehicle][i].txPacketsPerSec) && (0 == pSMRS->radio_streams[iVehicle][i].rxPacketsPerSec) )
                  log_line("* Local VID/stream %s/%d: rx/tx packets/sec: %u / %u (total rx/tx packets: %u/%u)", szVID, i, pSMRS->radio_streams[iVehicle][i].rxPacketsPerSec, pSMRS->radio_streams[iVehicle][i].txPacketsPerSec, pSMRS->radio_streams[iVehicle][i].totalRxPackets, pSMRS->radio_streams[iVehicle][i].totalTxPackets);
               else
                  log_line("* Local VID/stream %s/%d: rx/tx packets/sec: %u / %u", szVID, i, pSMRS->radio_streams[iVehicle][i].rxPacketsPerSec, pSMRS->radio_streams[iVehicle][i].txPacketsPerSec);
            }
         }
      }
   }

   return iReturn;
}

void radio_stats_set_radio_link_rt_delay(shared_mem_radio_stats* pSMRS, int iLocalRadioLink, u32 delay, u32 timeNow)
{
   if ( NULL == pSMRS || iLocalRadioLink < 0 || iLocalRadioLink >= MAX_RADIO_INTERFACES )
      return;

   u32 avg = 0;
   for( int i=0; i<MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY-1; i++ )
   {
      s_uLastLinkRTDelayValues[iLocalRadioLink][i] = s_uLastLinkRTDelayValues[iLocalRadioLink][i+1];
      avg += s_uLastLinkRTDelayValues[iLocalRadioLink][i];
   }
   s_uLastLinkRTDelayValues[iLocalRadioLink][MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY-1] = delay;
   avg += delay;
   avg /= MAX_RADIO_LINKS_ROUNDTRIP_TIMES_HISTORY;

   pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMs = (avg*3 + delay)/4;
   pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMsLastTime = timeNow;
   if ( pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMs < pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMinimMs )
      pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMinimMs = pSMRS->radio_links[iLocalRadioLink].linkDelayRoundtripMs;
}

void radio_stats_set_commands_rt_delay(shared_mem_radio_stats* pSMRS, u32 delay)
{
   if ( NULL == pSMRS )
      return;

   int count = sizeof(s_uLastCommandsRTDelayValues)/sizeof(s_uLastCommandsRTDelayValues[0]);
   u32 avg = 0;
   for( int i=0; i<count-1; i++ )
   {
      s_uLastCommandsRTDelayValues[i] = s_uLastCommandsRTDelayValues[i+1];
      avg += s_uLastCommandsRTDelayValues[i];
   }
   s_uLastCommandsRTDelayValues[count-1] = delay;
   avg += delay;
   avg /= count;

   pSMRS->uAverageCommandRoundtripMiliseconds = avg;

   if ( pSMRS->uMaxCommandRoundtripMiliseconds == MAX_U32 )
      pSMRS->uMaxCommandRoundtripMiliseconds = delay;

   if ( pSMRS->uAverageCommandRoundtripMiliseconds < pSMRS->uMinCommandRoundtripMiliseconds )
      pSMRS->uMinCommandRoundtripMiliseconds = pSMRS->uAverageCommandRoundtripMiliseconds;
}

void radio_stats_set_tx_card_for_radio_link(shared_mem_radio_stats* pSMRS, int iLocalRadioLink, int iTxCard)
{
   if ( NULL == pSMRS )
      return;
   pSMRS->radio_links[iLocalRadioLink].lastTxInterfaceIndex = iTxCard;
}

void radio_stats_set_card_current_frequency(shared_mem_radio_stats* pSMRS, int iRadioInterface, u32 freqKhz)
{
   if ( NULL == pSMRS )
      return;
   pSMRS->radio_interfaces[iRadioInterface].uCurrentFrequencyKhz = freqKhz;
}

void radio_stats_set_bad_data_on_current_rx_interval(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, int iRadioInterface)
{
   if ( NULL == pSMRS )
      return;
   if ( (iRadioInterface < 0) || (iRadioInterface >= MAX_RADIO_INTERFACES) )
      return;

   if ( 0 == pSMRS->radio_interfaces[iRadioInterface].hist_tmp_rxPacketsBadCount )
      pSMRS->radio_interfaces[iRadioInterface].hist_tmp_rxPacketsBadCount = 1;
   if ( 0 == pSMRS->radio_interfaces[iRadioInterface].hist_tmp_rxPacketsLostCount )
      pSMRS->radio_interfaces[iRadioInterface].hist_tmp_rxPacketsLostCount = 1;
   if ( 0 == s_uControllerLinkStats_tmpRecvLost[iRadioInterface] )
      s_uControllerLinkStats_tmpRecvLost[iRadioInterface] = 1;

   if ( NULL != pSMRXStats )
   {
      if ( 0 == pSMRXStats->interfaces[iRadioInterface].tmp_rxPacketsBad )
         pSMRXStats->interfaces[iRadioInterface].tmp_rxPacketsBad = 1;
      if ( 0 == pSMRXStats->interfaces[iRadioInterface].tmp_rxPacketsLost )
         pSMRXStats->interfaces[iRadioInterface].tmp_rxPacketsLost = 1;
   }
}

// Returns 1 if ok, -1 for error

int radio_stats_update_on_new_radio_packet_received(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow, int iInterfaceIndex, u8* pPacketBuffer, int iPacketLength, int iIsShortPacket, int iIsVideo, int iDataIsOk)
{
   if ( NULL == pSMRS )
      return -1;

   radio_hw_info_t* pRadioInfo = hardware_get_radio_info(iInterfaceIndex);
   if ( NULL == pRadioInfo )
   {
      log_softerror_and_alarm("Tried to update radio stats on invalid radio interface number %d. Invalid radio info.", iInterfaceIndex+1);
      return -1;
   }
   
   pSMRS->timeLastRxPacket = timeNow;
   if ( iIsVideo )
   {
      pSMRS->radio_interfaces[iInterfaceIndex].lastDbmVideo = pRadioInfo->monitor_interface_read.radioInfo.nDbm;
      pSMRS->radio_interfaces[iInterfaceIndex].lastDataRateVideo = ((int)pRadioInfo->monitor_interface_read.radioInfo.nRate) * 500 * 1000;
      pSMRS->radio_interfaces[iInterfaceIndex].lastDbm = pSMRS->radio_interfaces[iInterfaceIndex].lastDbmVideo;
   }
   else
   {
      pSMRS->radio_interfaces[iInterfaceIndex].lastDbmData = pRadioInfo->monitor_interface_read.radioInfo.nDbm;
      pSMRS->radio_interfaces[iInterfaceIndex].lastDataRateData = ((int)pRadioInfo->monitor_interface_read.radioInfo.nRate) * 500 * 1000;
      pSMRS->radio_interfaces[iInterfaceIndex].lastDbm = pSMRS->radio_interfaces[iInterfaceIndex].lastDbmData;
   }
   
   // -------------------------------------------------------------
   // Begin - Update last received packet time

   u32 uTimeGap = timeNow - pSMRS->radio_interfaces[iInterfaceIndex].timeLastRxPacket;
   if ( 0 == pSMRS->radio_interfaces[iInterfaceIndex].timeLastRxPacket )
      uTimeGap = 0;
   if ( uTimeGap > 254 )
      uTimeGap = 254;
   if ( pSMRS->radio_interfaces[iInterfaceIndex].hist_rxGapMiliseconds[0] == 0xFF )
      pSMRS->radio_interfaces[iInterfaceIndex].hist_rxGapMiliseconds[0] = uTimeGap;
   else if ( uTimeGap > pSMRS->radio_interfaces[iInterfaceIndex].hist_rxGapMiliseconds[0] )
      pSMRS->radio_interfaces[iInterfaceIndex].hist_rxGapMiliseconds[0] = uTimeGap;
     
   pSMRS->radio_interfaces[iInterfaceIndex].timeLastRxPacket = timeNow;
   
   
   // End - Update last received packet time
   // ----------------------------------------------------------------

   // ----------------------------------------------------------------------
   // Update rx bytes and packets count on interface

   pSMRS->radio_interfaces[iInterfaceIndex].totalRxBytes += iPacketLength;
   pSMRS->radio_interfaces[iInterfaceIndex].tmpRxBytes += iPacketLength;

   pSMRS->radio_interfaces[iInterfaceIndex].totalRxPackets++;
   pSMRS->radio_interfaces[iInterfaceIndex].tmpRxPackets++;

   // -------------------------------------------------------------------------
   // Begin - Update history and good/bad/lost packets for interface 

   pSMRS->radio_interfaces[iInterfaceIndex].hist_tmp_rxPacketsCount++;
   if ( NULL != pSMRXStats )
      pSMRXStats->interfaces[iInterfaceIndex].tmp_rxPackets++;

   s_uControllerLinkStats_tmpRecv[iInterfaceIndex]++;

   if ( (0 == iDataIsOk) || (iPacketLength <= 0) )
   {
      pSMRS->radio_interfaces[iInterfaceIndex].hist_tmp_rxPacketsBadCount++;
      if ( NULL != pSMRXStats )
         pSMRXStats->interfaces[iInterfaceIndex].tmp_rxPacketsBad++;
      s_uControllerLinkStats_tmpRecvBad[iInterfaceIndex]++;
   }

   if ( NULL != pPacketBuffer )
   {
      if ( iIsShortPacket )
      {
         t_packet_header_short* pPHS = (t_packet_header_short*)pPacketBuffer;
         u32 uNext = ((pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex + 1) & 0xFF);
         if ( pPHS->packet_id != uNext  )
         {
            u32 uLost = pPHS->packet_id - uNext;
            if ( pPHS->packet_id < uNext )
               uLost = pPHS->packet_id + 255 - uNext;
            pSMRS->radio_interfaces[iInterfaceIndex].hist_tmp_rxPacketsLostCount += uLost;
            pSMRS->radio_interfaces[iInterfaceIndex].totalRxPacketsLost += uLost;
            if ( NULL != pSMRXStats )
               pSMRXStats->interfaces[iInterfaceIndex].tmp_rxPacketsLost += uLost;
            s_uControllerLinkStats_tmpRecvLost[iInterfaceIndex] += uLost;
         }

         pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex = pPHS->packet_id;
      }
      else
      {
         t_packet_header* pPH = (t_packet_header*)pPacketBuffer;

         // Radio_packet_index is 0 for version 7.6 or older, skip gap calculation
         if ( 0 != pPH->radio_link_packet_index )
         if ( pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex != MAX_U32 )
         if ( pPH->radio_link_packet_index > pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex + 1 )
         {
            u32 uLost = pPH->radio_link_packet_index - pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex - 1;
            pSMRS->radio_interfaces[iInterfaceIndex].hist_tmp_rxPacketsLostCount += uLost;
            pSMRS->radio_interfaces[iInterfaceIndex].totalRxPacketsLost += uLost;
            if ( NULL != pSMRXStats )
               pSMRXStats->interfaces[iInterfaceIndex].tmp_rxPacketsLost += uLost;
            s_uControllerLinkStats_tmpRecvLost[iInterfaceIndex] += uLost;
         }

         pSMRS->radio_interfaces[iInterfaceIndex].lastReceivedRadioLinkPacketIndex = pPH->radio_link_packet_index;
      }
   }
   // End - Update history and good/bad/lost packets for interface 

   int nRadioLinkId = pSMRS->radio_interfaces[iInterfaceIndex].assignedLocalRadioLinkId;
   if ( nRadioLinkId < 0 || nRadioLinkId >= MAX_RADIO_INTERFACES )
   {
      if ( timeNow > s_uLastTimeDebugPacketRecvOnNoLink + 3000 )
      {
         s_uLastTimeDebugPacketRecvOnNoLink = timeNow;
         log_softerror_and_alarm("Received radio packet on radio interface %d that is not assigned to any radio links.", iInterfaceIndex+1);
      }
      return -1;
   }

   return 1;
}


// Returns 1 if ok, -1 for error

int radio_stats_update_on_unique_packet_received(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow, int iInterfaceIndex, u8* pPacketBuffer, int iPacketLength)
{
   if ( NULL == pSMRS )
      return -1;

   radio_hw_info_t* pRadioInfo = hardware_get_radio_info(iInterfaceIndex);
   if ( NULL == pRadioInfo )
   {
      log_softerror_and_alarm("Tried to update radio stats on invalid radio interface number %d. Invalid radio info.", iInterfaceIndex+1);
      return -1;
   }
   
   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
   int nRadioLinkId = pSMRS->radio_interfaces[iInterfaceIndex].assignedLocalRadioLinkId;

   u32 uVehicleId = pPH->vehicle_id_src;
   u32 uStreamPacketIndex = (pPH->stream_packet_idx) & PACKET_FLAGS_MASK_STREAM_PACKET_IDX;
   u32 uStreamIndex = (pPH->stream_packet_idx)>>PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX;
   if ( uStreamIndex >= MAX_RADIO_STREAMS )
   {
      log_softerror_and_alarm("Received invalid stream id %u, packet index %u, packet type: %s", uStreamIndex, uStreamPacketIndex, str_get_packet_type(pPH->packet_type));
      uStreamIndex = 0;
   }
      
   int iStreamsVehicleIndex = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( uVehicleId == pSMRS->radio_streams[i][0].uVehicleId )
      {
         iStreamsVehicleIndex = i;
         break;
      }
   }
   if ( iStreamsVehicleIndex == -1 )
   {
      for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
      {
         if ( 0 == pSMRS->radio_streams[i][0].uVehicleId )
         {
            iStreamsVehicleIndex = i;
            break;
         }
      }
    
      // No more room for new vehicles. Reuse existing one
      if ( -1 == iStreamsVehicleIndex )
         iStreamsVehicleIndex = MAX_CONCURENT_VEHICLES-1;

      for( int i=0; i<MAX_RADIO_STREAMS; i++ )
      {
         pSMRS->radio_streams[iStreamsVehicleIndex][i].uVehicleId = uVehicleId;
         pSMRS->radio_streams[iStreamsVehicleIndex][i].totalRxBytes = 0;
         pSMRS->radio_streams[iStreamsVehicleIndex][i].tmpRxBytes = 0;

         pSMRS->radio_streams[iStreamsVehicleIndex][i].totalRxPackets = 0;
         pSMRS->radio_streams[iStreamsVehicleIndex][i].tmpRxPackets = 0;
      }
   }

   // -------------------------------------------------------------
   // Begin - Update last received packet time

   pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].timeLastRxPacket = timeNow;

   if ( nRadioLinkId >= 0 && nRadioLinkId < MAX_RADIO_INTERFACES )
   {
      pSMRS->radio_links[nRadioLinkId].timeLastRxPacket = timeNow;
   }
   else
   {
      if ( timeNow > s_uLastTimeDebugPacketRecvOnNoLink + 3000 )
      {
         s_uLastTimeDebugPacketRecvOnNoLink = timeNow;
         log_softerror_and_alarm("Received radio packet on radio interface %d that is not assigned to any radio links.", iInterfaceIndex+1);
      }
      return -1;
   }

   // End - Update last received packet time
 
   if ( 0 ==  pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].totalRxPackets )
      log_line("[RadioStats] Start receiving radio stream %d from VID %u", (int)uStreamIndex, uVehicleId);

   pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].totalRxBytes += iPacketLength;
   pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].tmpRxBytes += iPacketLength;

   pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].totalRxPackets++;
   pSMRS->radio_streams[iStreamsVehicleIndex][uStreamIndex].tmpRxPackets++;
   
   pSMRS->radio_links[nRadioLinkId].totalRxBytes += iPacketLength;
   pSMRS->radio_links[nRadioLinkId].tmpRxBytes += iPacketLength;
   
   pSMRS->radio_links[nRadioLinkId].totalRxPackets++;
   pSMRS->radio_links[nRadioLinkId].tmpRxPackets++;

   return 1;
}

void radio_stats_update_on_packet_sent_on_radio_interface(shared_mem_radio_stats* pSMRS, u32 timeNow, int interfaceIndex, int iPacketLength)
{
   if ( NULL == pSMRS )
      return;
   if ( interfaceIndex < 0 || interfaceIndex >= hardware_get_radio_interfaces_count() )
      return;
   
   // Update radio interfaces

   pSMRS->radio_interfaces[interfaceIndex].totalTxBytes += iPacketLength;
   pSMRS->radio_interfaces[interfaceIndex].tmpTxBytes += iPacketLength;

   pSMRS->radio_interfaces[interfaceIndex].totalTxPackets++;
   pSMRS->radio_interfaces[interfaceIndex].tmpTxPackets++;
}

void radio_stats_update_on_packet_sent_on_radio_link(shared_mem_radio_stats* pSMRS, u32 timeNow, int iLocalLinkIndex, int iStreamIndex, int iPacketLength, int iChainedCount)
{
   if ( NULL == pSMRS )
      return;
   if ( iLocalLinkIndex < 0 || iLocalLinkIndex >= MAX_RADIO_INTERFACES )
      iLocalLinkIndex = 0;
   if ( iStreamIndex < 0 || iStreamIndex >= MAX_RADIO_STREAMS )
      iStreamIndex = 0;

   // Update radio link

   pSMRS->radio_links[iLocalLinkIndex].totalTxBytes += iPacketLength;
   pSMRS->radio_links[iLocalLinkIndex].tmpTxBytes += iPacketLength;

   pSMRS->radio_links[iLocalLinkIndex].totalTxPackets++;
   pSMRS->radio_links[iLocalLinkIndex].tmpTxPackets++;

   pSMRS->radio_links[iLocalLinkIndex].totalTxPacketsUncompressed += iChainedCount;
   pSMRS->radio_links[iLocalLinkIndex].tmpUncompressedTxPackets += iChainedCount;
}

void radio_stats_update_on_packet_sent_for_radio_stream(shared_mem_radio_stats* pSMRS, u32 timeNow, u32 uVehicleId, int iStreamIndex, int iPacketLength)
{
   if ( NULL == pSMRS )
      return;
   if ( iStreamIndex < 0 || iStreamIndex >= MAX_RADIO_STREAMS )
      iStreamIndex = 0;

   // Broadcasted packet?
   if ( uVehicleId == 0 )
      uVehicleId = MAX_U32;

   int iStreamsVehicleIndex = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( uVehicleId == pSMRS->radio_streams[i][0].uVehicleId )
      {
         iStreamsVehicleIndex = i;
         break;
      }
   }
   if ( iStreamsVehicleIndex == -1 )
   {
      for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
      {
         if ( 0 == pSMRS->radio_streams[i][0].uVehicleId )
         {
            iStreamsVehicleIndex = i;
            break;
         }
      }
    
      // No more room for new vehicles. Reuse existing one
      if ( -1 == iStreamsVehicleIndex )
         iStreamsVehicleIndex = MAX_CONCURENT_VEHICLES-1;

      for( int i=0; i<MAX_RADIO_STREAMS; i++ )
         pSMRS->radio_streams[iStreamsVehicleIndex][i].uVehicleId = uVehicleId;
   }

   // Update radio streams

   pSMRS->radio_streams[iStreamsVehicleIndex][iStreamIndex].totalTxBytes += iPacketLength;
   pSMRS->radio_streams[iStreamsVehicleIndex][iStreamIndex].tmpTxBytes += iPacketLength;

   pSMRS->radio_streams[iStreamsVehicleIndex][iStreamIndex].totalTxPackets++;
   pSMRS->radio_streams[iStreamsVehicleIndex][iStreamIndex].tmpTxPackets++;
}

void radio_controller_links_stats_reset(t_packet_data_controller_link_stats* pControllerStats)
{
   if ( NULL == pControllerStats )
      return;

   pControllerStats->flagsAndVersion = 0;
   pControllerStats->lastUpdateTime = 0;
   pControllerStats->radio_interfaces_count = hardware_get_radio_interfaces_count();
   pControllerStats->video_streams_count = 1;

   for( int i=0; i<CONTROLLER_LINK_STATS_HISTORY_MAX_SLICES; i++ )
   {
      for( int k=0; k<MAX_RADIO_INTERFACES; k++ )
         pControllerStats->radio_interfaces_rx_quality[k][i] = 255;
      for( int k=0; k<MAX_VIDEO_STREAMS; k++ )
      {
         pControllerStats->radio_streams_rx_quality[k][i] = 255;
         pControllerStats->video_streams_blocks_clean[k][i] = 0;
         pControllerStats->video_streams_blocks_reconstructed[k][i] = 0;
         pControllerStats->video_streams_blocks_max_ec_packets_used[k][i] = 0;
         pControllerStats->video_streams_requested_retransmission_packets[k][i] = 0;
      }
   }

   for( int i=0; i<MAX_RADIO_INTERFACES; i++ )
   {
      s_uControllerLinkStats_tmpRecv[i] = 0;
      s_uControllerLinkStats_tmpRecvBad[i] = 0;
      s_uControllerLinkStats_tmpRecvLost[i] = 0;
   }

   for( int k=0; k<MAX_VIDEO_STREAMS; k++ )
   {
      pControllerStats->tmp_radio_streams_rx_quality[k] = 0;
      pControllerStats->tmp_video_streams_blocks_clean[k] = 0;
      pControllerStats->tmp_video_streams_blocks_reconstructed[k] = 0;
      pControllerStats->tmp_video_streams_requested_retransmission_packets[k] = 0;
   }

}

void radio_controller_links_stats_periodic_update(t_packet_data_controller_link_stats* pControllerStats, u32 timeNow)
{
   if ( NULL == pControllerStats )
      return;

   if ( timeNow >= pControllerStats->lastUpdateTime && timeNow < pControllerStats->lastUpdateTime + CONTROLLER_LINK_STATS_HISTORY_SLICE_INTERVAL_MS )
      return;
   
   pControllerStats->lastUpdateTime = timeNow;

   for( int i=0; i<pControllerStats->radio_interfaces_count; i++ )
   {
      for( int k=CONTROLLER_LINK_STATS_HISTORY_MAX_SLICES-1; k>0; k-- )
         pControllerStats->radio_interfaces_rx_quality[i][k] = pControllerStats->radio_interfaces_rx_quality[i][k-1];

      if ( 0 == s_uControllerLinkStats_tmpRecv[i] )
         pControllerStats->radio_interfaces_rx_quality[i][0] = 0;
      else
         pControllerStats->radio_interfaces_rx_quality[i][0] = 100 - (100*(s_uControllerLinkStats_tmpRecvBad[i]+s_uControllerLinkStats_tmpRecvLost[i]))/(s_uControllerLinkStats_tmpRecv[i]+s_uControllerLinkStats_tmpRecvLost[i]);

      s_uControllerLinkStats_tmpRecv[i] = 0;
      s_uControllerLinkStats_tmpRecvBad[i] = 0;
      s_uControllerLinkStats_tmpRecvLost[i] = 0;
   }

   for( int i=0; i<pControllerStats->video_streams_count; i++ )
   {
      for( int k=CONTROLLER_LINK_STATS_HISTORY_MAX_SLICES-1; k>0; k-- )
      {
         pControllerStats->radio_streams_rx_quality[i][k] = pControllerStats->radio_streams_rx_quality[i][k-1];
         pControllerStats->video_streams_blocks_clean[i][k] = pControllerStats->video_streams_blocks_clean[i][k-1];
         pControllerStats->video_streams_blocks_reconstructed[i][k] = pControllerStats->video_streams_blocks_reconstructed[i][k-1];
         pControllerStats->video_streams_blocks_max_ec_packets_used[i][k] = pControllerStats->video_streams_blocks_max_ec_packets_used[i][k-1];
         pControllerStats->video_streams_requested_retransmission_packets[i][k] = pControllerStats->video_streams_requested_retransmission_packets[i][k-1];
      }
      pControllerStats->radio_streams_rx_quality[i][0] = pControllerStats->tmp_radio_streams_rx_quality[i];
      pControllerStats->video_streams_blocks_clean[i][0] = pControllerStats->tmp_video_streams_blocks_clean[i];
      pControllerStats->video_streams_blocks_reconstructed[i][0] = pControllerStats->tmp_video_streams_blocks_reconstructed[i];
      pControllerStats->video_streams_blocks_max_ec_packets_used[i][0] = pControllerStats->tmp_video_streams_blocks_max_ec_packets_used[i];
      pControllerStats->video_streams_requested_retransmission_packets[i][0] = pControllerStats->tmp_video_streams_requested_retransmission_packets[i];

      pControllerStats->tmp_radio_streams_rx_quality[i] = 0;
      pControllerStats->tmp_video_streams_blocks_clean[i] = 0;
      pControllerStats->tmp_video_streams_blocks_reconstructed[i] = 0;
      pControllerStats->tmp_video_streams_blocks_max_ec_packets_used[i] = 0;
      pControllerStats->tmp_video_streams_requested_retransmission_packets[i] = 0;
   }
}
