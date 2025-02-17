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
#include "../base/shared_mem.h"
#include "../common/string_utils.h"

#include "video_link_stats_overwrites.h"
#include "video_link_check_bitrate.h"
#include "processor_tx_video.h"
#include "utils_vehicle.h"
#include "packets_utils.h"
#include "timers.h"
#include "shared_vars.h"

u32 s_uTimeLastH264QuantizationCheck =0;
u32 s_uTimeLastQuantizationChangeTime = 0;
u8 s_uLastQuantizationSet = 0;
u8 s_uQuantizationOverflowValue = 0;

void video_link_set_last_quantization_set(u8 uQuantValue)
{
   s_uLastQuantizationSet = uQuantValue;
   s_uTimeLastQuantizationChangeTime = g_TimeNow;
}

void video_link_set_fixed_quantization_values(u8 uQuantValue)
{
   g_SM_VideoLinkStats.overwrites.currentH264QUantization = uQuantValue;
   video_link_set_last_quantization_set(g_SM_VideoLinkStats.overwrites.currentH264QUantization);
   send_control_message_to_raspivid( RASPIVID_COMMAND_ID_QUANTIZATION_INIT, g_SM_VideoLinkStats.overwrites.currentH264QUantization );
   send_control_message_to_raspivid( RASPIVID_COMMAND_ID_QUANTIZATION_MIN, g_SM_VideoLinkStats.overwrites.currentH264QUantization );
   s_uLastQuantizationSet = uQuantValue;
   s_uTimeLastQuantizationChangeTime = g_TimeNow;
}

u8 video_link_get_oveflow_quantization_value()
{
   return s_uQuantizationOverflowValue;
}

void video_link_reset_overflow_quantization_value()
{
   s_uQuantizationOverflowValue = 0;
}

int video_link_get_default_quantization_for_videobitrate(u32 uVideoBitRate)
{
   if ( uVideoBitRate < 2000000 )
      return 40;
   else if ( uVideoBitRate < 5000000 )
      return 30;
   else
      return 20;
}

void video_link_quantization_shift(int iDelta)
{
   if (!((g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags) & ENCODING_EXTRA_FLAG_ENABLE_VIDEO_ADAPTIVE_QUANTIZATION) )
   {
      if ( 0 != g_SM_VideoLinkStats.overwrites.currentH264QUantization )
         video_link_set_fixed_quantization_values(0);
      return;
   }
   
   if ( 0 == g_SM_VideoLinkStats.overwrites.currentH264QUantization )
      g_SM_VideoLinkStats.overwrites.currentH264QUantization = video_link_get_default_quantization_for_videobitrate(g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate);
   
   g_SM_VideoLinkStats.overwrites.currentH264QUantization += iDelta;

   if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization > 44 )
      g_SM_VideoLinkStats.overwrites.currentH264QUantization = 44;
   
   if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization < 4 )
      g_SM_VideoLinkStats.overwrites.currentH264QUantization = 4;
   
   if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization == s_uLastQuantizationSet )
      return;
   
   s_uLastQuantizationSet = g_SM_VideoLinkStats.overwrites.currentH264QUantization;
   s_uTimeLastQuantizationChangeTime = g_TimeNow;
   send_control_message_to_raspivid(RASPIVID_COMMAND_ID_QUANTIZATION_MIN, g_SM_VideoLinkStats.overwrites.currentH264QUantization);
}


void video_link_check_adjust_quantization_for_overload_periodic_loop()
{
   if ( (g_pCurrentModel == NULL) || (! g_pCurrentModel->hasCamera()) )
      return;
   if ( g_bVideoPaused )
      return;
   if ( (0 == g_TimeStartRaspiVid) || (g_TimeNow < g_TimeStartRaspiVid + 4000) )
      return;
   
   if ( g_TimeNow < s_uTimeLastQuantizationChangeTime + 200 )
      return;

   if ( g_TimeNow < g_TimeLastVideoProfileChanged + 500 )
      return;

   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH )
   if ( g_TimeNow < s_uTimeLastH264QuantizationCheck + 100 )
      return;

   if ( ! (g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH ) )
   if ( g_TimeNow < s_uTimeLastH264QuantizationCheck + 200 )
      return;
   
   s_uTimeLastH264QuantizationCheck = g_TimeNow;

   u32 uTotalVideoBitRateAvgFast = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverageLastMs(250);
   u32 uTotalVideoBitRateAvgSlow = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverageLastMs(1000);
   
   u32 uVideoBitRateAvgFast = g_pProcessorTxVideo->getCurrentVideoBitrateAverageLastMs(250);
   u32 uVideoBitRateAvgSlow = g_pProcessorTxVideo->getCurrentVideoBitrateAverageLastMs(1000);
   
   // This in Mbps. 1 = 1 Mbps
   u32 uMinVideoBitrateTxUsed = (u32) get_last_tx_video_datarate_mbps();
   if ( uMinVideoBitrateTxUsed < 2 )
   {
      log_softerror_and_alarm("VideoAdaptiveBitrate: Invalid minimum tx video radio datarate: %d Mbps. Reseting to 2 Mbps.", uMinVideoBitrateTxUsed);
      uMinVideoBitrateTxUsed = 2;
      send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_TX_BITRATE_TOO_LOW, 0, 0, 2); 
   }
   
   u32 uMaxVideoBitrateThreshold = (uMinVideoBitrateTxUsed * 1000 * 10) * DEFAULT_VIDEO_LINK_MAX_LOAD_PERCENT;

   /*
   log_line(DEBUG bit rate avg500/tot500: %.1f / %.1f  avg1000/tot1000: %.1f / %.1f,  min/max: %u / %.1f, set: %.1f",
      (float)uVideoBitRateAvgFast/1000.0/1000.0, (float)uTotalVideoBitRateAvgFast/1000.0/1000.0,
      (float)uVideoBitRateAvgSlow/1000.0/1000.0, (float)uTotalVideoBitRateAvgSlow/1000.0/1000.0,
      uMinVideoBitrateTxUsed, (float)uMaxVideoBitrateThreshold/1000.0/1000.0,
      (float)g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate/1000.0/1000.0 );
   */

   // Auto quantization is turned off ?
   if (!((g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags) & ENCODING_EXTRA_FLAG_ENABLE_VIDEO_ADAPTIVE_QUANTIZATION) )
   {
      if ( 0 != g_SM_VideoLinkStats.overwrites.currentH264QUantization )
         video_link_set_fixed_quantization_values(0);
      return;
   }

   // Adaptive video is turned off
   if (!((g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags) & ENCODING_EXTRA_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK_PARAMS) )
   {
      int dTime = 1000;
      bool bOverflow = false;

      if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH )
      {
         dTime = 250;
         if ( uVideoBitRateAvgFast > g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.1 )
            bOverflow = true;
         if ( uTotalVideoBitRateAvgFast > uMaxVideoBitrateThreshold*1.05 )
            bOverflow = true;
      }
      else
      {
         dTime = 500;
         if ( uVideoBitRateAvgSlow > g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.1 )
            bOverflow = true;
         if ( uTotalVideoBitRateAvgSlow > uMaxVideoBitrateThreshold*1.1 )
            bOverflow = true;
      }

      if ( bOverflow )
      if ( g_TimeNow >= s_uTimeLastQuantizationChangeTime + dTime )
      {
         if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization != 0 )
         if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization != 0xFF )
         if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization > s_uQuantizationOverflowValue )
            s_uQuantizationOverflowValue = g_SM_VideoLinkStats.overwrites.currentH264QUantization;
         video_link_quantization_shift(1);
         return;
      }

      if ( g_TimeNow >= s_uTimeLastQuantizationChangeTime + dTime*4 )
      {
         if ( uVideoBitRateAvgSlow < g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate*0.9 )
         //if ( g_SM_VideoLinkStats.overwrites.currentH264QUantization > s_uQuantizationOverflowValue+1 )
            video_link_quantization_shift(-1);
      }
      return;
   }

   // Check first if we must quickly shift bitrate down right now fast

   bool bOverflow = false;
   int dTime = 400;
   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH )
   {
      dTime = 250;
      if ( uVideoBitRateAvgFast >= g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.1 )
         bOverflow = true;
      if ( uTotalVideoBitRateAvgFast > uMaxVideoBitrateThreshold*1.1 )
         bOverflow = true;
   }
   else
   {
      dTime = 500;
      if ( uVideoBitRateAvgSlow >= g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.1 )
         bOverflow = true;    
      if ( uTotalVideoBitRateAvgSlow > uMaxVideoBitrateThreshold*1.1 )
         bOverflow = true;
   }

   if ( bOverflow )
   if ( g_TimeNow >= s_uTimeLastQuantizationChangeTime + dTime )  
   {
      video_link_quantization_shift(2);
      //if ( g_pCurrentModel->bDeveloperMode )
      //   log_line("[DEV] Auto bitrate: Must qickly shift video bitrate down. Current video bitrate: %.1f Mbps, total video data: %.1f Mbps. Current set video bitrate: %.1f Mbps. Current Q: %d.", uVideoBitRate/1000.0/1000.0,uTotalSentVideoBitRate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentH264QUantization);
      return;
   }

   // Must shift bitrate down slower to target

   bOverflow = false;
   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH )
   {
      dTime = 250;
      if ( uVideoBitRateAvgFast >= g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.05 )
         bOverflow = true;
      if ( uTotalVideoBitRateAvgFast >= uMaxVideoBitrateThreshold * 1.05)
         bOverflow = true;
   }
   else
   {
      dTime = 500;
      if ( uVideoBitRateAvgSlow >= g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate * 1.05 )
         bOverflow = true;    
      if ( uTotalVideoBitRateAvgSlow >= uMaxVideoBitrateThreshold * 1.05)
         bOverflow = true;
   }

   if ( bOverflow )
   if ( g_TimeNow >= s_uTimeLastQuantizationChangeTime + dTime )  
   {
      video_link_quantization_shift(1);
      //if ( g_pCurrentModel->bDeveloperMode )
      //   log_line("[DEV] Auto bitrate: Must shift video bitrate down. Current video bitrate: %.1f Mbps, total video data: %.1f Mbps. Current set video bitrate: %.1f Mbps. Current Q: %d.", uVideoBitRate/1000.0/1000.0,uTotalSentVideoBitRate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentH264QUantization);
     return;
   }

   // Must shift up to target

   bool bUndeflow = false;
   if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].encoding_extra_flags & ENCODING_EXTRA_FLAG_VIDEO_ADAPTIVE_QUANTIZATION_STRENGTH_HIGH )
   {
      dTime = 500;
      if ( uVideoBitRateAvgFast < g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate*0.9 )
         bUndeflow = true;
   }
   else
   {
      dTime = 1000;
      if ( uVideoBitRateAvgSlow < g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate*0.9 )
         bUndeflow = true;    
   }

   if ( bUndeflow )
   if ( g_TimeNow >= s_uTimeLastQuantizationChangeTime + dTime )  
   {
      video_link_quantization_shift(-1);
      //if ( g_pCurrentModel->bDeveloperMode )
      //   log_line("[DEV] Auto bitrate: Must shift video bitrate up. Current video bitrate: %.1f Mbps, total video data: %.1f Mbps. Current set video bitrate: %.1f Mbps. Current Q: %d.", uVideoBitRate/1000.0/1000.0,uTotalSentVideoBitRate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentSetVideoBitrate/1000.0/1000.0, g_SM_VideoLinkStats.overwrites.currentH264QUantization);
      return;
   }
}

void video_link_check_adjust_bitrate_for_overload()
{
   if ( (0 == g_TimeStartRaspiVid) || (g_TimeNow < g_TimeStartRaspiVid + 5000) )
      return;

   if ( g_pCurrentModel->uDeveloperFlags & DEVELOPER_FLAGS_BIT_DISABLE_VIDEO_OVERLOAD_CHECK )
      return;
      
   static u32 sl_uTimeLastVideoOverloadCheck = 0;

   if ( g_TimeNow < sl_uTimeLastVideoOverloadCheck+50 )
      return;

   sl_uTimeLastVideoOverloadCheck = g_TimeNow;

   u32 uTotalSentVideoBitRateAverage = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverage();
   u32 uTotalSentVideoBitRate = g_pProcessorTxVideo->getCurrentTotalVideoBitrateAverageLastMs(500);
   u32 uMaxTxTime = DEFAULT_TX_TIME_OVERLOAD;
   if ( (g_pCurrentModel->board_type == BOARD_TYPE_PIZERO) || (g_pCurrentModel->board_type == BOARD_TYPE_PIZEROW) )
         uMaxTxTime += 200;
   uMaxTxTime += (uTotalSentVideoBitRate/1000/1000)*20;

   if ( g_pCurrentModel->video_params.uVideoExtraFlags & VIDEO_FLAG_IGNORE_TX_SPIKES )
      uMaxTxTime = 900;

   // This is in Mbps. 1 = 1 Mbps
   int iMinVideoRadioTxBitrateUsed = get_last_tx_video_datarate_mbps();
   if ( iMinVideoRadioTxBitrateUsed < 2 )
   {
      log_softerror_and_alarm("VideoAdaptiveBitrate: Invalid minimum tx video radio datarate: %d Mbps. Reseting to 2 Mbps.", iMinVideoRadioTxBitrateUsed);
      iMinVideoRadioTxBitrateUsed = 2;
      send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_TX_BITRATE_TOO_LOW, 0, 0, 2); 
   }

   int iMaxAllowedThreshold = (iMinVideoRadioTxBitrateUsed * 1000 * 10) * DEFAULT_VIDEO_LINK_MAX_LOAD_PERCENT;
   int iMaxAllowedThresholdFast = (iMinVideoRadioTxBitrateUsed * 1000 * 10) * 85;
   
   // Check for TX time overload or data rate overload and lower video bitrate if needed

   bool bIsDataOverloadCondition = false;
   bool bIsTxOverloadCondition = false;
   bool bIsLocalOverloadCondition = false;
   bool bIsDataRateOverloadCondition = false;

   if ( g_uTotalVideoRadioTxTimePerSec > uMaxTxTime )
   {
      bIsDataOverloadCondition = true;
      bIsTxOverloadCondition = true;
   }

   if ( uTotalSentVideoBitRate > (u32)iMaxAllowedThresholdFast )
   if ( uTotalSentVideoBitRateAverage > (u32)iMaxAllowedThreshold )
   {
      bIsDataOverloadCondition = true;
      bIsDataRateOverloadCondition = true;
   }

   if ( g_uTimeLastVideoTxOverload > g_TimeNow - 4000 )
   {
      bIsDataOverloadCondition = true;
      bIsLocalOverloadCondition = true;
   }

   if ( ! bIsDataOverloadCondition )
   {
      // There is no current overload condition
      // Increase the video bitrate back to video profile target rate
   
      if ( g_TimeNow > g_TimeLastOverwriteBitrateDownOnTxOverload + 1000 )
      if ( g_TimeNow > g_TimeLastOverwriteBitrateUpOnTxOverload + 1000 )
      if ( (g_uTotalRadioTxTimePerSec < uMaxTxTime/3) && (uTotalSentVideoBitRateAverage < (u32)iMaxAllowedThreshold) )
      if ( video_stats_overwrites_decrease_videobitrate_overwrite() )
         g_TimeLastOverwriteBitrateUpOnTxOverload = g_TimeNow;

      return;
   }

   // Is in overload condition. Decrease target video profile bitrate

   if ( g_TimeNow >= g_SM_VideoLinkStats.timeLastAdaptiveParamsChangeDown + 200 )
   if ( g_TimeNow >= g_TimeLastOverwriteBitrateDownOnTxOverload + 250 )
   if ( g_TimeNow >= g_TimeLastOverwriteBitrateUpOnTxOverload + 250 )
   if ( video_stats_overwrites_increase_videobitrate_overwrite(uTotalSentVideoBitRate) )
   {
      g_uTimeLastVideoTxOverload = 0;
      g_TimeLastOverwriteBitrateDownOnTxOverload = g_TimeNow;

      u32 uParam = 0;
      if ( bIsDataRateOverloadCondition )
      {
         /*
         log_line("DEBUG overload, sent %u / %u bps, safe is %d bps, current profile: %s, current level %d, last profile changed down/up: %u/%u ms ago, last adaptive changed down/up: %u/%u ms ago",
            uTotalSentVideoBitRateAverage, uTotalSentVideoBitRate, iMaxAllowedThreshold,
            str_get_video_profile_name(g_SM_VideoLinkStats.overwrites.currentVideoLinkProfile), g_SM_VideoLinkStats.overwrites.currentProfileShiftLevel,
            g_TimeNow - g_SM_VideoLinkStats.timeLastProfileChangeDown,
            g_TimeNow - g_SM_VideoLinkStats.timeLastProfileChangeDown,
            g_TimeNow - g_SM_VideoLinkStats.timeLastAdaptiveParamsChangeDown,
            g_TimeNow - g_SM_VideoLinkStats.timeLastAdaptiveParamsChangeUp
            );
         */
         u32 bitrateTotal = uTotalSentVideoBitRateAverage;
         bitrateTotal /= 1000;
         u32 bitrateBar = iMaxAllowedThreshold;
         bitrateBar /= 1000;
         uParam = (bitrateTotal & 0xFFFF) | (bitrateBar << 16);
         send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_DATA_OVERLOAD, uParam, 0, 2);
      }
      if ( bIsTxOverloadCondition )
      {
         uParam = (g_uTotalRadioTxTimePerSec & 0xFFFF) | ((uMaxTxTime & 0xFFFF) << 16);
         send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_TX_OVERLOAD, uParam, 0, 2);
      }
      if ( bIsLocalOverloadCondition )
      {
         uParam = 0;
         send_alarm_to_controller(ALARM_ID_VEHICLE_VIDEO_TX_OVERLOAD, uParam, 1, 2);
      }
   }
}

