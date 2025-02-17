#include "../base/shared_mem.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../base/config.h"
#include "../base/commands.h"
#include "../base/models.h"
#include "../base/radio_utils.h"
#include "../base/encr.h"
#include "../common/radio_stats.h"
#include "../common/string_utils.h"

#include <time.h>
#include <sys/resource.h>

bool quit = false;

bool bSummary = false;
bool bOnlyErrors = false;
   
fd_set g_Readset;

u32 g_uStreamsLastPacketIndex[MAX_RADIO_STREAMS];
u32 g_uStreamsTotalLostPackets[MAX_RADIO_STREAMS];
u32 g_uLastVideoBlock = 0;
u32 g_uTotalLostPackets = 0;
u32 g_uTotalRecvPackets = 0;
u32 g_uTotalRecvPacketsTypes[256];
u32 g_uTotalRecvPacketsOnStreams[256];
u32 uSummaryLastUpdateTime = 0;
int g_iOnlyStreamId = -1;

u32 g_TimeNow = 0;
shared_mem_radio_stats g_SM_RadioStats;

void handle_sigint(int sig) 
{ 
   log_line("Caught signal to stop: %d\n", sig);
   quit = true;
} 

void process_packet_summary( int iInterfaceIndex, u8* pBuffer, int iBufferLength)
{
   int bCRCOk = 0;   
   int nPacketLength = packet_process_and_check(iInterfaceIndex, pBuffer, iBufferLength, &bCRCOk); 
   
   bool bVideoData = false;
   t_packet_header* pPH = (t_packet_header*)pBuffer;
   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO )
      bVideoData = true;

   int nRes = radio_stats_update_on_new_radio_packet_received(&g_SM_RadioStats, NULL, g_TimeNow, iInterfaceIndex, pBuffer, nPacketLength, 0, (int)bVideoData, 1);
       
   u32 packetIndex = (pPH->stream_packet_idx & PACKET_FLAGS_MASK_STREAM_PACKET_IDX);
   u32 uStreamId = pPH->stream_packet_idx >> PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX;

   u32 gap = 0;
   if ( g_uStreamsLastPacketIndex[uStreamId] != MAX_U32 )
         gap = packetIndex - g_uStreamsLastPacketIndex[uStreamId];
   if ( gap > 1 )
   {
      g_uTotalLostPackets += gap-1;
      g_uStreamsTotalLostPackets[uStreamId] += gap-1;
   }

   g_uStreamsLastPacketIndex[uStreamId] = packetIndex;
   
   if ( get_current_timestamp_ms() < uSummaryLastUpdateTime + 1000 )
      return;

   uSummaryLastUpdateTime = get_current_timestamp_ms();
   printf("Total recv pckts: %u/sec; ", g_uTotalRecvPackets );
   for( int i=0; i<MAX_RADIO_STREAMS; i++ )
   {
      if ( g_uTotalRecvPacketsOnStreams[i] > 0 )
         printf(" %u/sec on stream %d, ", g_uTotalRecvPacketsOnStreams[i], i);
      g_uTotalRecvPacketsTypes[i] = 0;
      g_uTotalRecvPacketsOnStreams[i] = 0;
   }

   printf("\nTotal lost pckts: %d/sec", g_uTotalLostPackets);
   for( int i=0; i<MAX_RADIO_STREAMS; i++ )
   {
      if ( g_uStreamsTotalLostPackets[i] > 0 )
         printf(", %u/sec lost on stream %d", g_uStreamsTotalLostPackets[i], i);
      g_uStreamsTotalLostPackets[i] = 0;
   }
   printf("\n");
   if ( g_uTotalRecvPackets + g_uTotalLostPackets > 0 )
      printf(" Lost: %.1f %% / sec\n", (100.0*(float)g_uTotalLostPackets)/((float)(g_uTotalLostPackets + g_uTotalRecvPackets)));
   g_uTotalRecvPackets = 0;
   g_uTotalLostPackets = 0;

   int iTotalRecv = 0;
   int iTotalLostBad = 0;
   int iSlices = 0;
   u32 uMs = 0;
   for( int k=0; k<MAX_HISTORY_RADIO_STATS_RECV_SLICES; k++ )
   {
      iTotalRecv += g_SM_RadioStats.radio_interfaces[0].hist_rxPacketsCount[k];
      iTotalLostBad += g_SM_RadioStats.radio_interfaces[0].hist_rxPacketsLostCount[k];
      iTotalLostBad += g_SM_RadioStats.radio_interfaces[0].hist_rxPacketsBadCount[k];
      iSlices++;
      uMs += g_SM_RadioStats.graphRefreshIntervalMs;
      if ( uMs >= 1000 )
         break;
   }
   char szBuff[256];
   if ( iTotalRecv + iTotalLostBad > 0 )
      sprintf(szBuff, "Recv/Lost (last sec, %d slices):  %d/%d, Lost: %.1f %%\n", iSlices, iTotalRecv, iTotalLostBad, 100.0*(float)iTotalLostBad/(float)(iTotalRecv+iTotalLostBad));
   else
      sprintf(szBuff, "Recv/Lost (last sec, %d slices):  %d/%d \n", iSlices, iTotalRecv, iTotalLostBad);
   printf(szBuff);
   printf("\n\n");
   fflush(stdout);
}

int process_packet_errors( int iInterfaceIndex, u8* pBuffer, int iBufferLength)
{
   int iResult = 0;
   radio_hw_info_t* pNICInfo = hardware_get_radio_info(iInterfaceIndex);
   int lastDBM = pNICInfo->monitor_interface_read.radioInfo.nDbm;
   int lastDataRate = pNICInfo->monitor_interface_read.radioInfo.nRate;

   bool bVideoData = false;
   t_packet_header* pPH = (t_packet_header*)pBuffer;
   if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) == PACKET_COMPONENT_VIDEO )
      bVideoData = true;
   int bCRCOk = 0;   
   int nPacketLength = packet_process_and_check(iInterfaceIndex, pBuffer, iBufferLength, &bCRCOk);
   int nRes = radio_stats_update_on_new_radio_packet_received(&g_SM_RadioStats, NULL, g_TimeNow, iInterfaceIndex, pBuffer, nPacketLength, 0, (int)bVideoData, 1);

   t_packet_header_video_full_77* pPHVF = NULL;
   if ( pPH->packet_type == PACKET_TYPE_VIDEO_DATA_FULL )
      pPHVF = (t_packet_header_video_full_77*) (pBuffer+sizeof(t_packet_header));
   
   u32 packetIndex = (pPH->stream_packet_idx & PACKET_FLAGS_MASK_STREAM_PACKET_IDX);
   u32 uStreamId = pPH->stream_packet_idx >> PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX;
   
   u32 gap = 0;
   if ( g_uStreamsLastPacketIndex[uStreamId] != MAX_U32 )
      gap = packetIndex - g_uStreamsLastPacketIndex[uStreamId];
   if ( gap > 1 )
   {
      g_uTotalLostPackets += gap-1;
      g_uStreamsTotalLostPackets[uStreamId] += gap-1;
      printf("\nMissing %u packets on stream %d, received index: %u, last index: %u\n",
         gap-1, uStreamId, packetIndex, g_uStreamsLastPacketIndex[uStreamId] );
      fflush(stdout);
      iResult = gap-1;
   }

   u32 videogap = 0;
   if ( pPH->packet_type == PACKET_TYPE_VIDEO_DATA_FULL )
   if ( pPHVF->video_block_index > g_uLastVideoBlock )
   {
      videogap = pPHVF->video_block_index - g_uLastVideoBlock;
      if ( videogap > 1 )
      {
         printf("\nMissing video packets: %u [%u to %u], video gap: %d [%u jump to %u]\n", gap-1, g_uStreamsLastPacketIndex[uStreamId], packetIndex, videogap-1, g_uLastVideoBlock, pPHVF->video_block_index );
         fflush(stdout);
      }
   }

   g_uStreamsLastPacketIndex[uStreamId] = packetIndex;
   if ( pPH->packet_type == PACKET_TYPE_VIDEO_DATA_FULL )
      g_uLastVideoBlock = pPHVF->video_block_index;

   return iResult;
}

void process_packet(int iInterfaceIndex )
{
   //printf("x");
   fflush(stdout);
         
   int nLength = 0;
   u8* pBuffer = radio_process_wlan_data_in(iInterfaceIndex, &nLength); 
   if ( NULL == pBuffer )
   {
      printf("NULL receive buffer. Ignoring...\n");
      fflush(stdout);
      return;
   }

   while ( nLength > 0 )
   { 
      int iMissingPackets = 0;
      if ( bSummary )
         process_packet_summary(iInterfaceIndex, pBuffer, nLength);
      if ( bOnlyErrors )
         iMissingPackets = process_packet_errors(iInterfaceIndex, pBuffer, nLength);

      t_packet_header* pPH = (t_packet_header*)pBuffer;
      u32 uStreamId = pPH->stream_packet_idx >> PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX;

      g_uTotalRecvPackets++;
      g_uTotalRecvPacketsTypes[pPH->packet_type]++;
      g_uTotalRecvPacketsOnStreams[uStreamId]++;

      if ( iMissingPackets > 0 )
      {
         printf("Missing %d packets. Buffer: %d bytes, packet: %d bytes, type: %s ", iMissingPackets, nLength, pPH->total_length, str_get_packet_type(pPH->packet_type));
         bool bHasFlags = false;
         if ( pPH->packet_flags_extended & PACKET_FLAGS_EXTENDED_BIT_SEND_ON_LOW_CAPACITY_LINK_ONLY )
            { bHasFlags = true; printf("[Sent on Low Link Only] "); }
         if ( pPH->packet_flags_extended & PACKET_FLAGS_EXTENDED_BIT_SEND_ON_HIGH_CAPACITY_LINK_ONLY )
            { bHasFlags = true; printf("[Sent on High Link Only] "); }
         if ( ! bHasFlags )
            printf("[No Link Affinity]");
         printf("\n");
         fflush(stdout);
      }
      if ( -1 != g_iOnlyStreamId )
      if ( uStreamId != g_iOnlyStreamId )
      {
         printf("x");
         fflush(stdout);
      }
      pBuffer += pPH->total_length;
      nLength -= pPH->total_length;
   }
}

int try_read_packets(int iInterfaceIndex)
{
   long miliSec = 500;
   struct timeval to;
   to.tv_sec = 0;
   to.tv_usec = miliSec*1000;
            
   int maxfd = -1;
   FD_ZERO(&g_Readset);
   for(int i=0; i<hardware_get_radio_interfaces_count(); ++i)
   {
      if ( i != iInterfaceIndex )
         continue;
      radio_hw_info_t* pNICInfo = hardware_get_radio_info(i);
      if ( pNICInfo->openedForRead )
      {
         FD_SET(pNICInfo->monitor_interface_read.selectable_fd, &g_Readset);
         if ( pNICInfo->monitor_interface_read.selectable_fd > maxfd )
            maxfd = pNICInfo->monitor_interface_read.selectable_fd;
      } 
   }

   int nResult = select(maxfd+1, &g_Readset, NULL, NULL, &to);
   return nResult;
}

int main(int argc, char *argv[])
{
   if ( argc < 4 )
   {
      printf("\nYou must specify network, frequency and a port number:  test_port_rx wlan0 5745 15 [-errors]/[-summary]/[stream index]\n   14 - uplink , 15 - downlink\n");
      printf("\n-errors : show only errors and missing packets\n");
      return -1;
   }
   
   signal(SIGINT, handle_sigint);
   signal(SIGTERM, handle_sigint);
   signal(SIGQUIT, handle_sigint);
   char* szCard = argv[1];
   int iFreq = atoi(argv[2]);
   int port = atoi(argv[3]);
   
   if ( strcmp(argv[argc-1], "-errors") == 0 )
      bOnlyErrors = true;
   else if ( strcmp(argv[argc-1], "-summary") == 0 )
      bSummary = true;
   else if ( argc >= 5 )
      g_iOnlyStreamId = atoi(argv[argc-1]);

   log_init("RX_TEST_PORT");
   log_enable_stdout();
   hardware_enumerate_radio_interfaces();
   int iInterfaceIndex = -1;
   for( int i=0; i<hardware_get_radio_interfaces_count(); ++i)
   {
      radio_hw_info_t* pNICInfo = hardware_get_radio_info(i);
      if ( 0 == strcmp(szCard, pNICInfo->szName ) )
      {
         //radio_utils_set_interface_frequency(i, freq, NULL);
       iInterfaceIndex = i;
       break;
      }
   }
   hardware_sleep_ms(100);

   radio_init_link_structures();
   radio_enable_crc_gen(1);

   if ( -1 == iInterfaceIndex )
   {
      printf("\nCan't find interface %s\n", szCard);
      return 0;
   }

   radio_utils_set_interface_frequency(NULL, iInterfaceIndex, iFreq, NULL);

   printf("\nStart receiving on lan: %s (interface %d), %d Mhz, port: %d\n", szCard, iInterfaceIndex+1, iFreq, port);

   int pcapR =  radio_open_interface_for_read(iInterfaceIndex, port);
   if ( pcapR < 0 )
   {
      printf("\nFailed to open port.\n");
      return -1;
   }

   int pcapW = radio_open_interface_for_write(iInterfaceIndex);

   printf("Opened wlan interface for read and write.\n");

   //FILE* fd = fopen("out.bin", "w");
   
   for( int i=0; i<MAX_RADIO_STREAMS; i++ )
   {
      g_uStreamsLastPacketIndex[i] = MAX_U32;
      g_uStreamsTotalLostPackets[i] = 0;
   }

   g_uTotalRecvPackets = 0;
   for( int i=0; i<256; i++ )
   {
      g_uTotalRecvPacketsTypes[i] = 0;
      g_uTotalRecvPacketsOnStreams[i] = 0;
   }

   radio_stats_reset(&g_SM_RadioStats, 100);

   g_SM_RadioStats.radio_interfaces[0].assignedLocalRadioLinkId = 0;
   g_SM_RadioStats.countLocalRadioInterfaces = 1;
   g_SM_RadioStats.countLocalRadioLinks = 1;

   if ( -1 == g_iOnlyStreamId )
      printf("\nLooking for any stream\n");
   else
      printf("\nLooking only for stream id %d\n", g_iOnlyStreamId);

   printf("\n\n\nStarted.\nWaiting for data...\n\n\n");
   fflush(stdout);
   
   while (!quit)
   {
      g_TimeNow = get_current_timestamp_ms();
      radio_stats_periodic_update(&g_SM_RadioStats, NULL, g_TimeNow);

      int nResult = try_read_packets(iInterfaceIndex);

      if( nResult > 0 )
         process_packet(iInterfaceIndex);
     
      if ( (! bSummary) && (! bOnlyErrors) )
      {
         if ( nResult == 0 )
            printf(".");
         else
            printf("x");
      }
   }
   //fclose(fd);
   radio_close_interface_for_write(iInterfaceIndex);
   radio_close_interface_for_read(iInterfaceIndex);
   printf("\nFinised receiving test.\n");
   return (0);
}
