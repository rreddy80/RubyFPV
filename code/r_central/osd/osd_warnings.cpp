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

#include "osd_common.h"
#include "osd_warnings.h"
#include "osd.h"

#include "../colors.h"
#include "../../renderer/render_engine.h"
#include "../shared_vars.h"
#include "../pairing.h"
#include "../link_watch.h"
#include "../timers.h"
#include "../warnings.h"
#include "osd_common.h"

extern bool s_bDebugOSDShowAll;

void osd_warnings_reset()
{
   g_bHasVideoDataOverloadAlarm = false;
   g_bHasVideoTxOverloadAlarm = false;
}

void osd_warnings_render()
{
   Model* pActiveModel = osd_get_current_data_source_vehicle_model();
   u32 uActiveVehicleId = osd_get_current_data_source_vehicle_id();
   
   int iCountVehicles = 0;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_VehiclesRuntimeInfo[i].uVehicleId != 0 )
         iCountVehicles++;
   }

   shared_mem_video_stream_stats* pVDS = NULL;
   for( int i=0; i<MAX_VIDEO_PROCESSORS; i++ )
   {
      if ( g_SM_VideoDecodeStats.video_streams[i].uVehicleId == uActiveVehicleId )
      {
         pVDS = &(g_SM_VideoDecodeStats.video_streams[i]);
         break;
      }
   }
   if ( (NULL == pVDS) || (NULL == pActiveModel) )
      return; 

   if ( ! g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].bGotFCTelemetry )
      return;

   if ( g_bHasVideoDataOverloadAlarm )
   if ( !(pVDS->encoding_extra_flags & ENCODING_EXTRA_FLAG_STATUS_ON_LOWER_BITRATE) )
   {
      g_bHasVideoDataOverloadAlarm = false;
      if ( NULL != g_pPopupVideoOverloadAlarm )
      {
         if ( popups_has_popup(g_pPopupVideoOverloadAlarm) )
            popups_remove(g_pPopupVideoOverloadAlarm);
         g_pPopupVideoOverloadAlarm = NULL;
      }

      warnings_add(0, "Video overload alarm cleared.", g_idIconCamera);
   }

   static u32 s_uLastTimeCheckAlarmFEC = 0;
   static u32 s_uTimeTriggeredAlarmFECOverload = 0;

   if ( g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].bGotRubyTelemetryInfo )
   if ( s_uLastTimeCheckAlarmFEC < g_TimeNow - 200 )
   {
      s_uLastTimeCheckAlarmFEC = g_TimeNow;

      if ( (pVDS->fec_time/1000) >= 300 )
      {
         if ( 0 == s_uTimeTriggeredAlarmFECOverload )
            s_uTimeTriggeredAlarmFECOverload = g_TimeNow;
         else if ( g_TimeNow > s_uTimeTriggeredAlarmFECOverload + 500 )
         {
            //log_line("FEC time too big: %d ms/s", g_SM_VideoDecodeStats.fec_time/1000);
            if ( pActiveModel->osd_params.show_overload_alarm )
               warnings_add_vehicle_overloaded(g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].uVehicleId);
            s_uTimeTriggeredAlarmFECOverload = 0;
         }
      }
      else
         s_uTimeTriggeredAlarmFECOverload = 0;
   }
   /////////////////

   float fAlfaOrg = g_pRenderEngine->getGlobalAlfa();

   float height_text = g_pRenderEngine->textHeight(g_idFontOSDWarnings);
   float height_text_big = g_pRenderEngine->textHeight(g_idFontOSDBig);
   float sizeIcon = height_text*1.1;
   float dyAlarm = height_text*1.8;
   float yAlarm = 1.0 - 0.02*osd_getScaleOSD()-osd_getMarginX() - 2.0*osd_getBarHeight() - height_text;
   float yAlarmIcon = yAlarm;//- height_text*0.1;
   float xAlarmIcon = osd_getMarginX() + 0.007*osd_getScaleOSD();
   float xAlarm = xAlarmIcon + sizeIcon*1.8/g_pRenderEngine->getAspectRatio();
   // Middle of screen important alarms
   float yAlarmsMiddle = 1.0 - 0.05*osd_getScaleOSD()-osd_getMarginY() - 2.0*osd_getBarHeight() - height_text_big;

   if ( NULL != g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel )
   if ( g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].pModel->osd_params.osd_flags2[osd_get_current_layout_index()] & OSD_FLAG2_LAYOUT_LEFT_RIGHT )
   {
      xAlarm += osd_getVerticalBarWidth();
      xAlarmIcon += osd_getVerticalBarWidth();
   }
   float fTextScale = 0.9;

   if ( g_bVideoProcessing || s_bDebugOSDShowAll )
   if ( ( g_TimeNow / 500 ) % 2 )
   {
      osd_set_colors();
      g_pRenderEngine->drawIcon(xAlarmIcon, yAlarmIcon, 1.2*height_text/g_pRenderEngine->getAspectRatio(), 1.2*height_text, g_idIconCamera);
      g_pRenderEngine->drawText(xAlarm, yAlarm, g_idFontOSDWarnings, "Processing video file...");

      yAlarm -= dyAlarm;
      yAlarmIcon -= dyAlarm;
   }

   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( (0 == g_VehiclesRuntimeInfo[i].uVehicleId) || (MAX_U32 == g_VehiclesRuntimeInfo[i].uVehicleId) || (NULL == g_VehiclesRuntimeInfo[i].pModel) )
         continue;

      if ( g_VehiclesRuntimeInfo[i].bGotRubyTelemetryInfo )
      {
         u8 air_flags = g_VehiclesRuntimeInfo[i].headerRubyTelemetryExtended.throttled;
         if ( (0 != (air_flags & 0x0F)) || s_bDebugOSDShowAll )
         if ( ( g_TimeNow / 500 ) % 2 )
         {
            g_pRenderEngine->setColors(get_Color_IconWarning());
            g_pRenderEngine->drawIcon(xAlarmIcon, yAlarmIcon, 1.2*height_text/g_pRenderEngine->getAspectRatio(), 1.2*height_text, g_idIconCPU);
            osd_set_colors();
            char szBuff[128];
            szBuff[0] = 0;
            if ( air_flags & 0b1000 )
            {
               strcat(szBuff, "Vechilce Temperature");
            }
            if ( (air_flags & 0b0100) || s_bDebugOSDShowAll )
            {
               if ( 0 != szBuff[0] )
                  strcat(szBuff, ", ");
               strcat(szBuff, "Vechicle CPU Throttled");
            }
            if ( air_flags & 0b0010 )
            {
               if ( 0 != szBuff[0] )
                  strcat(szBuff, ", ");
               strcat(szBuff, "Vechicle Frequency Capped");
            }
            if ( air_flags & 0b0001 )
            {
               if ( 0 != szBuff[0] )
                  strcat(szBuff, ", ");
               strcat(szBuff, "Vechicle Voltage Too Low");
            }
            g_pRenderEngine->drawText(xAlarm, yAlarm, g_idFontOSDWarnings, szBuff);

            yAlarm -= dyAlarm;
            yAlarmIcon -= dyAlarm;
         }
      }

      bool bShowFCTelemetryAlarm = false;
      if ( g_VehiclesRuntimeInfo[i].pModel->telemetry_params.fc_telemetry_type != TELEMETRY_TYPE_NONE )
      if ( g_VehiclesRuntimeInfo[i].bGotFCTelemetry )
      if ( ! g_VehiclesRuntimeInfo[i].bLinkLost )
      if ( ! g_VehiclesRuntimeInfo[i].bFCTelemetrySourcePresent )
         bShowFCTelemetryAlarm = true;

      if ( s_bDebugOSDShowAll )
         bShowFCTelemetryAlarm = true;

      if ( bShowFCTelemetryAlarm )
      if ( ( g_TimeNow / 500 ) % 2 )
      {
         g_pRenderEngine->setColors(get_Color_IconWarning());
         g_pRenderEngine->drawIcon(xAlarmIcon, yAlarmIcon, 1.2*height_text/g_pRenderEngine->getAspectRatio(), 1.2*height_text, g_idIconCPU);
         osd_set_colors();
         g_pRenderEngine->drawText(xAlarm, yAlarm, g_idFontOSDWarnings, "No telemetry from flight controller");
         yAlarm -= dyAlarm;
         yAlarmIcon -= dyAlarm;
      }

      if ( g_VehiclesRuntimeInfo[i].bRCFailsafeState || s_bDebugOSDShowAll )
      if ( ( g_TimeNow / 500 ) % 2 )
      {
         g_pRenderEngine->setColors(get_Color_IconError());
         g_pRenderEngine->drawIcon(xAlarmIcon, yAlarmIcon, 1.2*height_text/g_pRenderEngine->getAspectRatio(), 1.2*height_text, g_idIconJoystick);
         osd_set_colors();
         g_pRenderEngine->drawText(xAlarm, yAlarm, g_idFontOSDBig, "! RC FAILSAFE !");
         yAlarm -= dyAlarm;
         yAlarmIcon -= dyAlarm;
      }
     
      // Battery voltage alarm is on the middle of the screen
      
      if ( g_VehiclesRuntimeInfo[i].pModel->osd_params.battery_cell_count > 0 )
         g_VehiclesRuntimeInfo[i].iComputedBatteryCellCount = g_VehiclesRuntimeInfo[i].pModel->osd_params.battery_cell_count;

      if ( 0 == g_VehiclesRuntimeInfo[i].iComputedBatteryCellCount )
         g_VehiclesRuntimeInfo[i].iComputedBatteryCellCount = 1;

      g_VehiclesRuntimeInfo[i].bWarningBatteryVoltage = false;
      if ( g_VehiclesRuntimeInfo[i].bGotFCTelemetry )
      if ( g_VehiclesRuntimeInfo[i].pModel->osd_params.voltage_alarm_enabled )
      if ( g_VehiclesRuntimeInfo[i].headerFCTelemetry.voltage > 1000 )
      if ( g_VehiclesRuntimeInfo[i].headerFCTelemetry.voltage/1000.0/(float)g_VehiclesRuntimeInfo[i].iComputedBatteryCellCount < g_pCurrentModel->osd_params.voltage_alarm )
         g_VehiclesRuntimeInfo[i].bWarningBatteryVoltage = true;

      if ( s_bDebugOSDShowAll )
         g_VehiclesRuntimeInfo[i].bWarningBatteryVoltage = true;

      if ( g_VehiclesRuntimeInfo[i].bWarningBatteryVoltage )
      if ( ( g_TimeNow / 200 ) % 4 )
      {
         char szBuff[64];
         g_pRenderEngine->setColors(get_Color_IconError());
         float sizeIcon = height_text_big*1.2;
         yAlarm = yAlarmsMiddle;
         xAlarm = 0.22+osd_getMarginX() + sizeIcon*1.6;
         
         g_pRenderEngine->drawIcon(xAlarm-sizeIcon/g_pRenderEngine->getAspectRatio(), yAlarm - height_text_big*0.1, sizeIcon/g_pRenderEngine->getAspectRatio(), sizeIcon, g_idIconWarning);
         osd_set_colors();
         if ( iCountVehicles > 1 )
            sprintf(szBuff, "%s voltage is low (%.1f V) !", g_VehiclesRuntimeInfo[i].pModel->getLongName(), g_VehiclesRuntimeInfo[i].headerFCTelemetry.voltage/1000.0 );
         else
            sprintf(szBuff, "Vehicle voltage is low (%.1f V) !", g_VehiclesRuntimeInfo[i].headerFCTelemetry.voltage/1000.0 );
         g_pRenderEngine->drawText(xAlarm+0.01, yAlarm, g_idFontOSDBig, szBuff);
      }

      if ( g_VehiclesRuntimeInfo[i].bWarningBatteryVoltage )
         yAlarmsMiddle -= height_text_big + height_text;

      // Motor alarm

      static float s_fOSDWarningsAverageThrottle = 0.0;
      static float s_fOSDWarningsAverageCurrent = 0.0;

      if ( g_RouterIsReadyTimestamp > g_TimeNow-4000 )
      {
         s_fOSDWarningsAverageThrottle = g_VehiclesRuntimeInfo[i].headerFCTelemetry.throttle;
         s_fOSDWarningsAverageCurrent = g_VehiclesRuntimeInfo[i].headerFCTelemetry.current/1000.0;
      }
      else
      {
         s_fOSDWarningsAverageThrottle = s_fOSDWarningsAverageThrottle*0.98 + 0.02 * g_VehiclesRuntimeInfo[i].headerFCTelemetry.throttle;
         s_fOSDWarningsAverageCurrent = s_fOSDWarningsAverageCurrent * 0.98 + 0.02 * g_VehiclesRuntimeInfo[i].headerFCTelemetry.current/1000.0;
      }

      bool bShowMotorAlarm = false;
      if ( g_RouterIsReadyTimestamp < g_TimeNow-5000 )
      if ( g_VehiclesRuntimeInfo[i].pModel->alarms_params.uAlarmMotorCurrentThreshold & (1<<7) )
      if ( g_VehiclesRuntimeInfo[i].pModel->telemetry_params.fc_telemetry_type == TELEMETRY_TYPE_MAVLINK )
      if ( g_VehiclesRuntimeInfo[i].bGotFCTelemetry )
      if ( g_VehiclesRuntimeInfo[i].headerFCTelemetry.flags & FC_TELE_FLAGS_ARMED )
      if ( s_fOSDWarningsAverageThrottle > 30 )
      if ( s_fOSDWarningsAverageCurrent < ((float)(g_VehiclesRuntimeInfo[i].pModel->alarms_params.uAlarmMotorCurrentThreshold & 0x7F))/10.0 )
         bShowMotorAlarm = true;

      if ( s_bDebugOSDShowAll )
         bShowMotorAlarm = true;

      if ( bShowMotorAlarm )
      {
         if ( (( g_TimeNow / 200 ) % 5) )
         {
            char szBuff[64];
            g_pRenderEngine->setColors(get_Color_IconError());
            float sizeIcon = height_text_big*1.2;
            yAlarm = yAlarmsMiddle;
            xAlarm = 0.22+osd_getMarginX() + sizeIcon*1.6;
            
            g_pRenderEngine->drawIcon(xAlarm-sizeIcon/g_pRenderEngine->getAspectRatio(), yAlarm - height_text*0.6 - height_text_big*0.1, sizeIcon/g_pRenderEngine->getAspectRatio(), sizeIcon, g_idIconWarning);
            osd_set_colors();
            g_pRenderEngine->drawText(xAlarm+0.01, yAlarm-height_text, g_idFontOSDBig, "Vehicle motor is malfunctioning!");
            if ( iCountVehicles > 1 )
               sprintf(szBuff, "(%s: Throttle is %d%% and current consumtion is %.1f Amps)",
                  g_VehiclesRuntimeInfo[i].pModel->getLongName(),
                  g_VehiclesRuntimeInfo[i].headerFCTelemetry.throttle,
                  g_VehiclesRuntimeInfo[i].headerFCTelemetry.current/1000.0 );
            else
               sprintf(szBuff, "(Throttle is %d%% and current consumtion is %.1f Amps)",
                  g_VehiclesRuntimeInfo[i].headerFCTelemetry.throttle,
                  g_VehiclesRuntimeInfo[i].headerFCTelemetry.current/1000.0 );
            g_pRenderEngine->drawText(xAlarm+0.01, yAlarm + height_text*0.2, g_idFontOSD, szBuff);
         }
         yAlarmsMiddle -= height_text_big + 2.0*height_text;
      }
   }
   g_pRenderEngine->setGlobalAlfa(fAlfaOrg);
}
