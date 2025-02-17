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
#include "../base/hardware.h"
#include "../base/models.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../radio/fec.h"
#include "../base/camera_utils.h"
#include "../common/string_utils.h"
#include "shared_vars.h"
#include "timers.h"

#include "processor_tx_video.h"
#include "processor_relay.h"
#include "packets_utils.h"
#include "utils_vehicle.h"
#include <semaphore.h>

#define PACKET_FLAG_EMPTY 0
#define PACKET_FLAG_READ 1
#define PACKET_FLAG_SENT 2

typedef struct
{
   u8 flags;
   int packetLength;
   int currentReadPosition;
   u8* pRawData;
}
type_tx_packet_info;

typedef struct
{
   u32 video_block_index;
   u8 block_packets;
   u8 block_fecs;
   int video_data_length;
   type_tx_packet_info packetsInfo[MAX_TOTAL_PACKETS_IN_BLOCK];
}
type_tx_block_info;

type_tx_block_info s_BlocksTxBuffers[MAX_RXTX_BLOCKS_BUFFER];

u8* p_fec_data_packets[MAX_DATA_PACKETS_IN_BLOCK];
u8* p_fec_data_fecs[MAX_FECS_PACKETS_IN_BLOCK];

t_packet_header s_CurrentPH;
t_packet_header_video_full_77 s_CurrentPHVF;

bool s_bPauseVideoPacketsTX = false;
bool s_bPendingEncodingSwitch = false;

u32 sTimeLastFecTimeCalculation = 0;
u32 sTimeTotalFecTimeMicroSec = 0;

u32 s_uParseNALStartSequence = MAX_U32;
u32 s_uParseNALCurrentSlices = 0;
u32 s_uParseNALStartSequenceRadioOut = MAX_U32;
u32 s_uParseNALCurrentSlicesRadioOut = 0;
u32 s_uLastVideoFrameTime = 0;
u32 s_uLastVideoFrameTimeRadioOut = 0;

u32 s_lCountBytesSend = 0;
u32 s_lCountBytesVideoIn = 0; 
int s_CurrentMaxBlocksInBuffers = MAX_RXTX_BLOCKS_BUFFER;

int s_currentReadBufferIndex = 0;
int s_currentReadBlockPacketIndex = 0;

int s_LastSentBlockBufferIndex = -1;

u32 s_TimeLastTelemetryInfoUpdate = 0;
u32 s_TimeLastSemaphoreCheck = 0;
u32 s_TimeLastEncodingSchemeLog = 0;
u32 s_LastEncodingChangeAtVideoBlockIndex = 0;
u32 s_uCountEncodingChanges = 0;

int s_iRetransmissionsDuplicationIndex = 0;
u32 s_uLastReceivedRetransmissionRequestUniqueId = 0;
u32 s_uInjectFaultsCountPacketsDeclined = 0;

#define MAX_HISTORY_RETRANSMISSION_INFO 200

typedef struct
{
   u32 video_block_index;
   u8 video_packet_index;
   u32 uReceiveTime;
   u8 uRepeatCount;
}
type_retransmissions_history_info;

type_retransmissions_history_info s_listRetransmissionsSegmentsInfo[MAX_HISTORY_RETRANSMISSION_INFO];
int s_iCountRetransmissionsSegmentsInfo = 0;
u32 s_listLastRetransmissionsRequestsTimes[MAX_HISTORY_RETRANSMISSION_INFO];
int s_iCountLastRetransmissionsRequestsTimes = 0;

int s_iCurrentKeyFrameIntervalMs = -1;
int s_iPendingSetKeyframeIntervalMs = -1;

int ProcessorTxVideo::m_siInstancesCount = 0;

ProcessorTxVideo::ProcessorTxVideo(int iVideoStreamIndex, int iCameraIndex)
:m_bInitialized(false)
{
   m_iInstanceIndex = m_siInstancesCount;
   m_siInstancesCount++;

   m_iVideoStreamIndex = iVideoStreamIndex;
   m_iCameraIndex = iCameraIndex;

   m_iLastRequestedKeyframeIntervalFromController = 0;
   m_iLastRequestedAdaptiveVideoLevelFromController = 0;
   m_uLastSetCaptureVideoBitrate = 0;

   for( int i=0; i<MAX_VIDEO_BITRATE_HISTORY_VALUES; i++ )
   {
      m_BitrateHistorySamples[i].uTimeStampTaken = 0;
      m_BitrateHistorySamples[i].uTotalBitrateBPS = 0;
      m_BitrateHistorySamples[i].uVideoBitrateBPS = 0;
   }
   m_uVideoBitrateAverageSum = 0;
   m_uTotalVideoBitrateAverageSum = 0;
   m_uVideoBitrateAverage = 0;
   m_uTotalVideoBitrateAverage = 0;
   m_uIntervalMSComputeVideoBitrateSample = 50; // Must compute video bitrate averages quite often as they are used for auto adustments of video bitrate and quantization
   m_uTimeLastVideoBitrateSampleTaken = 0;
   m_iVideoBitrateSampleIndex = 0;
}

ProcessorTxVideo::~ProcessorTxVideo()
{
   m_siInstancesCount--;
}

bool ProcessorTxVideo::init()
{
   if ( m_bInitialized )
      return true;

   log_line("[VideoTX] Initialize video processor Tx instance number %d.", m_iInstanceIndex+1);

   m_bInitialized = true;

   return true;
}

bool ProcessorTxVideo::uninit()
{
   if ( ! m_bInitialized )
      return true;

   log_line("[VideoTX] Uninitialize video processor Tx instance number %d.", m_iInstanceIndex+1);
   
   m_bInitialized = false;
   return true;
}

// Returns bps
u32 ProcessorTxVideo::getCurrentVideoBitrate()
{
   return m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uVideoBitrateBPS;
}

// Returns bps
u32 ProcessorTxVideo::getCurrentVideoBitrateAverage()
{
   return m_uVideoBitrateAverage;
}

// Returns bps
u32 ProcessorTxVideo::getCurrentVideoBitrateAverageLastMs(u32 uMilisec)
{
   u32 uSum = 0;
   u32 uCount = 0;
   int iIndex = m_iVideoBitrateSampleIndex;
   
   u32 uTime0 = m_BitrateHistorySamples[iIndex].uTimeStampTaken;
   
   while ( (uCount < MAX_VIDEO_BITRATE_HISTORY_VALUES) && (m_BitrateHistorySamples[iIndex].uTimeStampTaken >= (uTime0 - uMilisec)) )
   {
      uSum += m_BitrateHistorySamples[iIndex].uVideoBitrateBPS;
      uCount++;
      iIndex--;
      if ( iIndex < 0 )
        iIndex = MAX_VIDEO_BITRATE_HISTORY_VALUES-1;
   }

   return uSum/uCount;
}


// Returns bps
u32 ProcessorTxVideo::getCurrentTotalVideoBitrate()
{
   return m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uTotalBitrateBPS;
}

// Returns bps
u32 ProcessorTxVideo::getCurrentTotalVideoBitrateAverage()
{
   return m_uTotalVideoBitrateAverage;
}

// Returns bps
u32 ProcessorTxVideo::getCurrentTotalVideoBitrateAverageLastMs(u32 uMilisec)
{
   u32 uSum = 0;
   u32 uCount = 0;
   int iIndex = m_iVideoBitrateSampleIndex;
   
   u32 uTime0 = m_BitrateHistorySamples[iIndex].uTimeStampTaken;
   
   while ( (uCount < MAX_VIDEO_BITRATE_HISTORY_VALUES) && (m_BitrateHistorySamples[iIndex].uTimeStampTaken >= (uTime0 - uMilisec)) )
   {
      uSum += m_BitrateHistorySamples[iIndex].uTotalBitrateBPS;
      uCount++;
      iIndex--;
      if ( iIndex < 0 )
        iIndex = MAX_VIDEO_BITRATE_HISTORY_VALUES-1;
   }

   return uSum/uCount;
}

void ProcessorTxVideo::setLastRequestedKeyframeFromController(int iKeyframeIntervalMs)
{
   m_iLastRequestedKeyframeIntervalFromController = iKeyframeIntervalMs;
}

void ProcessorTxVideo::setLastRequestedAdaptiveVideoLevelFromController(int iLevel)
{
   m_iLastRequestedAdaptiveVideoLevelFromController = iLevel;
}

void ProcessorTxVideo::setLastSetCaptureVideoBitrate(u32 uBitrate, bool bInitialValue)
{
   m_uLastSetCaptureVideoBitrate = uBitrate;
   if ( bInitialValue )
      m_uLastSetCaptureVideoBitrate |= (((u32)1) << 31);
   else
      m_uLastSetCaptureVideoBitrate &= ~(((u32)1) << 31);
}

int ProcessorTxVideo::getLastRequestedKeyframeFromController()
{
   return m_iLastRequestedKeyframeIntervalFromController;
}

int ProcessorTxVideo::getLastRequestedAdaptiveVideoLevelFromController()
{
   return m_iLastRequestedAdaptiveVideoLevelFromController;
}

u32 ProcessorTxVideo::getLastSetCaptureVideoBitrate()
{
   return m_uLastSetCaptureVideoBitrate;
}


void ProcessorTxVideo::periodicLoop()
{
   if ( g_TimeNow < (m_uTimeLastVideoBitrateSampleTaken + m_uIntervalMSComputeVideoBitrateSample) )
      return;
      
   u32 uDeltaTime = g_TimeNow - m_uTimeLastVideoBitrateSampleTaken;
   m_uTimeLastVideoBitrateSampleTaken = g_TimeNow;

   m_iVideoBitrateSampleIndex++;
   if ( m_iVideoBitrateSampleIndex >= MAX_VIDEO_BITRATE_HISTORY_VALUES )
      m_iVideoBitrateSampleIndex = 0;

   m_uVideoBitrateAverageSum -= m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uVideoBitrateBPS;
   m_uTotalVideoBitrateAverageSum -= m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uTotalBitrateBPS;
  
   m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uVideoBitrateBPS = ((s_lCountBytesVideoIn * 8) / uDeltaTime) * 1000;
   m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uTotalBitrateBPS = ((s_lCountBytesSend * 8) / uDeltaTime) * 1000;
   m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uTimeStampTaken = g_TimeNow;

   m_uVideoBitrateAverageSum += m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uVideoBitrateBPS;
   m_uTotalVideoBitrateAverageSum += m_BitrateHistorySamples[m_iVideoBitrateSampleIndex].uTotalBitrateBPS;
  
   m_uVideoBitrateAverage = m_uVideoBitrateAverageSum/MAX_VIDEO_BITRATE_HISTORY_VALUES;
   m_uTotalVideoBitrateAverage = m_uTotalVideoBitrateAverageSum/MAX_VIDEO_BITRATE_HISTORY_VALUES;
   
   s_lCountBytesSend = 0;
   s_lCountBytesVideoIn = 0;
}

void process_data_tx_video_set_current_keyframe_interval(int iKeyframeMs, const char* szReason)
{
   if ( (iKeyframeMs == -1) ||
        ((s_iPendingSetKeyframeIntervalMs == iKeyframeMs) && (g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs == iKeyframeMs) ) )
      return;
   s_iPendingSetKeyframeIntervalMs = iKeyframeMs;
   
   if ( s_iPendingSetKeyframeIntervalMs < DEFAULT_VIDEO_MIN_AUTO_KEYFRAME_INTERVAL )
      s_iPendingSetKeyframeIntervalMs = DEFAULT_VIDEO_MIN_AUTO_KEYFRAME_INTERVAL;
   if ( s_iPendingSetKeyframeIntervalMs > 10000 )
      s_iPendingSetKeyframeIntervalMs = 10000;

   if ( (u32)s_iPendingSetKeyframeIntervalMs > 20000 )
      s_iPendingSetKeyframeIntervalMs = 20000;

   if ( NULL != szReason )
      log_line("[VideoTX] Set pending keyframe interval to %d ms due to: %s (current used video keyframe: %d ms)", s_iPendingSetKeyframeIntervalMs, szReason, g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs);
   else
      log_line("[VideoTX] Set pending keyframe interval to %d ms due to N/A reason(current used video keyframe: %d ms)", s_iPendingSetKeyframeIntervalMs, g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs);
   g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs = s_iPendingSetKeyframeIntervalMs;
}

void _log_encoding_scheme()
{
   if ( g_TimeNow > s_TimeLastEncodingSchemeLog + 5000 )
   {
   s_TimeLastEncodingSchemeLog = g_TimeNow;
   char szScheme[64];
   char szVideoStream[64];
   strcpy(szScheme, "[Unknown]");
   strcpy(szVideoStream, "[Unknown]");

   // lower 4 bits: current video profile
   // higher 4 bits: user selected video profile

   strcpy(szScheme, str_get_video_profile_name(s_CurrentPHVF.video_link_profile & 0x0F));
   
   sprintf(szVideoStream, "[%d]", (s_CurrentPHVF.video_link_profile>>4) & 0x0F );
   u32 uValueDup = (s_CurrentPHVF.encoding_extra_flags & ENCODING_EXTRA_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT) >> 16;

   log_line("New current video profile used: %s (video stream Id: %s), extra params: 0x%08X, (for video res %d x %d, %d FPS, %d ms keyframe):",
      szScheme, szVideoStream, s_CurrentPHVF.encoding_extra_flags, s_CurrentPHVF.video_width, s_CurrentPHVF.video_height, s_CurrentPHVF.video_fps, s_CurrentPHVF.video_keyframe_interval_ms );
   log_line("New encoding scheme used: %d/%d block data/ECs, packet length: %d, R-data/R-retr: %d/%d",
      s_CurrentPHVF.block_packets, s_CurrentPHVF.block_fecs, s_CurrentPHVF.video_packet_length,
      (uValueDup & 0x0F), ((uValueDup >> 4) & 0x0F) );
   log_line("Encoding change (%u times) active starting with stream packet: %u, video block index: %u, video packet index: %u", s_uCountEncodingChanges,
      (s_CurrentPH.stream_packet_idx & PACKET_FLAGS_MASK_STREAM_PACKET_IDX), s_CurrentPHVF.video_block_index, s_CurrentPHVF.video_block_packet_index);
   return;
   }

   return;
   log_line("New encoding scheme: [%u/%u], %d/%d, %d/%d/%d", s_CurrentPHVF.video_block_index, s_CurrentPHVF.video_block_packet_index,
      (s_CurrentPHVF.video_link_profile>>4) & 0x0F, s_CurrentPHVF.video_link_profile & 0x0F,
         s_CurrentPHVF.block_packets, s_CurrentPHVF.block_fecs, s_CurrentPHVF.video_packet_length);
}

// Returns true if it changed

bool _check_update_video_link_profile_data()
{
   bool bChanged = false;

   if ( s_CurrentPHVF.video_link_profile != g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile )
   {
      s_CurrentPHVF.video_link_profile = g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile;
      bChanged = true;
   }
   if ( (s_CurrentPHVF.encoding_extra_flags & (~ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )) != (g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].encoding_extra_flags & (~ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE )) )
   {
      s_CurrentPHVF.encoding_extra_flags = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].encoding_extra_flags;
      bChanged = true;
   }
   if ( s_CurrentPHVF.video_width != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].width )
   {
      s_CurrentPHVF.video_width = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].width;
      bChanged = true;
   }
   if ( s_CurrentPHVF.video_height != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].height )
   {
      s_CurrentPHVF.video_height = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].height;
      bChanged = true;
   }
   if ( s_CurrentPHVF.video_fps != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].fps )
   {
      s_CurrentPHVF.video_fps = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].fps;
      bChanged = true;
   }
   
   if ( 0 != g_SM_VideoLinkStats.overwrites.currentDataBlocks )
   {
      if ( s_CurrentPHVF.block_packets != g_SM_VideoLinkStats.overwrites.currentDataBlocks )
      {
         s_CurrentPHVF.block_packets = g_SM_VideoLinkStats.overwrites.currentDataBlocks;
         bChanged = true;
      }
      if ( s_CurrentPHVF.block_fecs != g_SM_VideoLinkStats.overwrites.currentECBlocks )
      {
         s_CurrentPHVF.block_fecs = g_SM_VideoLinkStats.overwrites.currentECBlocks;
         bChanged = true;
      }
   }
   else
   {
      if ( s_CurrentPHVF.block_packets != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].block_packets )
      {
         s_CurrentPHVF.block_packets = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].block_packets;
         bChanged = true;
      }
      if ( s_CurrentPHVF.block_fecs != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].block_fecs )
      {
         s_CurrentPHVF.block_fecs = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].block_fecs;
         bChanged = true;
      }
   }

   if ( s_CurrentPHVF.video_packet_length != g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].packet_length )
   {
      s_CurrentPHVF.video_packet_length = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].packet_length;
      bChanged = true;
   }

   if ( bChanged )
   {
      s_uCountEncodingChanges++;
      s_LastEncodingChangeAtVideoBlockIndex = s_CurrentPHVF.video_block_index;
   }
   return bChanged;
}

void _reset_tx_buffers()
{
   if ( s_CurrentPHVF.video_packet_length < 100 )
   {
      s_CurrentPHVF.video_packet_length = 100;
      if ( NULL != g_pCurrentModel )
         s_CurrentPHVF.video_packet_length = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].packet_length;
   }
   if ( 0 == s_CurrentPHVF.block_packets )
      s_CurrentPHVF.block_packets = DEFAULT_VIDEO_BLOCK_PACKETS_HQ;

   for( int i=0; i<MAX_RXTX_BLOCKS_BUFFER; i++ )
   {
      for( int k=0; k<MAX_TOTAL_PACKETS_IN_BLOCK; k++ )
      {
         s_BlocksTxBuffers[i].packetsInfo[k].flags = PACKET_FLAG_EMPTY;
         s_BlocksTxBuffers[i].packetsInfo[k].packetLength = 0;
         s_BlocksTxBuffers[i].packetsInfo[k].currentReadPosition = 0;
      }
      s_BlocksTxBuffers[i].video_block_index = MAX_U32;

      s_BlocksTxBuffers[i].video_data_length = s_CurrentPHVF.video_packet_length;
      s_BlocksTxBuffers[i].block_packets = s_CurrentPHVF.block_packets;
      s_BlocksTxBuffers[i].block_fecs = s_CurrentPHVF.block_fecs;
   }
}

// returns true if packet should not be sent

bool _inject_recoverable_faults(int bufferIndex, u32 streamPacketIndex, int packetIndex, bool isRetransmitted)
{
   static int s_iInjectFaultCount = 0;

   if ( isRetransmitted )
      return false;

   if ( (rand()%200) < 1 )
      s_iInjectFaultCount++;

   if ( (rand()%3000) < 1 )
      s_iInjectFaultCount+=3;

   if ( s_iInjectFaultCount > 0 )
   {
      s_iInjectFaultCount--;
      return true;
   }
   
   return false;
}

// returns true if packet should not be sent

bool _inject_faults(int bufferIndex, u32 streamPacketIndex, int packetIndex, bool isRetransmitted)
{
   static u32 s_uIFLastTimeInject = 0;
   static u32 s_uIFLastTimeInjectDuration = 0;
   static bool s_bIFLastTimeInjectAllowRetransmissions = false;
   //static u32 s_uIFLastPacketToInject = 0;
   //static u32 s_uIFLastPacketToInjectDelta = 0;

   if ( ! isRetransmitted )
   if ( g_TimeNow > s_uIFLastTimeInject + s_uIFLastTimeInjectDuration )
   {
      log_line("Stopped injecting fault at video packet [%u/%d]", s_BlocksTxBuffers[bufferIndex].video_block_index, packetIndex);
      int iShortInject = ((rand()%5) == 2);
      if ( iShortInject )
      {
         s_uIFLastTimeInject = g_TimeNow + 1000+(rand()%3000);
         s_uIFLastTimeInjectDuration = 10 + (rand()%30);
         s_bIFLastTimeInjectAllowRetransmissions = (rand()%2)?true:false;
      }
      else
      {
         s_uIFLastTimeInject = g_TimeNow + 4000+(rand()%3000);
         s_uIFLastTimeInjectDuration = 20 + (rand()%200);
         //s_bIFLastTimeInjectAllowRetransmissions = (rand()%3)?false:true;
         s_bIFLastTimeInjectAllowRetransmissions = false;
      }
      s_uInjectFaultsCountPacketsDeclined = 0;
      log_line("Generated new %s inject faults segments, starting %d ms from now, %d ms long, allow retransmissions: %s;",
         (iShortInject?"short":"long"), s_uIFLastTimeInject - g_TimeNow, s_uIFLastTimeInjectDuration, s_bIFLastTimeInjectAllowRetransmissions?"yes":"no");
   }

   if ( g_TimeNow >= s_uIFLastTimeInject && g_TimeNow <= s_uIFLastTimeInject + s_uIFLastTimeInjectDuration )
   {
      if ( ! isRetransmitted )
      {
         if ( 0 == s_uInjectFaultsCountPacketsDeclined )
            log_line("Started inject faults period for %d ms, starting at video packet [%u/%d], allow retransmissions: %s;",
               s_uIFLastTimeInjectDuration, s_BlocksTxBuffers[bufferIndex].video_block_index, packetIndex, s_bIFLastTimeInjectAllowRetransmissions?"yes":"no");
         
         s_uInjectFaultsCountPacketsDeclined++;
         return true;
      }
      if ( s_bIFLastTimeInjectAllowRetransmissions )
         return false;
      return true;
   }

   /*
   if ( 0 )
   if ( (0 != s_uIFLastTimeInject) && (streamPacketIndex > s_uIFLastTimeInject + s_uIFLastPacketToInjectDelta) )
   {
      s_uIFLastPacketToInject = streamPacketIndex + 20 + (rand()%50);
      s_uIFLastPacketToInjectDelta = 1 + (rand()%10);
   }

   if ( 0 )
   if ( (0 != s_uIFLastPacketToInject) && (streamPacketIndex >= s_uIFLastPacketToInject && streamPacketIndex < s_uIFLastPacketToInject + s_uIFLastPacketToInjectDelta) )
      return true;
   return false;
   */


   return false;

   //if ( (rand()%500) < 10 )
   //    return true;


   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 500 ) == 200 )
   //   return true;
   //return false;

   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) > 200 )
   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) < 250 )
   //if ( packetIndex == 0 || packetIndex == 1 || packetIndex == 2 || packetIndex != s_BlocksTxBuffers[bufferIndex].block_packets+1 )
   //   return true;

   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 500 ) > 100 )
   //{
   //   if ( packetIndex == 4 || packetIndex == 7 || packetIndex == 8 )
   //         return false;
   //   return true;
   //}

   if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) == 100 ||
        ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) == 101 )
   if ( packetIndex == 0 || packetIndex == 1 || packetIndex == 2 || packetIndex == 3 )   
         return true;
   if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 500 ) > 250 )
   if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 500 ) < 450 )
   if ( packetIndex == 0 || packetIndex == 1 || packetIndex == 2 || packetIndex == s_BlocksTxBuffers[bufferIndex].block_packets+1 )
   if ( ! isRetransmitted )
   {
      static int sss_i = 0;
      sss_i++;
      return true;
   }

   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) > 950 )
   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) < 953 )
   //if ( packetIndex == 0 || packetIndex == 1 || packetIndex == s_BlocksTxBuffers[bufferIndex].block_packets+1 )
   //if ( ! isRetransmitted )
   //   return true;

   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) > 800 )
   //if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 1000 ) < 950 )
   //if ( packetIndex == 0 || packetIndex == 1 || packetIndex == 2 || packetIndex == s_BlocksTxBuffers[bufferIndex].block_packets+1 )
   //   return true;

      /*
      if ( ( s_BlocksTxBuffers[bufferIndex].video_block_index % 900 ) > 200 )
      {
         if ( sssbi != s_BlocksTxBuffers[bufferIndex].video_block_index )
         {
            sssbi = s_BlocksTxBuffers[bufferIndex].video_block_index;
            for( int i=0; i<10; i++ )
               ssspi[i] = 0;
            ssspi[0] = 1;
            for( int kk=0; kk<2; kk++ )
            {
                int i = rand()%10;
                if ( i > 9 ) i = 9;
                ssspi[i] = 1;
            }
         }
         if ( ssspi[packetIndex] == 1 )
         if ( ! isRetransmitted )
           return;
      }
      */

   return false;
}

// Sends a packet to radio

void _send_packet(int bufferIndex, int packetIndex, bool isRetransmitted, bool isDuplicationPacket, bool isLastBlockToSend)
{
   if ( s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].flags != PACKET_FLAG_SENT &&
        s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].flags != PACKET_FLAG_READ )
      return;

   /*
   // If it's a regular packet (not EC,retransmitted or duplicated packet):
   // Needs to duplicate previous packets? If yes, first send the older one from the previous block then the current one

   if ( (! isRetransmitted) && (! isDuplicationPacket) )
   if ( packetIndex < s_BlocksTxBuffers[bufferIndex].block_packets )
   {
      u32 uValueDup = (s_CurrentPHVF.encoding_extra_flags & ENCODING_EXTRA_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT) >> 16;

      // Manual duplication percent or auto? 0x0F - auto
      if ( (uValueDup & 0x0F) != 0 )
      {
         // Auto percent of duplication: then duplicate all video packets (but not the EC packets) on LQ profile
         if ( (uValueDup & 0x0F) == 0x0F )
         {
            if ( g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile == VIDEO_PROFILE_LQ )
            {
               bool bResend = false;
               if ( g_SM_VideoLinkStats.overwrites.currentECBlocks < g_SM_VideoLinkStats.overwrites.currentDataBlocks )
                  bResend = true;
               else if ( (packetIndex % 2) == 0 )
                  bResend = true;
               if ( bResend )
               {
                  int prevBlockBufferIndex = bufferIndex;
                  int prevBlockPacketIndex = packetIndex;
                  prevBlockBufferIndex--;
                  if ( prevBlockBufferIndex < 0 )
                     prevBlockBufferIndex = s_CurrentMaxBlocksInBuffers-1;

                  // If we have the old packet and it's the same encoding scheme, resend it

                  if ( (s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_READ) ||
                     (s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[packetIndex].flags & PACKET_FLAG_SENT) )
                  if ( (s_BlocksTxBuffers[prevBlockBufferIndex].block_packets == s_CurrentPHVF.block_packets) && 
                    (s_BlocksTxBuffers[prevBlockBufferIndex].block_fecs == s_CurrentPHVF.block_fecs ) &&
                    (s_BlocksTxBuffers[prevBlockBufferIndex].video_data_length == s_CurrentPHVF.video_packet_length ) )
                  if ( s_BlocksTxBuffers[prevBlockBufferIndex].video_block_index > s_LastEncodingChangeAtVideoBlockIndex )
                  {
                    _send_packet(prevBlockBufferIndex, prevBlockPacketIndex, false, true, false);
                  }
               }
            }
         }
      }
   }
   */

   s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].flags = PACKET_FLAG_SENT;

   t_packet_header* pHeader = (t_packet_header*)(s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].pRawData);
   t_packet_header_video_full_77* pHeaderVideo = (t_packet_header_video_full_77*)(s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].pRawData + sizeof(t_packet_header));
   
   if ( isRetransmitted )
      pHeaderVideo->uLastRecvVideoRetransmissionId = s_uLastReceivedRetransmissionRequestUniqueId;
   else
      pHeaderVideo->uLastRecvVideoRetransmissionId = 0;

   pHeader->stream_packet_idx = s_CurrentPH.stream_packet_idx;
   s_CurrentPH.stream_packet_idx++;
   s_CurrentPH.stream_packet_idx &= PACKET_FLAGS_MASK_STREAM_PACKET_IDX;
   s_CurrentPH.stream_packet_idx |= (STREAM_ID_VIDEO_1) << PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX;

   pHeaderVideo->uLastSetVideoBitrate = g_pProcessorTxVideo->getLastSetCaptureVideoBitrate();
   if ( isRetransmitted )
   {
      pHeader->packet_flags = PACKET_COMPONENT_VIDEO | PACKET_FLAGS_BIT_RETRANSMITED;
      pHeaderVideo->uLastAckKeyframeInterval = 0;
      pHeaderVideo->uLastAckLevelShift = 0;
   }
   else
   {
      pHeader->packet_flags = PACKET_COMPONENT_VIDEO;
      pHeader->packet_flags &= (~PACKET_FLAGS_BIT_CAN_START_TX);

      if ( isLastBlockToSend && packetIndex == s_BlocksTxBuffers[bufferIndex].block_packets+s_BlocksTxBuffers[bufferIndex].block_fecs-1 )
         pHeader->packet_flags |= PACKET_FLAGS_BIT_CAN_START_TX;

      if ( g_pCurrentModel->rxtx_sync_type != RXTX_SYNC_TYPE_ADV )
         pHeader->packet_flags |= PACKET_FLAGS_BIT_CAN_START_TX;

      if ( NULL != g_pProcessorTxVideo )
      {
         pHeaderVideo->uLastAckKeyframeInterval = (u16) g_pProcessorTxVideo->getLastRequestedKeyframeFromController();
         pHeaderVideo->uLastAckLevelShift = (u8) g_pProcessorTxVideo->getLastRequestedAdaptiveVideoLevelFromController();
      }
   }
   
   pHeader->packet_flags |= PACKET_FLAGS_BIT_HEADERS_ONLY_CRC;

   static u8 uLastVideoProfile = 0;
   if ( uLastVideoProfile != (pHeaderVideo->video_link_profile & 0x0F) )
   {
      uLastVideoProfile = (pHeaderVideo->video_link_profile & 0x0F);
      //log_line("DEBUG send video profile %d, %s, last ack video level requested: %d", (int)uLastVideoProfile, str_get_video_profile_name(uLastVideoProfile), (int)pHeaderVideo->uLastAckLevelShift);
   }

   // If video paused or sending only relayed remote video, do not send it

   if ( s_bPauseVideoPacketsTX || (! relay_current_vehicle_must_send_own_video_feeds()) )
   {
      return;
   }

   if ( g_pCurrentModel->bDeveloperMode )
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_INJECT_VIDEO_FAULTS )
   if ( _inject_faults(bufferIndex, pHeader->stream_packet_idx, packetIndex, isRetransmitted) )
      return;

   if ( g_pCurrentModel->bDeveloperMode )
   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_INJECT_RECOVERABLE_VIDEO_FAULTS )
   if ( _inject_recoverable_faults(bufferIndex, pHeader->stream_packet_idx, packetIndex, isRetransmitted) )
      return;

   send_packet_to_radio_interfaces(s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].pRawData, s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].packetLength);

   s_lCountBytesSend += s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].packetLength;
   s_lCountBytesSend += sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77) + 14; // 14 - radio headers


   if ( (! isRetransmitted) && (! isDuplicationPacket) )
   if ( NULL != g_pCurrentModel )
   if ( (g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_STATS_VIDEO_INFO) ||
        (g_iShowVideoKeyframesAfterRelaySwitch > 0) )
   {
      int iFoundNALVideoFrame = 0;
      int iFoundPosition = -1;

      u8* pTmp = s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].pRawData + sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77);
      int iLength = s_BlocksTxBuffers[bufferIndex].packetsInfo[packetIndex].packetLength;
      for( int k=0; k<iLength; k++ )
      {
          s_uParseNALStartSequenceRadioOut = (s_uParseNALStartSequenceRadioOut<<8) | (*pTmp);
          pTmp++;

          if ( (s_uParseNALStartSequenceRadioOut & 0xFFFFFF00) == 0x0100 )
          {
             u32 uNALTag = s_uParseNALStartSequenceRadioOut & 0b11111;
             if ( uNALTag == 1 || uNALTag == 5 )
             {
                s_uParseNALCurrentSlicesRadioOut++;
                if ( (int)s_uParseNALCurrentSlicesRadioOut >= camera_get_active_camera_h264_slices(g_pCurrentModel) )
                {
                   s_uParseNALCurrentSlicesRadioOut = 0;
                   iFoundNALVideoFrame = uNALTag;
                   g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize += k;
                   iFoundPosition = k;

                   if ( g_iShowVideoKeyframesAfterRelaySwitch > 0 )
                   {
                      log_line("[Debug] Transmitted keyframe after relay switch.");
                      g_iShowVideoKeyframesAfterRelaySwitch--;
                   }
                }
             }
             //if ((s_uParseVideoTag == 0x0121) || (s_uParseVideoTag == 0x0127))
          }
      }

      if ( iFoundNALVideoFrame )
      {
          u32 delta = g_TimeNow - s_uLastVideoFrameTimeRadioOut;
          if ( delta > 254 )
             delta = 254;
          if ( delta == 0 )
             delta = 1;
          
          g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize = g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize / 1000;
          if ( g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize > 127 )
             g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize = 127;

          g_VideoInfoStatsRadioOut.uLastIndex = (g_VideoInfoStatsRadioOut.uLastIndex+1) % MAX_FRAMES_SAMPLES;
          g_VideoInfoStatsRadioOut.uFramesTimes[g_VideoInfoStatsRadioOut.uLastIndex] = delta;
          g_VideoInfoStatsRadioOut.uFramesTypesAndSizes[g_VideoInfoStatsRadioOut.uLastIndex] = (g_VideoInfoStats.uFramesTypesAndSizes[g_VideoInfoStatsRadioOut.uLastIndex] & 0x80) | ((u8)g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize);
          
          u32 uNextIndex = (g_VideoInfoStatsRadioOut.uLastIndex+1) % MAX_FRAMES_SAMPLES;
          if ( iFoundNALVideoFrame == 5 )
             g_VideoInfoStatsRadioOut.uFramesTypesAndSizes[uNextIndex] |= (1<<7);
          else
             g_VideoInfoStatsRadioOut.uFramesTypesAndSizes[uNextIndex] &= 0x7F;
      
          g_VideoInfoStatsRadioOut.uTmpKeframeDeltaFrames++;
          static u32 s_uLastTimeNALKeyframeToSend = 0;
          if ( iFoundNALVideoFrame == 5 )
          {
             g_VideoInfoStatsRadioOut.uKeyframeIntervalMs = g_TimeNow - s_uLastTimeNALKeyframeToSend;
             s_uLastTimeNALKeyframeToSend = g_TimeNow;
             g_VideoInfoStatsRadioOut.uTmpKeframeDeltaFrames = 0; 
          }

          s_uLastVideoFrameTimeRadioOut = g_TimeNow;
          g_VideoInfoStatsRadioOut.uTmpCurrentFrameSize = iLength-iFoundPosition;
      }
      else
         g_VideoInfoStats.uTmpCurrentFrameSize += iLength;
   }
}

// Returns the number of packets ready to be sent

int process_data_tx_video_has_packets_ready_to_send()
{
   int countReadyToSend = 0;
   int prevBlockPacketIndex = s_currentReadBlockPacketIndex;
   int prevBlockBufferIndex = s_currentReadBufferIndex;

   // Look back max 3 blocks
   int countPackets = 3 * (s_BlocksTxBuffers[s_currentReadBlockPacketIndex].block_packets + s_BlocksTxBuffers[s_currentReadBlockPacketIndex].block_fecs);

   for( int i=0; i<=countPackets; i++ )
   {
      prevBlockPacketIndex--;
      if ( prevBlockPacketIndex < 0 )
      {
         prevBlockBufferIndex--;
         if ( prevBlockBufferIndex < 0 )
            prevBlockBufferIndex = s_CurrentMaxBlocksInBuffers-1;
         prevBlockPacketIndex = s_BlocksTxBuffers[prevBlockBufferIndex].block_packets + s_BlocksTxBuffers[prevBlockBufferIndex].block_fecs - 1;
         if ( prevBlockPacketIndex < 0 )
            prevBlockPacketIndex = 0;
      }
      if ( ( s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_READ ) &&
           ( !(s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_SENT)) )
         countReadyToSend++;
      else
         break;
   }
   return countReadyToSend;
}

// How many packets backwards to send (from the available ones)
// Returns the number of packets sent

int process_data_tx_video_send_packets_ready_to_send(int howMany)
{
   if ( 0 == howMany )
      return 0;

   int countSent = 0;

   int prevBlockBufferIndex = s_currentReadBufferIndex;
   int prevBlockPacketIndex = s_currentReadBlockPacketIndex;

   int countToSend = 0;
   for( int i=0; i<howMany; i++ )
   {
      prevBlockPacketIndex--;
      if ( prevBlockPacketIndex < 0 )
      {
         prevBlockPacketIndex = s_BlocksTxBuffers[prevBlockBufferIndex].block_packets + s_BlocksTxBuffers[prevBlockBufferIndex].block_fecs - 1;
         if ( prevBlockPacketIndex < 0 )
            prevBlockPacketIndex = 0;
         prevBlockBufferIndex--;
         if ( prevBlockBufferIndex < 0 )
            prevBlockBufferIndex = s_CurrentMaxBlocksInBuffers-1;
      }
      if ( ( s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_READ ) &&
           ( !(s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_SENT)) )
         countToSend++;
      else
         break;
   }

   u32 uMicroTime = 0;
   if ( countToSend > 5 )
      uMicroTime = get_current_timestamp_micros();

   for( int i=0; i<countToSend; i++ )
   {
      if ( ( s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_READ ) &&
           ( !(s_BlocksTxBuffers[prevBlockBufferIndex].packetsInfo[prevBlockPacketIndex].flags & PACKET_FLAG_SENT)) )
      {
         _send_packet(prevBlockBufferIndex, prevBlockPacketIndex, false, false, true);
         countSent++;
      }
      prevBlockPacketIndex++;
      if ( prevBlockPacketIndex >= s_BlocksTxBuffers[prevBlockBufferIndex].block_packets + s_BlocksTxBuffers[prevBlockBufferIndex].block_fecs )
      {
         prevBlockPacketIndex = 0;
         prevBlockBufferIndex++;
         if ( prevBlockBufferIndex >= s_CurrentMaxBlocksInBuffers )
            prevBlockBufferIndex = 0;
      }
   }

   static int sl_iCountSuccessiveOverloads = 0;
   if ( countToSend > 5 )
   {
      uMicroTime = get_current_timestamp_micros() - uMicroTime;
      u32 uVideoPacketsPerSec = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverage() / 1200 / 8;
      if ( uVideoPacketsPerSec < 10 )
         uVideoPacketsPerSec = 10;
      if ( uMicroTime/countToSend > 500000/uVideoPacketsPerSec )
      {
         //log_line("DEBUG sending %d packets took %u microseconds, %u packs/sec, counter %d", countToSend, uMicroTime, uVideoPacketsPerSec, sl_iCountSuccessiveOverloads);
         sl_iCountSuccessiveOverloads++;
         int iMaxOverloads = (int)uVideoPacketsPerSec/10;
         if ( g_pCurrentModel->video_params.uVideoExtraFlags & VIDEO_FLAG_IGNORE_TX_SPIKES )
            iMaxOverloads = (int)uVideoPacketsPerSec;
         if ( sl_iCountSuccessiveOverloads > iMaxOverloads )
         {
            //log_line("DEBUG in overload now");
            g_uTimeLastVideoTxOverload = g_TimeNow;
         }
      }
      else
      {
         if ( sl_iCountSuccessiveOverloads > 3 )
            sl_iCountSuccessiveOverloads -= 3;
      }
   }
   else if ( sl_iCountSuccessiveOverloads > 0 )
         sl_iCountSuccessiveOverloads--;
   return countSent;
}

// Returns the block index to send in the blocks buffer, or -1 if none ready

int process_data_tx_video_has_block_ready_to_send()
{
   // No new complete blocks? Return
   if ( s_LastSentBlockBufferIndex + 1 == s_currentReadBufferIndex )
      return -1;

   if ( (s_LastSentBlockBufferIndex == s_CurrentMaxBlocksInBuffers-1) && (s_currentReadBufferIndex == 0) )
      return -1;

   int blockIndexToSend = s_LastSentBlockBufferIndex+1;
   if ( blockIndexToSend >= s_CurrentMaxBlocksInBuffers )
      blockIndexToSend = 0;

   bool hasAllPackets = true;
   for( int i=0; i<s_BlocksTxBuffers[blockIndexToSend].block_packets+s_BlocksTxBuffers[blockIndexToSend].block_fecs; i++ )
      if ( s_BlocksTxBuffers[blockIndexToSend].packetsInfo[i].flags == PACKET_FLAG_EMPTY )
      {
         hasAllPackets = false;
         break;
      }

   if ( ! hasAllPackets )
      return -1;

   return blockIndexToSend;
}

int process_data_tx_video_get_pending_blocks_to_send_count()
{
   // No new complete blocks? Return
   if ( s_LastSentBlockBufferIndex + 1 == s_currentReadBufferIndex )
      return -1;

   if ( (s_LastSentBlockBufferIndex == s_CurrentMaxBlocksInBuffers-1) && (s_currentReadBufferIndex == 0) )
      return -1;

   int count = s_currentReadBufferIndex - s_LastSentBlockBufferIndex - 1;
   if ( count < 0 )
      count += s_CurrentMaxBlocksInBuffers;
   return count;
}

// Returns how many blocks where sent

int process_data_tx_video_send_first_complete_block(bool isLastBlockToSend)
{
   // No new complete blocks? Return
   int blockIndexToSend = process_data_tx_video_has_block_ready_to_send();
   if ( -1 == blockIndexToSend )
      return 0;

   for( int i=0; i<s_BlocksTxBuffers[blockIndexToSend].block_packets+s_BlocksTxBuffers[blockIndexToSend].block_fecs; i++ )
      _send_packet(blockIndexToSend, i, false, false, isLastBlockToSend);

   s_LastSentBlockBufferIndex = blockIndexToSend;
   return 1;
}


// Returns true if a block is complete

bool _onNewCompletePacketReadFromInput()
{
   u32 uTimeDiff = g_TimeNow - g_TimeLastVideoPacketIn;
   g_TimeLastVideoPacketIn = g_TimeNow;
   
   // If video is paused or if transmitting only remote relay video, then skip it
   if ( s_bPauseVideoPacketsTX || (! relay_current_vehicle_must_send_own_video_feeds()) )
   {
      s_LastSentBlockBufferIndex = -1;
      s_currentReadBufferIndex = 0;
      s_currentReadBlockPacketIndex = 0;
      s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition = 0;
      _reset_tx_buffers();
      return false;
   }

   if ( 0xFF == g_PHVehicleTxStats.historyVideoPacketsGapMax[0] )
      g_PHVehicleTxStats.historyVideoPacketsGapMax[0] = uTimeDiff;
   if ( uTimeDiff > g_PHVehicleTxStats.historyVideoPacketsGapMax[0] )
      g_PHVehicleTxStats.historyVideoPacketsGapMax[0] = uTimeDiff;

   g_PHVehicleTxStats.tmp_uVideoIntervalsCount++;
   g_PHVehicleTxStats.tmp_uVideoIntervalsSum += uTimeDiff;
   
   s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].flags = PACKET_FLAG_READ;

   s_currentReadBlockPacketIndex++;
   s_CurrentPHVF.video_block_packet_index++;

   s_CurrentPHVF.video_link_profile = g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile;
   s_CurrentPHVF.encoding_extra_flags = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].encoding_extra_flags;
      
   s_CurrentPHVF.uLastRecvVideoRetransmissionId = 0;
   s_CurrentPHVF.encoding_extra_flags2 = g_SM_VideoLinkStats.overwrites.currentH264QUantization;
   s_CurrentPHVF.encoding_extra_flags &= ~ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE;
   if ( g_SM_VideoLinkStats.overwrites.profilesTopVideoBitrateOverwritesDownward[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile] != 0 )
      s_CurrentPHVF.encoding_extra_flags |= ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE;

   if ( g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs < 255*250 )
      s_CurrentPHVF.video_keyframe_interval_ms = g_SM_VideoLinkStats.overwrites.uCurrentKeyframeMs;
   else
      s_CurrentPHVF.video_keyframe_interval_ms = 255*250;

   // Still on the data packets ?

   if ( s_currentReadBlockPacketIndex < s_CurrentPHVF.block_packets )
   {
      s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].flags = PACKET_FLAG_EMPTY;
      s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].packetLength = sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77) + s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length;
      s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition = 0;

      t_packet_header* pHeader = (t_packet_header*)(s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].pRawData);
      t_packet_header_video_full_77* pVideo = (t_packet_header_video_full_77*)(((u8*)(pHeader)) + sizeof(t_packet_header));
      memcpy(pHeader, &s_CurrentPH, sizeof(t_packet_header));
      memcpy(pVideo, &s_CurrentPHVF, sizeof(t_packet_header_video_full_77));

      return false;
   }

   // Generate and add FEC packets if configured so.

   if ( s_CurrentPHVF.block_fecs > 0 )
   {
      for( int i=0; i<s_CurrentPHVF.block_packets; i++ )
         p_fec_data_packets[i] = ((u8*)s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[i].pRawData) + sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77);

      for( int i=0; i<s_CurrentPHVF.block_fecs; i++ )
         p_fec_data_fecs[i] = ((u8*)s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_CurrentPHVF.block_packets+i].pRawData) + sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77);

      u32 tTemp = get_current_timestamp_micros();
      fec_encode(s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length, p_fec_data_packets, s_CurrentPHVF.block_packets, p_fec_data_fecs, s_CurrentPHVF.block_fecs);
      tTemp = get_current_timestamp_micros() - tTemp;
      sTimeTotalFecTimeMicroSec += tTemp;

      for( int i=0; i<s_CurrentPHVF.block_fecs; i++ )
      {
         s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].flags = PACKET_FLAG_READ;
         s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].packetLength = sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77) + s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length;
         s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition = 5000;

         t_packet_header* pHeader = (t_packet_header*)(s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].pRawData);
         t_packet_header_video_full_77* pVideo = (t_packet_header_video_full_77*)(((u8*)(pHeader)) + sizeof(t_packet_header));
         memcpy(pHeader, &s_CurrentPH, sizeof(t_packet_header));
         memcpy(pVideo, &s_CurrentPHVF, sizeof(t_packet_header_video_full_77));

         s_currentReadBlockPacketIndex++;
         s_CurrentPHVF.video_block_packet_index++;
      }
   }

   // Move to next block

   s_CurrentPHVF.video_block_index++;
   s_CurrentPHVF.video_block_packet_index = 0;

   s_currentReadBufferIndex++;
   s_currentReadBlockPacketIndex = 0;

   if ( s_currentReadBufferIndex >= s_CurrentMaxBlocksInBuffers )
   {
      s_currentReadBufferIndex = 0;
      //log_line("Buffer overlaped");
   }

   s_BlocksTxBuffers[s_currentReadBufferIndex].video_block_index = s_CurrentPHVF.video_block_index;
   for( int i=0; i<MAX_TOTAL_PACKETS_IN_BLOCK; i++ )
      s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[i].flags = PACKET_FLAG_EMPTY;


   if ( s_bPendingEncodingSwitch )
   {
      //log_line("Applying pending encodings change (starting at video block index %u):", s_CurrentPHVF.video_block_index);
      s_bPendingEncodingSwitch = false;

      if ( _check_update_video_link_profile_data() )
         _log_encoding_scheme();

      s_CurrentPH.vehicle_id_src = g_pCurrentModel->vehicle_id;
      s_CurrentPH.total_length = sizeof(t_packet_header)+sizeof(t_packet_header_video_full_77) + s_CurrentPHVF.video_packet_length;
   }

   s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length = s_CurrentPHVF.video_packet_length;
   s_BlocksTxBuffers[s_currentReadBufferIndex].block_packets = s_CurrentPHVF.block_packets;
   s_BlocksTxBuffers[s_currentReadBufferIndex].block_fecs = s_CurrentPHVF.block_fecs;

   s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[0].packetLength = sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77) + s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length;
   s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[0].currentReadPosition = 0;

   t_packet_header* pHeader = (t_packet_header*)(s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[0].pRawData);
   t_packet_header_video_full_77* pVideo = (t_packet_header_video_full_77*)(((u8*)(pHeader)) + sizeof(t_packet_header));
   memcpy(pHeader, &s_CurrentPH, sizeof(t_packet_header));
   memcpy(pVideo, &s_CurrentPHVF, sizeof(t_packet_header_video_full_77));

   return true;
}

bool process_data_tx_video_init()
{
   for( int i=0; i<MAX_RXTX_BLOCKS_BUFFER; i++ )
   for( int k=0; k<MAX_TOTAL_PACKETS_IN_BLOCK; k++ )
   {
      s_BlocksTxBuffers[i].packetsInfo[k].pRawData = (u8*) malloc(MAX_PACKET_TOTAL_SIZE);
      if ( NULL == s_BlocksTxBuffers[i].packetsInfo[k].pRawData )
      {
         log_error_and_alarm("[VideoTx] Failed to alocate memory for buffers.");
         return false;
      }
   }

   s_uParseNALStartSequence = MAX_U32;
   s_uParseNALStartSequenceRadioOut = MAX_U32;
   s_uParseNALCurrentSlices = 0;
   s_uParseNALStartSequenceRadioOut = 0;
   s_uLastVideoFrameTime = 0;
   s_uLastVideoFrameTimeRadioOut = 0;
   g_iFramesSinceLastH264KeyFrame = 0;
   s_uCountEncodingChanges = 0;
   _reset_tx_buffers();

   log_line("[VideoTx] Allocated %u Mb for video Tx buffers (%d blocks)", (u32)MAX_RXTX_BLOCKS_BUFFER * (u32)MAX_TOTAL_PACKETS_IN_BLOCK * (u32) MAX_PACKET_TOTAL_SIZE / 1000 / 1000, MAX_RXTX_BLOCKS_BUFFER );

   radio_packet_init(&s_CurrentPH, PACKET_COMPONENT_VIDEO | PACKET_FLAGS_BIT_HEADERS_ONLY_CRC, PACKET_TYPE_VIDEO_DATA_FULL, STREAM_ID_VIDEO_1);

   s_CurrentPH.vehicle_id_src = g_pCurrentModel->vehicle_id;
   s_CurrentPH.vehicle_id_dest = 0;
   s_CurrentPH.total_length = sizeof(t_packet_header)+sizeof(t_packet_header_video_full_77) + g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].packet_length;

   _check_update_video_link_profile_data();

   if ( (NULL != g_pCurrentModel) && g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile >= 0 && g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile < MAX_VIDEO_LINK_PROFILES )
   {
      if ( g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].keyframe_ms > 0 )
         s_iCurrentKeyFrameIntervalMs = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].keyframe_ms;
      else
         s_iCurrentKeyFrameIntervalMs = DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
   }
   else
   {
      if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].keyframe_ms > 0 )
         s_iCurrentKeyFrameIntervalMs = g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].keyframe_ms;
      else
         s_iCurrentKeyFrameIntervalMs = DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
   }
   s_CurrentPHVF.video_stream_and_type = 0 | (VIDEO_TYPE_H264<<4);
   s_CurrentPHVF.video_block_index = 0;
   s_CurrentPHVF.video_block_packet_index = 0;
   s_CurrentPHVF.fec_time = 0;

   s_currentReadBufferIndex = 0;
   s_currentReadBlockPacketIndex = 0;

   s_BlocksTxBuffers[0].video_block_index = 0;
   s_BlocksTxBuffers[0].video_data_length = s_CurrentPHVF.video_packet_length;
   s_BlocksTxBuffers[0].block_packets = s_CurrentPHVF.block_packets;
   s_BlocksTxBuffers[0].block_fecs = s_CurrentPHVF.block_fecs;

   s_BlocksTxBuffers[0].packetsInfo[0].flags = PACKET_FLAG_EMPTY;
   s_BlocksTxBuffers[0].packetsInfo[0].packetLength = sizeof(t_packet_header) + sizeof(t_packet_header_video_full_77) + s_BlocksTxBuffers[0].video_data_length;
   s_BlocksTxBuffers[0].packetsInfo[0].currentReadPosition = 0;

   t_packet_header* pHeader = (t_packet_header*)(s_BlocksTxBuffers[0].packetsInfo[0].pRawData);
   t_packet_header_video_full_77* pVideo = (t_packet_header_video_full_77*)(((u8*)(pHeader)) + sizeof(t_packet_header));
   memcpy(pHeader, &s_CurrentPH, sizeof(t_packet_header));
   memcpy(pVideo, &s_CurrentPHVF, sizeof(t_packet_header_video_full_77));

   process_data_tx_video_reset_retransmissions_history_info();

   for( int i=0; i<MAX_TOTAL_PACKETS_IN_BLOCK; i++ )
      s_BlocksTxBuffers[0].packetsInfo[i].flags = PACKET_FLAG_EMPTY;

   s_LastSentBlockBufferIndex = -1;


   u32 videoBitrate = g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].bitrate_fixed_bps;
   float miliPerBlock = (float)((u32)(1000 * 8 * (u32)g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].packet_length * (u32)g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].block_packets)) / (float)videoBitrate;
   u32 uMaxWindowMS = 500;
   u32 bytesToBuffer = (((float)videoBitrate / 1000.0) * uMaxWindowMS)/8.0;
   log_line("[VideoTx] Need buffers to cache %u bits of video (%u bytes of video)", bytesToBuffer*8, bytesToBuffer);
   u32 uMaxBlocksToBuffer = 2 + bytesToBuffer/g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].packet_length/g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].block_packets;
   
   // Add extra time for last retransmissions (50 milisec)
   if ( miliPerBlock > 0.0001 )
      uMaxBlocksToBuffer += 50.0/miliPerBlock;
   
   uMaxBlocksToBuffer *= 1.2;

   if ( uMaxBlocksToBuffer < 5 )
      uMaxBlocksToBuffer = 5;
   if ( uMaxBlocksToBuffer >= MAX_RXTX_BLOCKS_BUFFER )
      uMaxBlocksToBuffer = MAX_RXTX_BLOCKS_BUFFER-1;

   log_line("[VideoTx] Computed result: Will cache a maximum of %u video blocks, for a total of %d miliseconds of video (max retransmission window is %u ms); one block stores %.1f miliseconds of video",
       uMaxBlocksToBuffer, (int)((float)miliPerBlock * (float)uMaxBlocksToBuffer), uMaxWindowMS, miliPerBlock);

   s_CurrentMaxBlocksInBuffers = MAX_RXTX_BLOCKS_BUFFER;

   g_TimeLastVideoPacketIn = get_current_timestamp_ms();
   _log_encoding_scheme();

   return true;
}

bool process_data_tx_video_uninit()
{
   for( int i=0; i<MAX_RXTX_BLOCKS_BUFFER; i++ )
   for( int k=0; k<MAX_TOTAL_PACKETS_IN_BLOCK; k++ )
   {
      free(s_BlocksTxBuffers[i].packetsInfo[k].pRawData);
      s_BlocksTxBuffers[i].packetsInfo[k].pRawData = NULL;
   }
   return true;
}

void _process_command_resend_packets(u8* pPacketBuffer, u8 packetType)
{   
   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
   u8* pData = pPacketBuffer + sizeof(t_packet_header);

   if ( packetType == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2 )
   {
      memcpy((u8*)&s_uLastReceivedRetransmissionRequestUniqueId, pData, sizeof(u32));
      pData += sizeof(u32); // Skip: Retransmission request unique Id
   }

   //u8 videoStreamId = *pData; //Skip video stream Id
   pData++;
   u8 countSegmentsRequested = *pData;
   pData++;

   u32 requested_video_block_index = MAX_U32;
   u8  requested_video_packet_index = 0;
   u8  requested_retry_count = 0;
   
   
   {
      char szBuff[256];
      char szTmp[32];
      szBuff[0] = 0;
      u8* pTmp = pData;
      for( u8 c=0; c<countSegmentsRequested; c++ )
      {
         memcpy(&requested_video_block_index, pTmp, sizeof(u32));
         pTmp += sizeof(u32);
         requested_video_packet_index = *pTmp;
         pTmp++;
         requested_retry_count = *pTmp;
         pTmp++;

         if ( 0 != szBuff[0] )
            strcat(szBuff, ", ");
         sprintf(szTmp, "[%u/%d] x %d", requested_video_block_index, requested_video_packet_index, requested_retry_count);
         strcat(szBuff, szTmp);
         if ( c > 4 )
            break;
      }
   }

   g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsUnique++;

   if ( s_iCountLastRetransmissionsRequestsTimes < MAX_HISTORY_RETRANSMISSION_INFO )
   {
      s_listLastRetransmissionsRequestsTimes[s_iCountLastRetransmissionsRequestsTimes] = g_TimeNow;
      s_iCountLastRetransmissionsRequestsTimes++;
   }

   if ( g_SM_VideoLinkGraphs.tmp_vehileReceivedRetransmissionsRequestsCount < 255 )
      g_SM_VideoLinkGraphs.tmp_vehileReceivedRetransmissionsRequestsCount++;
   if ( g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPackets + countSegmentsRequested <= 255 )
      g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPackets += countSegmentsRequested;
   else
      g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPackets = 255;

   for( u8 c=0; c<countSegmentsRequested; c++ )
   {
      memcpy(&requested_video_block_index, pData, sizeof(u32));
      pData += sizeof(u32);
      requested_video_packet_index = *pData;
      pData++;
      requested_retry_count = *pData;
      pData++;


      int iDuplicateSegmentIndex = -1;
      for( int i=0; i<s_iCountRetransmissionsSegmentsInfo; i++ )
         if ( (s_listRetransmissionsSegmentsInfo[i].video_block_index == requested_video_block_index) &&
              (s_listRetransmissionsSegmentsInfo[i].video_packet_index == requested_video_packet_index) )
         {
            iDuplicateSegmentIndex = i;
            break;
         }

      if ( -1 != iDuplicateSegmentIndex )
      {
         s_listRetransmissionsSegmentsInfo[iDuplicateSegmentIndex].uRepeatCount++;
         g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsRetried++;
      }
      else if ( s_iCountRetransmissionsSegmentsInfo < MAX_HISTORY_RETRANSMISSION_INFO )
      {
         s_listRetransmissionsSegmentsInfo[s_iCountRetransmissionsSegmentsInfo].video_block_index = requested_video_block_index;
         s_listRetransmissionsSegmentsInfo[s_iCountRetransmissionsSegmentsInfo].video_packet_index = requested_video_packet_index;
         s_listRetransmissionsSegmentsInfo[s_iCountRetransmissionsSegmentsInfo].uRepeatCount = 0;
         s_listRetransmissionsSegmentsInfo[s_iCountRetransmissionsSegmentsInfo].uReceiveTime = g_TimeNow;
         g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsUnique++;
      }
      
      if ( requested_retry_count > 1 )
      {
         if ( g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPacketsRetried + (requested_retry_count-1) <= 255 )
            g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPacketsRetried += (requested_retry_count-1);
         else
            g_SM_VideoLinkGraphs.tmp_vehicleReceivedRetransmissionsRequestsPacketsRetried = 255;
      }
      if ( requested_video_block_index > s_CurrentPHVF.video_block_index )
         continue;

      int diff = s_CurrentPHVF.video_block_index-requested_video_block_index;
      if ( diff >= s_CurrentMaxBlocksInBuffers )
         continue;

      int bufferIndex = s_currentReadBufferIndex - diff;
      if ( bufferIndex < 0 )
         bufferIndex += s_CurrentMaxBlocksInBuffers;

      if ( s_BlocksTxBuffers[bufferIndex].video_block_index != requested_video_block_index )
         continue;

      if ( requested_video_packet_index >= s_BlocksTxBuffers[bufferIndex].block_packets + s_BlocksTxBuffers[bufferIndex].block_fecs )
         continue;

      if ( s_BlocksTxBuffers[bufferIndex].packetsInfo[requested_video_packet_index].flags != PACKET_FLAG_READ &&
           s_BlocksTxBuffers[bufferIndex].packetsInfo[requested_video_packet_index].flags != PACKET_FLAG_SENT )
         continue;

      //log_line("Resending packet [%u/%d]", requested_video_block_index, requested_video_packet_index);

      _send_packet(bufferIndex, (int)requested_video_packet_index, true, false, false);

      s_iRetransmissionsDuplicationIndex++;
      bool bDoDuplicate = false;
      u32 uValue = 0xFF;
      if ( NULL != g_pCurrentModel && ((s_CurrentPHVF.encoding_extra_flags & ENCODING_EXTRA_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT) != ENCODING_EXTRA_FLAG_RETRANSMISSIONS_DUPLICATION_PERCENT_AUTO) )
         uValue = (s_CurrentPHVF.encoding_extra_flags & ENCODING_EXTRA_FLAG_MASK_RETRANSMISSIONS_DUPLICATION_PERCENT) >> 16;

      // Manual duplication percent or auto ? 0xF0 - auto
      if ( (uValue >> 4) != 0x0F )
      {
         int percent = (int)((uValue & 0xFF) >> 4); // from 0 to 10, 0x0F for auto
         if ( percent <= 10 )
         {
            int freq = 0;
            if ( percent != 0 )
               freq = (10/percent);
            if ( percent < 7 )
               freq++;
            if ( percent >= 9 )
               freq = 0;
            //log_line("percent: %d, freq: %d, current: %d", percent, freq, s_iRetransmissionsDuplicationIndex);
            if ( percent > 0 && s_iRetransmissionsDuplicationIndex > freq )
               bDoDuplicate = true;
         }
      }

      if ( (uValue >> 4) == 0x0F )
      if ( g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile == VIDEO_PROFILE_LQ )
      if ( g_SM_VideoLinkStats.overwrites.currentProfileShiftLevel > 1 )
         bDoDuplicate = true;

      if ( bDoDuplicate )
      {
         s_iRetransmissionsDuplicationIndex = 0;
         _send_packet(bufferIndex, (int)requested_video_packet_index, true, false, false);
      }
   }

   if ( pPH->total_length > sizeof(t_packet_header) + 2*sizeof(u8) + countSegmentsRequested*(sizeof(u32) + 2*sizeof(u8)) )
   {
      // Extract controller radio & video links stats from pData pointer
      //u8 flagsAndVersion = *pData;
      pData++;
   }
}

bool process_data_tx_video_command(int iRadioInterface, u8* pPacketBuffer)
{
   if ( ! (s_CurrentPHVF.encoding_extra_flags & ENCODING_EXTRA_FLAG_ENABLE_RETRANSMISSIONS) )
      return false;

   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;

   if ( pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS || pPH->packet_type == PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS2 )
      _process_command_resend_packets(pPacketBuffer, pPH->packet_type );

   return true;
}

bool process_data_tx_video_loop()
{
   if ( g_TimeNow >= sTimeLastFecTimeCalculation + 500)
   {
      sTimeLastFecTimeCalculation = g_TimeNow;
      s_CurrentPHVF.fec_time = 2*sTimeTotalFecTimeMicroSec;
      sTimeTotalFecTimeMicroSec = 0;

      // Update retransmission statistics for the last 5 secs
      // Discard all the info older than 5 secs

      g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsUniqueLast5Sec = s_iCountRetransmissionsSegmentsInfo;
      g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsRetriedLast5Sec = 0;

      for( int i=s_iCountRetransmissionsSegmentsInfo-1; i>=0; i-- )
      {
         if ( s_listRetransmissionsSegmentsInfo[i].uReceiveTime == 0 || s_listRetransmissionsSegmentsInfo[i].uReceiveTime < g_TimeNow-500 )
         {
            g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsUniqueLast5Sec = s_iCountRetransmissionsSegmentsInfo - i;
             
            // Move segments down

            for( int k=0; k<s_iCountRetransmissionsSegmentsInfo-i-1; k++ )
               memcpy((u8*)&(s_listRetransmissionsSegmentsInfo[k]), (u8*)&(s_listRetransmissionsSegmentsInfo[k+i+1]), sizeof(type_retransmissions_history_info));
            s_iCountRetransmissionsSegmentsInfo = s_iCountRetransmissionsSegmentsInfo-i-1;
            break;
         }
         if ( s_listRetransmissionsSegmentsInfo[i].uRepeatCount > 0 )
            g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsSegmentsRetriedLast5Sec++;
      }

      g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsUniqueLast5Sec = s_iCountLastRetransmissionsRequestsTimes;
         
      for( int i=s_iCountLastRetransmissionsRequestsTimes-1; i>=0; i-- )
      {
         if ( s_listLastRetransmissionsRequestsTimes[i] == 0 || s_listLastRetransmissionsRequestsTimes[i] < g_TimeNow - 500 )
         {
            for( int k=0; k<s_iCountLastRetransmissionsRequestsTimes-i-1; k++ )
               s_listLastRetransmissionsRequestsTimes[k] = s_listLastRetransmissionsRequestsTimes[k+i+1];
            s_iCountLastRetransmissionsRequestsTimes = s_iCountLastRetransmissionsRequestsTimes - i - 1;
            g_PHTE_Retransmissions.totalReceivedRetransmissionsRequestsUniqueLast5Sec = s_iCountLastRetransmissionsRequestsTimes;
            break;
         }
      }
   }

   g_pProcessorTxVideo->periodicLoop();

   return true;
}

u8* process_data_tx_video_get_current_buffer_to_read_pointer()
{
   return ((u8*)s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].pRawData)+sizeof(t_packet_header)+sizeof(t_packet_header_video_full_77) + s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition;
}

int process_data_tx_video_get_current_buffer_to_read_size()
{
   return s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length - s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition;
}

// Returns true if a block is complete

bool process_data_tx_video_on_data_read_complete(int countRead)
{
   if ( countRead <= 0 )
      return false;

   static u32 s_uLastTimeReceivedNALUKeyframe = 0;

   if ( NULL != g_pCurrentModel )
   //if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_STATS_VIDEO_INFO)
   {
      int iFoundNALVideoFrame = 0;
      int iFoundPosition = -1;

      u8* pTmp = process_data_tx_video_get_current_buffer_to_read_pointer();
      for( int k=0; k<countRead; k++ )
      {
          s_uParseNALStartSequence = (s_uParseNALStartSequence<<8) | (*pTmp);
          pTmp++;

          if ( (s_uParseNALStartSequence & 0xFFFFFF00) == 0x0100 )
          {
             u32 uNALTag = s_uParseNALStartSequence & 0b11111;
             if ( uNALTag == 1 || uNALTag == 5 )
             {
                s_uParseNALCurrentSlices++;
                if ( (int)s_uParseNALCurrentSlices >= camera_get_active_camera_h264_slices(g_pCurrentModel) )
                {
                   s_uParseNALCurrentSlices = 0;
                   iFoundNALVideoFrame = uNALTag;
                   g_VideoInfoStats.uTmpCurrentFrameSize += k;
                   iFoundPosition = k;
                   if ( g_iShowVideoKeyframesAfterRelaySwitch > 0 )
                   {
                      log_line("[Debug] Read video keyframe after relay switch.");
                      g_iShowVideoKeyframesAfterRelaySwitch--;
                   }
                }
             }
             //if ((s_uParseVideoTag == 0x0121) || (s_uParseVideoTag == 0x0127))
          }
      }

      if ( iFoundNALVideoFrame )
      {
          u32 delta = g_TimeNow - s_uLastVideoFrameTime;
          if ( delta > 254 )
             delta = 254;
          if ( delta == 0 )
             delta = 1;

          g_VideoInfoStats.uTmpCurrentFrameSize = g_VideoInfoStats.uTmpCurrentFrameSize / 1000;
          if ( g_VideoInfoStats.uTmpCurrentFrameSize > 127 )
             g_VideoInfoStats.uTmpCurrentFrameSize = 127;

          g_VideoInfoStats.uLastIndex = (g_VideoInfoStats.uLastIndex+1) % MAX_FRAMES_SAMPLES;
          g_VideoInfoStats.uFramesTimes[g_VideoInfoStats.uLastIndex] = delta;
          g_VideoInfoStats.uFramesTypesAndSizes[g_VideoInfoStats.uLastIndex] = (g_VideoInfoStats.uFramesTypesAndSizes[g_VideoInfoStats.uLastIndex] & 0x80) | ((u8)g_VideoInfoStats.uTmpCurrentFrameSize);
          
          u32 uNextIndex = (g_VideoInfoStats.uLastIndex+1) % MAX_FRAMES_SAMPLES;
          if ( iFoundNALVideoFrame == 5 )
          {
             // Keyframe

             g_VideoInfoStats.uFramesTypesAndSizes[uNextIndex] |= (1<<7);
             g_VideoInfoStats.uKeyframeIntervalMs = g_TimeNow - s_uLastTimeReceivedNALUKeyframe;
             s_uLastTimeReceivedNALUKeyframe = g_TimeNow;
             if ( s_iCurrentKeyFrameIntervalMs == -1 )
             if ( g_iFramesSinceLastH264KeyFrame > 0 )
                s_iCurrentKeyFrameIntervalMs = 1;
             g_iFramesSinceLastH264KeyFrame = 0;
          }
          else
          {
             g_VideoInfoStats.uFramesTypesAndSizes[uNextIndex] &= 0x7F;
             g_iFramesSinceLastH264KeyFrame++;

             if ( s_iPendingSetKeyframeIntervalMs != s_iCurrentKeyFrameIntervalMs )
             if ( s_iPendingSetKeyframeIntervalMs > 0 && s_iCurrentKeyFrameIntervalMs > 0 )
             {
                bool bUpdate = false;
                if ( s_iPendingSetKeyframeIntervalMs > s_iCurrentKeyFrameIntervalMs )
                if ( ((s_iCurrentKeyFrameIntervalMs < 500 ) && (g_TimeNow >= s_uLastTimeReceivedNALUKeyframe + s_iCurrentKeyFrameIntervalMs/2)) || 
                     ((s_iCurrentKeyFrameIntervalMs >= 500 ) && (g_TimeNow >= s_uLastTimeReceivedNALUKeyframe + (s_iCurrentKeyFrameIntervalMs*4)/5)) )
                   bUpdate = true;
                if ( s_iPendingSetKeyframeIntervalMs < s_iCurrentKeyFrameIntervalMs )
                if ( ((s_iCurrentKeyFrameIntervalMs < 500 ) && (g_TimeNow >= s_uLastTimeReceivedNALUKeyframe + s_iPendingSetKeyframeIntervalMs*3/4)) || 
                     ((s_iCurrentKeyFrameIntervalMs >= 500 ) && (g_TimeNow >= s_uLastTimeReceivedNALUKeyframe + s_iPendingSetKeyframeIntervalMs*4/5)) )
                   bUpdate = true;

                if ( bUpdate )
                {
                   s_iCurrentKeyFrameIntervalMs = s_iPendingSetKeyframeIntervalMs;
                   int iCurrentFPS = 30;
                   if ( NULL != g_pCurrentModel )
                     iCurrentFPS = g_pCurrentModel->video_link_profiles[g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile].fps;
      
                   int iKeyFrameCountValue = (iCurrentFPS * s_iCurrentKeyFrameIntervalMs) / 1000; 
                   u32 uKeyframeCount = iKeyFrameCountValue;

                   if ( iKeyFrameCountValue < 200 )
                      uKeyframeCount = iKeyFrameCountValue;
                   else
                   {
                      uKeyframeCount = 200 + iKeyFrameCountValue/20;
                      if ( uKeyframeCount > 254 )
                         uKeyframeCount = 254;
                   }
                   log_line("[KeyFrame] Sending keyframe count %d to the video capture program (for current fps %d and keyframe interval: %d ms)", uKeyframeCount, iCurrentFPS, s_iCurrentKeyFrameIntervalMs);
                   send_control_message_to_raspivid(RASPIVID_COMMAND_ID_KEYFRAME, uKeyframeCount);
                }
             }
          }

          g_VideoInfoStats.uTmpKeframeDeltaFrames++;
          if ( iFoundNALVideoFrame == 5 )
          {
             g_VideoInfoStats.uTmpKeframeDeltaFrames = 0; 
          }

          s_uLastVideoFrameTime = g_TimeNow;
          g_VideoInfoStats.uTmpCurrentFrameSize = countRead-iFoundPosition;
      }
      else
         g_VideoInfoStats.uTmpCurrentFrameSize += countRead;
   }

   s_lCountBytesVideoIn += countRead;

   s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition += countRead;

   if ( s_BlocksTxBuffers[s_currentReadBufferIndex].packetsInfo[s_currentReadBlockPacketIndex].currentReadPosition < s_BlocksTxBuffers[s_currentReadBufferIndex].video_data_length )
      return false;
   return _onNewCompletePacketReadFromInput();
}

void process_data_tx_video_signal_encoding_changed()
{
   //log_line("TXVideo: Received request to update local encode parameters.");
   s_bPendingEncodingSwitch = true;
}

void process_data_tx_video_signal_model_changed()
{
   //log_line("TXVideo: Received model changed notification");
   s_bPendingEncodingSwitch = true;
}

void process_data_tx_video_pause_tx()
{
   s_bPauseVideoPacketsTX = true;
}

void process_data_tx_video_resume_tx()
{
   s_bPauseVideoPacketsTX = false;
}

void process_data_tx_video_reset_retransmissions_history_info()
{
   memset((u8*)&g_PHTE_Retransmissions, 0, sizeof(g_PHTE_Retransmissions));

   memset((u8*)&s_listRetransmissionsSegmentsInfo, 0, sizeof(type_retransmissions_history_info) * MAX_HISTORY_RETRANSMISSION_INFO);
   s_iCountRetransmissionsSegmentsInfo = 0;
   s_iCountLastRetransmissionsRequestsTimes = 0;
}