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

#include "../../base/base.h"
#include "../../base/config.h"
#include "../../base/shared_mem.h"
#include "../../base/hdmi_video.h"
#include "../../base/ctrl_settings.h"
#include "../../base/ctrl_interfaces.h"
#include "../../common/string_utils.h"
#include "../../radio/radiolink.h"

#include <ctype.h>
#include "../shared_vars.h"
#include "../colors.h"
#include "../../renderer/render_engine.h"
#include "osd_common.h"
#include "osd.h"
#include "osd_stats_dev.h"
#include "osd_stats_radio.h"
#include "../local_stats.h"
#include "../launchers_controller.h"
#include "../link_watch.h"
#include "../pairing.h"
#include "../timers.h"

float s_fOSDStatsGraphLinesAlpha = 0.9;
float s_fOSDStatsGraphBottomLinesAlpha = 0.6;
float s_OSDStatsLineSpacing = 1.0;
float s_fOSDStatsMargin = 0.016;
bool s_bDebugStatsShowAll = false;

static int s_iPeakTotalPacketsInBuffer = 0;
static int s_iPeakCountRender = 0;

static u32 s_uStatsRenderRCCount = 0;

u32 s_idFontStats = 0;
u32 s_idFontStatsSmall = 0;

#define SNAPSHOT_HISTORY_SIZE 3

static u32 s_uOSDSnapshotTakeTime = 0;
static u32 s_uOSDSnapshotCount = 0;
static u32 s_uOSDSnapshotLastDiscardedBuffers = 0;
static u32 s_uOSDSnapshotLastDiscardedSegments = 0;
static u32 s_uOSDSnapshotLastDiscardedPackets = 0;
static t_packet_header_ruby_telemetry_extended_extra_info_retransmissions s_Snapshot_PHRTE_Retransmissions;

static u32 s_uOSDSnapshotHistoryDiscardedBuffers[SNAPSHOT_HISTORY_SIZE];
static u32 s_uOSDSnapshotHistoryDiscardedSegments[SNAPSHOT_HISTORY_SIZE];
static u32 s_uOSDSnapshotHistoryDiscardedPackets[SNAPSHOT_HISTORY_SIZE];

static shared_mem_radio_stats s_OSDSnapshot_RadioStats;
static shared_mem_video_stream_stats_rx_processors s_OSDSnapshot_VideoDecodeStats;
static shared_mem_video_stream_stats_history_rx_processors s_OSDSnapshot_VideoDecodeHist;
static shared_mem_controller_retransmissions_stats_rx_processors s_OSDSnapshot_ControllerVideoRetransmissionsStats;

#define MAX_OSD_COLUMNS 10

static int   s_iCountOSDStatsBoundingBoxes;
static float s_iOSDStatsBoundingBoxesX[50];
static float s_iOSDStatsBoundingBoxesY[50];
static float s_iOSDStatsBoundingBoxesW[50];
static float s_iOSDStatsBoundingBoxesH[50];
static int   s_iOSDStatsBoundingBoxesIds[50];
static int   s_iOSDStatsBoundingBoxesColumns[50];
static float s_fOSDStatsColumnsHeights[MAX_OSD_COLUMNS];
static float s_fOSDStatsColumnsWidths[MAX_OSD_COLUMNS];

static float s_fOSDStatsSpacingH = 0.0;
static float s_fOSDStatsSpacingV = 0.0;
static float s_fOSDStatsMarginH = 0.0;
static float s_fOSDStatsMarginVBottom = 0.0;
static float s_fOSDStatsMarginVTop = 0.0;

static float s_fOSDStatsWindowsMinimBoxHeight = 0.0;
static float s_fOSDVideoDecodeWidthZoom = 1.0;


static u32 s_uOSDMaxFrameDeviationCamera = 0;
static u32 s_uOSDMaxFrameDeviationTx = 0;
static u32 s_uOSDMaxFrameDeviationRx = 0;
static u32 s_uOSDMaxFrameDeviationPlayer = 0;

void _osd_stats_draw_line(float xLeft, float xRight, float y, u32 uFontId, const char* szTextLeft, const char* szTextRight)
{
   g_pRenderEngine->drawText(xLeft, y, uFontId, szTextLeft);
   if ( (NULL != szTextRight) && (0 != szTextRight[0]) )
      g_pRenderEngine->drawTextLeft(xRight, y, uFontId, szTextRight);
}

void osd_stats_init()
{
   s_uStatsRenderRCCount = 0;
   s_uOSDMaxFrameDeviationCamera = 0;
   s_uOSDMaxFrameDeviationTx = 0;
   s_uOSDMaxFrameDeviationRx = 0;
   s_uOSDMaxFrameDeviationPlayer = 0;
}


void osd_stats_video_decode_snapshot_update(int iDeveloperMode, shared_mem_radio_stats* pSM_RadioStats, shared_mem_video_stream_stats* pVDS, shared_mem_video_stream_stats_history* pVDSH, shared_mem_video_stream_stats_rx_processors* pSM_VideoStats, shared_mem_video_stream_stats_history_rx_processors* pSM_VideoHistoryStats, shared_mem_controller_retransmissions_stats_rx_processors* pSM_ControllerRetransmissionsStats, shared_mem_controller_retransmissions_stats* pCRS)
{
   Preferences* p = get_Preferences();
   ControllerSettings* pCS = get_ControllerSettings();

   if ( g_bHasVideoDecodeStatsSnapshot && ( s_uOSDSnapshotTakeTime > 1 ) && (pCS->iVideoDecodeStatsSnapshotClosesOnTimeout == 1) )
   if ( g_TimeNow > s_uOSDSnapshotTakeTime + 10000 )
   {
      g_bHasVideoDecodeStatsSnapshot = false;
      s_uOSDSnapshotTakeTime = 1;
   }

   // Initialize for the first time
   if ( 0 == s_uOSDSnapshotTakeTime )
   {
      s_uOSDSnapshotTakeTime = 1;
      s_uOSDSnapshotCount = 0;

      s_uOSDSnapshotLastDiscardedBuffers = pVDS->total_DiscardedBuffers;
      s_uOSDSnapshotLastDiscardedSegments = pVDS->total_DiscardedSegments;
      s_uOSDSnapshotLastDiscardedPackets = pVDS->total_DiscardedLostPackets;

      for( int i=0; i<SNAPSHOT_HISTORY_SIZE; i++ )
      {
         s_uOSDSnapshotHistoryDiscardedBuffers[i] = 0;
         s_uOSDSnapshotHistoryDiscardedSegments[i] = 0;
         s_uOSDSnapshotHistoryDiscardedPackets[i] = 0;
      }
   }

   bool bMustTakeSnapshot = false;
   if ( g_TimeNow > g_RouterIsReadyTimestamp + 5000 )
   if ( s_uOSDSnapshotLastDiscardedBuffers != pVDS->total_DiscardedBuffers ||
        s_uOSDSnapshotLastDiscardedSegments != pVDS->total_DiscardedSegments ||
        s_uOSDSnapshotLastDiscardedPackets != pVDS->total_DiscardedLostPackets )
      bMustTakeSnapshot = true;

   bool bHasActivity = false;
   bool bHasSilenceAtStart = true;
   for ( int i=0; i<12; i++ )
   {
      if ( i >= MAX_HISTORY_VIDEO_INTERVALS )
         break;
      if ( i < 5 )
      if ( (pCRS->history[i].uCountRequestedRetransmissions > 0) ||
           (pCRS->history[i].uCountRequestedPacketsForRetransmission > 0) ||
           (pVDSH->missingTotalPacketsAtPeriod[i] > 0) )
         bHasSilenceAtStart = false;

      if ( i >= 5 )
      if ( (pCRS->history[i].uCountRequestedRetransmissions > 0) ||
           (pCRS->history[i].uCountRequestedPacketsForRetransmission > 0) ||
           (pVDSH->missingTotalPacketsAtPeriod[i] > 0) )
         bHasActivity = true;

      if ( i > 5 )
      if ( bHasActivity && bHasSilenceAtStart )
      {
         bMustTakeSnapshot = true;
         break;
      }
   }

   if ( bMustTakeSnapshot )
   {
      bool bTakeSnapshot = false;
      if ( !g_bHasVideoDecodeStatsSnapshot )
         bTakeSnapshot = true;
      else if ( ( pCS->iVideoDecodeStatsSnapshotClosesOnTimeout == 2 ) && (g_TimeNow > s_uOSDSnapshotTakeTime+500) )
         bTakeSnapshot = true;
      if ( bTakeSnapshot )
      {
         s_uOSDSnapshotTakeTime = g_TimeNow;
         s_uOSDSnapshotCount++;
         g_bHasVideoDecodeStatsSnapshot = true;

         for( int i=SNAPSHOT_HISTORY_SIZE-1; i>0; i-- )
         {
            s_uOSDSnapshotHistoryDiscardedBuffers[i] = s_uOSDSnapshotHistoryDiscardedBuffers[i-1];
            s_uOSDSnapshotHistoryDiscardedSegments[i] = s_uOSDSnapshotHistoryDiscardedSegments[i-1];
            s_uOSDSnapshotHistoryDiscardedPackets[i] = s_uOSDSnapshotHistoryDiscardedPackets[i-1];
         }

         s_uOSDSnapshotHistoryDiscardedBuffers[0] = pVDS->total_DiscardedBuffers - s_uOSDSnapshotLastDiscardedBuffers;
         s_uOSDSnapshotHistoryDiscardedSegments[0] = pVDS->total_DiscardedSegments - s_uOSDSnapshotLastDiscardedSegments;
         s_uOSDSnapshotHistoryDiscardedPackets[0] = pVDS->total_DiscardedLostPackets - s_uOSDSnapshotLastDiscardedPackets;

         s_uOSDSnapshotLastDiscardedBuffers = pVDS->total_DiscardedBuffers;
         s_uOSDSnapshotLastDiscardedSegments = pVDS->total_DiscardedSegments;
         s_uOSDSnapshotLastDiscardedPackets = pVDS->total_DiscardedLostPackets;

         memcpy( &s_OSDSnapshot_RadioStats, pSM_RadioStats, sizeof(shared_mem_radio_stats));
         memcpy( &s_OSDSnapshot_VideoDecodeStats, pSM_VideoStats, sizeof(shared_mem_video_stream_stats_rx_processors));
         memcpy( &s_OSDSnapshot_VideoDecodeHist, pSM_VideoHistoryStats, sizeof(shared_mem_video_stream_stats_history_rx_processors));
         memcpy( &s_OSDSnapshot_ControllerVideoRetransmissionsStats, pSM_ControllerRetransmissionsStats, sizeof(shared_mem_controller_retransmissions_stats_rx_processors));
         memcpy( &s_Snapshot_PHRTE_Retransmissions, &(g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtraInfoRetransmissions), sizeof(t_packet_header_ruby_telemetry_extended_extra_info_retransmissions));
      }
   }
}

float osd_render_stats_video_decode_get_height(int iDeveloperMode, bool bIsSnapshot, shared_mem_radio_stats* pSM_RadioStats, shared_mem_video_stream_stats_rx_processors* pSM_VideoStats, shared_mem_video_stream_stats_history_rx_processors* pSM_VideoHistoryStats, shared_mem_controller_retransmissions_stats_rx_processors* pSM_ControllerRetransmissionsStats, float scale)
{
   Model* pActiveModel = osd_get_current_data_source_vehicle_model();
   u32 uActiveVehicleId = osd_get_current_data_source_vehicle_id();

   if ( NULL == pActiveModel )
      return 0.0;

   ControllerSettings* pCS = get_ControllerSettings();
   float height_text = g_pRenderEngine->textHeight(s_idFontStats);
   float height_text_small = g_pRenderEngine->textHeight(s_idFontStatsSmall);
   float hGraph = 0.04;
   float hGraphHistory = 2.2*height_text;
   
   float height = 2.0 *s_fOSDStatsMargin*1.0;

   bool bIsMinimal = false;
   bool bIsCompact = false;
   bool bIsNormal = false;
   bool bIsExtended = false;

   if ( pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_MINIMAL_VIDEO_DECODE_STATS )
      bIsMinimal = true;
   else if ( pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS )
      bIsCompact = true;
   else if ( pActiveModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_EXTENDED_VIDEO_DECODE_STATS )
      bIsExtended = true;
   else
      bIsNormal = true;
   
   // Title
   //if ( ! bIsMinimal )
      height += height_text*s_OSDStatsLineSpacing;

   // Stream info
   if ( bIsMinimal || bIsCompact || bIsNormal || bIsExtended )
     height += height_text_small*s_OSDStatsLineSpacing;

   // EC Scheme
   if ( bIsCompact || bIsNormal || bIsExtended )
      height += height_text*s_OSDStatsLineSpacing;
  
   // Dynamic Params
   if ( bIsCompact || bIsNormal || bIsExtended )
      height += 7.2*height_text*s_OSDStatsLineSpacing;
   if ( bIsNormal || bIsExtended )
      height += height_text*s_OSDStatsLineSpacing;

   // Rx packets Buffers
   if ( bIsExtended )
   {
      float hBar = 0.014*scale;
      height += hBar + height_text;
   }

   // History graph
   height += height_text_small*1.2;
   height += hGraphHistory;
   height += height_text_small*0.3;
  
   if ( iDeveloperMode )
   if ( bIsExtended )
   {
      // Max EC packets used per interval
      height += height_text_small*1.2;
      height += hGraph*0.6;

      // History gap graph
      height += height_text_small*1.2;
      height += hGraph*0.6;

      // Pending good blocks graph
      height += height_text_small*1.2;
      height += hGraph*0.8;
   }


   if ( bIsNormal || bIsExtended )
      height += height_text * 2.0;
   if ( bIsExtended )
      height += height_text * 3.0;

   // History requested retransmissions

   if ( bIsNormal || bIsExtended )
   if ( iDeveloperMode )
   {
      height += height_text_small*2.0;
      height += hGraph;
   }

   // Dev requested Retransmissions
   if ( iDeveloperMode )
     height += 5.0*height_text*s_OSDStatsLineSpacing;

   return height;
}

float osd_render_stats_video_decode_get_width(int iDeveloperMode, bool bIsSnapshot, shared_mem_radio_stats* pSM_RadioStats, shared_mem_video_stream_stats_rx_processors* pSM_VideoStats, shared_mem_video_stream_stats_history_rx_processors* pSM_VideoHistoryStats, shared_mem_controller_retransmissions_stats_rx_processors* pSM_ControllerRetransmissionsStats, float scale)
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
   {
      if ( (!bIsSnapshot) && (!(osd_get_current_data_source_vehicle_model()->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS)) )
         return g_fOSDStatsForcePanelWidth*s_fOSDVideoDecodeWidthZoom;
      else
         return g_fOSDStatsForcePanelWidth;
   }
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAAAAA AAAAAAAA A");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   if ( NULL != osd_get_current_data_source_vehicle_model() && ((osd_get_current_data_source_vehicle_model()->osd_params.osd_flags[osd_get_current_layout_index()]) & OSD_FLAG_EXTENDED_VIDEO_DECODE_STATS) )
      width *= 1.55;
   return width;
}

float osd_render_stats_video_decode(float xPos, float yPos, int iDeveloperMode, bool bIsSnapshot, shared_mem_radio_stats* pSM_RadioStats, shared_mem_video_stream_stats_rx_processors* pSM_VideoStats, shared_mem_video_stream_stats_history_rx_processors* pSM_VideoHistoryStats, shared_mem_controller_retransmissions_stats_rx_processors* pSM_ControllerRetransmissionsStats, float scale)
{
   if ( NULL == osd_get_current_data_source_vehicle_model() || NULL == pSM_RadioStats || NULL == pSM_VideoStats || NULL == pSM_VideoHistoryStats || NULL == pSM_ControllerRetransmissionsStats )
      return 0.0;

   Model* pActiveModel = osd_get_current_data_source_vehicle_model();
   u32 uActiveVehicleId = osd_get_current_data_source_vehicle_id();

   if ( NULL == pActiveModel )
      return 0.0;

   shared_mem_video_stream_stats* pVDS = NULL;
   shared_mem_video_stream_stats_history* pVDSH = NULL;
   shared_mem_controller_retransmissions_stats* pCRS = NULL;
   
   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( pSM_VideoStats->video_streams[i].uVehicleId == uActiveVehicleId )
      {
         pVDS = &(pSM_VideoStats->video_streams[i]);
         pVDSH = &(pSM_VideoHistoryStats->video_streams[i]);
         break;
      }
   }

   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( pSM_ControllerRetransmissionsStats->video_streams[i].uVehicleId == uActiveVehicleId )
      {
         pCRS = &(pSM_ControllerRetransmissionsStats->video_streams[i]);
         break;
      }
   }

   if ( (NULL == pVDS) || (NULL == pCRS) || (NULL == pActiveModel) )
      return 0.0;

   int iIndexRouterRuntimeInfo = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i] == uActiveVehicleId )
         iIndexRouterRuntimeInfo = i;
   }
   if ( -1 == iIndexRouterRuntimeInfo )
       return 0.0;

   bool bIsMinimal = false;
   bool bIsCompact = false;
   bool bIsNormal = false;
   bool bIsExtended = false;

   if ( pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_MINIMAL_VIDEO_DECODE_STATS )
      bIsMinimal = true;
   else if ( pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS )
      bIsCompact = true;
   else if ( pActiveModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_EXTENDED_VIDEO_DECODE_STATS )
      bIsExtended = true;
   else
      bIsNormal = true;

   Preferences* p = get_Preferences();
   ControllerSettings* pCS = get_ControllerSettings();

   if ( ! bIsSnapshot )
      osd_stats_video_decode_snapshot_update(iDeveloperMode, pSM_RadioStats, pVDS, pVDSH, pSM_VideoStats, pSM_VideoHistoryStats, pSM_ControllerRetransmissionsStats, pCRS);

   float video_mbps = pSM_RadioStats->radio_streams[0][STREAM_ID_VIDEO_1].rxBytesPerSec*8/1000.0/1000.0;

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( pSM_RadioStats->radio_streams[i][0].uVehicleId == pActiveModel->vehicle_id )
      {
         video_mbps = pSM_RadioStats->radio_streams[i][STREAM_ID_VIDEO_1].rxBytesPerSec*8/1000.0/1000.0;
         break;
      }
   }

   s_iPeakCountRender++;
   float height_text = g_pRenderEngine->textHeight(s_idFontStats);
   float height_text_small = g_pRenderEngine->textHeight(s_idFontStatsSmall);
   float hGraph = 0.04;
   float hGraphHistory = 2.2*height_text;

   float width = osd_render_stats_video_decode_get_width(iDeveloperMode, bIsSnapshot, pSM_RadioStats, pSM_VideoStats, pSM_VideoHistoryStats, pSM_ControllerRetransmissionsStats, scale);
   float height = osd_render_stats_video_decode_get_height(iDeveloperMode, bIsSnapshot, pSM_RadioStats, pSM_VideoStats, pSM_VideoHistoryStats, pSM_ControllerRetransmissionsStats, scale);
   float wPixel = g_pRenderEngine->getPixelWidth();

   char szBuff[64];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin;
   width -= 2*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;
   
   float y = yPos;

   // -------------------------
   // Begin - Title

   //if ( ! bIsMinimal )
   {
      if ( bIsSnapshot )
      {
         sprintf(szBuff, "Snapshot %d", s_uOSDSnapshotCount);
         g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, szBuff);
      }
      else
      {
         //if ( ! (pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS) )
         g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, "Video Stream");
      
         szBuff[0] = 0;
         char szMode[32];
         //strcpy(szMode, str_get_video_profile_name(pVDS->video_link_profile & 0x0F));
         //if ( pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS )
         //   szMode[0] = 0;
         strcpy(szMode, str_get_video_profile_name(pVDS->video_link_profile & 0x0F));
         int diffEC = pVDS->fec_packets_per_block - pActiveModel->video_link_profiles[pVDS->video_link_profile & 0x0F].block_fecs;

         if ( diffEC > 0 )
         {
            char szTmp[16];
            sprintf(szTmp, "-%d", diffEC);
            strcat(szMode, szTmp);
         }

         if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )
            strcat(szMode, "-");
     
         sprintf(szBuff, "%s %.1f Mbs", szMode, video_mbps);
         if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )
            sprintf(szBuff, "%s- %.1f Mbs", szMode, video_mbps);
         if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfo )
         {
            sprintf(szBuff, "%s %.1f (%.1f) Mbs", szMode, video_mbps, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtended.downlink_tx_video_bitrate_bps/1000.0/1000.0);
            if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )
               sprintf(szBuff, "%s- %.1f (%.1f) Mbs", szMode, video_mbps, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtended.downlink_tx_video_bitrate_bps/1000.0/1000.0);
         }
         u32 uRealDataRate = pActiveModel->getLinkRealDataRate(0);
         if ( pActiveModel->radioLinksParams.links_count > 1 )
         if ( pActiveModel->getLinkRealDataRate(1) > uRealDataRate )
            uRealDataRate = pActiveModel->getLinkRealDataRate(1);

         if ( video_mbps*1000*1000 >= (float)(uRealDataRate) * DEFAULT_VIDEO_LINK_MAX_LOAD_PERCENT / 100.0 )
            g_pRenderEngine->setColors(get_Color_IconWarning());
         
         if ( g_bHasVideoDataOverloadAlarm && (g_TimeLastVideoDataOverloadAlarm > 0) && (g_TimeNow <  g_TimeLastVideoDataOverloadAlarm + 5000) )
            g_pRenderEngine->setColors(get_Color_IconError());

         if ( ! (pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_COMPACT_VIDEO_DECODE_STATS) )
         {
            osd_show_value_left(xPos+width,yPos, szBuff, s_idFontStats);
            //y += height_text*1.3*s_OSDStatsLineSpacing;
         }
         else
            osd_show_value_left(xPos+width,yPos, szBuff, s_idFontStatsSmall);
      }
   
      y += height_text*s_OSDStatsLineSpacing;
   }
   // End - Title
   // -------------------------

   osd_set_colors();


   char szCurrentProfile[64];
   szCurrentProfile[0] = 0;
   strcpy(szCurrentProfile, str_get_video_profile_name(pVDS->video_link_profile & 0x0F));
   if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )
      strcat(szCurrentProfile, "-");

   // Stream Info

   if ( bIsMinimal || bIsCompact || bIsNormal || bIsExtended )
   {
      u32 uVehicleIdVideo = 0;
      if ( (osd_get_current_data_source_vehicle_index() >= 0) && (osd_get_current_data_source_vehicle_index() < MAX_CONCURENT_VEHICLES) )
         uVehicleIdVideo = g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uVehicleId;
      if ( link_has_received_videostream(uVehicleIdVideo) )
      {
         strcpy(szBuff, "N/A");
         for( int i=0; i<getOptionsVideoResolutionsCount(); i++ )
            if ( g_iOptionsVideoWidth[i] == pVDS->width )
            if ( g_iOptionsVideoHeight[i] == pVDS->height )
            {
               sprintf(szBuff, "%s %s %d fps %d ms KF", szCurrentProfile, g_szOptionsVideoRes[i], pVDS->fps, pVDS->keyframe_ms);
               break;
            }

      }
      else
         sprintf(szBuff, "Stream: N/A");
      g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);
      y += height_text_small*s_OSDStatsLineSpacing;
   }

   u32 msData = 0;
   u32 msFEC = 0;
   u32 videoBitrate = 1;
   videoBitrate = pActiveModel->video_link_profiles[(pVDS->video_link_profile & 0x0F)].bitrate_fixed_bps;
   msData = (1000*8*pVDS->data_packets_per_block*pVDS->video_packet_length)/(pActiveModel->video_link_profiles[(pVDS->video_link_profile & 0x0F)].bitrate_fixed_bps+1);
   msFEC = (1000*8*pVDS->fec_packets_per_block*pVDS->video_packet_length)/(pActiveModel->video_link_profiles[(pVDS->video_link_profile & 0x0F)].bitrate_fixed_bps+1);

   if ( bIsCompact || bIsNormal || bIsExtended )
   {
      if ( pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].keyframe_ms > 0 )
         sprintf(szBuff, "EC: %s %d/%d/%d, %d ms KF (Fixed)", szCurrentProfile, pVDS->data_packets_per_block, pVDS->fec_packets_per_block, pVDS->video_packet_length, pVDS->keyframe_ms);
      else
         sprintf(szBuff, "EC: %s %d/%d/%d, %d ms KF (Auto)", szCurrentProfile, pVDS->data_packets_per_block, pVDS->fec_packets_per_block, pVDS->video_packet_length, pVDS->keyframe_ms);
      g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
   }
  

   // --------------------------------------
   // Begin - Dynamic params

   if ( bIsCompact || bIsNormal || bIsExtended )
   {
      strcpy(szBuff, "Retransmissions: ");
      float wtmp = g_pRenderEngine->textWidth(s_idFontStats, szBuff);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);

      if ( ! pVDS->isRetransmissionsOn )
      {
         g_pRenderEngine->setColors(get_Color_IconWarning());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, "Off");
         osd_set_colors();
      }
      else
      {
         char szBuff3[64];
         strcpy(szBuff3, "On");
         g_pRenderEngine->setColors(get_Color_IconSucces());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff3);
         osd_set_colors();

         //sprintf(szBuff, "Params: 2Way / %s %s / %d ms / %d ms / %d ms", szDynamic,
         //       szBuff3, 5*(((pVDS->encoding_extra_flags) & 0xFF00) >> 8), pCS->nRetryRetransmissionAfterTimeoutMS, pCS->nRequestRetransmissionsOnVideoSilenceMs );
            
         sprintf(szBuff3, " Max %d ms", 5*(((pVDS->encoding_extra_flags) & 0xFF00) >> 8));
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         if ( bIsNormal || bIsExtended )
         {
            y += height_text*s_OSDStatsLineSpacing;
            sprintf(szBuff3, " Retry %d ms", pCS->nRetryRetransmissionAfterTimeoutMS);
            g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         }
      }
      y += height_text*s_OSDStatsLineSpacing; // line 1
      
      //g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry

      strcpy(szBuff, "Adaptive Video: ");
      wtmp = g_pRenderEngine->textWidth(s_idFontStats, szBuff);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);

      if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK_PARAMS )
      {
         char szBuff3[64];
         sprintf(szBuff3, "On");
         g_pRenderEngine->setColors(get_Color_IconSucces());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff3);
         osd_set_colors();

         sprintf(szBuff3, " Dynamic %d", pActiveModel->video_params.videoAdjustmentStrength);
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
      }
      else
      {
         g_pRenderEngine->setColors(get_Color_IconWarning());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, "Off");
         osd_set_colors();
      }

      y += height_text*s_OSDStatsLineSpacing; // line 2
      
      char szVideoLevelRecv[64];
      strcpy(szVideoLevelRecv, str_get_video_profile_name(pVDS->video_link_profile & 0x0F));
      sprintf(szBuff, "Auto Quantisation (for %s): ", szVideoLevelRecv);
      wtmp = g_pRenderEngine->textWidth(s_idFontStats, szBuff);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);

      if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_VIDEO_ADAPTIVE_QUANTIZATION )
      {
         sprintf(szBuff, "On ");
         g_pRenderEngine->setColors(get_Color_IconSucces());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff);
         osd_set_colors();

         sprintf(szBuff, "(%d)", (int)(pVDS->uEncodingExtraFlags2 & 0xFF));
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff);
         
         //sprintf(szBuff3, " Dynamic %d", pActiveModel->video_params.videoAdjustmentStrength);
         //g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
      }
      else
      {
         g_pRenderEngine->setColors(get_Color_IconWarning());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, "Off");
         osd_set_colors();
      }

      y += height_text*s_OSDStatsLineSpacing; // line 3
      
      strcpy(szBuff, "Keyframes: ");
      wtmp = g_pRenderEngine->textWidth(s_idFontStats, szBuff);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);

      if ( pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].keyframe_ms > 0 )
      {
         char szBuff3[64];
         sprintf(szBuff3, "Fixed");
         g_pRenderEngine->setColors(get_Color_IconWarning());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff3);
         osd_set_colors();
         sprintf(szBuff, " %d ms", pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].keyframe_ms );
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff);
      }
      else
      {
         char szBuff3[64];
         sprintf(szBuff3, "Auto");
         g_pRenderEngine->setColors(get_Color_IconSucces());
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff3);
         wtmp += g_pRenderEngine->textWidth(s_idFontStats, szBuff3);

         static int sl_iLastKeyframeValue = 0;
         static u32 sl_uLastKeyframeValueChange = 0;
         if ( pVDS->keyframe_ms != sl_iLastKeyframeValue )
         {
            sl_iLastKeyframeValue = pVDS->keyframe_ms;
            sl_uLastKeyframeValueChange = g_TimeNow;
         }

         if ( g_TimeNow < sl_uLastKeyframeValueChange+1000 )
            g_pRenderEngine->setColors(get_Color_OSDChangedValue());
         else
            osd_set_colors();
         sprintf(szBuff, " %d ms", pVDS->keyframe_ms );
         g_pRenderEngine->drawText(xPos + wtmp, y, s_idFontStats, szBuff);
         osd_set_colors();
      }
      y += height_text*s_OSDStatsLineSpacing; // line 4

      char szBuff2[64];
      str_format_bitrate((int)(pVDS->uLastSetVideoBitrate & 0x7FFFFFFF), szBuff);
      if ( pVDS->uLastSetVideoBitrate & (1<<31) )
         sprintf(szBuff2, "(Def) %s", szBuff);
      else
         sprintf(szBuff2, "(Adj) %s", szBuff);
      
      sprintf(szBuff, "Set bitrate: %s", szBuff2);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
      //_osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "Set bitrate:", szBuff2);
      y += height_text*s_OSDStatsLineSpacing;

      
      strcpy(szBuff, "Video Level (Req / Ack / Recv): ");
      wtmp = g_pRenderEngine->textWidth(s_idFontStats, szBuff);
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing; // line 5

      char szVideoLevelRequested[64];
      char szVideoLevelAck[64];
      strcpy(szVideoLevelRequested, osd_format_video_adaptive_level(pActiveModel, g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndexRouterRuntimeInfo].iCurrentTargetLevelShift) );
      strcpy(szVideoLevelAck, osd_format_video_adaptive_level(pActiveModel, pVDS->iLastAckVideoLevelShift) );
      
      int diffEC = pVDS->fec_packets_per_block - pActiveModel->video_link_profiles[pVDS->video_link_profile & 0x0F].block_fecs;
      if ( diffEC > 0 )
      {
         char szTmp[16];
         sprintf(szTmp, "-%d", diffEC);
         strcat(szVideoLevelRecv, szTmp);
      }
      else if ( pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )
         strcat(szVideoLevelRecv, "-");
      
      sprintf(szBuff, "%s / %s / %s", szVideoLevelRequested, szVideoLevelAck, szVideoLevelRecv);

      static u32 sl_uLastTimeChangedVideoLevelInfo = 0;
      static char sl_szLastVideoLevelInfo[128];
      if ( 0 == sl_uLastTimeChangedVideoLevelInfo )
         sl_szLastVideoLevelInfo[0] = 0;
      if ( 0 != strcmp(szBuff, sl_szLastVideoLevelInfo) )
      {
         strcpy(sl_szLastVideoLevelInfo, szBuff);
         sl_uLastTimeChangedVideoLevelInfo = g_TimeNow;
      }

      if ( g_TimeNow < sl_uLastTimeChangedVideoLevelInfo + 1000 )
         g_pRenderEngine->setColors(get_Color_OSDChangedValue());
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);      
      osd_set_colors(); 
      y += height_text*s_OSDStatsLineSpacing; // line 6

      y += 0.2*height_text*s_OSDStatsLineSpacing;
   }

   // End - Dynamic params
   // --------------------------------------

   int maxPacketsCount = 1;
   if ( 0 < pVDS->maxPacketsInBuffers )
      maxPacketsCount = pVDS->maxPacketsInBuffers;

   float fWarningFullPercent = 0.5;

  
   // ---------------------------------------
   // Begin - Draw Rx packets buffer

   if ( bIsExtended )
   {
      y += height_text*0.1;
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Buffers:");
      float wPrefix = g_pRenderEngine->textWidth(s_idFontStats, "Buffers:") + 0.02*scale;

      float hBar = 0.014*scale;
      width -= wPrefix;
      osd_set_colors_background_fill();
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      g_pRenderEngine->setStroke(255,255,255,1.0);
      if ( pVDS->currentPacketsInBuffers > fWarningFullPercent*maxPacketsCount )
         g_pRenderEngine->setStroke(250,0,0, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawRoundRect(xPos+wPrefix, y, width, hBar, 0.003*scale);
      wPrefix += 2.0/g_pRenderEngine->getScreenWidth();
      width -= 4.0/g_pRenderEngine->getScreenWidth();
      y += 2.0/g_pRenderEngine->getScreenHeight();
      hBar -= 4.0/g_pRenderEngine->getScreenHeight();

      float fBarScale = width/100.0;
      int packs = pVDS->maxPacketsInBuffers;
      if ( packs > 0 )
         fBarScale = width/(float)(packs+1); // max buffer length

      g_pRenderEngine->setFill(145,45,45, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setStroke(0,0,0,0);
      g_pRenderEngine->setStrokeSize(0);
   
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      double* pc = get_Color_OSDText();
      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      if ( pVDS->currentPacketsInBuffers > s_iPeakTotalPacketsInBuffer )
         s_iPeakTotalPacketsInBuffer = pVDS->currentPacketsInBuffers;
      packs = pVDS->maxPacketsInBuffers;
      if ( s_iPeakTotalPacketsInBuffer > packs )
         s_iPeakTotalPacketsInBuffer = packs+1;
      g_pRenderEngine->drawLine(xPos + wPrefix + fBarScale*s_iPeakTotalPacketsInBuffer, y, xPos + wPrefix + fBarScale*s_iPeakTotalPacketsInBuffer, y+hBar);
      g_pRenderEngine->drawLine(xPos + wPrefix + fBarScale*s_iPeakTotalPacketsInBuffer+wPixel, y, xPos + wPrefix + fBarScale*s_iPeakTotalPacketsInBuffer+wPixel, y+hBar);

      //if ( 0 == (s_iPeakCountRender % 3) )
      if ( s_iPeakTotalPacketsInBuffer > 0 )
         s_iPeakTotalPacketsInBuffer--;

      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      
      g_pRenderEngine->drawRect(xPos+wPrefix+1.0/g_pRenderEngine->getScreenWidth(), y, fBarScale*pVDS->currentPacketsInBuffers-2.0/g_pRenderEngine->getScreenWidth(), hBar);
      osd_set_colors();
      
      hBar += 4.0/g_pRenderEngine->getScreenHeight();
      sprintf(szBuff, "%d", pVDS->maxPacketsInBuffers);
      g_pRenderEngine->drawText(xPos + wPrefix-2.0/g_pRenderEngine->getScreenWidth(), y+hBar, s_idFontStatsSmall, "0");
      g_pRenderEngine->drawTextLeft(xPos + wPrefix + width-2.0/g_pRenderEngine->getScreenWidth(), y+hBar, s_idFontStatsSmall, szBuff);

      y += hBar;
      y += height_text*0.9;
   }

   // End - Draw rx packets buffers
   // ---------------------------------------

   int iHistoryStartRetransmissionsIndex = MAX_HISTORY_VIDEO_INTERVALS;
   int iHistoryEndRetransmissionsIndex = 0;
   int totalHistoryValues = MAX_HISTORY_VIDEO_INTERVALS;

   if ( ! bIsExtended )
      totalHistoryValues = MAX_HISTORY_VIDEO_INTERVALS*3/4;

   if ( bIsSnapshot )
   {
      while ( (totalHistoryValues > 10) && 
              (pVDSH->missingTotalPacketsAtPeriod[totalHistoryValues-1] == 0) &&
              (pCRS->history[totalHistoryValues-1].uCountRequestedPacketsForRetransmission == 0) )
         totalHistoryValues--;
      iHistoryStartRetransmissionsIndex = totalHistoryValues;
      totalHistoryValues += 5;
      if ( totalHistoryValues > MAX_HISTORY_VIDEO_INTERVALS )
         totalHistoryValues = MAX_HISTORY_VIDEO_INTERVALS;

      while ( (iHistoryEndRetransmissionsIndex < iHistoryStartRetransmissionsIndex) && 
              (pVDSH->missingTotalPacketsAtPeriod[iHistoryEndRetransmissionsIndex] == 0) &&
              (pCRS->history[iHistoryEndRetransmissionsIndex].uCountRequestedPacketsForRetransmission == 0) )
      iHistoryEndRetransmissionsIndex++;    

      if ( totalHistoryValues < 24 )
         totalHistoryValues = 24;
   }


   //------------------------------------
   // Begin - Output history video packets graph
   
   float dxGraph = 0.01*scale;
   width = widthMax - dxGraph;
   float widthBar = width / totalHistoryValues;

   int maxGraphValue = 4;
   for( int i=0; i<totalHistoryValues; i++ )
   {
      if ( pVDSH->outputHistoryBlocksDiscardedPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksDiscardedPerPeriod[i];
      if ( pVDSH->outputHistoryBlocksBadPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksBadPerPeriod[i];
      if ( pVDSH->outputHistoryBlocksOkPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksOkPerPeriod[i];
      if ( pVDSH->outputHistoryBlocksReconstructedPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksReconstructedPerPeriod[i];
      if ( pVDSH->outputHistoryBlocksRetrasmitedPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksRetrasmitedPerPeriod[i];
      //if ( pCRS->history[i].uCountRequestedRetransmissions > maxGraphValue )
      //   maxGraphValue = pCRS->history[i].uCountRequestedRetransmissions;

      if ( pVDSH->outputHistoryBlocksRetrasmitedPerPeriod[i] + pVDSH->outputHistoryBlocksReconstructedPerPeriod[i] > maxGraphValue )
         maxGraphValue = pVDSH->outputHistoryBlocksRetrasmitedPerPeriod[i] + pVDSH->outputHistoryBlocksReconstructedPerPeriod[i];
   }

   y += height_text_small*0.2;
   sprintf(szBuff,"%d ms/bar, buff: max %d blocks", pVDSH->outputHistoryIntervalMs, pVDS->maxBlocksAllowedInBuffers);
   if ( bIsSnapshot )
   {
      sprintf(szBuff,"%d ms/bar, dx: %d", pVDSH->outputHistoryIntervalMs, iHistoryEndRetransmissionsIndex);
      g_pRenderEngine->drawTextLeft(xPos+widthMax, y, s_idFontStats, szBuff);
   }
   else
   {
      g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, s_idFontStatsSmall, szBuff);
      float w = g_pRenderEngine->textWidth(s_idFontStatsSmall, szBuff);

      sprintf(szBuff,"%.1f sec, ", (((float)totalHistoryValues) * pVDSH->outputHistoryIntervalMs)/1000.0);
      g_pRenderEngine->drawTextLeft(xPos+widthMax-w, y-height_text_small*0.2, s_idFontStatsSmall, szBuff);
      w += g_pRenderEngine->textWidth(s_idFontStatsSmall, szBuff);
   }

   y += height_text_small*1.0;

   float yStartGraphs = y;

   sprintf(szBuff, "%d", maxGraphValue);
   g_pRenderEngine->drawText(xPos, y-height_text_small*0.5, s_idFontStatsSmall, szBuff);
   g_pRenderEngine->drawText(xPos, y+hGraphHistory-height_text_small*0.5, s_idFontStatsSmall, "0");

   double* pc = get_Color_OSDText();
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphBottomLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphBottomLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraphHistory, xPos + dxGraph + width, y+hGraphHistory);
   float midLine = hGraphHistory/2.0;
   for( float i=0; i<=width-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(0);

   u32 maxHistoryPacketsGap = 0;
   float hBarBad = 0.0;
   float hBarMissing = 0.0;
   float hBarReconstructed = 0.0;
   float hBarOk = 0.0;
   float hBarRetransmitted = 0.0;
   float hBarBottom = 0.0;
   float yBottomGraph = y + hGraphHistory;// - 1.0/g_pRenderEngine->getScreenHeight();

   float xBarSt = xPos + widthMax - g_pRenderEngine->getPixelWidth();
   float xBarMid = xBarSt + widthBar*0.5;
   float xBarEnd = xBarSt + widthBar - g_pRenderEngine->getPixelWidth();

   float fWidthBarRect = widthBar-wPixel;
   if ( fWidthBarRect < 2.0 * wPixel )
      fWidthBarRect = widthBar;

   for( int i=0; i<totalHistoryValues; i++ )
   {
      xBarSt -= widthBar;
      xBarEnd -= widthBar;

      if ( pVDSH->outputHistoryBlocksDiscardedPerPeriod[i] > 0 )
      {
         hBarBad = hGraph;
         float hBarBadTop = yBottomGraph-hGraphHistory;
         if ( pVDSH->outputHistoryBlocksOkPerPeriod[i] > 0 ||
              pVDSH->outputHistoryBlocksReconstructedPerPeriod[i] > 0 )
         {
            hBarBad = hGraphHistory*0.5;
            if ( pVDSH->outputHistoryBlocksReconstructedPerPeriod[i] > 0 )
               g_pRenderEngine->setFill(0,200,30, s_fOSDStatsGraphLinesAlpha);
            else
               g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->drawRect(xBarSt, hBarBadTop + hBarBad, widthBar-wPixel, hBarBad);
         }
         if ( pVDSH->outputHistoryReceivedVideoPackets[i] > 0 || pVDSH->outputHistoryReceivedVideoRetransmittedPackets[i] > 0 )
            g_pRenderEngine->setFill(240,220,0,0.85);
         else
            g_pRenderEngine->setFill(240,20,0,0.85);
         g_pRenderEngine->drawRect(xBarSt, hBarBadTop, widthBar-wPixel, hBarBad);
         continue;
      }

      u32 val = pVDSH->outputHistoryBlocksMaxPacketsGapPerPeriod[i];
      if ( val > maxHistoryPacketsGap )
         maxHistoryPacketsGap = val;

      hBarBad = 0.0;
      hBarMissing = 0.0;
      hBarReconstructed = 0.0;
      hBarOk = 0.0;
      hBarRetransmitted = 0.0;
      float fPercentageUsed = 0.0;

      float percentBad = 0.0;
      percentBad = (float)(pVDSH->outputHistoryBlocksBadPerPeriod[i])/(float)(maxGraphValue);
      if ( percentBad > 1.0 ) percentBad = 1.0;
      if ( percentBad > 0.001 )
      {
         hBarBad = hGraphHistory*percentBad;
         g_pRenderEngine->setFill(240,220,0,0.65);
         g_pRenderEngine->drawRect(xBarSt, yBottomGraph - hGraphHistory * fPercentageUsed - hBarBad, fWidthBarRect, hBarBad);
         fPercentageUsed += percentBad;
         if ( fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0;
      }

      float percentMissing = (float)(pVDSH->outputHistoryBlocksMissingPerPeriod[i])/(float)(maxGraphValue);
      if ( percentMissing > 1.0 ) percentMissing = 1.0;
      if ( percentMissing > 0.001 )
      {
         if ( percentMissing + fPercentageUsed > 1.0 )
            percentMissing = 1.0 - fPercentageUsed;
         if ( percentMissing < 0.0 )
            percentMissing = 0.0;
         hBarMissing = hGraphHistory*percentMissing;
         g_pRenderEngine->setFill(240,0,0,0.74);
         g_pRenderEngine->drawRect(xBarSt, yBottomGraph - hGraphHistory * fPercentageUsed - hBarMissing, fWidthBarRect, hBarMissing);
         fPercentageUsed += percentMissing;
         if ( fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0;
      }

      float percentOk = (float)(pVDSH->outputHistoryBlocksOkPerPeriod[i])/(float)(maxGraphValue);
      if ( percentOk > 1.0 ) percentOk = 1.0;
      if ( percentOk > 0.001 )
      {
         if ( percentOk + fPercentageUsed > 1.0 )
            percentOk = 1.0 - fPercentageUsed;
         if ( percentOk < 0.0 )
            percentOk = 0.0;
         hBarOk = hGraphHistory*percentOk;
         if ( pVDSH->outputHistoryBlocksReconstructedPerPeriod[i] > 0 )
            g_pRenderEngine->setFill(pc[0]*0.9, pc[1]*0.9, pc[2]*0.6, s_fOSDStatsGraphLinesAlpha*0.5);
         else
            g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha*0.9);

         g_pRenderEngine->drawRect(xBarSt, yBottomGraph - hGraphHistory * fPercentageUsed - hBarOk, fWidthBarRect, hBarOk);
         fPercentageUsed += percentOk;
         if ( fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0;
      }
      float percentRecon = (float)(pVDSH->outputHistoryBlocksReconstructedPerPeriod[i])/(float)(maxGraphValue);
      if ( percentRecon > 1.0 ) percentRecon = 1.0;
      if ( percentRecon > 0.001 )
      {
         if ( percentRecon + fPercentageUsed > 1.0 )
            percentRecon = 1.0 - fPercentageUsed;
         if ( percentRecon < 0.0 )
            percentRecon = 0.0;
         hBarReconstructed = hGraphHistory*percentRecon;
         g_pRenderEngine->setFill(0,200,30, s_fOSDStatsGraphLinesAlpha);
         g_pRenderEngine->drawRect(xBarSt, yBottomGraph - hGraphHistory * fPercentageUsed - hBarReconstructed, fWidthBarRect, hBarReconstructed);
         fPercentageUsed += percentRecon;
         if ( fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0;
      }

      float percentRetrans = (float)(pVDSH->outputHistoryBlocksRetrasmitedPerPeriod[i])/(float)(maxGraphValue);
      if ( percentRetrans > 1.0 ) percentRetrans = 1.0;
      if ( percentRetrans > 0.001 )
      {
         if ( percentRetrans + fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0 - percentRetrans;
         
         hBarRetransmitted = hGraphHistory*percentRetrans;
         g_pRenderEngine->setFill(20,20,230, s_fOSDStatsGraphLinesAlpha);
         g_pRenderEngine->drawRect(xBarSt, yBottomGraph - hGraphHistory * fPercentageUsed - hBarRetransmitted, fWidthBarRect, hBarRetransmitted);
         fPercentageUsed += percentRetrans;
         if ( fPercentageUsed > 1.0 )
            fPercentageUsed = 1.0;
       
      }
      if ( 0 < pCRS->history[i].uCountRequestedRetransmissions )
      {
         if ( pCRS->history[i].uCountReRequestedPacketsForRetransmission > 0 )
            g_pRenderEngine->setFill(10,50,250, s_fOSDStatsGraphLinesAlpha);
         else
            g_pRenderEngine->setFill(10,200,210, s_fOSDStatsGraphLinesAlpha);

         float hPR = hGraphHistory*(0.1 + 0.25*(float)(pCRS->history[i].uCountRequestedRetransmissions)/(float)(maxGraphValue));
         g_pRenderEngine->drawRect(xBarSt, y, fWidthBarRect, hPR);
      }
   }

   for( int i=totalHistoryValues; i<MAX_HISTORY_VIDEO_INTERVALS; i++ )
   {
      u32 val = pVDSH->outputHistoryBlocksMaxPacketsGapPerPeriod[i];
      if ( val > maxHistoryPacketsGap )
         maxHistoryPacketsGap = val;
   }
   y += hGraphHistory + height_text_small*0.3;

   // End history buffer
   //----------------------------------------------------------------

   osd_set_colors();

   // Max EC packets used

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( bIsExtended )
   {
      g_pRenderEngine->setColors(get_Color_Dev());
      float maxValue = 2.0;
      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pVDSH->outputHistoryMaxECPacketsUsedPerPeriod[i] > maxValue )
            maxValue = pVDSH->outputHistoryMaxECPacketsUsedPerPeriod[i];
      }

      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, "Max EC packets used");
      y += height_text_small*1.0;

      sprintf(szBuff, "%d", (int)maxValue);
      g_pRenderEngine->drawText(xPos, y-height_text_small*0.5, s_idFontStatsSmall, szBuff);
      g_pRenderEngine->drawText(xPos, y+hGraph*0.6-height_text_small*0.5, s_idFontStatsSmall, "0");

      pc = get_Color_Dev();
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
      g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph*0.6, xPos + dxGraph + width, y+hGraph*0.6);
      midLine = hGraph*0.6/2.0;
      for( float i=0; i<=width-2.0*wPixel; i+= 5*wPixel )
         g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

      g_pRenderEngine->setStrokeSize(0);

      float hPixel = g_pRenderEngine->getPixelHeight();
      yBottomGraph = y + hGraph*0.6 - hPixel;

      xBarSt = xPos + widthMax - widthBar;
      xBarEnd = xBarSt + widthBar - g_pRenderEngine->getPixelWidth();
      xBarMid = xBarSt + widthBar*0.5;
      for( int i=0; i<totalHistoryValues; i++ )
      {
         float fPercent = (float)pVDSH->outputHistoryMaxECPacketsUsedPerPeriod[i]/maxValue;
         if ( pVDSH->outputHistoryMaxECPacketsUsedPerPeriod[i] > 0 )
            g_pRenderEngine->drawRect(xBarSt, yBottomGraph-hGraph*0.6*fPercent + hPixel, widthBar-wPixel, hGraph*0.6*fPercent);
         xBarSt -= widthBar;
         xBarMid -= widthBar;
         xBarEnd -= widthBar;
      }
      y += hGraph*0.6;
      y += height_text_small*0.2;
   }

   // Max EC packets used
   //---------------------------------------------

   // ----------------------------------------------------
   // History packets max gap graph

   if ( iDeveloperMode )
   if ( bIsExtended )
   {
      g_pRenderEngine->setColors(get_Color_Dev());

      g_pRenderEngine->drawTextLeft(xPos+widthMax, y, s_idFontStatsSmall, "Packets Max Gap");
      y += height_text_small*1.0;
      int maxGraphValue = 4;

      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pVDSH->outputHistoryBlocksMaxPacketsGapPerPeriod[i] > maxGraphValue )
            maxGraphValue = pVDSH->outputHistoryBlocksMaxPacketsGapPerPeriod[i];
      }

      sprintf(szBuff, "%d", maxGraphValue);
      g_pRenderEngine->drawText(xPos, y-height_text_small*0.5, s_idFontStatsSmall, szBuff);
      g_pRenderEngine->drawText( xPos, y+hGraph*0.6-height_text_small*0.5, s_idFontStatsSmall, "0");

      double* pc = get_Color_Dev();
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
      g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph*0.6, xPos + dxGraph + width, y+hGraph*0.6);
      float midLine = hGraph*0.6/2.0;
   
      for( float i=0.0; i<=width-2.0*wPixel; i+= 5.0*wPixel )
         g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

      g_pRenderEngine->setStrokeSize(0);
      float hBarGap = 0.0;
      yBottomGraph = y + hGraph*0.6 - 1.0/g_pRenderEngine->getScreenHeight();

      g_pRenderEngine->setFill(200,50,50, s_fOSDStatsGraphLinesAlpha);

      for( int i=0; i<totalHistoryValues; i++ )
      {
         hBarGap = 0;
         u8 val = pVDSH->outputHistoryBlocksMaxPacketsGapPerPeriod[i]; 
         if ( val > 0 )
         {
            hBarGap = hGraph*0.6 * (0.1 + 0.9 * val/(float)maxGraphValue);
            if ( hBarGap > hGraph*0.6 )
               hBarGap = hGraph*0.6;
            g_pRenderEngine->drawRect(xPos+widthMax-(i+1)*widthBar, yBottomGraph-hBarGap, widthBar-wPixel, hBarGap);
         }
      }

      y += hGraph*0.6;
      y += height_text_small*0.2;
   }

   // History packets max gap graph
   // ----------------------------------------------------

   // -----------------------------------------
   // Pending good blocks to output graph

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( bIsExtended )
   {
      g_pRenderEngine->setColors(get_Color_Dev());
      float maxValue = 1.0;
      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pVDSH->outputHistoryMaxGoodBlocksPendingPerPeriod[i] > maxValue )
            maxValue = pVDSH->outputHistoryMaxGoodBlocksPendingPerPeriod[i];
      }

      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, "Max good blocks pending output");
      y += height_text_small;

      sprintf(szBuff, "%d", (int)maxValue);
      g_pRenderEngine->drawText(xPos, y-height_text_small*0.5, s_idFontStatsSmall, szBuff);
      g_pRenderEngine->drawText(xPos, y+hGraph*0.8-height_text_small*0.5, s_idFontStatsSmall, "0");

      pc = get_Color_Dev();
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
      g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph*0.8, xPos + dxGraph + width, y+hGraph*0.8);
      midLine = hGraph*0.8/2.0;
      for( float i=0; i<=width-2.0*wPixel; i+= 5*wPixel )
         g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

      g_pRenderEngine->setStrokeSize(0);

      float hPixel = g_pRenderEngine->getPixelHeight();
      yBottomGraph = y + hGraph*0.8 - hPixel;

      xBarSt = xPos + widthMax - widthBar;
      xBarEnd = xBarSt + widthBar - g_pRenderEngine->getPixelWidth();
      xBarMid = xBarSt + widthBar*0.5;

      for( int i=0; i<totalHistoryValues; i++ )
      {
         float fPercent = (float)pVDSH->outputHistoryMaxGoodBlocksPendingPerPeriod[i]/maxValue;
         if ( pVDSH->outputHistoryMaxGoodBlocksPendingPerPeriod[i] > 0 )
            g_pRenderEngine->drawRect(xBarSt, yBottomGraph-hGraph*0.8*fPercent+hPixel, widthBar-wPixel, hGraph*0.8*fPercent);

         xBarSt -= widthBar;
         xBarMid -= widthBar;
         xBarEnd -= widthBar;
      }
      y += hGraph*0.8;
      y += height_text_small*0.2;
   }

   // Pending good blocks to output graph
   // -----------------------------------------

   osd_set_colors();

   if ( bIsNormal || bIsExtended )
   {
      int maxPacketsPerSec = 0;
      for( int i=0; i<MAX_CONCURENT_VEHICLES; i++)
      {
         if ( pSM_RadioStats->radio_streams[i][0].uVehicleId == pActiveModel->vehicle_id )
         {
            maxPacketsPerSec = pSM_RadioStats->radio_streams[0][STREAM_ID_VIDEO_1].rxPacketsPerSec;
            break;
         }
      }
      
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Received Packets/sec:");
      sprintf(szBuff, "%d", maxPacketsPerSec);
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
   }
   if ( bIsExtended )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Received Blocks/sec:");

      u32 uCountBlocksOut = 0;
      u32 uTimeMs = 0;
      for( int i=0; i<totalHistoryValues/2; i++ )
      {
         uCountBlocksOut += pVDSH->outputHistoryBlocksOkPerPeriod[i];
         uCountBlocksOut += pVDSH->outputHistoryBlocksReconstructedPerPeriod[i];
         uTimeMs += pVDSH->outputHistoryIntervalMs;
      }
      if ( uTimeMs != 0 )
         sprintf(szBuff, "%d", uCountBlocksOut * 1000 / uTimeMs );
      else
         sprintf(szBuff, "0");

      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
   }

   if ( bIsNormal || bIsExtended )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Discarded buff/seg/pack:");
      sprintf(szBuff, "%u/%u/%u", pVDS->total_DiscardedBuffers, pVDS->total_DiscardedSegments, pVDS->total_DiscardedLostPackets);
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
   }

   if ( bIsExtended )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Lost packets max gap:");
      sprintf(szBuff, "%d", maxHistoryPacketsGap);
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Lost packets max gap time:");
      if ( videoBitrate > 0 )
         sprintf(szBuff, "%d ms", (maxHistoryPacketsGap*pVDS->video_packet_length*1000*8)/videoBitrate);
      else
         sprintf(szBuff, "N/A ms");
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
   }

   //-------------------------------------------------
   // History requested retransmissions vs missing packets

   if ( bIsNormal || bIsExtended )
   if ( iDeveloperMode )
   {
      float hGraphRetransmissions = hGraph;
     
      float maxValue = 1.0;
      float maxRecv = 0.0;
      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pCRS->history[i].uCountReRequestedPacketsForRetransmission > maxValue )
            maxValue = pCRS->history[i].uCountReRequestedPacketsForRetransmission;
         if ( pCRS->history[i].uCountRequestedPacketsForRetransmission > maxValue )
            maxValue = pCRS->history[i].uCountRequestedPacketsForRetransmission;

         if ( pCRS->history[i].uCountReRequestedPacketsForRetransmission + pCRS->history[i].uCountRequestedPacketsForRetransmission > maxValue )
            maxValue = pCRS->history[i].uCountReRequestedPacketsForRetransmission + pCRS->history[i].uCountRequestedPacketsForRetransmission;

         if ( pVDSH->missingTotalPacketsAtPeriod[i] > maxValue )
            maxValue = pVDSH->missingTotalPacketsAtPeriod[i];
      }
      
      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pCRS->history[i].uCountReceivedRetransmissionPackets > maxRecv )
            maxRecv = pCRS->history[i].uCountReceivedRetransmissionPackets;
      }

      if ( maxValue > 20 )
         maxValue = 20;
      if ( maxRecv > 20 )
         maxRecv = 20;

      osd_set_colors();

      float ftmp = 0;

      y += height_text_small*0.3;

      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, " packets");
      ftmp += g_pRenderEngine->textWidth(s_idFontStatsSmall, " packets");
    
      g_pRenderEngine->setFill(40,250,50, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setStroke(40,250,50, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawTextLeft(rightMargin-ftmp, y, s_idFontStatsSmall, " Recv");
      ftmp += g_pRenderEngine->textWidth(s_idFontStatsSmall, " Recv");
      osd_set_colors();

      g_pRenderEngine->setFill(10,50,200, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setStroke(10,50,200, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawTextLeft(rightMargin-ftmp, y, s_idFontStatsSmall, " Re-Req /");
      ftmp += g_pRenderEngine->textWidth(s_idFontStatsSmall, " Re-Req /");
      osd_set_colors();

      g_pRenderEngine->setFill(150,180,250, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setStroke(150,180,250, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawTextLeft(rightMargin-ftmp, y, s_idFontStatsSmall, "/ Req /");
      ftmp += g_pRenderEngine->textWidth(s_idFontStatsSmall, "/ Req /");
      osd_set_colors();

      g_pRenderEngine->setFill(255,50,0, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setStroke(255,50,0, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawTextLeft(rightMargin-ftmp, y, s_idFontStatsSmall, "Missing ");
      ftmp += g_pRenderEngine->textWidth(s_idFontStatsSmall, "Missing ");
      osd_set_colors();

      y += height_text_small*1.2;
   
      osd_set_colors();

      sprintf(szBuff, "%d", (int)maxValue);
      g_pRenderEngine->drawTextLeft(xPos+dxGraph-2.0*g_pRenderEngine->getPixelWidth(), y-height_text_small*0.5, s_idFontStatsSmall, szBuff);
      sprintf(szBuff, "%d", (int)(maxValue/2.0));
      g_pRenderEngine->drawTextLeft(xPos+dxGraph-2.0*g_pRenderEngine->getPixelWidth(), y+hGraphRetransmissions*0.5-height_text_small*0.5, s_idFontStatsSmall, szBuff);
      g_pRenderEngine->drawTextLeft(xPos+dxGraph-2.0*g_pRenderEngine->getPixelWidth(), y+hGraphRetransmissions-height_text_small*0.5, s_idFontStatsSmall, "0");

      pc = get_Color_OSDText();
      g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
      g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
      g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraphRetransmissions, xPos + dxGraph + width, y+hGraphRetransmissions);
      midLine = hGraphRetransmissions/2.0;
      for( float i=0; i<=width-2.0*wPixel; i+= 5*wPixel )
         g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

      g_pRenderEngine->setStrokeSize(0);

      yBottomGraph = y + hGraphRetransmissions - 1.0/g_pRenderEngine->getScreenHeight();

      xBarSt = xPos + widthMax - widthBar;
      xBarEnd = xBarSt + widthBar;
      
      float hPixel = g_pRenderEngine->getPixelHeight();
      
      for( int i=0; i<totalHistoryValues; i++ )
      {
         g_pRenderEngine->setStrokeSize(1.0);
          
         float fPercentTotalReq = (float)pCRS->history[i].uCountRequestedPacketsForRetransmission/(maxValue+maxRecv);
         if ( fPercentTotalReq > 1.0 )
            fPercentTotalReq = 1.0;
         float fPercentTotalReReq = (float)pCRS->history[i].uCountReRequestedPacketsForRetransmission/(maxValue+maxRecv);
         if ( fPercentTotalReReq > 1.0 )
            fPercentTotalReReq = 1.0;

         float fPercentTotalRecv = (float)pCRS->history[i].uCountReceivedRetransmissionPackets/(maxValue+maxRecv);
         if ( fPercentTotalRecv > 1.0 )
            fPercentTotalRecv = 1.0;

         float fPercentTotalMissing = (float)pVDSH->missingTotalPacketsAtPeriod[i]/maxValue;
         if ( fPercentTotalMissing > 1.0 )
            fPercentTotalMissing = 1.0;

         float hBarReq = hGraphRetransmissions*fPercentTotalReq;
         float hBarReReq = hGraphRetransmissions*fPercentTotalReReq;
         float hBarRecv = hGraphRetransmissions*fPercentTotalRecv;

         float yBottom = yBottomGraph;
         if ( pCRS->history[i].uCountReceivedRetransmissionPackets != 0 )
         {
            g_pRenderEngine->setStroke(40,250,50, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->setFill(40,250,50, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->drawRect(xBarSt, yBottom - hBarRecv, widthBar-wPixel, hBarRecv);
            yBottom -= hBarRecv;
         } 

         if ( pCRS->history[i].uCountRequestedPacketsForRetransmission != 0 )
         {
            g_pRenderEngine->setStroke(250,250,250, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->setFill(150,180,250, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->drawRect(xBarSt, yBottom - hBarReq, widthBar-wPixel, hBarReq);
            yBottom -= hBarReq;
         }
         if ( pCRS->history[i].uCountReRequestedPacketsForRetransmission != 0 )
         {
            g_pRenderEngine->setStroke(200,200,200, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->setFill(10,50,200, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->drawRect(xBarSt, yBottom - hBarReReq, widthBar-wPixel, hBarReReq);
            yBottom -= hBarReReq;
         }         
         if ( (pVDSH->missingTotalPacketsAtPeriod[i] != 0) ||
               ((i<totalHistoryValues-1) && (pVDSH->missingTotalPacketsAtPeriod[i+1] != 0)) )
         {
            g_pRenderEngine->setStrokeSize(2.0);
            g_pRenderEngine->setStroke(255,50,0, s_fOSDStatsGraphLinesAlpha);
            g_pRenderEngine->drawLine(xBarSt, y+hGraphRetransmissions-hGraphRetransmissions*fPercentTotalMissing, xBarSt + widthBar, y+hGraphRetransmissions-hGraphRetransmissions*fPercentTotalMissing);
            if ( i < totalHistoryValues-1 )
            {
               float fPercentTotalMissingNext = (float)pVDSH->missingTotalPacketsAtPeriod[i+1]/maxValue;
               if ( fPercentTotalMissingNext > 1.0 )
                  fPercentTotalMissingNext = 1.0;
            
               g_pRenderEngine->drawLine(xBarSt, y+hGraphRetransmissions-hGraphRetransmissions*fPercentTotalMissing, xBarSt, y+hGraphRetransmissions-hGraphRetransmissions*fPercentTotalMissingNext);    
            }
         }

         xBarSt -= widthBar;
         xBarEnd -= widthBar;
      }

      y += hGraphRetransmissions;
      y += height_text_small * 0.5;
   }
   // History requested retransmissions vs missing packets
   //-------------------------------------------------


   // Dev requested retransmissions

   if ( iDeveloperMode )
   {
      g_pRenderEngine->setColors(get_Color_Dev());

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Retransmissions (Req/Ack/Recv):");
      y += height_text*s_OSDStatsLineSpacing;

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "   Total:");
      strcpy(szBuff, "N/A");
      int percent = 0;
      if ( pCRS->totalRequestedRetransmissions > 0 )
         percent = pCRS->totalReceivedRetransmissions*100/pCRS->totalRequestedRetransmissions;
      if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryExtraInfoRetransmissions )
         sprintf(szBuff, "%u / %u / %u %d%%", pCRS->totalRequestedRetransmissions, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtraInfoRetransmissions.totalReceivedRetransmissionsRequestsUnique, pCRS->totalReceivedRetransmissions, percent);
      else
         sprintf(szBuff, "%u / N/A / %u %d%%", pCRS->totalRequestedRetransmissions, pCRS->totalReceivedRetransmissions, percent);

      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "   Last 500ms:");
      strcpy(szBuff, "N/A");
      percent = 0;
      if ( pCRS->totalRequestedRetransmissionsLast500Ms > 0 )
         percent = pCRS->totalReceivedRetransmissionsLast500Ms*100/pCRS->totalRequestedRetransmissionsLast500Ms;
      if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryExtraInfoRetransmissions )
         sprintf(szBuff, "%u / %u / %u %d%%", pCRS->totalRequestedRetransmissionsLast500Ms, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtraInfoRetransmissions.totalReceivedRetransmissionsRequestsUniqueLast5Sec, pCRS->totalReceivedRetransmissionsLast500Ms, percent);
      else
         sprintf(szBuff, "%u / N/A / %u %d%%", pCRS->totalRequestedRetransmissionsLast500Ms, pCRS->totalReceivedRetransmissionsLast500Ms, percent);
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "   Segments:");
      strcpy(szBuff, "N/A");
      percent = 0;
      if ( 0 != pCRS->totalRequestedVideoPackets )
         percent = pCRS->totalReceivedVideoPackets*100/pCRS->totalRequestedVideoPackets;

      if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryExtraInfoRetransmissions )
         sprintf(szBuff, "%u / %u / %u %d%%", pCRS->totalRequestedVideoPackets, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerRubyTelemetryExtraInfoRetransmissions.totalReceivedRetransmissionsRequestsSegmentsUnique, pCRS->totalReceivedVideoPackets, percent );
      else
         sprintf(szBuff, "%u / N/A / %u %d%%", pCRS->totalRequestedVideoPackets, pCRS->totalReceivedVideoPackets, percent );
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;

      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Retr Time (min/avg/max) ms:");
      
      u8 uMin = 0xFF;
      u8 uMax = 0;
      u32 uAverage = 0;
      u32 uCountValues = 0;
      for( int i=0; i<MAX_HISTORY_VIDEO_INTERVALS;i++ )
      {
         if ( pCRS->history[i].uAverageRetransmissionRoundtripTime == 0 )
            continue;
         if ( pCRS->history[i].uCountReceivedRetransmissionPackets == 0 )
            continue;
         uCountValues++;
         uAverage += pCRS->history[i].uAverageRetransmissionRoundtripTime;

         if ( pCRS->history[i].uMinRetransmissionRoundtripTime < uMin )
            uMin = pCRS->history[i].uMinRetransmissionRoundtripTime;
         if ( pCRS->history[i].uMaxRetransmissionRoundtripTime > uMax )
            uMax = pCRS->history[i].uMaxRetransmissionRoundtripTime;
      }
      if ( uCountValues > 0 )
         uAverage = uAverage/uCountValues;
      if ( uMin == 0xFF )
         sprintf(szBuff, "0/0/0");
      else
         sprintf(szBuff, "%d/%d/%d", uMin, uAverage, uMax);
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
      y += height_text*s_OSDStatsLineSpacing;
      
      osd_set_colors();
   }

   osd_set_colors();

//////////////////////////////////////////////////

/*
   // --------------------------------------------------------------
   // History received/ack/completed/dropped retransmissions

   if ( bIsSnapshot )
   {
      osd_set_colors();

      y += height_text_small*0.2;
      g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, "Req/Ack/Done/Dropped Retransmissions");
      y += height_text_small*1.1;

      g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + width, y);         
      g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + width, y+hGraph);
     
      yBottomGraph = y + hGraph;

      xBarSt = xPos + widthMax - widthBar;
      xBarEnd = xBarSt + widthBar - g_pRenderEngine->getPixelWidth();
      xBarMid = xBarSt + widthBar*0.5;

      for( int i=0; i<totalHistoryValues; i++ )
      {
         if ( pCRS->history[i].uCountRequestedRetransmissions > 0 )
         {
            g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha*0.9);
            g_pRenderEngine->fillCircle(xBarMid, yBottomGraph - hGraph*0.13, hGraph*0.12);
         }

         if ( pCRS->history[i].uCountAcknowledgedRetransmissions > 0 )
         {
            g_pRenderEngine->setFill(50,100,250, s_fOSDStatsGraphLinesAlpha*0.9);
            g_pRenderEngine->fillCircle(xBarMid, yBottomGraph - hGraph*0.37, hGraph*0.12);
         }

         if ( pCRS->history[i].uCountCompletedRetransmissions > 0 )
         {
            g_pRenderEngine->setFill(100, 250, 100, s_fOSDStatsGraphLinesAlpha*0.9);
            g_pRenderEngine->fillCircle(xBarMid, yBottomGraph - hGraph*0.63, hGraph*0.12);
         }
        
         if ( pCRS->history[i].uCountDroppedRetransmissions > 0 )
         {
            g_pRenderEngine->setFill(250,50,50, s_fOSDStatsGraphLinesAlpha*0.9);
            g_pRenderEngine->fillCircle(xBarMid, yBottomGraph - hGraph*0.87, hGraph*0.12);
         }
        
         xBarSt -= widthBar;
         xBarMid -= widthBar;
         xBarEnd -= widthBar;

      }
      y += hGraph;
      y += height_text_small*0.3;
      
      osd_set_colors();
   }

   // History received/ack/completed/dropped retransmissions
   // --------------------------------------------------------------
*/

   /*
   osd_set_colors();
   if ( ! bIsSnapshot )
   if ( (pActiveModel->osd_params.osd_flags2[osd_get_current_layout_index()]) & OSD_FLAG2_SHOW_ADAPTIVE_VIDEO_GRAPH )
   {
      y += height_text_small*0.7;
      osd_render_stats_adaptive_video_graph(xPos, y);
      y += osd_render_stats_adaptive_video_graph_get_height();
   }
*/

   osd_set_colors();

   if ( bIsSnapshot )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Press [Cancel] to dismiss");
      y += height_text*s_OSDStatsLineSpacing;
   }

   return height;
}


float osd_render_stats_telemetry_get_height(float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height = 2.0 *s_fOSDStatsMargin*scale*1.1 + 0.9*height_text*s_OSDStatsLineSpacing;

   height += 11*height_text*s_OSDStatsLineSpacing;
   return height;
}

float osd_render_stats_telemetry_get_width(float scale)
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAA AAAAAAAA AAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}


float osd_render_stats_telemetry(float xPos, float yPos, float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height_text_s = g_pRenderEngine->textHeight(s_idFontStats)*scale;

   float width = osd_render_stats_telemetry_get_width(scale);
   float height = osd_render_stats_telemetry_get_height(scale);

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*scale*0.7;
   width -= 2.0*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;
   float wPixel = g_pRenderEngine->getPixelWidth();

   g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, "Telemetry Stats");
   if ( NULL != g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel )
      sprintf(szBuff, "Update rate: %d Hz", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel->telemetry_params.update_rate);
   else
      sprintf(szBuff, "N/A");
   g_pRenderEngine->drawTextLeft(rightMargin, yPos, s_idFontStats, szBuff);
   
   float y = yPos + height_text*1.5*s_OSDStatsLineSpacing;
   float yTop = y;

   u32 uMaxLostTime = TIMEOUT_TELEMETRY_LOST;   
   if ( NULL != g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel->telemetry_params.update_rate > 10 )
      uMaxLostTime = TIMEOUT_TELEMETRY_LOST/2;
   
   static u32 s_uTimeOSDRubyTelemetryLostShowRedUntill = 0;
   static u32 s_uTimeOSDRubyTelemetryLostRedValue = 0;
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bRubyTelemetryLost )
   {
      s_uTimeOSDRubyTelemetryLostShowRedUntill = g_TimeNow+2000;
      s_uTimeOSDRubyTelemetryLostRedValue = g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetry;
   }

   static u32 s_uTimeOSDFCTelemetryLostShowRedUntill = 0;
   static u32 s_uTimeOSDFCTelemetryLostRedValue = 0;
   if ( g_TimeNow > g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetry + uMaxLostTime )
   {
      s_uTimeOSDFCTelemetryLostShowRedUntill = g_TimeNow+2000;
      s_uTimeOSDFCTelemetryLostRedValue = g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetry;
   }

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Telemetry set timeout:");
   sprintf(szBuff, "%d ms", uMaxLostTime);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   /*
   g_pRenderEngine->drawText(xPos, y, height_text, s_idFontStats, "Last Ruby telemetry:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfo )
      sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetry);
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDRubyTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDRubyTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   if ( g_TimeNow < s_uTimeOSDRubyTelemetryLostShowRedUntill )
      osd_set_colors();
      
   y += height_text*s_OSDStatsLineSpacing;
   */

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Last Ruby telem (full):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfo )
      sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetryExtended);
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDRubyTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDRubyTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Last Ruby telem (short):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfoShort )
   {
      if ( g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetryShort >= 1000 )
         sprintf(szBuff, "%u sec ago", (g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetryShort)/1000);
      else
         sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvRubyTelemetryShort);
   }
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDRubyTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDRubyTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   /*
   g_pRenderEngine->drawText(xPos, y, height_text, s_idFontStats, "Last FC telem:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetry);
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDFCTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDFCTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   
   g_pRenderEngine->drawTextLeft( rightMargin, y, height_text, s_idFontStats, szBuff);   
   if ( g_TimeNow < s_uTimeOSDRubyTelemetryLostShowRedUntill )
      osd_set_colors();
   
   y += height_text*s_OSDStatsLineSpacing;
   */

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Last FC telem (full):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetryFull);
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDFCTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDFCTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   y += height_text*s_OSDStatsLineSpacing;


   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Last FC telem (short):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetryShort )
   {
      if ( g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetryShort >= 1000 )
         sprintf(szBuff, "%u sec ago", (g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetryShort)/1000);
      else
         sprintf(szBuff, "%u ms ago", g_TimeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].uTimeLastRecvFCTelemetryShort);
   }
   else
      strcpy(szBuff, "Never");
   if ( g_TimeNow < s_uTimeOSDFCTelemetryLostShowRedUntill )
   {
      sprintf(szBuff, "%u ms ago", s_uTimeOSDFCTelemetryLostRedValue);
      g_pRenderEngine->setColors(get_Color_IconError());
   }
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Ruby Freq (full/short):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfo )
      sprintf(szBuff, "%d/%d Hz", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].iFrequencyRubyTelemetryFull, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].iFrequencyRubyTelemetryShort);
   else
      strcpy(szBuff, "N/A");
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "FC Freq (full/short):");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d/%d Hz", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].iFrequencyFCTelemetryFull, g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].iFrequencyFCTelemetryShort);
   else
      strcpy(szBuff, "N/A");
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Data from FC:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d kbps", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_kbps);
   else
      strcpy(szBuff, "N/A");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_kbps == 0 )
      g_pRenderEngine->setColors(get_Color_IconError());
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Messages from FC:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d msg/sec", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.extra_info[6]);
   else
      strcpy(szBuff, "N/A");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.extra_info[6] == 0 )
      g_pRenderEngine->setColors(get_Color_IconError());
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Heartbeats from FC:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d msg/sec", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_hudmsgpersec & 0x0F);
   else
      strcpy(szBuff, "N/A");
   if ( (g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_hudmsgpersec & 0x0F) == 0 )
      g_pRenderEngine->setColors(get_Color_IconError());
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "SysMsgs from FC:");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d msg/sec", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_hudmsgpersec >> 4);
   else
      strcpy(szBuff, "N/A");
   if ( (g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.fc_hudmsgpersec >> 4) == 0 )
      g_pRenderEngine->setColors(get_Color_IconError());
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);   
   osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;

   return height;
}


float osd_render_stats_audio_decode_get_height(float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height = 2.0 *s_fOSDStatsMargin*scale*1.1 + 0.7*height_text*s_OSDStatsLineSpacing;
   
   if ( (NULL != osd_get_current_data_source_vehicle_model()) && osd_get_current_data_source_vehicle_model()->audio_params.has_audio_device && osd_get_current_data_source_vehicle_model()->audio_params.enabled )
   {
      if ( NULL == g_pSM_AudioDecodeStats )
         return height + height_text;
   }
   else
      return height + height_text;
   return height;
}

float osd_render_stats_audio_decode_get_width(float scale)
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAAAAA AAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}

float osd_render_stats_audio_decode(float xPos, float yPos, float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;

   float width = osd_render_stats_audio_decode_get_width(scale);
   float height = osd_render_stats_audio_decode_get_height(scale);

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*scale*0.7;
   width -= 2*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;

   g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, "Audio Decode Stats");
   
   float y = yPos + height_text*1.3*s_OSDStatsLineSpacing;
   float yTop = y;

   osd_set_colors();

   if ( NULL == g_pSM_AudioDecodeStats )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "No Data");
      return height;
   }

   return height;
}


float osd_render_stats_rc_get_height(float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height = 2.0 *s_fOSDStatsMargin*scale*1.1 + 0.7*height_text*s_OSDStatsLineSpacing;

   height += 0.05*scale;
   height += 4*height_text*s_OSDStatsLineSpacing;
   height += 5*height_text*s_OSDStatsLineSpacing;
   return height;
}

float osd_render_stats_rc_get_width(float scale)
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAAAAA AAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}

float osd_render_stats_rc(float xPos, float yPos, float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height_text_s = g_pRenderEngine->textHeight(s_idFontStats)*scale;

   float width = osd_render_stats_rc_get_width(scale);
   float height = osd_render_stats_rc_get_height(scale);

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*scale*0.7;
   width -= 2.0*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;
   float wPixel = g_pRenderEngine->getPixelWidth();

   g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, "RC Stats");
   sprintf(szBuff, "%.1f sec", RC_INFO_HISTORY_SIZE*50/1000.0);
   g_pRenderEngine->drawTextLeft(rightMargin, yPos, s_idFontStats, szBuff);
   
   float y = yPos + height_text*1.5*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Active Link:");
   strcpy(szBuff, "Ruby RC");
   if ( ! osd_get_current_data_source_vehicle_model()->rc_params.rc_enabled )
      strcpy(szBuff, "External");
   if ( NULL != osd_get_current_data_source_vehicle_model() && ((osd_get_current_data_source_vehicle_model()->rc_params.flags & RC_FLAGS_OUTPUT_ENABLED) == 0 ) )
      strcpy(szBuff, "External");

   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing + height_text*0.2;

   float yTop = y;

   if ( NULL == osd_get_current_data_source_vehicle_model() || (! g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotRubyTelemetryInfo) )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "No info from vehicle"); 
      return height + 0.012;
   }
   if ( NULL == g_pSM_DownstreamInfoRC )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Not connected"); 
      return height + 0.012;
   }

   // Graph
   float dxGraph = height_text_s*0.6;
   widthMax -= dxGraph;
   float hGraph = 0.03*scale;
   float widthBar = (float)widthMax / (float)RC_INFO_HISTORY_SIZE;
   int maxGap = 0;

   if ( ! osd_get_current_data_source_vehicle_model()->rc_params.rc_enabled )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "RC link is disabled");
      y += height_text*s_OSDStatsLineSpacing;
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "No RC link graph to show");
      y += hGraph + height_text*0.7 - height_text*s_OSDStatsLineSpacing;
   }
   else
   {
   g_pRenderEngine->drawText(xPos, y-height_text*0.1, s_idFontStatsSmall, "4");
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text*0.6, s_idFontStatsSmall, "0");

   double* pc = get_Color_OSDText();
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);

   if ( g_SM_DownstreamInfoRC.is_failsafe )
   {
      g_pRenderEngine->setFill(190,10,0,0.6);
      g_pRenderEngine->setStroke(190,10,0,0.6);
   }

   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph+widthMax, y);
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph+widthMax, y+hGraph);
   for( float i=0; i<widthMax-4.0*wPixel; i+=4.0*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+hGraph/2.0, xPos + dxGraph + i + 2.0*wPixel, y+hGraph/2.0);

   g_pRenderEngine->drawLine(xPos+dxGraph + g_SM_DownstreamInfoRC.last_history_slice*widthBar, y+hGraph-g_pRenderEngine->getPixelHeight(), xPos+dxGraph + g_SM_DownstreamInfoRC.last_history_slice*widthBar, y);
   g_pRenderEngine->drawLine(xPos+dxGraph + g_SM_DownstreamInfoRC.last_history_slice*widthBar+wPixel, y+hGraph-g_pRenderEngine->getPixelHeight(), xPos+dxGraph + g_SM_DownstreamInfoRC.last_history_slice*widthBar+wPixel, y);

   float yBottomGraph = y+hGraph;
   for( int i=0; i<RC_INFO_HISTORY_SIZE; i++ )
   {
      if ( i == g_SM_DownstreamInfoRC.last_history_slice )
         continue;
      u8 val = g_SM_DownstreamInfoRC.history[i];
      u8 cRecv = val & 0x0F;
      u8 cGap = (val >> 4) & 0x0F;
      if ( cGap > maxGap )
         maxGap = cGap;
      if ( cGap > 4 )
         cGap = 4;
      if ( cRecv > 4 )
         cRecv = 4;
      float hBar = hGraph*cRecv/(float)4.0;
      float hGap = hGraph*cGap/(float)4.0;
      if ( cRecv > 0 )
      {
         g_pRenderEngine->setFill(190,190,190,0.7);
         g_pRenderEngine->setStroke(190,190,190,0.7);
         g_pRenderEngine->setStrokeSize(1.0);
         g_pRenderEngine->drawRect(xPos+dxGraph+i*widthBar, yBottomGraph-hBar, widthBar, hBar);
      }
      if ( cGap > 0 )
      {
         g_pRenderEngine->setFill(190,10,0,0.7);
         g_pRenderEngine->setStroke(190,10,0,0.7);
         g_pRenderEngine->setStrokeSize(1.0);
         g_pRenderEngine->drawRect(xPos+dxGraph+i*widthBar, y, widthBar, hGap);
      }
   }

   if ( g_SM_DownstreamInfoRC.is_failsafe )
   {
      g_pRenderEngine->setFill(190,10,0,0.6);
      g_pRenderEngine->setStroke(190,10,0,0.6);
      g_pRenderEngine->setStrokeSize(0);
      g_pRenderEngine->drawText(xPos+height_text*1.2, y+hGraph-height_text*1.5, s_idFontStatsSmall, "! FAILSAFE !");
   }

   osd_set_colors();   

   y += hGraph + height_text*0.5;
   }

   osd_set_colors();

   bool bShowMAVLink = false;
   if ( NULL != osd_get_current_data_source_vehicle_model() && ( ! osd_get_current_data_source_vehicle_model()->rc_params.rc_enabled ) )
      bShowMAVLink = true;
   else if ( NULL != osd_get_current_data_source_vehicle_model() && ((osd_get_current_data_source_vehicle_model()->rc_params.flags & RC_FLAGS_OUTPUT_ENABLED) == 0 ) )
      bShowMAVLink = true;
   if ( g_SM_DownstreamInfoRC.is_failsafe )   
   if ( NULL != osd_get_current_data_source_vehicle_model() && osd_get_current_data_source_vehicle_model()->rc_params.failsafeFlags == RC_FAILSAFE_NOOUTPUT )
      bShowMAVLink = true;

   sprintf(szBuff, "CH1: %04d", g_SM_DownstreamInfoRC.rc_channels[0]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH1: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[0]);
   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);

   sprintf(szBuff, "CH5: %04d", g_SM_DownstreamInfoRC.rc_channels[4]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH5: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[4]);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   sprintf(szBuff, "CH2: %04d", g_SM_DownstreamInfoRC.rc_channels[1]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH2: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[1]);
   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);

   sprintf(szBuff, "CH6: %04d", g_SM_DownstreamInfoRC.rc_channels[5]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH6: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[5]);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   sprintf(szBuff, "CH3: %04d", g_SM_DownstreamInfoRC.rc_channels[2]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH3: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[2]);
   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);

   sprintf(szBuff, "CH7: %04d", g_SM_DownstreamInfoRC.rc_channels[6]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH7: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[6]);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   sprintf(szBuff, "CH4: %04d", g_SM_DownstreamInfoRC.rc_channels[3]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH4: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[3]);
   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, szBuff);

   sprintf(szBuff, "CH8: %04d", g_SM_DownstreamInfoRC.rc_channels[7]);
   if ( bShowMAVLink && g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "ECH8: %04d", g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetryRCChannels.channels[7]);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text*s_OSDStatsLineSpacing;
   y += height_text*0.3;

   if ( ! g_pCurrentModel->rc_params.rc_enabled )
      return height;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Recv frames:");
   sprintf(szBuff, "%u", g_SM_DownstreamInfoRC.recv_packets);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Lost frames:");
   sprintf(szBuff, "%u", g_SM_DownstreamInfoRC.lost_packets);
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max gap:");
   sprintf(szBuff, "%d ms", maxGap * 1000 / g_pCurrentModel->rc_params.rc_frames_per_second );
   g_pRenderEngine->drawTextLeft( rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Failsafed count:");
   sprintf(szBuff, "%d", g_SM_DownstreamInfoRC.failsafe_count);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   return height;
}

float osd_render_stats_efficiency_get_height(float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height = 2.0 *s_fOSDStatsMargin*scale*1.1 + 0.7*height_text*s_OSDStatsLineSpacing;

   height += 2*height_text*s_OSDStatsLineSpacing;
   return height;
}

float osd_render_stats_efficiency_get_width(float scale)
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAAAAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}

float osd_render_stats_efficiency(float xPos, float yPos, float scale)
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;

   float width = osd_render_stats_efficiency_get_width(scale);
   float height = osd_render_stats_efficiency_get_height(scale);

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*scale*0.7;
   width -= 2*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;

   g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, "Efficiency Stats");
   
   float y = yPos + height_text*1.3*s_OSDStatsLineSpacing;
   float yTop = y;

   osd_set_colors();

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "mA/km:");
   float eff = 0.0;
   u32 dist = g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.total_distance;
   if ( NULL != g_pCurrentModel && g_pCurrentModel->m_Stats.uCurrentFlightDistance > 500 )
      eff = (float)g_pCurrentModel->m_Stats.uCurrentFlightTotalCurrent/10.0/((float)g_pCurrentModel->m_Stats.uCurrentFlightDistance/100.0/1000.0);
   sprintf(szBuff, "%d", (int)eff);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "mA/h:");
   eff = 0;
   if ( NULL != g_pCurrentModel && g_pCurrentModel->m_Stats.uCurrentFlightTime > 0 )
      eff = ( (float)g_pCurrentModel->m_Stats.uCurrentFlightTotalCurrent/10.0/((float)g_pCurrentModel->m_Stats.uCurrentFlightTime))*3600.0;
   sprintf(szBuff, "%d", (int)eff);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   return height;
}


float osd_render_stats_video_stream_keyframe_info_get_height()
{
   float height_text = g_pRenderEngine->textHeight(s_idFontStats);
   float height = 2.0 *s_fOSDStatsMargin*1.1 + 1.1*height_text*s_OSDStatsLineSpacing;

   float hGraph = height_text*2.0;

   ControllerSettings* pCS = get_ControllerSettings();

   height += 2*height_text*s_OSDStatsLineSpacing;

   // stats
   height += 8.0 * height_text*s_OSDStatsLineSpacing;

   if ( pCS->iShowVideoStreamInfoCompact )
      return height;

   // Detailed view

   height += 0.3*height_text*s_OSDStatsLineSpacing;
   // kb/frame
   height += hGraph;
   height += height_text;

   // camera source
   height += hGraph;
   height += 3*height_text*s_OSDStatsLineSpacing;

   // radio out
   height += height_text*0.6;
   height += hGraph;
   height += 3*height_text*s_OSDStatsLineSpacing;

   // radio in
   height += height_text*0.6;
   height += hGraph;
   height += 3*height_text*s_OSDStatsLineSpacing;

   // player output
   height += height_text*0.6;
   height += hGraph;
   height += 3*height_text*s_OSDStatsLineSpacing;

   return height;
}

float osd_render_stats_video_stream_keyframe_info_get_width()
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAA AAAAA AAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}

float osd_render_stats_video_stream_keyframe_info(float xPos, float yPos)
{
   Model* pActiveModel = osd_get_current_data_source_vehicle_model();
   u32 uActiveVehicleId = osd_get_current_data_source_vehicle_id();
   
   shared_mem_video_stream_stats* pVDS = NULL;
   int iIndexRouterRuntimeInfo = -1;

   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( g_SM_VideoDecodeStats.video_streams[i].uVehicleId == uActiveVehicleId )
         pVDS = &(g_SM_VideoDecodeStats.video_streams[i]);
   }
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i] == uActiveVehicleId )
         iIndexRouterRuntimeInfo = i;
   }
   if ( (NULL == pVDS) || (NULL == pActiveModel) || (-1 == iIndexRouterRuntimeInfo) )
      return 0.0; 

   ControllerSettings* pCS = get_ControllerSettings();

   float height_text = g_pRenderEngine->textHeight(s_idFontStats);
   float height_text_small = g_pRenderEngine->textHeight(s_idFontStatsSmall);
   
   float hGraph = height_text*2.0;
   float wPixel = g_pRenderEngine->getPixelWidth();
   float fStroke = OSD_STRIKE_WIDTH;

   float width = osd_render_stats_video_stream_keyframe_info_get_width();
   float height = osd_render_stats_video_stream_keyframe_info_get_height();

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();
 

   xPos += s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*0.7;
   width -= 2*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;

   sprintf(szBuff, "Video Keyframe (%u FPS / ", pVDS->fps);
   if ( pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].keyframe_ms < 0 )
      strcat(szBuff, "Auto KF)");
   else
      strcat(szBuff, "Fixed KF)");
   
   g_pRenderEngine->drawText(xPos, yPos, s_idFontStats, szBuff);
   
   float y = yPos + height_text*1.2*s_OSDStatsLineSpacing;
   float yTop = y;

   osd_set_colors();

   int iSlices = 1;
   if ( pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].width <= 1280 )
   if ( pActiveModel->video_link_profiles[pActiveModel->video_params.user_selected_video_link_profile].height <= 720 )
      iSlices = pActiveModel->video_params.iH264Slices;
   if ( iSlices < 1 )
      iSlices = 1;

   float dxGraph = g_pRenderEngine->textWidth(s_idFontStatsSmall, "88 ms");
   float widthGraph = widthMax - dxGraph;
   float widthBar = widthGraph / (float)MAX_FRAMES_SAMPLES;
   

   if ( 0 == g_iDeltaVideoInfoBetweenVehicleController )
   if ( g_SM_VideoInfoStats.uLastIndex > MAX_FRAMES_SAMPLES/2 )
   {
      g_iDeltaVideoInfoBetweenVehicleController = g_SM_VideoInfoStats.uLastIndex - g_VideoInfoStatsFromVehicle.uLastIndex; 
   }

   static u32 s_uLastTimeKeyFrameValueChangedInOSD = 0;
   static int s_iLastRequestedKeyFrameMsInOSD = 0;
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndexRouterRuntimeInfo].iLastRequestedKeyFrameMs != pVDS->iLastAckKeyframeInterval )
      s_uLastTimeKeyFrameValueChangedInOSD = g_TimeNow;
   if ( s_iLastRequestedKeyFrameMsInOSD != g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndexRouterRuntimeInfo].iLastRequestedKeyFrameMs )
   {
      s_iLastRequestedKeyFrameMsInOSD = g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndexRouterRuntimeInfo].iLastRequestedKeyFrameMs;
      s_uLastTimeKeyFrameValueChangedInOSD = g_TimeNow;
   }
   bool bShowChanging = false;
   if ( g_TimeNow < s_uLastTimeKeyFrameValueChangedInOSD + 1000 )
      bShowChanging = true;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Requested Keyframe:");
   sprintf(szBuff, "%d ms", g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iIndexRouterRuntimeInfo].iLastRequestedKeyFrameMs);
   if ( bShowChanging )
      g_pRenderEngine->setColors(get_Color_OSDChangedValue());
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   if ( bShowChanging )
      osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Ack Keyframe:");
   sprintf(szBuff, "%d ms", pVDS->iLastAckKeyframeInterval);
   if ( bShowChanging )
      g_pRenderEngine->setColors(get_Color_OSDChangedValue());
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   if ( bShowChanging )
      osd_set_colors();
   y += height_text*s_OSDStatsLineSpacing;


   int iKeyframeSizeBitsSum = 0;
   int iKeyframeCount = 0;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x80 )
      {
         iKeyframeSizeBitsSum += 8*(int)(g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x7F);
         iKeyframeCount++;
      }
   }

   if ( iKeyframeCount > 1 )
      iKeyframeSizeBitsSum /= iKeyframeCount;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Recv KF:");
   sprintf(szBuff, "%d ms", pVDS->keyframe_ms);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Recv KF:");
   sprintf(szBuff, "x%d ms @ %d kbits/keyfr", g_VideoInfoStatsFromVehicle.uKeyframeIntervalMs, iKeyframeSizeBitsSum);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   u32 uMaxValue = 0;
   u32 uMinValue = MAX_U32;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( (g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x7F) < uMinValue )
         uMinValue = g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x7F;
      if ( (g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x7F) > uMaxValue )
         uMaxValue = g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[i] & 0x7F;
   }
   if ( uMinValue == MAX_U32 )
      uMinValue = 0;
   if ( uMaxValue <= uMinValue )
      uMaxValue = uMinValue+4;
   
   uMinValue *= 8;
   uMaxValue *= 8;

   u32 uMaxFrameKb = uMaxValue;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Avg/Max Frame size:");
   y += height_text*s_OSDStatsLineSpacing;
   sprintf(szBuff, "%d / %u kbits", g_VideoInfoStatsFromVehicle.uAverageFrameSize/1000, uMaxFrameKb);
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;
   

   if ( ! pCS->iShowVideoStreamInfoCompact )
   {
   //--------------------------------------------------------------------------
   // kb/frame

   sprintf(szBuff, "Camera source (%d FPS)", g_VideoInfoStatsFromVehicle.uAverageFPS/iSlices);
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*1.2;

   float yBottomGraph = y + hGraph;
   yBottomGraph = ((int)(yBottomGraph/g_pRenderEngine->getPixelHeight())) * g_pRenderEngine->getPixelHeight();

   sprintf(szBuff, "%d kb", (int)uMaxValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y-height_text_small*0.6, s_idFontStatsSmall, szBuff);
   
   sprintf(szBuff, "%d kb", (int)(uMaxValue+uMinValue)/2);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph*0.5-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   sprintf(szBuff, "%d kb", (int)uMinValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->drawLine(xPos, y, xPos + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos, y+hGraph, xPos + widthGraph, y+hGraph);

   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+i, y+0.5*hGraph, xPos + i + 2.0*wPixel, y+0.5*hGraph);
   
   g_pRenderEngine->setStrokeSize(2.1);

   float xBarStart = xPos;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
       int iRealIndex = ((i-g_iDeltaVideoInfoBetweenVehicleController)+2*MAX_FRAMES_SAMPLES)%MAX_FRAMES_SAMPLES;
       u32 uValue = 8*((u32)(g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iRealIndex] & 0x7F));
       
       if ( iRealIndex != g_VideoInfoStatsFromVehicle.uLastIndex+1 )
       {
          float hBar = hGraph*(float)(uValue-uMinValue)/(float)(uMaxValue - uMinValue);
          
          if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             g_pRenderEngine->setStroke(70,70,250,0.96);
             g_pRenderEngine->setFill(70,70,250,0.96);
             g_pRenderEngine->setStrokeSize(2.1); 
          }
          
          g_pRenderEngine->drawRect(xBarStart, y+hGraph-hBar, widthBar, hBar);

          if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             osd_set_colors();
             g_pRenderEngine->setStrokeSize(2.1); 
          }
       }
       xBarStart += widthBar;

       if ( iRealIndex == g_VideoInfoStatsFromVehicle.uLastIndex )
       {
          g_pRenderEngine->setStroke(250,250,50,0.9);
          g_pRenderEngine->drawLine(xBarStart, y, xBarStart, y+hGraph);
          osd_set_colors();
          g_pRenderEngine->setStrokeSize(2.1);
       }
   }

   y += hGraph + height_text*0.4;

   y += height_text*0.4;
   
   // ------------------------------------------------------------------------
   // Camera source info

   
   yBottomGraph = y + hGraph;
   yBottomGraph = ((int)(yBottomGraph/g_pRenderEngine->getPixelHeight())) * g_pRenderEngine->getPixelHeight();
   
   uMaxValue = 0;
   uMinValue = MAX_U32;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( g_VideoInfoStatsFromVehicle.uFramesTimes[i] < uMinValue )
         uMinValue = g_VideoInfoStatsFromVehicle.uFramesTimes[i];
      if ( g_VideoInfoStatsFromVehicle.uFramesTimes[i] > uMaxValue )
         uMaxValue = g_VideoInfoStatsFromVehicle.uFramesTimes[i];
   }
   if ( uMinValue == MAX_U32 )
      uMinValue = 0;
   if ( uMaxValue <= uMinValue )
      uMaxValue = uMinValue+4;
   
   sprintf(szBuff, "%d ms", (int)uMaxValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y-height_text_small*0.6, s_idFontStatsSmall, szBuff);
   
   sprintf(szBuff, "%d ms", (int)(uMaxValue+uMinValue)/2);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph*0.5-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   sprintf(szBuff, "%d ms", (int)uMinValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->drawLine(xPos, y, xPos + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos, y+hGraph, xPos + widthGraph, y+hGraph);

   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+i, y+0.5*hGraph, xPos + i + 2.0*wPixel, y+0.5*hGraph);
   
   g_pRenderEngine->setStrokeSize(2.1);

   u32 uPrevValue = 0;
   int iIndexPrev = 0;
   xBarStart = xPos;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
       int iRealIndex = ((i-g_iDeltaVideoInfoBetweenVehicleController)+MAX_FRAMES_SAMPLES)%MAX_FRAMES_SAMPLES;
       if ( iRealIndex != g_VideoInfoStatsFromVehicle.uLastIndex+1 )
       {
          float hBar = hGraph*(float)(g_VideoInfoStatsFromVehicle.uFramesTimes[iRealIndex]-uMinValue)/(float)(uMaxValue - uMinValue);
          
          if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             g_pRenderEngine->setStroke(70,70,250,0.96);
             g_pRenderEngine->setFill(70,70,250,0.96);
             g_pRenderEngine->setStrokeSize(2.1); 
          }

          g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBar, xBarStart+widthBar, y+hGraph-hBar);

          if ( (i != 0) && (g_VideoInfoStatsFromVehicle.uFramesTimes[iRealIndex] != uPrevValue ) )
          {
             float hBarPrev = hGraph*(float)(uPrevValue-uMinValue)/(float)(uMaxValue - uMinValue);
         
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                g_pRenderEngine->setStroke(70,70,250,0.96);
                g_pRenderEngine->setFill(70,70,250,0.96);
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          
             g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBarPrev, xBarStart, y+hGraph-hBar);
 
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                osd_set_colors();
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          }

          if ( g_VideoInfoStatsFromVehicle.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             osd_set_colors();
             g_pRenderEngine->setStrokeSize(2.1); 
          }
       }
       uPrevValue = g_VideoInfoStatsFromVehicle.uFramesTimes[iRealIndex];
       iIndexPrev = iRealIndex;
       xBarStart += widthBar;

       if ( iRealIndex == g_VideoInfoStatsFromVehicle.uLastIndex )
       {
          g_pRenderEngine->setStroke(250,250,50,0.9);
          g_pRenderEngine->drawLine(xBarStart, y, xBarStart, y+hGraph);
          osd_set_colors();
          g_pRenderEngine->setStrokeSize(2.1);
       }
   }

   y += hGraph + height_text*0.4;


   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, "Avg/Min/Max frame:");
   sprintf(szBuff, "%d / %d / %d ms", g_VideoInfoStatsFromVehicle.uAverageFrameTime, uMinValue, uMaxValue);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text_small*s_OSDStatsLineSpacing;
   
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max deviation from avg:");
   strcpy(szBuff, "N/A");
   sprintf(szBuff, "%d ms", g_VideoInfoStatsFromVehicle.uMaxFrameDeltaTime);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   y += height_text*0.4;
   
   // -----------------------------------------------------------
   // Radio output info

   sprintf(szBuff, "Radio output (%d FPS)", g_VideoInfoStatsFromVehicleRadioOut.uAverageFPS/iSlices);
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*1.2;

   yBottomGraph = y + hGraph;
   yBottomGraph = ((int)(yBottomGraph/g_pRenderEngine->getPixelHeight())) * g_pRenderEngine->getPixelHeight();
   
   uMaxValue = 0;
   uMinValue = MAX_U32;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[i] < uMinValue )
         uMinValue = g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[i];
      if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[i] > uMaxValue )
         uMaxValue = g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[i];
   }
   if ( uMinValue == MAX_U32 )
      uMinValue = 0;
   if ( uMaxValue <= uMinValue )
      uMaxValue = uMinValue+4;
   

   sprintf(szBuff, "%d ms", (int)uMaxValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y-height_text_small*0.6, s_idFontStatsSmall, szBuff);
   
   sprintf(szBuff, "%d ms", (int)(uMaxValue+uMinValue)/2);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph*0.5-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   sprintf(szBuff, "%d ms", (int)uMinValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->drawLine(xPos, y, xPos + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos, y+hGraph, xPos +widthGraph, y+hGraph);

   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+i, y+0.5*hGraph, xPos + i + 2.0*wPixel, y+0.5*hGraph);
   
   g_pRenderEngine->setStrokeSize(2.1);

  
   uPrevValue = 0;
   iIndexPrev = 0;
   xBarStart = xPos;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
       int iRealIndex = ((i-g_iDeltaVideoInfoBetweenVehicleController)+MAX_FRAMES_SAMPLES)%MAX_FRAMES_SAMPLES;
       if ( iRealIndex != g_VideoInfoStatsFromVehicleRadioOut.uLastIndex+1 )
       {
          float hBar = hGraph*(float)(g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[iRealIndex]-uMinValue)/(float)(uMaxValue - uMinValue);
          
          if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             g_pRenderEngine->setStroke(70,70,250,0.96);
             g_pRenderEngine->setFill(70,70,250,0.96);
             g_pRenderEngine->setStrokeSize(2.1); 
          }

          g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBar, xBarStart+widthBar, y+hGraph-hBar);

          if ( (i != 0) && (g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[iRealIndex] != uPrevValue ) )
          {
             float hBarPrev = hGraph*(float)(uPrevValue-uMinValue)/(float)(uMaxValue - uMinValue);
         
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                g_pRenderEngine->setStroke(70,70,250,0.96);
                g_pRenderEngine->setFill(70,70,250,0.96);
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          
             g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBarPrev, xBarStart, y+hGraph-hBar);
 
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                osd_set_colors();
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          }

          if ( g_VideoInfoStatsFromVehicleRadioOut.uFramesTypesAndSizes[iRealIndex] & (1<<7) )
          {
             osd_set_colors();
             g_pRenderEngine->setStrokeSize(2.1); 
          }
       }
       uPrevValue = g_VideoInfoStatsFromVehicleRadioOut.uFramesTimes[iRealIndex];
       iIndexPrev = iRealIndex;
       xBarStart += widthBar;

       if ( iRealIndex == g_VideoInfoStatsFromVehicleRadioOut.uLastIndex )
       {
          g_pRenderEngine->setStroke(250,250,50,0.9);
          g_pRenderEngine->drawLine(xBarStart, y, xBarStart, y+hGraph);
          osd_set_colors();
          g_pRenderEngine->setStrokeSize(2.1);
       }
   }

   y += hGraph + height_text*0.4;

   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, "Avg/Min/Max frame:");
   strcpy(szBuff, "N/A");
   sprintf(szBuff, "%d / %d / %d ms", g_VideoInfoStatsFromVehicleRadioOut.uAverageFrameTime, uMinValue, uMaxValue);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text_small*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max deviation from avg:");
   strcpy(szBuff, "N/A");
   sprintf(szBuff, "%d ms", g_VideoInfoStatsFromVehicleRadioOut.uMaxFrameDeltaTime);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;
   
   y += height_text*0.4;

  
   // -----------------------------------------------------------
   // Radio In graph

   if ( NULL != g_pSM_VideoInfoStatsRadioIn )
      sprintf(szBuff, "Radio In (%d FPS)", g_SM_VideoInfoStatsRadioIn.uAverageFPS/iSlices);
   else
      strcpy(szBuff, "Radio In");

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*1.2;

   if ( NULL == g_pSM_VideoInfoStatsRadioIn )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "No Info.");
      return yTop;
   }

   yBottomGraph = y + hGraph;
   yBottomGraph = ((int)(yBottomGraph/g_pRenderEngine->getPixelHeight())) * g_pRenderEngine->getPixelHeight();
   
   uMaxValue = 0;
   uMinValue = MAX_U32;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( g_SM_VideoInfoStatsRadioIn.uFramesTimes[i] < uMinValue )
         uMinValue = g_SM_VideoInfoStatsRadioIn.uFramesTimes[i];
      if ( g_SM_VideoInfoStatsRadioIn.uFramesTimes[i] > uMaxValue )
         uMaxValue = g_SM_VideoInfoStatsRadioIn.uFramesTimes[i];
   }
   if ( uMinValue == MAX_U32 )
      uMinValue = 0;
   if ( uMaxValue <= uMinValue )
      uMaxValue = uMinValue+4;
   
   sprintf(szBuff, "%d ms", (int)uMaxValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y-height_text_small*0.6, s_idFontStatsSmall, szBuff);
   
   sprintf(szBuff, "%d ms", (int)(uMaxValue+uMinValue)/2);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph*0.5-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   sprintf(szBuff, "%d ms", (int)uMinValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->drawLine(xPos, y, xPos + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos, y+hGraph, xPos +widthGraph, y+hGraph);

   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+i, y+0.5*hGraph, xPos + i + 2.0*wPixel, y+0.5*hGraph);
   
   g_pRenderEngine->setStrokeSize(2.1);

   uPrevValue = 0;
   iIndexPrev = 0;
   xBarStart = xPos;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
       if ( i != g_SM_VideoInfoStatsRadioIn.uLastIndex+1 )
       {
          float hBar = hGraph*(float)(g_SM_VideoInfoStatsRadioIn.uFramesTimes[i]-uMinValue)/(float)(uMaxValue - uMinValue);
          
          if ( g_SM_VideoInfoStatsRadioIn.uFramesTypesAndSizes[i] & (1<<7) )
          {
             g_pRenderEngine->setStroke(70,70,250,0.96);
             g_pRenderEngine->setFill(70,70,250,0.96);
             g_pRenderEngine->setStrokeSize(2.1); 
          }

          g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBar, xBarStart+widthBar, y+hGraph-hBar);
          
          
          if ( (i != 0) && (g_SM_VideoInfoStatsRadioIn.uFramesTimes[i] != uPrevValue ) )
          {
             float hBarPrev = hGraph*(float)(uPrevValue-uMinValue)/(float)(uMaxValue - uMinValue);

             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_SM_VideoInfoStatsRadioIn.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                g_pRenderEngine->setStroke(70,70,250,0.96);
                g_pRenderEngine->setFill(70,70,250,0.96);
                g_pRenderEngine->setStrokeSize(2.1); 
             }

             g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBarPrev, xBarStart, y+hGraph-hBar);
          
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_SM_VideoInfoStatsRadioIn.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                osd_set_colors();
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          }

          if ( g_SM_VideoInfoStatsRadioIn.uFramesTypesAndSizes[i] & (1<<7) )
          {
             osd_set_colors();
             g_pRenderEngine->setStrokeSize(2.1); 
          }
       }

       uPrevValue = g_SM_VideoInfoStatsRadioIn.uFramesTimes[i];
       iIndexPrev = i;
       xBarStart += widthBar;

       if ( i == g_SM_VideoInfoStatsRadioIn.uLastIndex )
       {
          g_pRenderEngine->setStroke(250,250,50,0.9);
          g_pRenderEngine->drawLine(xBarStart, y, xBarStart, y+hGraph);
          osd_set_colors();
          g_pRenderEngine->setStrokeSize(2.1);
       }
   }

   y += hGraph + height_text*0.4;

   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, "Avg/Min/Max frame:");
   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_VideoInfoStatsRadioIn )
      sprintf(szBuff, "%d / %d / %d ms", g_SM_VideoInfoStatsRadioIn.uAverageFrameTime, uMinValue, uMaxValue);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text_small*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max deviation from avg:");
   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_VideoInfoStatsRadioIn )
      sprintf(szBuff, "%d ms", g_SM_VideoInfoStatsRadioIn.uMaxFrameDeltaTime);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;


   // -----------------------------------------------------------
   // Player output info

   sprintf(szBuff, "Player output (%d FPS)", g_SM_VideoInfoStats.uAverageFPS/iSlices);
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*1.2;

   if ( NULL == g_pSM_VideoInfoStats )
   {
      g_pRenderEngine->drawText(xPos, y, s_idFontStats, "No Info.");
      return yTop;
   }

   yBottomGraph = y + hGraph;
   yBottomGraph = ((int)(yBottomGraph/g_pRenderEngine->getPixelHeight())) * g_pRenderEngine->getPixelHeight();
   
   uMaxValue = 0;
   uMinValue = MAX_U32;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
      if ( g_SM_VideoInfoStats.uFramesTimes[i] < uMinValue )
         uMinValue = g_SM_VideoInfoStats.uFramesTimes[i];
      if ( g_SM_VideoInfoStats.uFramesTimes[i] > uMaxValue )
         uMaxValue = g_SM_VideoInfoStats.uFramesTimes[i];
   }
   if ( uMinValue == MAX_U32 )
      uMinValue = 0;
   if ( uMaxValue <= uMinValue )
      uMaxValue = uMinValue+4;
   
   sprintf(szBuff, "%d ms", (int)uMaxValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y-height_text_small*0.6, s_idFontStatsSmall, szBuff);
   
   sprintf(szBuff, "%d ms", (int)(uMaxValue+uMinValue)/2);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph*0.5-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   sprintf(szBuff, "%d ms", (int)uMinValue);
   g_pRenderEngine->drawText(xPos+widthGraph+4.0*wPixel, y+hGraph-height_text_small*0.6, s_idFontStatsSmall,szBuff);
   
   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->drawLine(xPos, y, xPos + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos, y+hGraph, xPos +widthGraph, y+hGraph);

   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+i, y+0.5*hGraph, xPos + i + 2.0*wPixel, y+0.5*hGraph);
   
   g_pRenderEngine->setStrokeSize(2.1);

   uPrevValue = 0;
   iIndexPrev = 0;
   xBarStart = xPos;
   for( int i=0; i<MAX_FRAMES_SAMPLES; i++ )
   {
       if ( i != g_SM_VideoInfoStats.uLastIndex+1 )
       {
          float hBar = hGraph*(float)(g_SM_VideoInfoStats.uFramesTimes[i]-uMinValue)/(float)(uMaxValue - uMinValue);
          
          if ( g_SM_VideoInfoStats.uFramesTypesAndSizes[i] & (1<<7) )
          {
             g_pRenderEngine->setStroke(70,70,250,0.96);
             g_pRenderEngine->setFill(70,70,250,0.96);
             g_pRenderEngine->setStrokeSize(2.1); 
          }

          g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBar, xBarStart+widthBar, y+hGraph-hBar);
          
          
          if ( (i != 0) && (g_SM_VideoInfoStats.uFramesTimes[i] != uPrevValue ) )
          {
             float hBarPrev = hGraph*(float)(uPrevValue-uMinValue)/(float)(uMaxValue - uMinValue);

             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_SM_VideoInfoStats.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                g_pRenderEngine->setStroke(70,70,250,0.96);
                g_pRenderEngine->setFill(70,70,250,0.96);
                g_pRenderEngine->setStrokeSize(2.1); 
             }

             g_pRenderEngine->drawLine(xBarStart, y+hGraph-hBarPrev, xBarStart, y+hGraph-hBar);
          
             if ( iIndexPrev >= 0 && iIndexPrev < MAX_FRAMES_SAMPLES )
             if ( g_SM_VideoInfoStats.uFramesTypesAndSizes[iIndexPrev] & (1<<7) )
             {
                osd_set_colors();
                g_pRenderEngine->setStrokeSize(2.1); 
             }
          }

          if ( g_SM_VideoInfoStats.uFramesTypesAndSizes[i] & (1<<7) )
          {
             osd_set_colors();
             g_pRenderEngine->setStrokeSize(2.1); 
          }
       }

       uPrevValue = g_SM_VideoInfoStats.uFramesTimes[i];
       iIndexPrev = i;
       xBarStart += widthBar;

       if ( i == g_SM_VideoInfoStats.uLastIndex )
       {
          g_pRenderEngine->setStroke(250,250,50,0.9);
          g_pRenderEngine->drawLine(xBarStart, y, xBarStart, y+hGraph);
          osd_set_colors();
          g_pRenderEngine->setStrokeSize(2.1);
       }
   }

   y += hGraph + height_text*0.4;

   g_pRenderEngine->drawText(xPos, y, s_idFontStatsSmall, "Avg/Min/Max frame:");
   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_VideoInfoStats )
      sprintf(szBuff, "%d / %d / %d ms", g_SM_VideoInfoStats.uAverageFrameTime, uMinValue, uMaxValue);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStatsSmall, szBuff);
   y += height_text_small*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max deviation from avg:");
   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_VideoInfoStats )
      sprintf(szBuff, "%d ms", g_SM_VideoInfoStats.uMaxFrameDeltaTime);
   g_pRenderEngine->drawTextLeft(rightMargin, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   }
   
   // -------------------------
   // Stats

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Deviation cam/tx/rx now:");
   y += height_text*s_OSDStatsLineSpacing;
   
   if ( NULL != g_pSM_VideoInfoStats && NULL != g_pSM_VideoInfoStatsRadioIn)
      sprintf(szBuff, "%u / %u / %d / %d ms", g_VideoInfoStatsFromVehicle.uMaxFrameDeltaTime, g_VideoInfoStatsFromVehicleRadioOut.uMaxFrameDeltaTime, g_SM_VideoInfoStatsRadioIn.uMaxFrameDeltaTime, g_SM_VideoInfoStats.uMaxFrameDeltaTime );
   else
      sprintf(szBuff, "%u / %u / N/A / N/A ms", g_VideoInfoStatsFromVehicle.uMaxFrameDeltaTime, g_VideoInfoStatsFromVehicleRadioOut.uMaxFrameDeltaTime );
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   if ( g_TimeNow > g_RouterIsReadyTimestamp + 6000 )
   {
      if ( g_VideoInfoStatsFromVehicle.uMaxFrameDeltaTime > s_uOSDMaxFrameDeviationCamera )
         s_uOSDMaxFrameDeviationCamera = g_VideoInfoStatsFromVehicle.uMaxFrameDeltaTime;
      if ( g_VideoInfoStatsFromVehicleRadioOut.uMaxFrameDeltaTime > s_uOSDMaxFrameDeviationTx )
         s_uOSDMaxFrameDeviationTx = g_VideoInfoStatsFromVehicleRadioOut.uMaxFrameDeltaTime;
      
      if ( NULL != g_pSM_VideoInfoStatsRadioIn )
      if ( g_SM_VideoInfoStatsRadioIn.uMaxFrameDeltaTime > s_uOSDMaxFrameDeviationRx )
         s_uOSDMaxFrameDeviationRx = g_SM_VideoInfoStatsRadioIn.uMaxFrameDeltaTime;

      if ( NULL != g_pSM_VideoInfoStats )
      if ( g_SM_VideoInfoStats.uMaxFrameDeltaTime > s_uOSDMaxFrameDeviationPlayer )
         s_uOSDMaxFrameDeviationPlayer = g_SM_VideoInfoStats.uMaxFrameDeltaTime;
   }

   g_pRenderEngine->drawText(xPos, y, s_idFontStats, "Max dev cam/tx/rx all:");
   y += height_text*s_OSDStatsLineSpacing;
   
   if ( NULL != g_pSM_VideoInfoStats && NULL != g_pSM_VideoInfoStatsRadioIn )
      sprintf(szBuff, "%u / %u / %d / %d ms", s_uOSDMaxFrameDeviationCamera, s_uOSDMaxFrameDeviationTx, s_uOSDMaxFrameDeviationRx, s_uOSDMaxFrameDeviationPlayer );
   else
      sprintf(szBuff, "%u / %u / N/A / N/A ms", s_uOSDMaxFrameDeviationCamera, s_uOSDMaxFrameDeviationTx );
   g_pRenderEngine->drawText(xPos, y, s_idFontStats, szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   return height;
}


float osd_render_stats_flight_end(float scale)
{
   u32 fontId = g_idFontOSD;
   float height_text = g_pRenderEngine->textHeight(fontId)*scale;
   Preferences* pP = get_Preferences();

   //float width = 0.38 * scale;
   float width = g_pRenderEngine->textWidth(fontId, "AAAAAAAA AAAAAAAA AAAAAA AAAAAA AAA AAAAAAAA");

   float lineHeight = height_text*s_OSDStatsLineSpacing*1.4;
   float height = 4.0 *s_fOSDStatsMargin*scale*1.1 + lineHeight;
   height += 14.6*lineHeight + 0.8 * height_text;
   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      height += lineHeight;

   float iconHeight = 2.2*lineHeight;
   float iconWidth = iconHeight/g_pRenderEngine->getAspectRatio();
   float xPos = (1.0-width)/2.0;
   float yPos = (1.0-height)/2.0;
   char szBuff[512];

   float fAlpha = g_pRenderEngine->setGlobalAlfa(0.9);

   float alfa = 0.9;
   if ( pP->iInvertColorsOSD )
      alfa = 0.8;
   double pc[4];
   pc[0] = get_Color_OSDBackground()[0];
   pc[1] = get_Color_OSDBackground()[1];
   pc[2] = get_Color_OSDBackground()[2];
   pc[3] = 0.9;
   g_pRenderEngine->setColors(pc, alfa);

   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN*scale);
   osd_set_colors();

   xPos += 2*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += 2*s_fOSDStatsMargin*scale*0.7;
   width -= 4*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();

   float widthMax = width;
   float rightMargin = xPos + width;

   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      g_pRenderEngine->drawText(xPos, yPos, fontId, "Post Flight Statistics");
   else
      g_pRenderEngine->drawText(xPos, yPos, fontId, "Post Run Statistics");

   if ( iconWidth > 0.001 )
   {
      u32 idIcon = osd_getVehicleIcon( g_pCurrentModel->vehicle_type );
      g_pRenderEngine->setColors(get_Color_MenuText(), 0.7);
      g_pRenderEngine->setStrokeSize(MENU_OUTLINEWIDTH);
      g_pRenderEngine->drawIcon(xPos+width - iconWidth - 0.9*POPUP_ROUND_MARGIN*scale/g_pRenderEngine->getAspectRatio() , yPos-height_text*0.1, iconWidth, iconHeight, idIcon);
   }
   float y = yPos + lineHeight;

   osd_set_colors();

   if ( NULL == g_pCurrentModel || (!g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry) )
   {
      g_pRenderEngine->setGlobalAlfa(fAlpha);
      return height;
   }

   strcpy(szBuff, g_pCurrentModel->getLongName());
   szBuff[0] = toupper(szBuff[0]);
   g_pRenderEngine->drawText(xPos, y, fontId, szBuff);
   y += lineHeight*1.9;

   g_pRenderEngine->drawLine(xPos, y-lineHeight*0.6, xPos+width, y-lineHeight*0.6);
   
   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      sprintf(szBuff, "Flight No. %d", (int)g_pCurrentModel->m_Stats.uTotalFlights);
   else
      sprintf(szBuff, "Run No. %d", (int)g_pCurrentModel->m_Stats.uTotalFlights);

   g_pRenderEngine->drawText(xPos, y, fontId, szBuff);
   y += lineHeight;

   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      g_pRenderEngine->drawText(xPos, y, fontId, "Flight time:");
   else
      g_pRenderEngine->drawText(xPos, y, fontId, "Run time:");
   if ( g_pCurrentModel->m_Stats.uCurrentFlightTime >= 3600 )
      sprintf(szBuff, "%dh:%02dm:%02ds", g_pCurrentModel->m_Stats.uCurrentFlightTime/3600, (g_pCurrentModel->m_Stats.uCurrentFlightTime/60)%60, (g_pCurrentModel->m_Stats.uCurrentFlightTime)%60);
   else
      sprintf(szBuff, "%02dm:%02ds", (g_pCurrentModel->m_Stats.uCurrentFlightTime/60)%60, (g_pCurrentModel->m_Stats.uCurrentFlightTime)%60);
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      g_pRenderEngine->drawText(xPos, y, fontId, "Total flight distance:");
   else
      g_pRenderEngine->drawText(xPos, y, fontId, "Total traveled distance:");

   if ( pP->iUnits == prefUnitsImperial )
   {
      if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
         sprintf(szBuff, "%.1f mi", _osd_convertKm(g_pCurrentModel->m_Stats.uCurrentFlightDistance/100)/1000.0);
      else
         sprintf(szBuff, "0.0 mi");
   }
   else
   {
      if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
         sprintf(szBuff, "%.1f km", (g_pCurrentModel->m_Stats.uCurrentFlightDistance/100)/1000.0);
      else
         sprintf(szBuff, "0.0 km");
   }
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   g_pRenderEngine->drawText(xPos, y, fontId, "Total current:");
   sprintf(szBuff, "%d mA", (int)(g_pCurrentModel->m_Stats.uCurrentFlightTotalCurrent/10));
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   g_pRenderEngine->drawText(xPos, y, fontId, "Max Distance:");
   if ( g_pCurrentModel->m_Stats.uCurrentMaxDistance < 1500 )
   {
      if ( pP->iUnits == prefUnitsImperial )
         sprintf(szBuff, "%d ft", (int)_osd_convertMeters(g_pCurrentModel->m_Stats.uCurrentMaxDistance));
      else
         sprintf(szBuff, "%d m", (int)_osd_convertMeters(g_pCurrentModel->m_Stats.uCurrentMaxDistance));
   }
   else
   {
      if ( pP->iUnits == prefUnitsImperial )
         sprintf(szBuff, "%.1f mi", _osd_convertKm(g_pCurrentModel->m_Stats.uCurrentMaxDistance/1000.0));
      else
         sprintf(szBuff, "%.1f km", _osd_convertKm(g_pCurrentModel->m_Stats.uCurrentMaxDistance/1000.0));
   }
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
   {
      g_pRenderEngine->drawText(xPos, y, fontId, "Max Altitude:");
      if ( pP->iUnits == prefUnitsImperial )
         sprintf(szBuff, "%d ft", (int)_osd_convertMeters(g_pCurrentModel->m_Stats.uCurrentMaxAltitude));
      else
         sprintf(szBuff, "%d m", (int)_osd_convertMeters(g_pCurrentModel->m_Stats.uCurrentMaxAltitude));
      g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
      y += lineHeight;
   }

   g_pRenderEngine->drawText(xPos, y, fontId, "Max Current:");
   sprintf(szBuff, "%.1f A", (float)g_pCurrentModel->m_Stats.uCurrentMaxCurrent/1000.0);
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   g_pRenderEngine->drawText(xPos, y, fontId, "Min Voltage:");
   sprintf(szBuff, "%.1f V", (float)g_pCurrentModel->m_Stats.uCurrentMinVoltage/1000.0);
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   if ( pP->iUnits == prefUnitsImperial )
      g_pRenderEngine->drawText(xPos, y, fontId, "Efficiency mA/mi:");
   else
      g_pRenderEngine->drawText(xPos, y, fontId, "Efficiency mA/km:");
   float eff = 0.0;
   if ( g_pCurrentModel->m_Stats.uCurrentFlightDistance > 500 )
   {
      float fv = (float)_osd_convertKm((float)g_pCurrentModel->m_Stats.uCurrentFlightDistance/100.0/1000.0);
      if ( fv > 0.0000001 )
         eff = g_pCurrentModel->m_Stats.uCurrentFlightTotalCurrent/10.0/fv;
      sprintf(szBuff, "%d", (int)eff);
   }
   else
      sprintf(szBuff, "Too Short");
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   g_pRenderEngine->drawText(xPos, y, fontId, "Efficiency mA/h:");
   eff = 0;
   if ( g_pCurrentModel->m_Stats.uCurrentFlightTime > 0 )
      eff = ((float)g_pCurrentModel->m_Stats.uCurrentTotalCurrent/10.0/((float)g_pCurrentModel->m_Stats.uCurrentFlightTime))*3600.0;
   sprintf(szBuff, "%d", (int)eff);
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   y += lineHeight*0.5;
   g_pRenderEngine->drawLine(xPos, y-3.0*g_pRenderEngine->getPixelHeight(), xPos+width, y-3.0*g_pRenderEngine->getPixelHeight());

   g_pRenderEngine->drawText(xPos, y, fontId, "Odometer:");
   if ( pP->iUnits == prefUnitsImperial )
      sprintf(szBuff, "%.1f Mi", _osd_convertKm(g_pCurrentModel->m_Stats.uTotalFlightDistance/100.0/1000.0));
   else
      sprintf(szBuff, "%.1f Km", _osd_convertKm(g_pCurrentModel->m_Stats.uTotalFlightDistance/100.0/1000.0));
   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   if ( g_pCurrentModel->vehicle_type == MODEL_TYPE_DRONE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_AIRPLANE ||
        g_pCurrentModel->vehicle_type == MODEL_TYPE_HELI )
      g_pRenderEngine->drawText(xPos, y, fontId, "All time flight time:");
   else
      g_pRenderEngine->drawText(xPos, y, fontId, "All time drive time:");

   if ( g_pCurrentModel->m_Stats.uTotalFlightTime >= 3600 )
      sprintf(szBuff, "%dh:%02dm:%02ds", g_pCurrentModel->m_Stats.uTotalFlightTime/3600, (g_pCurrentModel->m_Stats.uTotalFlightTime/60)%60, (g_pCurrentModel->m_Stats.uTotalFlightTime)%60);
   else
      sprintf(szBuff, "%02dm:%02ds", (g_pCurrentModel->m_Stats.uTotalFlightTime/60)%60, (g_pCurrentModel->m_Stats.uTotalFlightTime)%60);

   g_pRenderEngine->drawTextLeft(rightMargin, y, fontId, szBuff);
   y += lineHeight;

   g_pRenderEngine->setGlobalAlfa(fAlpha);

   ///////////////////////////////////
   y += height_text * 1.8;
   float width_text = g_pRenderEngine->textWidth(s_idFontStats, "Ok");    
   float selectionMargin = 0.01;
   float fWidthSelection = width_text + 2.0*selectionMargin;
   g_pRenderEngine->setColors(get_Color_ItemSelectedBg());
   g_pRenderEngine->drawRoundRect(xPos-1.0*selectionMargin/g_pRenderEngine->getAspectRatio(), y-0.8*selectionMargin, fWidthSelection+2.0*selectionMargin/g_pRenderEngine->getAspectRatio(), height_text+2*selectionMargin, selectionMargin);
   g_pRenderEngine->setColors(get_Color_ItemSelectedText());
   g_pRenderEngine->drawText(xPos, y+g_pRenderEngine->getPixelHeight()*1.2, g_idFontMenu, "Ok");

   osd_set_colors();
   g_pRenderEngine->drawMessageLines("Press [Ok/Menu] or [Back/Cancel] to close this stats.", xPos + fWidthSelection + width_text, y - height_text*0.2, MENU_TEXTLINE_SPACING, rightMargin-(xPos + fWidthSelection + width_text), s_idFontStats);
   return height;
}

float osd_render_stats_flights(float scale)
{
/*
   Preferences* pP = get_Preferences();
   float text_scale = osd_getFontHeight()*scale;
   float height_text = TextHeight(*render_getFontOSD(), text_scale);

   text_scale *= 0.6;
   height_text *= 0.6;

   float width = toScreenX(0.64) * scale;
   float height = height_text*4.0;

   float xPos = toScreenX(0.2) * scale;
   float yPos = toScreenY(0.18) * scale;
   char szBuff[512];

   float fAlpha = render_get_globalAlfa(); 
   render_set_globalAlfa(0.85);
   float alfa = 0.7;
   if ( pP->iInvertColorsOSD )
      alfa = 0.74;

   render_set_colors(get_Color_OSDBackground(), alfa);

   Roundrect(xPos, yPos, width, height, 1.5*toScreenX(POPUP_ROUND_MARGIN)*scale, 1.5*toScreenX(POPUP_ROUND_MARGIN)*scale);
   osd_set_colors();

   xPos += toScreenX(s_fOSDStatsMargin)*scale;
   yPos = yPos + height - toScreenX(s_fOSDStatsMargin)*scale*0.5;
   width -= 2*toScreenX(s_fOSDStatsMargin)*scale;

   float widthMax = width;
   float rightMargin = xPos + width;

   if ( NULL == g_pCurrentModel )
   {
      render_set_globalAlfa(fAlpha);
      return height;
   }

   sprintf(szBuff, "%s Statistics", g_pCurrentModel->getShortName());
   draw_message(szBuff, xPos, yPos, text_scale*0.98, render_getFontOSD());
   
   yPos -= height_text*1.4;

   osd_set_colors();

   if ( pP->iUnits == prefUnitsImperial )
      sprintf(szBuff, "Total flights: %d,  Total flight time: %dh:%02dm:%02ds,  Total flight distance: %.1f mi",
                       g_pCurrentModel->stats_TotalFlights, g_pCurrentModel->stats_TotalFlightTime/10/3600,
                       (g_pCurrentModel->stats_TotalFlightTime/10/60)%60, (g_pCurrentModel->stats_TotalFlightTime/10)%60,
                       _osd_convertKm(g_pCurrentModel->stats_TotalFlightDistance/10.0/1000.0));
   else
      sprintf(szBuff, "Total flights: %d,  Total flight time: %dh:%02dm:%02ds,  Total flight distance: %.1f km",
              g_pCurrentModel->stats_TotalFlights, g_pCurrentModel->stats_TotalFlightTime/10/3600,
              (g_pCurrentModel->stats_TotalFlightTime/10/60)%60, (g_pCurrentModel->stats_TotalFlightTime/10)%60,
              g_pCurrentModel->stats_TotalFlightDistance/10.0/1000.0);
   draw_message(szBuff, xPos, yPos, text_scale*0.9, render_getFontOSD());

   render_set_globalAlfa(fAlpha);

   return height;
*/
return 0.0;
}

float osd_render_stats_dev_get_height()
{
   Preferences* p = get_Preferences();

   float height_text = g_pRenderEngine->textHeight(s_idFontStats);
   float height_text_small = g_pRenderEngine->textHeight(s_idFontStats);
   float hGraph = 0.04;
   float hGraphSmall = 0.028;

   float height = 1.6 *s_fOSDStatsMargin*1.3 + 2 * 0.7*height_text*s_OSDStatsLineSpacing;

   if ( p->iDebugShowDevRadioStats )
      height += 12 * height_text * s_OSDStatsLineSpacing;

   if ( p->iDebugShowDevVideoStats )
   {
      height += 1.0*(hGraph + height_text) + 4.0*(hGraphSmall + height_text);
   }
   return height;
}

float osd_render_stats_dev_get_width()
{
   if ( g_fOSDStatsForcePanelWidth > 0.01 )
      return g_fOSDStatsForcePanelWidth;
   float width = g_pRenderEngine->textWidth(s_idFontStats, "AAAAAAAA AAAAAAAA AAAAAAAA AAA");
   width += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();
   return width;
}


float osd_render_stats_dev(float xPos, float yPos, float scale)
{
   Preferences* p = get_Preferences();

   float height_text = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float height_text_small = g_pRenderEngine->textHeight(s_idFontStats)*scale;
   float hGraphRegular = 0.04*scale;
   float hGraphSmall = 0.028*scale;
   float hGraph = hGraphRegular;

   float width = osd_render_stats_dev_get_width();
   float height = osd_render_stats_dev_get_height();

   char szBuff[128];

   osd_set_colors_background_fill(g_fOSDStatsBgTransparency);
   g_pRenderEngine->drawRoundRect(xPos, yPos, width, height, 1.5*POPUP_ROUND_MARGIN);
   osd_set_colors();

   xPos += s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   yPos += s_fOSDStatsMargin*scale*0.7;
   width -= 2*s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();
   float widthMax = width;
   float rightMargin = xPos + width;

   g_pRenderEngine->setColors(get_Color_Dev());

   g_pRenderEngine->drawText( xPos, yPos, s_idFontStats, "Developer Mode");
   
   float y = yPos + height_text*1.3*s_OSDStatsLineSpacing;
   float yTop = y;

   u32 linkMin = 900;
   u32 linkMax = 0;

   if ( g_bIsRouterReady )
   {
      for( int i=0; i<g_SM_RadioStats.countLocalRadioLinks; i++ )
      {
         if ( (0 == g_SM_RadioStats.radio_links[i].linkDelayRoundtripMs) || (g_SM_RadioStats.radio_links[i].linkDelayRoundtripMsLastTime == 0) || (g_SM_RadioStats.radio_links[i].linkDelayRoundtripMsLastTime == MAX_U32) || (g_SM_RadioStats.radio_links[i].linkDelayRoundtripMsLastTime < g_TimeNow-1000) )
            continue;
         if ( g_SM_RadioStats.radio_links[i].linkDelayRoundtripMs > linkMax )
            linkMax = g_SM_RadioStats.radio_links[i].linkDelayRoundtripMs;
         if ( g_SM_RadioStats.radio_links[i].linkDelayRoundtripMinimMs < linkMin )
            linkMin = g_SM_RadioStats.radio_links[i].linkDelayRoundtripMinimMs;
      }
   }

   if ( p->iDebugShowDevRadioStats )
   {
   if ( 0 != linkMax )
      sprintf(szBuff, "%d ms", linkMax);
   else
      strcpy(szBuff, "N/A");
   _osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "Link RT (max):", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   if ( 0 != linkMin )
      sprintf(szBuff, "%d ms", linkMin);
   else
      strcpy(szBuff, "N/A");
   _osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "Link RT (minim):", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   // TO FIX
   /*
   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_ControllerRetransmissionsStats && MAX_U32 != g_SM_ControllerRetransmissionsStats.retransmissionTimeAverage && MAX_U32 != g_SM_ControllerRetransmissionsStats.retransmissionTimeMinim )
      sprintf(szBuff, "%d/%d ms", g_SM_ControllerRetransmissionsStats.retransmissionTimeMinim, g_SM_ControllerRetransmissionsStats.retransmissionTimeAverage);
   if ( ! (g_pCurrentModel->video_link_profiles[(g_SM_VideoDecodeStats.video_link_profile & 0x0F)].encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_RETRANSMISSIONS ) )
      strcpy(szBuff, "Disabled");
   _osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "Video RT Min/Avg:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_ControllerRetransmissionsStats && MAX_U32 != g_SM_ControllerRetransmissionsStats.retransmissionTimeLast )
      sprintf(szBuff, "%d ms", g_SM_ControllerRetransmissionsStats.retransmissionTimeLast);
   if ( ! (g_pCurrentModel->video_link_profiles[(g_SM_VideoDecodeStats.video_link_profile & 0x0F)].encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_RETRANSMISSIONS ) )
      strcpy(szBuff, "Disabled");
   _osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "Video RT Last:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   g_pRenderEngine->drawText(xPos, y, height_text, s_idFontStats, "Retransmissions:");
   y += height_text*s_OSDStatsLineSpacing;
   float dx = s_fOSDStatsMargin*scale/g_pRenderEngine->getAspectRatio();

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_ControllerRetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1], ((g_SM_ControllerRetransmissionsStats.totalCountReceivedVideoPackets[1] + g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1])>0)?g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]*100/(g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]+g_SM_ControllerRetransmissionsStats.totalCountReceivedVideoPackets[1]):0);
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Requested:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_ControllerRetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_SM_ControllerRetransmissionsStats.totalCountReRequestedPackets[1], (g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]>0)?(g_SM_ControllerRetransmissionsStats.totalCountReRequestedPackets[1]*100/g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]):0);
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Retried:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_ControllerRetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_SM_ControllerRetransmissionsStats.totalCountReceivedRetransmissions[1], (g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]>0)?(g_SM_ControllerRetransmissionsStats.totalCountReceivedRetransmissions[1]*100/g_SM_ControllerRetransmissionsStats.totalCountRequestedRetransmissions[1]):0);
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Received:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_RetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_pSM_RetransmissionsStats->totalCountReceivedRetransmissionsDuplicated[1], (g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]>0)?(g_pSM_RetransmissionsStats->totalCountReceivedRetransmissionsDuplicated[1]*100/g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]):0);
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Recv Duplicated:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_RetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_pSM_RetransmissionsStats->totalCountReceivedRetransmissionsDiscarded[1], (g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]>0)?(g_pSM_RetransmissionsStats->totalCountReceivedRetransmissionsDiscarded[1]*100/g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]):0 );
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Discarded:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;


   strcpy(szBuff, "N/A");
   if ( NULL != g_pSM_RetransmissionsStats )
      sprintf(szBuff, "%u/sec (%d%%)", g_pSM_RetransmissionsStats->totalCountRecvOriginalAfterRetransmission[1], (g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]>0)?(g_pSM_RetransmissionsStats->totalCountRecvOriginalAfterRetransmission[1]*100/g_pSM_RetransmissionsStats->totalCountReceivedRetransmissions[1]):0 );
   _osd_stats_draw_line(xPos+dx, rightMargin, y, s_idFontStats, "Original recv after retr.:", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   sprintf(szBuff, "N/A");
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      sprintf(szBuff, "%d", (int)(g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].headerFCTelemetry.extra_info[6]));
   _osd_stats_draw_line(xPos, rightMargin, y, s_idFontStats, "FC msg/sec", szBuff);
   y += height_text*s_OSDStatsLineSpacing;

   */
   }
   

   osd_set_colors();

   if ( p->iDebugShowDevVideoStats )
   {
   // ------------------------------------------------
   // Retransmissions graphs

   // TO FIX
    /*
   y += (height_text-height_text_small);

   static int s_iOSDStatsDevMaxGraphValue = 0;

   int maxGraphValue = g_pSM_RetransmissionsStats->maxValue;
   if ( maxGraphValue >= s_iOSDStatsDevMaxGraphValue )
   {
      s_iOSDStatsDevMaxGraphValue = maxGraphValue;
   }
   else
   {
      if ( s_iOSDStatsDevMaxGraphValue > 50 )
         s_iOSDStatsDevMaxGraphValue-=5;
      else if ( s_iOSDStatsDevMaxGraphValue > 0 )
         s_iOSDStatsDevMaxGraphValue--;
   }

   int totalHistoryValues = MAX_HISTORY_INTERVALS_RETRANSMISSIONS_STATS;
   float dxGraph = 0.01*scale;
   float widthGraph = widthMax - dxGraph;
   float widthBar = widthGraph / totalHistoryValues;

   g_pRenderEngine->setColors(get_Color_Dev());

   g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, "Regular vs Retransmissions");

   y += height_text_small*0.9;

   float yTopGrid = y;

   hGraph = hGraphSmall;

   double* pc = get_Color_OSDText();
   float midLine = hGraph/2.0;
   float wPixel = g_pRenderEngine->getPixelWidth();
   float yBtm = y + hGraph - g_pRenderEngine->getPixelHeight();
   float yTmp;
   float yTmpPrev;


   // Actually received and retransmissions counts

   int iMax = s_iOSDStatsDevMaxGraphValue;
   for( int i=0; i<totalHistoryValues; i++ )
   {
      if ( iMax < g_pSM_RetransmissionsStats->expectedVideoPackets[i] )
         iMax = g_pSM_RetransmissionsStats->expectedVideoPackets[i];
      if ( iMax < g_pSM_RetransmissionsStats->receivedVideoPackets[i] )
         iMax = g_pSM_RetransmissionsStats->receivedVideoPackets[i];
   }
   if ( iMax <= 0 )
      iMax = 1;
   sprintf(szBuff, "%d", iMax);
   g_pRenderEngine->drawText(xPos, y-0.3*height_text_small, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   //sprintf(szBuff, "%d", g_pSM_RetransmissionsStats->expectedVideoPackets[totalHistoryValues-1]);
   sprintf(szBuff, "%d", iMax/2);
   g_pRenderEngine->drawText(xPos, y+0.5*hGraph-0.6*height_text_small, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text_small*0.8, height_text_small*0.9, s_idFontStatsSmall, "0");

   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + widthGraph, y+hGraph);
   //for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
   //   g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(1.0);

   float xBarSt = xPos + dxGraph;
   float xBarCenter = xPos + dxGraph + widthBar*0.5;
   float xBarCenterPrev = xPos + dxGraph - widthBar*0.5;

   int index = g_pSM_RetransmissionsStats->currentSlice;

   for( int i=0; i<totalHistoryValues; i++ )
   {
      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->requestedRetransmissions[index] / (float)iMax;
      g_pRenderEngine->setStroke(50,120,200,0.7);
      g_pRenderEngine->setFill(50,120,200,0.7);
      //g_pRenderEngine->drawLine(xBarSt, yTmp, xBarSt + widthBar, yTmp);
      g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);

      if ( i != 0 && index != totalHistoryValues-1 )
      {
      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->expectedVideoPackets[index] / (float)iMax;
      yTmpPrev = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->expectedVideoPackets[index+1] / (float)iMax;
      g_pRenderEngine->setStroke(230,230,0,0.75);
      g_pRenderEngine->drawLine(xBarCenter, yTmp, xBarCenterPrev, yTmpPrev);
      g_pRenderEngine->drawLine(xBarCenter, yTmp-g_pRenderEngine->getPixelHeight(), xBarCenterPrev, yTmpPrev-g_pRenderEngine->getPixelHeight());

      yTmp = yBtm - g_pRenderEngine->getPixelHeight() - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->receivedVideoPackets[index] / (float)iMax;
      yTmpPrev = yBtm - g_pRenderEngine->getPixelHeight() - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->receivedVideoPackets[index+1] / (float)iMax;
      g_pRenderEngine->setStroke(20,250,70,0.8);
      g_pRenderEngine->drawLine(xBarCenter, yTmp, xBarCenterPrev, yTmpPrev);
      g_pRenderEngine->drawLine(xBarCenter, yTmp-g_pRenderEngine->getPixelHeight(), xBarCenterPrev, yTmpPrev-g_pRenderEngine->getPixelHeight());
      }
      index--;
      if ( index < 0 )
         index = totalHistoryValues-1;
      xBarSt += widthBar;
      xBarCenter += widthBar;
      xBarCenterPrev += widthBar;
   }

   y += hGraph;

   // Retransmissions graphs

   g_pRenderEngine->setColors(get_Color_Dev());

   hGraph = hGraphRegular;

   y += (height_text-height_text_small);
   sprintf(szBuff,"%d ms/bar", g_pSM_RetransmissionsStats->refreshInterval);
   g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   float w = g_pRenderEngine->textWidth(height_text*0.9, s_idFontStats, szBuff);
   w += 0.02*scale;
   sprintf(szBuff,"%.1f seconds", (((float)totalHistoryValues) * g_pSM_RetransmissionsStats->refreshInterval)/1000.0);
   g_pRenderEngine->drawTextLeft(xPos+widthMax-w, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   w += g_pRenderEngine->textWidth( height_text*0.9, s_idFontStats, szBuff);

   y += height_text_small*0.9;

   sprintf(szBuff, "%d", s_iOSDStatsDevMaxGraphValue);
   g_pRenderEngine->drawText(xPos, y-0.3*height_text_small, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text_small*0.8, height_text_small*0.9, s_idFontStatsSmall, "0");

   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + widthGraph, y+hGraph);
   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(1.0);

   xBarSt = xPos + dxGraph;
   xBarCenter = xPos + dxGraph + widthBar*0.5;
   xBarCenterPrev = xPos + dxGraph - widthBar*0.5;
   yBtm = y + hGraph - g_pRenderEngine->getPixelHeight();

   index = g_pSM_RetransmissionsStats->currentSlice;

   for( int i=0; i<totalHistoryValues; i++ )
   {
      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->requestedRetransmissions[index] / (float)s_iOSDStatsDevMaxGraphValue;
      g_pRenderEngine->setStroke(50,120,200,0.7);
      g_pRenderEngine->setFill(50,120,200,0.7);
      //g_pRenderEngine->drawLine(xBarSt, yTmp, xBarSt + widthBar, yTmp);
      g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);

      if ( i != 0 && index != totalHistoryValues-1 )
      {
      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->receivedRetransmissions[index] / (float)s_iOSDStatsDevMaxGraphValue;
      yTmpPrev = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->receivedRetransmissions[index+1] / (float)s_iOSDStatsDevMaxGraphValue;
      g_pRenderEngine->setStroke(20,250,70,0.8);
      g_pRenderEngine->drawLine(xBarCenter, yTmp, xBarCenterPrev, yTmpPrev);
      g_pRenderEngine->drawLine(xBarCenter, yTmp-g_pRenderEngine->getPixelHeight(), xBarCenterPrev, yTmpPrev-g_pRenderEngine->getPixelHeight());

      yTmp = yBtm - g_pRenderEngine->getPixelHeight() - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->ignoredRetransmissions[index] / (float)s_iOSDStatsDevMaxGraphValue;
      yTmpPrev = yBtm - g_pRenderEngine->getPixelHeight() - (hGraph-2*g_pRenderEngine->getPixelHeight())* (float)g_pSM_RetransmissionsStats->ignoredRetransmissions[index+1] / (float)s_iOSDStatsDevMaxGraphValue;
      if ( g_pSM_RetransmissionsStats->ignoredRetransmissions[i] > 0 )
      {
         g_pRenderEngine->setStroke(200,20,0,0.65);
         g_pRenderEngine->setFill(200,20,0,0.65);
         g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);
      }
      //g_pRenderEngine->drawLine(xBarCenter, yTmp, xBarCenterPrev, yTmpPrev);
      //g_pRenderEngine->drawLine(xBarCenter, yTmp+g_pRenderEngine->getPixelHeight(), xBarCenterPrev, yTmpPrev+g_pRenderEngine->getPixelHeight());
      }

      index--;
      if ( index < 0 )
         index = totalHistoryValues-1;
      xBarSt += widthBar;
      xBarCenter += widthBar;
      xBarCenterPrev += widthBar;
   }

   y += hGraph;

   // Percent retried

   g_pRenderEngine->setColors(get_Color_Dev());

   hGraph = hGraphSmall;

   float fPercentMax = 0;
   int sumReceived = 0;
   int sumIgnored = 0;
   int sumRequested = 0;
   int sumReRequested = 0;
   for( int i=0; i<totalHistoryValues; i++ )
   {
      if ( g_pSM_RetransmissionsStats->requestedRetransmissions[i] > 0 )
      if ( (float)g_pSM_RetransmissionsStats->reRequestedRetransmissions[i]/(float)g_pSM_RetransmissionsStats->requestedRetransmissions[i] > fPercentMax )
         fPercentMax = (float)g_pSM_RetransmissionsStats->reRequestedRetransmissions[i]/(float)g_pSM_RetransmissionsStats->requestedRetransmissions[i];

      sumReceived += g_pSM_RetransmissionsStats->receivedRetransmissions[i];
      sumIgnored += g_pSM_RetransmissionsStats->ignoredRetransmissions[i];
      sumRequested += g_pSM_RetransmissionsStats->requestedRetransmissions[i];
      sumReRequested += g_pSM_RetransmissionsStats->reRequestedRetransmissions[i];
   }
   int average = 0;
   if ( sumRequested > 0 )
      average = sumReRequested*100/sumRequested;

   if ( fPercentMax < 1.0 )
      fPercentMax = 1.0;
   y += (height_text-height_text_small);
   sprintf(szBuff,"Percentage Retried (avg: %d%%)", average);
   g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   y += height_text_small*0.9;

   sprintf(szBuff, "%d%%", (int)(fPercentMax*100.0) );
   g_pRenderEngine->drawText(xPos, y-0.2*height_text_small, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text_small*0.8, height_text_small*0.9, s_idFontStatsSmall, "0%");

   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + widthGraph, y+hGraph);
   midLine = hGraph/2.0;
   wPixel = g_pRenderEngine->getPixelWidth();
   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(1.0);

   xBarSt = xPos + dxGraph;
   yBtm = y + hGraph - g_pRenderEngine->getPixelHeight();

   index = g_pSM_RetransmissionsStats->currentSlice;

   
   for( int i=0; i<totalHistoryValues; i++ )
   {
      g_pRenderEngine->setStroke(250,250,240, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(250,250,240, s_fOSDStatsGraphLinesAlpha);

      if ( g_pSM_RetransmissionsStats->requestedRetransmissions[i] > 0 && index != totalHistoryValues-1 )
      {
      float percent = (float)g_pSM_RetransmissionsStats->reRequestedRetransmissions[index]/(float)g_pSM_RetransmissionsStats->requestedRetransmissions[index];

      g_pRenderEngine->setStroke(250,250,50, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(250,250,50, s_fOSDStatsGraphLinesAlpha);

      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight()) * percent / fPercentMax;
      g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);
      }
      index--;
      if ( index < 0 )
         index = totalHistoryValues-1;
      xBarSt += widthBar;
      xBarCenter += widthBar;
      xBarCenterPrev += widthBar;
   }
   y += hGraph;


   // Percent received

   g_pRenderEngine->setColors(get_Color_Dev());

   hGraph = hGraphSmall;
   y += (height_text-height_text_small);

   average = 0;
   if ( sumRequested > 0 )
      average = sumReceived*100/sumRequested;

   sprintf(szBuff,"Percentage Received (avg %d%%)", average);
   g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   y += height_text_small*0.9;

   g_pRenderEngine->drawText(xPos, y-0.2*height_text_small, height_text_small*0.9, s_idFontStatsSmall, "100%");
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text_small*0.8, height_text_small*0.9, s_idFontStatsSmall, "0%");

   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + widthGraph, y+hGraph);
   midLine = hGraph/2.0;
   wPixel = g_pRenderEngine->getPixelWidth();
   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(1.0);

   xBarSt = xPos + dxGraph;
   yBtm = y + hGraph - g_pRenderEngine->getPixelHeight();

   index = g_pSM_RetransmissionsStats->currentSlice;
   
   for( int i=0; i<totalHistoryValues; i++ )
   {
      g_pRenderEngine->setStroke(250,250,240, s_fOSDStatsGraphLinesAlpha);
      g_pRenderEngine->setFill(250,250,240, s_fOSDStatsGraphLinesAlpha);

      if ( index != totalHistoryValues-1 )
      {
      float percent = 0.0;
      if ( g_pSM_RetransmissionsStats->requestedRetransmissions[index] > 0 )
         percent = (float)g_pSM_RetransmissionsStats->receivedRetransmissions[index] / (float)g_pSM_RetransmissionsStats->requestedRetransmissions[index];
      if ( percent < 0.0 ) percent = 0.0;
      if ( percent > 1.0 )
      {
         percent = 1.0;
         g_pRenderEngine->setStroke(250,250,50, s_fOSDStatsGraphLinesAlpha);
         g_pRenderEngine->setFill(250,250,50, s_fOSDStatsGraphLinesAlpha);
      }
      yTmp = yBtm - (hGraph-2*g_pRenderEngine->getPixelHeight())* percent;
      g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);
      }

      index--;
      if ( index < 0 )
         index = totalHistoryValues-1;
      xBarSt += widthBar;
      xBarCenter += widthBar;
      xBarCenterPrev += widthBar;
   }
   y += hGraph;

   // Percent discarded

   g_pRenderEngine->setColors(get_Color_Dev());

   y += (height_text-height_text_small);

   average = 0;
   if ( sumReceived > 0 )
      average = sumIgnored*100/sumReceived;

   sprintf(szBuff,"Percentage Discarded (avg %d%%)", average);
   g_pRenderEngine->drawTextLeft(xPos+widthMax, y-height_text_small*0.2, height_text_small*0.9, s_idFontStatsSmall, szBuff);
   y += height_text_small*0.9;

   g_pRenderEngine->drawText(xPos, y-0.2*height_text_small, height_text_small*0.9, s_idFontStatsSmall, "100%");
   g_pRenderEngine->drawText(xPos, y+hGraph-height_text_small*0.8, height_text_small*0.9, s_idFontStatsSmall, "0%");

   g_pRenderEngine->setStrokeSize(OSD_STRIKE_WIDTH);
   g_pRenderEngine->setStroke(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(pc[0], pc[1], pc[2], s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->drawLine(xPos+dxGraph, y, xPos + dxGraph + widthGraph, y);         
   g_pRenderEngine->drawLine(xPos+dxGraph, y+hGraph, xPos + dxGraph + widthGraph, y+hGraph);
   midLine = hGraph/2.0;
   wPixel = g_pRenderEngine->getPixelWidth();
   for( float i=0; i<=widthGraph-2.0*wPixel; i+= 5*wPixel )
      g_pRenderEngine->drawLine(xPos+dxGraph+i, y+midLine, xPos + dxGraph + i + 2.0*wPixel, y+midLine);         

   g_pRenderEngine->setStrokeSize(1.0);

   xBarSt = xPos + dxGraph;
   yBtm = y + hGraph - g_pRenderEngine->getPixelHeight();

   index = g_pSM_RetransmissionsStats->currentSlice;
   
   g_pRenderEngine->setStroke(250,250,240, s_fOSDStatsGraphLinesAlpha);
   g_pRenderEngine->setFill(250,250,240, s_fOSDStatsGraphLinesAlpha);

   for( int i=0; i<totalHistoryValues; i++ )
   {
      if ( index != totalHistoryValues-1 )
      {
      float percent = 0.0;
      if ( g_pSM_RetransmissionsStats->receivedRetransmissions[index] > 0 )
         percent = (float)g_pSM_RetransmissionsStats->ignoredRetransmissions[index] / (float)g_pSM_RetransmissionsStats->receivedRetransmissions[index];
      if ( percent < 0.0 ) percent = 0.0;
      if ( percent > 1.0 ) percent = 1.0;
      float fh = (hGraph-2*g_pRenderEngine->getPixelHeight())* percent;
      int tmp = fh/g_pRenderEngine->getPixelHeight();
      fh = g_pRenderEngine->getPixelHeight() * (float)tmp;
      yTmp = yBtm - fh;
      g_pRenderEngine->drawRect(xBarSt, yTmp, widthBar-wPixel, yBtm-yTmp);
      }
      index--;
      if ( index < 0 )
         index = totalHistoryValues-1;
      xBarSt += widthBar;
      xBarCenter += widthBar;
      xBarCenterPrev += widthBar;
   }
   y += hGraph;

   float yBottomGrid = y;

   xBarSt = xPos + dxGraph + widthGraph;


   osd_set_colors();
   g_pRenderEngine->setStrokeSize(1.0);
   g_pRenderEngine->drawLine(xPos+dxGraph + g_pSM_RetransmissionsStats->currentSlice*widthBar+widthBar*0.5, yTopGrid, xPos+dxGraph + g_pSM_RetransmissionsStats->currentSlice*widthBar, yBottomGrid);
   g_pRenderEngine->drawLine(xPos+dxGraph + g_pSM_RetransmissionsStats->currentSlice*widthBar+widthBar*0.5 + wPixel, yTopGrid, xPos+dxGraph + g_pSM_RetransmissionsStats->currentSlice*widthBar+wPixel, yBottomGrid);
  
   g_pRenderEngine->setStroke(250,250,240, s_fOSDStatsGraphLinesAlpha*0.7);
   g_pRenderEngine->setFill(250,250,240, s_fOSDStatsGraphLinesAlpha*0.7);

   for( int i=0; i<totalHistoryValues; i++ )
   {
      xBarSt -= widthBar;
      if ( i%3 )
         continue;

      g_pRenderEngine->drawLine(xBarSt, yTopGrid, xBarSt, yBottomGrid);
   }
*/
   }
   return height;
}

void _osd_render_stats_panels_horizontal()
{
   if ( NULL == g_pCurrentModel )
      return;
   
   ControllerSettings* pCS = get_ControllerSettings();
   Preferences* p = get_Preferences();
   float fStatsSize = 1.0;

   float yMax = 1.0-osd_getMarginY()-osd_getBarHeight()-0.01 - osd_getSecondBarHeight();

   if ( g_pCurrentModel->osd_params.layout == osdLayoutLean )
      yMax -= 0.05; 

   float xWidth = 0.44*fStatsSize;
   float xSpacing = 0.01*fStatsSize;

   float yStats = yMax;
   float xStats = 0.99-osd_getMarginX();

   if ( g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_LAYOUT_LEFT_RIGHT )
   {
      float fWidth = osd_getVerticalBarWidth();
      xStats -= fWidth;
      yMax = 1.0-osd_getMarginY()-0.01;
      yStats = yMax;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowDevVideoStats || p->iDebugShowDevRadioStats )
   {
      osd_render_stats_dev(xStats, yStats-osd_render_stats_dev_get_height(), fStatsSize);
      xStats -= osd_render_stats_dev_get_width() + xSpacing;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoGraphs )
   {
      osd_render_stats_video_graphs(xStats, yStats-osd_render_stats_video_graphs_get_height());
      xStats -= osd_render_stats_video_graphs_get_width() + xSpacing;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoStats )
   {
      osd_render_stats_video_stats(xStats, yStats-osd_render_stats_video_stats_get_height());
      xStats -= osd_render_stats_video_stats_get_width() + xSpacing;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( NULL != g_pCurrentModel && (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_SEND_BACK_VEHICLE_TX_GAP) )
   {
      osd_render_stats_graphs_vehicle_tx_gap(xStats, yStats-osd_render_stats_graphs_vehicle_tx_gap_get_height());
      xStats -= osd_render_stats_graphs_vehicle_tx_gap_get_width() + xSpacing;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( NULL != g_pCurrentModel && (g_pCurrentModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_VIDEO_BITRATE_HISTORY) )
   {
      osd_render_stats_video_bitrate_history(xStats - osd_render_stats_video_bitrate_history_get_width(), yStats-osd_render_stats_video_bitrate_history_get_height());
      xStats -= osd_render_stats_video_bitrate_history_get_width() + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_VIDEO) )
   {
      float hStat = osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize);
      osd_render_stats_video_decode(xStats, yStats-hStat, pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize);
      xStats -= osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize) + xSpacing;
      if ( p->iDebugShowVideoSnapshotOnDiscard )
      if ( s_uOSDSnapshotTakeTime > 1 && g_TimeNow < s_uOSDSnapshotTakeTime + 15000 )
      {
         hStat = osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, fStatsSize);
         osd_render_stats_video_decode(xStats, yStats-hStat, pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, fStatsSize);
         xStats -= osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, fStatsSize) + xSpacing;
      }
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_LINKS) )
   if ( g_bIsRouterReady )
   {
      osd_render_stats_local_radio_links( xStats, yStats-osd_render_stats_local_radio_links_get_height(&g_SM_RadioStats, fStatsSize), "Radio Links", &g_SM_RadioStats, fStatsSize);
      xStats -= osd_render_stats_local_radio_links_get_width(&g_SM_RadioStats, fStatsSize) + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_INTERFACES) )
   if ( g_bIsRouterReady )
   {
      osd_render_stats_radio_interfaces( xStats, yStats-osd_render_stats_radio_interfaces_get_height(&g_SM_RadioStats), "Radio Interfaces", &g_SM_RadioStats);
      xStats -= osd_render_stats_radio_interfaces_get_width(&g_SM_RadioStats) + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_SHOW_EFFICIENCY_STATS) )
   {
      osd_render_stats_efficiency(xStats, yStats - osd_render_stats_efficiency_get_height(fStatsSize), fStatsSize);
      xStats -= osd_render_stats_efficiency_get_width(fStatsSize) + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_TELEMETRY_STATS) )
   {
      osd_render_stats_telemetry(xStats, yStats-osd_render_stats_telemetry_get_height(fStatsSize), fStatsSize);
      xStats -= osd_render_stats_telemetry_get_width(fStatsSize) + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_AUDIO_DECODE_STATS) )
   {
      osd_render_stats_audio_decode(xStats, yStats-osd_render_stats_audio_decode_get_height(fStatsSize), fStatsSize);
      xStats -= osd_render_stats_audio_decode_get_width(fStatsSize) + xSpacing;
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RC) )
   {
      osd_render_stats_rc(xStats, yStats-osd_render_stats_rc_get_height(fStatsSize), fStatsSize);
      xStats -= osd_render_stats_rc_get_width(fStatsSize) + xSpacing;
   }

}

void _osd_render_stats_panels_vertical()
{
   ControllerSettings* pCS = get_ControllerSettings();
   Preferences* p = get_Preferences();
   float fStatsSize = 1.0;
  
   float hBar = osd_getBarHeight();
   float hBar2 = osd_getSecondBarHeight();
   if ( g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_LAYOUT_LEFT_RIGHT )
   {
      hBar = 0.0;
      hBar2 = 0.0;
   }

   float yMin = osd_getMarginY() + hBar + hBar2 + 0.04; 
   float yMax = 1.0-osd_getMarginY() - hBar - hBar2 - 0.03;
   
   if ( g_pCurrentModel->osd_params.layout == osdLayoutLean )
      yMax -= 0.05; 

   float yStats = yMin;
   float xStats = 0.99-osd_getMarginX();
   float fMaxColumnWidth = 0.0;
   float fSpacingH = 0.01;
   float fSpacingV = 0.01;

   if ( g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_LAYOUT_LEFT_RIGHT )
      xStats -= osd_getVerticalBarWidth();

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowDevVideoStats || p->iDebugShowDevRadioStats )
   {
      if ( yStats + osd_render_stats_dev_get_height() > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }     
      osd_render_stats_dev(xStats-osd_render_stats_dev_get_width(), yStats, fStatsSize);
      yStats += osd_render_stats_dev_get_height();
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_dev_get_width() )
         fMaxColumnWidth = osd_render_stats_dev_get_width();
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoGraphs )
   {
      if ( yStats + osd_render_stats_video_graphs_get_height() > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }     
      osd_render_stats_video_graphs(xStats-osd_render_stats_video_graphs_get_width(), yStats);
      yStats += osd_render_stats_video_graphs_get_height();
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_video_graphs_get_width() )
         fMaxColumnWidth = osd_render_stats_video_graphs_get_width();
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoStats )
   {
      if ( yStats + osd_render_stats_video_stats_get_height() > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }     
      osd_render_stats_video_stats(xStats-osd_render_stats_video_stats_get_width(), yStats);
      yStats += osd_render_stats_video_stats_get_height();
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_video_stats_get_width() )
         fMaxColumnWidth = osd_render_stats_video_stats_get_width();
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( NULL != g_pCurrentModel && (g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_SEND_BACK_VEHICLE_TX_GAP) )
   {
      if ( yStats + osd_render_stats_graphs_vehicle_tx_gap_get_height() > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }     
      osd_render_stats_graphs_vehicle_tx_gap(xStats-osd_render_stats_graphs_vehicle_tx_gap_get_width(), yStats);
      yStats += osd_render_stats_graphs_vehicle_tx_gap_get_height();
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_graphs_vehicle_tx_gap_get_width() )
         fMaxColumnWidth = osd_render_stats_graphs_vehicle_tx_gap_get_width();
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( NULL != g_pCurrentModel && (g_pCurrentModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_VIDEO_BITRATE_HISTORY) )
   {
      if ( yStats + osd_render_stats_video_bitrate_history_get_height() > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }     
      osd_render_stats_video_bitrate_history(xStats-osd_render_stats_video_bitrate_history_get_width(), yStats);
      yStats += osd_render_stats_video_bitrate_history_get_height();
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_video_bitrate_history_get_width() )
         fMaxColumnWidth = osd_render_stats_video_bitrate_history_get_width();
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_SHOW_EFFICIENCY_STATS) )
   {
      if ( yStats + osd_render_stats_efficiency_get_height(fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }  
      osd_render_stats_efficiency(xStats-osd_render_stats_efficiency_get_width(1.0), yStats, fStatsSize);
      yStats += osd_render_stats_efficiency_get_height(fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_efficiency_get_width(fStatsSize) )
         fMaxColumnWidth = osd_render_stats_efficiency_get_width(fStatsSize);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_LINKS) )
   if ( g_bIsRouterReady )
   {
      if ( yStats + osd_render_stats_local_radio_links_get_height(&g_SM_RadioStats, fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }         
      osd_render_stats_local_radio_links( xStats-osd_render_stats_local_radio_links_get_width(&g_SM_RadioStats, fStatsSize), yStats, "Radio Links", &g_SM_RadioStats, fStatsSize);
      yStats += osd_render_stats_local_radio_links_get_height(&g_SM_RadioStats, fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_local_radio_links_get_width(&g_SM_RadioStats, fStatsSize) )
         fMaxColumnWidth = osd_render_stats_local_radio_links_get_width(&g_SM_RadioStats, fStatsSize);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_INTERFACES) )
   if ( g_bIsRouterReady )
   {
      if ( yStats + osd_render_stats_radio_interfaces_get_height(&g_SM_RadioStats) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }         
      osd_render_stats_radio_interfaces( xStats-osd_render_stats_radio_interfaces_get_width(&g_SM_RadioStats), yStats, "Radio Interfaces", &g_SM_RadioStats);
      yStats += osd_render_stats_radio_interfaces_get_height(&g_SM_RadioStats);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_radio_interfaces_get_width(&g_SM_RadioStats) )
         fMaxColumnWidth = osd_render_stats_radio_interfaces_get_width(&g_SM_RadioStats);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_TELEMETRY_STATS) )
   {
      if ( yStats + osd_render_stats_telemetry_get_height(fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }  
      osd_render_stats_telemetry(xStats-osd_render_stats_telemetry_get_width(fStatsSize), yStats, fStatsSize);
      yStats += osd_render_stats_telemetry_get_height(fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_telemetry_get_width(fStatsSize) )
         fMaxColumnWidth = osd_render_stats_telemetry_get_width(fStatsSize);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_AUDIO_DECODE_STATS) )
   {
      if ( yStats + osd_render_stats_audio_decode_get_height(fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }  
      osd_render_stats_audio_decode(xStats-osd_render_stats_audio_decode_get_width(fStatsSize), yStats, fStatsSize);
      yStats += osd_render_stats_audio_decode_get_height(fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_audio_decode_get_width(fStatsSize) )
         fMaxColumnWidth = osd_render_stats_audio_decode_get_width(fStatsSize);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RC) )
   {
      if ( yStats + osd_render_stats_rc_get_height(fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }  
      osd_render_stats_rc(xStats-osd_render_stats_rc_get_width(fStatsSize), yStats, fStatsSize);
      yStats += osd_render_stats_rc_get_height(fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_rc_get_width(fStatsSize) )
         fMaxColumnWidth = osd_render_stats_rc_get_width(fStatsSize);
   }

   if ( s_bDebugStatsShowAll || (g_pCurrentModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_VIDEO) )
   {
      if ( yStats + osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize) > yMax )
      {
         yStats = yMin;
         xStats -= fMaxColumnWidth + fSpacingH;
         fMaxColumnWidth = 0.0;
      }         
      osd_render_stats_video_decode(xStats - osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, false,  &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize), yStats, pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize);
      yStats += osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize);
      yStats += fSpacingV;
      if ( fMaxColumnWidth < osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize) )
         fMaxColumnWidth = osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, fStatsSize);
   }

   if ( p->iDebugShowFullRXStats )
      osd_render_stats_full_rx_port();
}

void _osd_stats_swap_pannels(int iIndex1, int iIndex2)
{
   int tmp = s_iOSDStatsBoundingBoxesIds[iIndex1];
   s_iOSDStatsBoundingBoxesIds[iIndex1] = s_iOSDStatsBoundingBoxesIds[iIndex2];
   s_iOSDStatsBoundingBoxesIds[iIndex2] = tmp;

   float f = s_iOSDStatsBoundingBoxesX[iIndex1];
   s_iOSDStatsBoundingBoxesX[iIndex1] = s_iOSDStatsBoundingBoxesX[iIndex2];
   s_iOSDStatsBoundingBoxesX[iIndex2] = f;

   f = s_iOSDStatsBoundingBoxesY[iIndex1];
   s_iOSDStatsBoundingBoxesY[iIndex1] = s_iOSDStatsBoundingBoxesY[iIndex2];
   s_iOSDStatsBoundingBoxesY[iIndex2] = f;

   f = s_iOSDStatsBoundingBoxesW[iIndex1];
   s_iOSDStatsBoundingBoxesW[iIndex1] = s_iOSDStatsBoundingBoxesW[iIndex2];
   s_iOSDStatsBoundingBoxesW[iIndex2] = f;

   f = s_iOSDStatsBoundingBoxesH[iIndex1];
   s_iOSDStatsBoundingBoxesH[iIndex1] = s_iOSDStatsBoundingBoxesH[iIndex2];
   s_iOSDStatsBoundingBoxesH[iIndex2] = f;
}

void _osd_stats_autoarange_left( int iCountArranged, int iCurrentColumn )
{
   if ( iCountArranged >= s_iCountOSDStatsBoundingBoxes )
      return;

   if ( 0 == iCountArranged && 0 == iCurrentColumn )
      for( int i=0; i<MAX_OSD_COLUMNS; i++ )
         s_fOSDStatsColumnsWidths[i] = g_fOSDStatsForcePanelWidth;

   float fColumnCurrentHeight = 0.0;
   float fCurrentColumnX = s_fOSDStatsMarginH;
   for( int i=0; i<iCurrentColumn; i++ )
      fCurrentColumnX += (s_fOSDStatsSpacingH + s_fOSDStatsColumnsWidths[i]);
   
   s_iOSDStatsBoundingBoxesX[iCountArranged] = fCurrentColumnX;// + s_iOSDStatsBoundingBoxesW[iCountArranged];
   s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop;
   fColumnCurrentHeight += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;  

   if ( s_iOSDStatsBoundingBoxesW[iCountArranged] > s_fOSDStatsColumnsWidths[iCurrentColumn] )
      s_fOSDStatsColumnsWidths[iCurrentColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];

   iCountArranged++;
   
   // Try to fit more panels to current column
 
   int iCountAdded = 0;
   for( int i=iCountArranged; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      if ( fColumnCurrentHeight + s_iOSDStatsBoundingBoxesH[i] <= 1.0 - s_fOSDStatsMarginVBottom - s_fOSDStatsMarginVTop )
      {
         // Move it to sorted position
         _osd_stats_swap_pannels(iCountArranged, i);
         s_iOSDStatsBoundingBoxesX[iCountArranged] = fCurrentColumnX;// + s_iOSDStatsBoundingBoxesW[iCountArranged];
         s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop + fColumnCurrentHeight;
         fColumnCurrentHeight += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
         iCountArranged++;
      }
   }

   // Still have panels to add? Move to next column
   if ( iCountArranged < s_iCountOSDStatsBoundingBoxes )
   {
      _osd_stats_autoarange_left(iCountArranged, iCurrentColumn+1);
   }
}


void _osd_stats_autoarange_right( int iCountArranged, int iCurrentColumn )
{
   if ( iCountArranged >= s_iCountOSDStatsBoundingBoxes )
      return;

   if ( 0 == iCountArranged && 0 == iCurrentColumn )
      for( int i=0; i<MAX_OSD_COLUMNS; i++ )
         s_fOSDStatsColumnsWidths[i] = g_fOSDStatsForcePanelWidth;

   float fColumnCurrentHeight = 0.0;
   float fCurrentColumnX = 1.0-s_fOSDStatsMarginH;
   for( int i=0; i<iCurrentColumn; i++ )
      fCurrentColumnX -= (s_fOSDStatsSpacingH + s_fOSDStatsColumnsWidths[i]);
   
   s_iOSDStatsBoundingBoxesX[iCountArranged] = fCurrentColumnX - s_iOSDStatsBoundingBoxesW[iCountArranged];
   s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop;
   
   if ( s_iOSDStatsBoundingBoxesW[iCountArranged] > s_fOSDStatsColumnsWidths[iCurrentColumn] )
      s_fOSDStatsColumnsWidths[iCurrentColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];

   fColumnCurrentHeight += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;  
   iCountArranged++;
   
   // Try to fit more panels to current column
 
   int iCountAdded = 0;
   for( int i=iCountArranged; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      if ( fColumnCurrentHeight + s_iOSDStatsBoundingBoxesH[i] <= 1.0 - (s_fOSDStatsMarginVBottom+s_fOSDStatsMarginVTop) )
      {
         // Move it to sorted position
         _osd_stats_swap_pannels(iCountArranged, i);
         s_iOSDStatsBoundingBoxesX[iCountArranged] = fCurrentColumnX - s_iOSDStatsBoundingBoxesW[iCountArranged];
         s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop + fColumnCurrentHeight;
         fColumnCurrentHeight += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
         iCountArranged++;
      }
   }

   // Still have panels to add? Move to next column

   if ( iCountArranged < s_iCountOSDStatsBoundingBoxes )
   {
      _osd_stats_autoarange_right(iCountArranged, iCurrentColumn+1);
   }
}


void _osd_stats_autoarange_top()
{
   for( int i=0; i<MAX_OSD_COLUMNS; i++ )
      s_fOSDStatsColumnsHeights[i] = s_fOSDStatsMarginVTop;

   // Add first the tall ones that take full height

   int iCountArranged = 0;
   int iColumn = 0;
   float fRowCurrentWidth = 0;
   for( int i=0; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      if ( s_iOSDStatsBoundingBoxesH[i] >= 1.0 - s_fOSDStatsMarginVBottom - s_fOSDStatsMarginVTop - s_fOSDStatsWindowsMinimBoxHeight - s_fOSDStatsSpacingV )
      {
         // Move it to sorted position
         _osd_stats_swap_pannels(iCountArranged, i);
         s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop;
         s_iOSDStatsBoundingBoxesColumns[iCountArranged] = iColumn;
         s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
         s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];

         fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH;   
         iCountArranged++;
         iColumn++;
      }
   }

   // Add to remaning empty columns

   if ( fRowCurrentWidth < 1.0 - 2.0*s_fOSDStatsMarginH )
   {
      for( int i=iCountArranged; i<s_iCountOSDStatsBoundingBoxes; i++ )
      {
         if ( fRowCurrentWidth + s_iOSDStatsBoundingBoxesW[iCountArranged] <= 1.0 - 2.0*s_fOSDStatsMarginH )
         {
            // Move it to sorted position
            _osd_stats_swap_pannels(iCountArranged, i);
            s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
            s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop;
            s_iOSDStatsBoundingBoxesColumns[iCountArranged] = iColumn;
            fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH;
            s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
            s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];
           
            iCountArranged++;
            iColumn++;
         }
      }
   }
   
   // Add rempaining panels to where they can fit
   
   while ( iCountArranged < s_iCountOSDStatsBoundingBoxes )
   {
      bool bFittedInColumn = false;
      for( int iColumn=0; iColumn<9; iColumn++ )
      {
         if ( s_fOSDStatsColumnsHeights[iColumn] <= s_fOSDStatsMarginVTop + 0.01 )
            continue;
         if ( s_fOSDStatsColumnsHeights[iColumn] + s_iOSDStatsBoundingBoxesH[iCountArranged]  <= 1.0 - 0.7*s_fOSDStatsMarginVBottom )
         {
            s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged];
            for( int i=0; i<iColumn; i++ )
                s_iOSDStatsBoundingBoxesX[iCountArranged] -= (s_fOSDStatsSpacingH + s_fOSDStatsColumnsWidths[i]);
            
            if ( s_iOSDStatsBoundingBoxesW[iCountArranged] > s_fOSDStatsColumnsWidths[iColumn] )
            {
               float dx = s_iOSDStatsBoundingBoxesW[iCountArranged] - s_fOSDStatsColumnsWidths[iColumn];
               s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];
               // Move all already arranged on next columns
               for( int i=0; i<iCountArranged; i++ )
                  if ( s_iOSDStatsBoundingBoxesColumns[i] >= iColumn )
                     s_iOSDStatsBoundingBoxesX[i] -= dx;
            }

            s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsColumnsHeights[iColumn];
            s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
            
            bFittedInColumn = true;
            break;
         }
      }

      if ( ! bFittedInColumn )
      {
         //s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         //s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsMarginVTop;
         //fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH; 

         s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         s_iOSDStatsBoundingBoxesY[iCountArranged] = s_fOSDStatsColumnsHeights[9];
         s_fOSDStatsColumnsHeights[9] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
      }
      iCountArranged++;
   }
}

void _osd_stats_autoarange_bottom()
{
   for( int i=0; i<MAX_OSD_COLUMNS; i++ )
      s_fOSDStatsColumnsHeights[i] = s_fOSDStatsMarginVTop;

   // Add first the tall ones that take full height

   int iCountArranged = 0;
   int iColumn = 0;
   float fRowCurrentWidth = 0;
   for( int i=0; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      if ( s_iOSDStatsBoundingBoxesH[i] >= 1.0 - s_fOSDStatsMarginVBottom - s_fOSDStatsMarginVTop - s_fOSDStatsWindowsMinimBoxHeight - s_fOSDStatsSpacingV )
      {
         // Move it to sorted position
         _osd_stats_swap_pannels(iCountArranged, i);
         s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         s_iOSDStatsBoundingBoxesY[iCountArranged] = 1.0 - s_fOSDStatsMarginVTop - s_iOSDStatsBoundingBoxesH[iCountArranged];
         s_iOSDStatsBoundingBoxesColumns[iCountArranged] = iColumn;
         s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
         s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];
         
         fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH;
         iCountArranged++;
         iColumn++;
      }
   }

   // Add to remaning empty columns

   if ( fRowCurrentWidth < 1.0 - 2.0*s_fOSDStatsMarginH )
   {
      for( int i=iCountArranged; i<s_iCountOSDStatsBoundingBoxes; i++ )
      {
         if ( fRowCurrentWidth + s_iOSDStatsBoundingBoxesW[iCountArranged] <= 1.0 - 2.0*s_fOSDStatsMarginH )
         {
            // Move it to sorted position
            _osd_stats_swap_pannels(iCountArranged, i);
            s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
            s_iOSDStatsBoundingBoxesY[iCountArranged] = 1.0 - s_fOSDStatsMarginVTop - s_iOSDStatsBoundingBoxesH[iCountArranged];
            s_iOSDStatsBoundingBoxesColumns[iCountArranged] = iColumn;
            fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH;
            s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
            s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];
            iCountArranged++;
            iColumn++;
         }
      }
   }
   
   // Add rempaining panels to where they can fit
   
   while ( iCountArranged < s_iCountOSDStatsBoundingBoxes )
   {
      bool bFittedInColumn = false;
      for( int iColumn=0; iColumn<9; iColumn++ )
      {
         if ( s_fOSDStatsColumnsHeights[iColumn] <= s_fOSDStatsMarginVTop + 0.01 )
            continue;
         if ( s_fOSDStatsColumnsHeights[iColumn] + s_iOSDStatsBoundingBoxesH[iCountArranged] <= 1.0 - 0.7*s_fOSDStatsMarginVBottom )
         {
            s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged];
            for( int i=0; i<iColumn; i++ )
                s_iOSDStatsBoundingBoxesX[iCountArranged] -= (s_fOSDStatsSpacingH + s_fOSDStatsColumnsWidths[i]);
            
            if ( s_iOSDStatsBoundingBoxesW[iCountArranged] > s_fOSDStatsColumnsWidths[iColumn] )
            {
               float dx = s_iOSDStatsBoundingBoxesW[iCountArranged] - s_fOSDStatsColumnsWidths[iColumn];
               s_fOSDStatsColumnsWidths[iColumn] = s_iOSDStatsBoundingBoxesW[iCountArranged];
               // Move all already arranged on next columns
               for( int i=0; i<iCountArranged; i++ )
                  if ( s_iOSDStatsBoundingBoxesColumns[i] >= iColumn )
                     s_iOSDStatsBoundingBoxesX[i] -= dx;
            }

            s_iOSDStatsBoundingBoxesY[iCountArranged] = 1.0 - s_fOSDStatsColumnsHeights[iColumn] - s_iOSDStatsBoundingBoxesH[iCountArranged];
            s_fOSDStatsColumnsHeights[iColumn] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
            bFittedInColumn = true;
            break;
         }
      }

      if ( ! bFittedInColumn )
      {
         //s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         //s_iOSDStatsBoundingBoxesY[iCountArranged] = 1.0 - s_fOSDStatsMarginVTop - s_iOSDStatsBoundingBoxesH[iCountArranged];
         //fRowCurrentWidth += s_iOSDStatsBoundingBoxesW[iCountArranged] + s_fOSDStatsSpacingH; 
         s_iOSDStatsBoundingBoxesX[iCountArranged] = 1.0 - s_fOSDStatsMarginH - s_iOSDStatsBoundingBoxesW[iCountArranged] - fRowCurrentWidth;
         s_iOSDStatsBoundingBoxesY[iCountArranged] = 1.0 - s_fOSDStatsColumnsHeights[9] - s_iOSDStatsBoundingBoxesH[iCountArranged];
         s_fOSDStatsColumnsHeights[9] += s_iOSDStatsBoundingBoxesH[iCountArranged] + s_fOSDStatsSpacingV;
      }
      iCountArranged++;
   }
}

void osd_render_stats_panels()
{
   if ( NULL == g_pCurrentModel )
      return;
   Model* pModel = osd_get_current_data_source_vehicle_model();
   if ( NULL == pModel )
      return;

   ControllerSettings* pCS = get_ControllerSettings();
   Preferences* p = get_Preferences();

   s_idFontStats = g_idFontStats;
   s_idFontStatsSmall = g_idFontStatsSmall;

   s_OSDStatsLineSpacing = 1.0;

   //if ( osd_getScaleOSDStats() >= 1.0 )
   //   g_fOSDStatsForcePanelWidth = 0.2*osd_getScaleOSDStats();
   //else
   //   g_fOSDStatsForcePanelWidth = 0.2*osd_getScaleOSDStats();

   g_fOSDStatsForcePanelWidth = g_pRenderEngine->textWidth(s_idFontStats, "AAAAA AAAAAAAA AAAAAA AAAAAA");
   g_fOSDStatsForcePanelWidth += 2.0*s_fOSDStatsMargin/g_pRenderEngine->getAspectRatio();

   for( int i=0; i<MAX_OSD_COLUMNS; i++ )
      s_fOSDStatsColumnsWidths[i] = g_fOSDStatsForcePanelWidth;

   s_iCountOSDStatsBoundingBoxes = 0;

   g_fOSDStatsBgTransparency = 1.0;
   switch ( ((pModel->osd_params.osd_preferences[pModel->osd_params.layout])>>20) & 0x0F )
   {
      case 0: g_fOSDStatsBgTransparency = 0.25; break;
      case 1: g_fOSDStatsBgTransparency = 0.5; break;
      case 2: g_fOSDStatsBgTransparency = 0.75; break;
      case 3: g_fOSDStatsBgTransparency = 0.98; break;
   }

   s_fOSDStatsSpacingH = 0.014/g_pRenderEngine->getAspectRatio();
   s_fOSDStatsSpacingV = 0.014;

   s_fOSDStatsMarginH = osd_getMarginX() + 0.01/g_pRenderEngine->getAspectRatio();
   s_fOSDStatsMarginVBottom = osd_getMarginY() + osd_getBarHeight() + osd_getSecondBarHeight() + 0.01;
   s_fOSDStatsMarginVTop = s_fOSDStatsMarginVBottom;


   if ( (pModel->osd_params.osd_flags[pModel->osd_params.layout] & OSD_FLAG_SHOW_CPU_INFO ) ||
        p->iShowControllerCPUInfo )
      s_fOSDStatsMarginVTop += osd_getFontHeight();
   
   if ( pModel->osd_params.osd_flags2[pModel->osd_params.layout] & OSD_FLAG2_LAYOUT_LEFT_RIGHT )
   {
      s_fOSDStatsMarginH = osd_getVerticalBarWidth() + osd_getMarginX() + 0.02;
      s_fOSDStatsMarginVBottom = osd_getMarginY();
      s_fOSDStatsMarginVTop = s_fOSDStatsMarginVBottom;
   }

   if ( s_fOSDStatsMarginH < 0.01 )
      s_fOSDStatsMarginH = 0.01;
   if ( s_fOSDStatsMarginVBottom < 0.01 )
      s_fOSDStatsMarginVBottom = 0.01;
   if ( s_fOSDStatsMarginVTop < 0.01 )
      s_fOSDStatsMarginVTop = 0.01;
   
   // Max id used: 19

   if ( pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_RADIO_RX_HISTORY_CONTROLLER )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 17;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_rx_history_get_width(false);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_rx_history_get_height(false);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_RADIO_RX_HISTORY_VEHICLE )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 18;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_rx_history_get_width(true);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_rx_history_get_height(true);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_CONTROLLER_ADAPTIVE_VIDEO_INFO )
   if ( NULL != g_pSM_RouterVehiclesRuntimeInfo )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 14;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_adaptive_video_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_adaptive_video_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_LINKS) )
   if ( g_bIsRouterReady )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 7;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_local_radio_links_get_width(&g_SM_RadioStats, 1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_local_radio_links_get_height(&g_SM_RadioStats, 1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RADIO_INTERFACES) )
   if ( g_bIsRouterReady )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 8;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_interfaces_get_width(&g_SM_RadioStats);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_radio_interfaces_get_height(&g_SM_RadioStats);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_VIDEO) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 6;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, 1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, 1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( p->iDebugShowVideoSnapshotOnDiscard )
   if ( g_bHasVideoDecodeStatsSnapshot )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 11;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_decode_get_width(pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, 1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_decode_get_height(pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, 1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_SHOW_EFFICIENCY_STATS) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 9;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_efficiency_get_width(1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_efficiency_get_height(1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags[osd_get_current_layout_index()] & OSD_FLAG_SHOW_STATS_VIDEO_INFO) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 12;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_stream_keyframe_info_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_stream_keyframe_info_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_TELEMETRY_STATS) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 15;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_telemetry_get_width(1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_telemetry_get_height(1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_AUDIO_DECODE_STATS) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 16;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_audio_decode_get_width(1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_audio_decode_get_height(1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( s_bDebugStatsShowAll || (pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_SHOW_STATS_RC) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 10;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_rc_get_width(1.0);
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_rc_get_height(1.0);
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowDevVideoStats || p->iDebugShowDevRadioStats )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 1;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_dev_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_dev_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( NULL != pModel && (pModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_SEND_BACK_VEHICLE_TX_GAP) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 4;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_graphs_vehicle_tx_gap_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_graphs_vehicle_tx_gap_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( NULL != pModel && (pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_VIDEO_BITRATE_HISTORY) )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 5;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_bitrate_history_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_bitrate_history_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoStats )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 3;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_stats_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_stats_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   if ( pCS->iDeveloperMode || s_bDebugStatsShowAll )
   if ( p->iDebugShowVehicleVideoGraphs )
   {
      s_iOSDStatsBoundingBoxesIds[s_iCountOSDStatsBoundingBoxes] = 2;
      s_iOSDStatsBoundingBoxesW[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_graphs_get_width();
      s_iOSDStatsBoundingBoxesH[s_iCountOSDStatsBoundingBoxes] = osd_render_stats_video_graphs_get_height();
      s_iCountOSDStatsBoundingBoxes++;
   }

   s_fOSDStatsWindowsMinimBoxHeight = 2.0;
   for( int i=0; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      s_iOSDStatsBoundingBoxesColumns[i] = -1;

      if ( s_iOSDStatsBoundingBoxesH[i] < s_fOSDStatsWindowsMinimBoxHeight  )
         s_fOSDStatsWindowsMinimBoxHeight = s_iOSDStatsBoundingBoxesH[i];
   }


   // Auto arange

   if ( pModel->osd_params.osd_preferences[osd_get_current_layout_index()] & OSD_PREFERENCES_BIT_FLAG_ARANGE_STATS_WINDOWS_LEFT )
      _osd_stats_autoarange_left(0, 0);
   else if ( pModel->osd_params.osd_preferences[osd_get_current_layout_index()] & OSD_PREFERENCES_BIT_FLAG_ARANGE_STATS_WINDOWS_RIGHT )
      _osd_stats_autoarange_right(0, 0);
   else if ( pModel->osd_params.osd_preferences[osd_get_current_layout_index()] & OSD_PREFERENCES_BIT_FLAG_ARANGE_STATS_WINDOWS_BOTTOM )
      _osd_stats_autoarange_bottom();
   else
      _osd_stats_autoarange_top();

   // Draw

   for( int i=0; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      if ( s_iOSDStatsBoundingBoxesIds[i] == 14 )
         osd_render_stats_adaptive_video(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);
      
      if ( s_iOSDStatsBoundingBoxesIds[i] == 1 )
         osd_render_stats_dev(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], 1.0);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 2 )
         osd_render_stats_video_graphs(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 3 )
         osd_render_stats_video_stats(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 4 )
         osd_render_stats_graphs_vehicle_tx_gap(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 5 )
         osd_render_stats_video_bitrate_history(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);
      
      if ( s_iOSDStatsBoundingBoxesIds[i] == 6 )
         osd_render_stats_video_decode(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], pCS->iDeveloperMode, false, &g_SM_RadioStats, &g_SM_VideoDecodeStats, &g_SM_VDS_history, &g_SM_ControllerRetransmissionsStats, 1.0);
      
      if ( s_iOSDStatsBoundingBoxesIds[i] == 11 )
         osd_render_stats_video_decode(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], pCS->iDeveloperMode, true, &s_OSDSnapshot_RadioStats, &s_OSDSnapshot_VideoDecodeStats, &s_OSDSnapshot_VideoDecodeHist, &s_OSDSnapshot_ControllerVideoRetransmissionsStats, 1.0);
     
      if ( s_iOSDStatsBoundingBoxesIds[i] == 12 )
        osd_render_stats_video_stream_keyframe_info(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i]);
      
      if ( s_iOSDStatsBoundingBoxesIds[i] == 7 )
         osd_render_stats_local_radio_links( s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], "Radio Links", &g_SM_RadioStats, 1.0);
      
      if ( s_iOSDStatsBoundingBoxesIds[i] == 8 )
         osd_render_stats_radio_interfaces( s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], "Radio Interfaces", &g_SM_RadioStats);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 9 )
         osd_render_stats_efficiency(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], 1.0);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 15 )
         osd_render_stats_telemetry(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], 1.0);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 16 )
         osd_render_stats_audio_decode(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], 1.0);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 10 )
         osd_render_stats_rc(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], 1.0);

      if ( s_iOSDStatsBoundingBoxesIds[i] == 17 )
         osd_render_stats_radio_rx_history(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], false);
      if ( s_iOSDStatsBoundingBoxesIds[i] == 18 )
         osd_render_stats_radio_rx_history(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], true);
      
      //char szBuff[32];
      //sprintf(szBuff, "%d", i);
      //g_pRenderEngine->drawText(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], s_idFontStats, szBuff);
   }


   // Draw the ones that should be on top

   for( int i=0; i<s_iCountOSDStatsBoundingBoxes; i++ )
   {
      //if ( s_iOSDStatsBoundingBoxesIds[i] == 19 )
      //   osd_render_stats_radio_interfaces_graph(s_iOSDStatsBoundingBoxesX[i], s_iOSDStatsBoundingBoxesY[i], &g_SM_RadioStatsInterfaceRxGraph);
   }

   if ( pModel->osd_params.osd_flags3[osd_get_current_layout_index()] & OSD_FLAG3_SHOW_RADIO_RX_GRAPH_CONTROLLER )
   {
      osd_render_stats_radio_interfaces_graph(0.0, 0.0, &g_SM_RadioStatsInterfaceRxGraph);
   }

   if ( p->iDebugShowFullRXStats )
      osd_render_stats_full_rx_port();
}
