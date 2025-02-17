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
#include "../base/models.h"
#include "../base/models_list.h"
#include "../common/string_utils.h"
#include "../common/relay_utils.h"
#include "../radio/radiopacketsqueue.h"

#include "shared_vars.h"
#include "timers.h"

#include "video_link_adaptive.h"
#include "processor_rx_video.h"

extern t_packet_queue s_QueueRadioPackets;

bool s_bReceivedKeyFrameFromVideoStream = false;

void video_link_keyframe_init(u32 uVehicleId)
{
   Model* pModel = findModelWithId(uVehicleId);
   if ( NULL == pModel )
      return;

   int iInfoIndex = getVehicleRuntimeIndex(uVehicleId);
   if ( -1 == iInfoIndex )
      return;

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = -1;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastAcknowledgedKeyFrameMs = -1;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount = 0;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame = 0;
   s_bReceivedKeyFrameFromVideoStream = false;
   log_line("Initialized adaptive video keyframe info, default start keyframe interval: %d ms, for VID %u (name: %s)", g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[0].iLastRequestedKeyFrameMs, uVehicleId, pModel->getLongName());
}

void video_link_keyframe_set_intial_received_level(u32 uVehicleId, int iReceivedKeyframeMs)
{
   Model* pModel = findModelWithId(uVehicleId);
   if ( NULL == pModel )
      return;

   int iInfoIndex = getVehicleRuntimeIndex(uVehicleId);
   if ( -1 == iInfoIndex )
      return;

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = iReceivedKeyframeMs;
   
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs < 0 )
       g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = pModel->video_params.uMaxAutoKeyframeIntervalMs;

   log_line("Initial keyframe received from the video stream: %d ms", g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs); 
   
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastAcknowledgedKeyFrameMs = g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount = 0;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame = g_TimeNow;
   s_bReceivedKeyFrameFromVideoStream = true;
   log_line("Done setting initial keyframe state for VID %u based on received keframe interval of %d ms from video stream.", uVehicleId, iReceivedKeyframeMs);

}

void video_link_keyframe_set_current_level_to_request(u32 uVehicleId, int iKeyframeMs)
{
   Model* pModel = findModelWithId(uVehicleId);
   if ( NULL == pModel )
      return;

   int iInfoIndex = getVehicleRuntimeIndex(uVehicleId);

   if ( -1 == iInfoIndex )
      return;

   s_bReceivedKeyFrameFromVideoStream = true;
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs == iKeyframeMs )
      return;

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = iKeyframeMs;
   
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs < 0 )
       g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = pModel->video_params.uMaxAutoKeyframeIntervalMs;

   log_line("Set current keyframe interval to request from VID %u to %d ms", uVehicleId, g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs);
   
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount = 0;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame = g_TimeNow;
}

void _video_link_keyframe_check_send_to_vehicle(u32 uVehicleId)
{
   Model* pModel = findModelWithId(uVehicleId);
   if ( NULL == pModel )
      return;

   int iInfoIndex = getVehicleRuntimeIndex(uVehicleId);
   if ( -1 == iInfoIndex )
      return;
   if ( ! g_SM_RouterVehiclesRuntimeInfo.iPairingDone[iInfoIndex] )
      return;
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs == -1 )
      return;

   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs == g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastAcknowledgedKeyFrameMs )
      return;

   if ( g_TimeNow < g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame + g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uUpdateInterval + g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount*10 )
     return;

   t_packet_header PH;
   radio_packet_init(&PH, PACKET_COMPONENT_VIDEO, PACKET_TYPE_VIDEO_SWITCH_VIDEO_KEYFRAME_TO_VALUE, STREAM_ID_DATA);
   PH.vehicle_id_src = g_uControllerId;
   PH.vehicle_id_dest = uVehicleId;
   PH.total_length = sizeof(t_packet_header) + sizeof(u32) + sizeof(u8);

   u32 uKeyframeMs = (u32) g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs;
   u8 uVideoStreamIndex = 0;
   u8 packet[MAX_PACKET_TOTAL_SIZE];
   memcpy(packet, (u8*)&PH, sizeof(t_packet_header));
   memcpy(packet+sizeof(t_packet_header), (u8*)&uKeyframeMs, sizeof(u32));
   memcpy(packet+sizeof(t_packet_header) + sizeof(u32), (u8*)&uVideoStreamIndex, sizeof(u8));
   packets_queue_inject_packet_first(&s_QueueRadioPackets, packet);

   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount == 0 )
     log_line("Request keyframe %u ms from VID %u (previous requested keyframe was %d ms, last ack keyframe was %d ms)",
        uKeyframeMs, uVehicleId,
        g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs,
        g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastAcknowledgedKeyFrameMs );
   else
     log_line("Request keyframe %u ms (retry %d) from VID %u (previous requested keyframe was %d ms, last ack keyframe was %d ms)",
        uKeyframeMs, g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount,
        uVehicleId,
        g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs,
        g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastAcknowledgedKeyFrameMs );

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame = g_TimeNow;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount++;
   if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount > 100 )
      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount = 20;
}

void _video_link_keyframe_request_new_keyframe_interval(u32 uVehicleId, int iIntervalMs)
{
   if ( iIntervalMs < DEFAULT_VIDEO_MIN_AUTO_KEYFRAME_INTERVAL )
      iIntervalMs = DEFAULT_VIDEO_MIN_AUTO_KEYFRAME_INTERVAL;
   if ( iIntervalMs > 20000 )
      iIntervalMs = 20000;

   Model* pModel = findModelWithId(uVehicleId);
   if ( NULL == pModel )
      return;

   if ( iIntervalMs > (int)pModel->video_params.uMaxAutoKeyframeIntervalMs )
      iIntervalMs = pModel->video_params.uMaxAutoKeyframeIntervalMs;

   int iInfoIndex = getVehicleRuntimeIndex(uVehicleId);
   if ( -1 == iInfoIndex )
      return;

   if ( iIntervalMs == g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs )
      return;

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMs = iIntervalMs;
   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].iLastRequestedKeyFrameMsRetryCount = 0;

   g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uTimeLastRequestedKeyFrame = g_TimeNow - g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[iInfoIndex].uUpdateInterval - 1;
}

void video_link_keyframe_periodic_loop()
{
   if ( g_bSearching || NULL == g_pCurrentModel || g_bUpdateInProgress )
      return;
   if ( ! s_bReceivedKeyFrameFromVideoStream )
      return;
     
   if ( g_bIsControllerLinkToVehicleLost || g_bIsVehicleLinkToControllerLost )
      return;
     
   u32 s_uTimeLastKeyFrameCheckVehicles = 0;


   // Send/Resend keyframe to vehicle if needed
      
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      Model* pModel = findModelWithId(g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i]);
      if ( (NULL == pModel) || (pModel->is_spectator) )
         continue;
      if ( ! g_SM_RouterVehiclesRuntimeInfo.iPairingDone[i] )
         continue;

      _video_link_keyframe_check_send_to_vehicle(pModel->vehicle_id);
   }

   // Do adjustments only once ever 100 ms
   if ( g_TimeNow < s_uTimeLastKeyFrameCheckVehicles + 100 )
       return;

   s_uTimeLastKeyFrameCheckVehicles = g_TimeNow;

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      Model* pModel = findModelWithId(g_SM_RouterVehiclesRuntimeInfo.uVehiclesIds[i]);
      if ( (NULL == pModel) || (pModel->is_spectator) )
         continue;

      if ( ! g_SM_RouterVehiclesRuntimeInfo.iPairingDone[i] )
         continue;
        
      // To fix
      int iCurrentVideoProfile = 0;
      for( int k=0; k<MAX_VIDEO_PROCESSORS; k++ )
      {
         if ( NULL == g_pVideoProcessorRxList[k] )
            break;
         iCurrentVideoProfile = g_pVideoProcessorRxList[k]->getCurrentlyReceivedVideoProfile();
      }
      if ( iCurrentVideoProfile == -1 )
         iCurrentVideoProfile = pModel->video_params.user_selected_video_link_profile;
   
      int iCurrentProfileKeyFrameMs = pModel->video_link_profiles[iCurrentVideoProfile].keyframe_ms;

      int iCurrentFPS = 0;
      // To fix
      for( int k=0; k<MAX_VIDEO_PROCESSORS; k++ )
      {
         if ( NULL == g_pVideoProcessorRxList[k] )
            break;
         iCurrentFPS = g_pVideoProcessorRxList[k]->getCurrentlyReceivedVideoFPS();
      }

      if ( iCurrentFPS < 1 )
         iCurrentFPS = pModel->video_link_profiles[iCurrentVideoProfile].fps;

      _video_link_keyframe_check_send_to_vehicle(pModel->vehicle_id);

      // If relaying is enabled on this vehicle and video from this vehicle is not visible, do not adjust anything (will default to relay keyframe interval)
   
      if ( ! relay_controller_must_display_video_from(g_pCurrentModel, pModel->vehicle_id) )
         continue;

      // User set a fixed keyframe value? Then do nothing (just set it if not set);

      if ( iCurrentProfileKeyFrameMs > 0 )
      {
         if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs != iCurrentProfileKeyFrameMs )
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs = iCurrentProfileKeyFrameMs;

         if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs == g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastAcknowledgedKeyFrameMs )
            continue;

         _video_link_keyframe_check_send_to_vehicle(pModel->vehicle_id);
         continue;
      }

      if ( g_TimeNow < g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastRequestedKeyFrame + 50 )
         continue;

      float fParamsChangeStrength = (float)pModel->video_params.videoAdjustmentStrength / 10.0;
      
      // Max intervals is MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS * 20 ms = 4 seconds
      // Minus one because the current index is still processing/invalid

      int iIntervalsToCheckDown = 3 + (1.0-0.7*fParamsChangeStrength) * MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS;
      int iIntervalsToCheckUp = iIntervalsToCheckDown + 20 + 40 * (1.0-fParamsChangeStrength);
      if ( iIntervalsToCheckDown >= MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS-1 )
         iIntervalsToCheckDown = MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS-2;
      if ( iIntervalsToCheckUp >= MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS-1 )
         iIntervalsToCheckUp = MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS-2;
      
      int iThresholdUp = 1 + (0.1 + 0.08*(0.9-fParamsChangeStrength)) * iIntervalsToCheckUp;
      int iThresholdDownLevel1 = 1 + (0.01 + 0.08 * (1.0-fParamsChangeStrength) )* iIntervalsToCheckDown;
      int iThresholdDownLevel2 = 1 + (0.04 + 0.16 * (1.0-fParamsChangeStrength) )* iIntervalsToCheckDown;
      int iThresholdDownLevel4 = (0.03 + 0.2*(1.0-fParamsChangeStrength)) * iIntervalsToCheckDown;
      int iThresholdDownLevel5 = (0.03 + 0.1*(1.0-fParamsChangeStrength))* iIntervalsToCheckDown;


      //if ( (int)(g_TimeNow - g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftUp) / (int)g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uUpdateInterval < iIntervalsToCheckUp )
      //   iIntervalsToCheckUp = (g_TimeNow - g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftUp) / g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uUpdateInterval;

      if ( (int)(g_TimeNow - g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown) / (int)g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uUpdateInterval < iIntervalsToCheckDown )
         iIntervalsToCheckDown = (g_TimeNow - g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown) / g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uUpdateInterval;
      
      if ( iIntervalsToCheckDown < 1 )
         iIntervalsToCheckDown = 1;
      if ( iIntervalsToCheckUp < iIntervalsToCheckDown )
         iIntervalsToCheckUp = iIntervalsToCheckDown;

      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsKeyFrameMs1 = ((u16)iIntervalsToCheckDown) | (((u16)iIntervalsToCheckUp)<<16);
      int iCountReconstructedDown = 0;
      int iCountRetransmissionsDown = 0;
      int iCountReRetransmissionsDown = 0;
      int iCountMissingSegmentsDown = 0;

      int iCountReconstructedUp = 0;
      int iCountRetransmissionsUp = 0;
      int iCountReRetransmissionsUp = 0;
      int iCountMissingSegmentsUp = 0;

      int iIndex = g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uCurrentIntervalIndex - 1;
      if ( iIndex < 0 )
         iIndex = MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS;

      for( int k=0; k<iIntervalsToCheckDown; k++ )
      {
         iIndex--;
         if ( iIndex < 0 )
            iIndex = MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS;
         iCountReconstructedDown += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsOuputRecontructedVideoPackets[iIndex]!=0)?1:0;
         iCountRetransmissionsDown += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsRequestedRetransmissions[iIndex]!=0)?1:0;
         iCountReRetransmissionsDown += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsRetriedRetransmissions[iIndex]!=0)?1:0;
         iCountMissingSegmentsDown += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsMissingVideoPackets[iIndex]!=0)?1:0;
      }

      iCountReconstructedUp = iCountReconstructedDown;
      iCountRetransmissionsUp = iCountRetransmissionsDown;
      iCountReRetransmissionsUp = iCountReRetransmissionsDown;
      iCountMissingSegmentsUp = iCountMissingSegmentsDown;

      for( int k=iIntervalsToCheckDown; k<iIntervalsToCheckUp; k++ )
      {
         iIndex--;
         if ( iIndex < 0 )
            iIndex = MAX_CONTROLLER_ADAPTIVE_VIDEO_INFO_INTERVALS;
         iCountReconstructedUp += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsOuputRecontructedVideoPackets[iIndex]!=0)?1:0;
         iCountRetransmissionsUp += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsRequestedRetransmissions[iIndex]!=0)?1:0;
         iCountReRetransmissionsUp += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsRetriedRetransmissions[iIndex]!=0)?1:0;
         iCountMissingSegmentsUp += (g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsMissingVideoPackets[iIndex]!=0)?1:0;
      }

      int iNewKeyFrameIntervalMs = -1;

      // Check move up

      if ( g_TimeNow > g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastRequestedKeyFrame + 900 )
      if ( iCountReconstructedUp < iThresholdUp )
      if ( iCountRetransmissionsUp < iThresholdUp )
      if ( iCountReRetransmissionsUp < iThresholdUp )
      if ( iCountMissingSegmentsUp < iThresholdUp )
      {
         iNewKeyFrameIntervalMs = g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs * 4 / 3;
         if ( iNewKeyFrameIntervalMs > (int)pModel->video_params.uMaxAutoKeyframeIntervalMs )
            iNewKeyFrameIntervalMs = pModel->video_params.uMaxAutoKeyframeIntervalMs;
         if ( iNewKeyFrameIntervalMs != g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs )
         {
            _video_link_keyframe_request_new_keyframe_interval(pModel->vehicle_id, iNewKeyFrameIntervalMs);
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftUp = g_TimeNow;
         }
      }

      // Check move down

      if ( iCountReconstructedDown > iThresholdDownLevel1 )
      {
         if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs > (int)pModel->video_params.uMaxAutoKeyframeIntervalMs / 2)
         {
            iNewKeyFrameIntervalMs = pModel->video_params.uMaxAutoKeyframeIntervalMs / 2;
            _video_link_keyframe_request_new_keyframe_interval(pModel->vehicle_id, iNewKeyFrameIntervalMs);
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown = g_TimeNow;
         }
      }

      if ( iCountReconstructedDown > iThresholdDownLevel2 )
      {
         if ( g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].iLastRequestedKeyFrameMs > (int)pModel->video_params.uMaxAutoKeyframeIntervalMs / 4)
         {
            iNewKeyFrameIntervalMs = iCurrentFPS * pModel->video_params.uMaxAutoKeyframeIntervalMs / 4;
            _video_link_keyframe_request_new_keyframe_interval(pModel->vehicle_id, iNewKeyFrameIntervalMs);
            g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown = g_TimeNow;
         }
      }

      if ( (iCountRetransmissionsDown > iThresholdDownLevel4) ||
           (iCountMissingSegmentsDown > iThresholdDownLevel4) )
      {
         iNewKeyFrameIntervalMs = 2 * DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
         _video_link_keyframe_request_new_keyframe_interval(pModel->vehicle_id, iNewKeyFrameIntervalMs);
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown = g_TimeNow;
      }

      if ( iCountReRetransmissionsDown > iThresholdDownLevel5 )
      {
         iNewKeyFrameIntervalMs = DEFAULT_VIDEO_AUTO_KEYFRAME_INTERVAL;
         _video_link_keyframe_request_new_keyframe_interval(pModel->vehicle_id, iNewKeyFrameIntervalMs);
         g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uTimeLastKFShiftDown = g_TimeNow;
      }

      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsKeyFrameMs2 = ((u8)iThresholdUp) | (((u8)iThresholdDownLevel1)<<8) | (((u8)iThresholdDownLevel2)<<16);
      g_SM_RouterVehiclesRuntimeInfo.vehicles_adaptive_video[i].uIntervalsKeyFrameMs3 = ((u8)iThresholdDownLevel4) | (((u8)iThresholdDownLevel5)<<8);
   }
}

