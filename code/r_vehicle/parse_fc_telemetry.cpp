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

#include "parse_fc_telemetry.h"
#include "parse_fc_telemetry_ltm.h"
#include <math.h>
#include "../../mavlink/common/mavlink.h"
#include "../base/models.h"
#include "../radio/radiopackets2.h"
#include "shared_vars.h"
#include "timers.h"

//#define DEBUG_MAV 1

extern bool g_bDebug;

bool s_bHasReceivedGPSInfo = false;
bool s_bHasReceivedGPSPos = false;
bool s_bHasReceivedHeartbeat = false;

#define MAX_FC_MESSAGES_HISTORY 10
char s_szLastMessages[MAX_FC_MESSAGES_HISTORY][FC_MESSAGE_MAX_LENGTH];
u32 s_uLastMessagesTimes[MAX_FC_MESSAGES_HISTORY];
int s_iNextMessageIndex = 0;

u32 s_TimeLastMAVLink_Altitude = 0;
long s_LastMAVLink_Altitude = 0; // in 1/10 meters

int s_iHeartbeatMsgCount = 0;
int s_iSystemMsgCount = 0;

#ifndef HAVE_ENUM_COPTER_MODE
#define HAVE_ENUM_COPTER_MODE

typedef enum COPTER_MODE
{
   COPTER_MODE_STABILIZE=0, /*  | */
   COPTER_MODE_ACRO=1, /*  | */
   COPTER_MODE_ALT_HOLD=2, /*  | */
   COPTER_MODE_AUTO=3, /*  | */
   COPTER_MODE_GUIDED=4, /*  | */
   COPTER_MODE_LOITER=5, /*  | */
   COPTER_MODE_RTL=6, /*  | */
   COPTER_MODE_CIRCLE=7, /*  | */
   COPTER_MODE_LAND=9, /*  | */
   COPTER_MODE_DRIFT=11, /*  | */
   COPTER_MODE_SPORT=13, /*  | */
   COPTER_MODE_FLIP=14, /*  | */
   COPTER_MODE_AUTOTUNE=15, /*  | */
   COPTER_MODE_POSHOLD=16, /*  | */
   COPTER_MODE_BRAKE=17, /*  | */
   COPTER_MODE_THROW=18, /*  | */
   COPTER_MODE_AVOID_ADSB=19, /*  | */
   COPTER_MODE_GUIDED_NOGPS=20, /*  | */
   COPTER_MODE_SMART_RTL=21, /*  | */
   COPTER_MODE_ENUM_END=22, /*  | */
} COPTER_MODE;
#endif 

#ifndef HAVE_ENUM_PLANE_MODE
#define HAVE_ENUM_PLANE_MODE
typedef enum PLANE_MODE
{
   PLANE_MODE_MANUAL=0, /*  | */
   PLANE_MODE_CIRCLE=1, /*  | */
   PLANE_MODE_STABILIZE=2, /*  | */
   PLANE_MODE_TRAINING=3, /*  | */
   PLANE_MODE_ACRO=4, /*  | */
   PLANE_MODE_FLY_BY_WIRE_A=5, /*  | */
   PLANE_MODE_FLY_BY_WIRE_B=6, /*  | */
   PLANE_MODE_CRUISE=7, /*  | */
   PLANE_MODE_AUTOTUNE=8, /*  | */
   PLANE_MODE_AUTO=10, /*  | */
   PLANE_MODE_RTL=11, /*  | */
   PLANE_MODE_LOITER=12, /*  | */
   PLANE_MODE_TAKEOFF=13, /*  | */
   PLANE_MODE_AVOID_ADSB=14, /*  | */
   PLANE_MODE_GUIDED=15, /*  | */
   PLANE_MODE_INITIALIZING=16, /*  | */
   PLANE_MODE_QSTABILIZE=17, /*  | */
   PLANE_MODE_QHOVER=18, /*  | */
   PLANE_MODE_QLOITER=19, /*  | */
   PLANE_MODE_QLAND=20, /*  | */
   PLANE_MODE_QRTL=21, /*  | */
   PLANE_MODE_QAUTOTUNE=22, /*  | */
   PLANE_MODE_ENUM_END=23, /*  | */
} PLANE_MODE; 
#endif

#ifndef HAVE_ENUM_ROVER_MODE
#define HAVE_ENUM_ROVER_MODE
typedef enum ROVER_MODE
{
   ROVER_MODE_MANUAL=0, /*  | */
   ROVER_MODE_ACRO=1, /*  | */
   ROVER_MODE_STEERING=3, /*  | */
   ROVER_MODE_HOLD=4, /*  | */
   ROVER_MODE_LOITER=5, /*  | */
   ROVER_MODE_AUTO=10, /*  | */
   ROVER_MODE_RTL=11, /*  | */
   ROVER_MODE_SMART_RTL=12, /*  | */
   ROVER_MODE_GUIDED=15, /*  | */
   ROVER_MODE_INITIALIZING=16, /*  | */
   ROVER_MODE_ENUM_END=17, /*  | */
} ROVER_MODE;
#endif
 
mavlink_status_t statusMav;
mavlink_message_t msgMav;
u32 s_vehicleMavId = 1;
int s_iAllowAnyVehicleSysId = 0;


void _rotate_point(float x, float y, float xCenter, float yCenter, float angle, float* px, float* py)
{
   if ( NULL == px || NULL == py )
      return;
   float s = sin(angle*3.14159/180.0);
   float c = cos(angle*3.14159/180.0);

   x = (x-xCenter);
   y = (y-yCenter);
   *px = x * c - y * s;
   *py = x * s + y * c;

   *px += xCenter;
   *py += yCenter;
}


void parse_telemetry_init(u32 vehicleMavId)
{
   s_vehicleMavId = vehicleMavId;
   s_bHasReceivedGPSInfo = false;
   s_bHasReceivedGPSPos = false;
   s_bHasReceivedHeartbeat = false;

   s_iNextMessageIndex = 0;
   for( int i=0; i<MAX_FC_MESSAGES_HISTORY; i++ )
   {
      s_szLastMessages[i][0] = 0;
      s_uLastMessagesTimes[i] = 0;
   }
   
   s_iHeartbeatMsgCount = 0;
   s_iSystemMsgCount = 0;
}

void parse_telemetry_allow_any_sysid(int iAllow)
{
   s_iAllowAnyVehicleSysId = iAllow;
}


int get_heartbeat_msg_count()
{
   return s_iHeartbeatMsgCount;
}

int get_system_msg_count()
{
   return s_iSystemMsgCount;
}

void reset_heartbeat_msg_count()
{
   s_iHeartbeatMsgCount = 0;
}

void reset_system_msg_count()
{
   s_iSystemMsgCount = 0;
}

void _add_fc_message(char* szMessage)
{
   if ( (NULL == szMessage) || (0 == szMessage[0]) )
      return;

   strncpy(s_szLastMessages[s_iNextMessageIndex], szMessage, FC_MESSAGE_MAX_LENGTH-1);
   s_szLastMessages[s_iNextMessageIndex][FC_MESSAGE_MAX_LENGTH-1] = 0;
   s_uLastMessagesTimes[s_iNextMessageIndex] = g_TimeNow;
   s_iNextMessageIndex++;
   
   if ( s_iNextMessageIndex >= MAX_FC_MESSAGES_HISTORY )
   {
      for( int i=0; i<MAX_FC_MESSAGES_HISTORY-1; i++ )
      {
         strcpy(&s_szLastMessages[i][0], &s_szLastMessages[i+1][0]);
         s_uLastMessagesTimes[i] = s_uLastMessagesTimes[i+1];
      }
      s_iNextMessageIndex = MAX_FC_MESSAGES_HISTORY-1;
   }
}

bool _check_add_fc_message(char* szMessage)
{
   if ( (NULL == szMessage) || (0 == szMessage[0]) )
      return false;

   char szTmpMsg[FC_MESSAGE_MAX_LENGTH];
   strncpy(szTmpMsg, szMessage, FC_MESSAGE_MAX_LENGTH-1);
   szTmpMsg[FC_MESSAGE_MAX_LENGTH-1] = 0;

   bool bDuplicateMessage = false;

   for( int i=0; i<s_iNextMessageIndex; i++ )
   {
      if ( 0 != s_szLastMessages[i] )
      if ( 0 == strcmp(szTmpMsg, s_szLastMessages[i]) )
      {
         bDuplicateMessage = true;
         break;
      }
   }

   if ( ! bDuplicateMessage )
   {
      _add_fc_message(szMessage);
      return true;
   }

   // If the duplicate message is too frequent, do not add it to the list
   
   bool bMustDiscard = false;

   for( int i=0; i<s_iNextMessageIndex; i++ )
   {
      if ( 0 == s_szLastMessages[i] )
         continue;
      if ( 0 != strcmp(szTmpMsg, s_szLastMessages[i]) )
         continue;

      if ( s_uLastMessagesTimes[i] > g_TimeNow - 3000 )
      {
         bMustDiscard = true;
         s_uLastMessagesTimes[i] = g_TimeNow;
      }
   }

   if ( bMustDiscard )
      return false;

   _add_fc_message(szMessage);
   return true;
}

void _process_mav_message(t_packet_header_fc_telemetry* pdpfct, t_packet_header_ruby_telemetry_extended_v3* pPHRTE, u8 vehicleType)
{
   char szBuff[512];
   u32 tmp32;
   u8 tmp8;
   int tmpi;
   int imah = 0;
   //u32 uTmp32;

   if ( 0 == s_iAllowAnyVehicleSysId )
   if ( (msgMav.sysid != s_vehicleMavId) && (msgMav.sysid != 0) )
      return;

   //#ifdef DEBUG_MAV
   //log_line("MAV Msg id: %d", msgMav.msgid);
   //printf("MAV Msg id: %d\n", msgMav.msgid);
   //#endif

   switch (msgMav.msgid)
   { 
      case MAVLINK_MSG_ID_STATUSTEXT:
         mavlink_msg_statustext_get_text(&msgMav, szBuff);
         if ( _check_add_fc_message(szBuff) )
            log_line("MAV status text: %s", szBuff);
         #ifdef DEBUG_MAV
         printf("MAV status text: %s\n", szBuff);
         #endif
         break;

      case MAVLINK_MSG_ID_STATUSTEXT_LONG:
         mavlink_msg_statustext_long_get_text(&msgMav, szBuff);
         if ( _check_add_fc_message(szBuff) )
            log_line("MAV status text long: %s", szBuff);
         #ifdef DEBUG_MAV
         printf("MAV status text long: %s\n", szBuff);
         #endif
         break;

      case MAVLINK_MSG_ID_HEARTBEAT:
         #ifdef DEBUG_MAV
         log_line("MAV Heart Beat, type: %d, autopilot: %d", mavlink_msg_heartbeat_get_type(&msgMav), mavlink_msg_heartbeat_get_autopilot(&msgMav));
         printf("MAV Heart Beat, type: %d, autopilot: %d\n", mavlink_msg_heartbeat_get_type(&msgMav), mavlink_msg_heartbeat_get_autopilot(&msgMav));
         #endif
         tmp32 = mavlink_msg_heartbeat_get_custom_mode(&msgMav);
         tmp8 = mavlink_msg_heartbeat_get_base_mode(&msgMav);
         pdpfct->flight_mode = 0;
         /*
         switch ( tmp8 )
         {
            case 0: 
            case 64:
            case 66:
            case 81:
            case 88:
            case 92:
               pdpfct->flight_mode &= ~FLIGHT_MODE_ARMED; //disarmed
               break;

            case 1:
            case 192:
            case 194:
            case 208:
            case 209:
            case 216:
            case 220:
               pdpfct->flight_mode |= FLIGHT_MODE_ARMED;
               break;

            default:
               if ( tmp8 > 100 )
                  pdpfct->flight_mode |= FLIGHT_MODE_ARMED;
               else if ( tmp8 < 100 )
                  pdpfct->flight_mode &= ~FLIGHT_MODE_ARMED;
               break;
         };
         */
         if ( tmp8 & MAV_MODE_FLAG_SAFETY_ARMED )
            pdpfct->flight_mode |= FLIGHT_MODE_ARMED;
         else
            pdpfct->flight_mode &= ~FLIGHT_MODE_ARMED;

         if ( vehicleType == MODEL_TYPE_AIRPLANE )
         {
         //log_line("plane tmp32: %u", tmp32);
         switch ( tmp32 )
         {
            case PLANE_MODE_MANUAL: pdpfct->flight_mode |= FLIGHT_MODE_MANUAL; break;
            case PLANE_MODE_CIRCLE: pdpfct->flight_mode |= FLIGHT_MODE_CIRCLE; break;
            case PLANE_MODE_STABILIZE: pdpfct->flight_mode |= FLIGHT_MODE_STAB; break;
            case PLANE_MODE_FLY_BY_WIRE_A: pdpfct->flight_mode |= FLIGHT_MODE_FBWA; break;
            case PLANE_MODE_FLY_BY_WIRE_B: pdpfct->flight_mode |= FLIGHT_MODE_FBWB; break;
            case PLANE_MODE_ACRO: pdpfct->flight_mode |= FLIGHT_MODE_ACRO; break;
            case PLANE_MODE_AUTO: pdpfct->flight_mode |= FLIGHT_MODE_AUTO; break;
            case PLANE_MODE_AUTOTUNE: pdpfct->flight_mode |= FLIGHT_MODE_AUTOTUNE; break;
            case PLANE_MODE_RTL: pdpfct->flight_mode |= FLIGHT_MODE_RTL; break;
            case PLANE_MODE_LOITER: pdpfct->flight_mode |= FLIGHT_MODE_LOITER; break;
            case PLANE_MODE_TAKEOFF: pdpfct->flight_mode |= FLIGHT_MODE_TAKEOFF; break;
            case PLANE_MODE_CRUISE: pdpfct->flight_mode |= FLIGHT_MODE_CRUISE; break;
            case PLANE_MODE_QSTABILIZE: pdpfct->flight_mode |= FLIGHT_MODE_QSTAB; break;
            case PLANE_MODE_QHOVER: pdpfct->flight_mode |= FLIGHT_MODE_QHOVER; break;
            case PLANE_MODE_QLOITER: pdpfct->flight_mode |= FLIGHT_MODE_QLOITER; break;
            case PLANE_MODE_QLAND: pdpfct->flight_mode |= FLIGHT_MODE_QLAND; break;
            case PLANE_MODE_QRTL: pdpfct->flight_mode |= FLIGHT_MODE_QRTL; break;
         };
         }
         else if ( vehicleType == MODEL_TYPE_CAR )
         {
         switch ( tmp32 )
         {
            case ROVER_MODE_MANUAL: pdpfct->flight_mode |= FLIGHT_MODE_MANUAL; break;
            case ROVER_MODE_ACRO:   pdpfct->flight_mode |= FLIGHT_MODE_ACRO; break;
            case ROVER_MODE_STEERING: pdpfct->flight_mode |= FLIGHT_MODE_STAB; break;
            case ROVER_MODE_HOLD:   pdpfct->flight_mode |= FLIGHT_MODE_POSHOLD; break;
            case ROVER_MODE_LOITER: pdpfct->flight_mode |= FLIGHT_MODE_LOITER; break;
            case ROVER_MODE_RTL:    pdpfct->flight_mode |= FLIGHT_MODE_RTL; break;
            case ROVER_MODE_SMART_RTL: pdpfct->flight_mode |= FLIGHT_MODE_RTL; break;
         };
         }
         else
         {
         //log_line("drone tmp32: %u", tmp32);
         switch ( tmp32 )
         {
            case COPTER_MODE_STABILIZE: pdpfct->flight_mode |= FLIGHT_MODE_STAB; break;
            case COPTER_MODE_ALT_HOLD: pdpfct->flight_mode |= FLIGHT_MODE_ALTH; break;
            case COPTER_MODE_LOITER: pdpfct->flight_mode |= FLIGHT_MODE_LOITER; break;
            case COPTER_MODE_AUTO: pdpfct->flight_mode |= FLIGHT_MODE_AUTO; break;
            case COPTER_MODE_LAND: pdpfct->flight_mode |= FLIGHT_MODE_LAND; break;
            case COPTER_MODE_RTL: pdpfct->flight_mode |= FLIGHT_MODE_RTL; break;
            case COPTER_MODE_SMART_RTL: pdpfct->flight_mode |= FLIGHT_MODE_RTL; break;
            case COPTER_MODE_AUTOTUNE: pdpfct->flight_mode |= FLIGHT_MODE_AUTOTUNE; break;
            case COPTER_MODE_POSHOLD: pdpfct->flight_mode |= FLIGHT_MODE_POSHOLD; break;
            case COPTER_MODE_ACRO: pdpfct->flight_mode |= FLIGHT_MODE_ACRO; break;
            case COPTER_MODE_CIRCLE: pdpfct->flight_mode |= FLIGHT_MODE_CIRCLE; break;
         };
         }
         if ( pdpfct->flight_mode & FLIGHT_MODE_ARMED )
            pdpfct->flags |= FC_TELE_FLAGS_ARMED;
         else
            pdpfct->flags &= ~FC_TELE_FLAGS_ARMED;

         s_bHasReceivedHeartbeat = true;
         s_iHeartbeatMsgCount++;
         break;

      case MAVLINK_MSG_ID_BATTERY_STATUS:
         imah = mavlink_msg_battery_status_get_current_consumed(&msgMav);
         pdpfct->mah = (imah<0)?0:imah;
         #ifdef DEBUG_MAV
         log_line("MAV battery status: mah: %d, %u", imah, pdpfct->mah);
         printf("MAV battery status: mah: %d, %u\n", imah, pdpfct->mah);
         #endif
         break;

      case MAVLINK_MSG_ID_SYS_STATUS:
         imah = mavlink_msg_sys_status_get_current_battery(&msgMav);
         pdpfct->voltage = mavlink_msg_sys_status_get_voltage_battery(&msgMav);
         pdpfct->current = (imah<0)?0:(imah*10U);
         #ifdef DEBUG_MAV
         log_line("MAV Sys Status: volt: %f, amps: %f", pdpfct->voltage/100.0f, pdpfct->current/100.0f);
         printf("MAV Sys Status: volt: %f, amps: %f\n", pdpfct->voltage/100.0f, pdpfct->current/100.0f);
         #endif
         s_iSystemMsgCount++;
         break;
      
      case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
         pdpfct->altitude_abs = mavlink_msg_global_position_int_get_alt(&msgMav) / 10.0f + 100000;
         pdpfct->altitude = mavlink_msg_global_position_int_get_relative_alt(&msgMav) / 10.0f + 100000;
         //log_line("alt: %f, abs: %f", ((int)pdpfct->altitude-100000)/100.0, ((int)pdpfct->altitude_abs-100000)/100.0);
         {
            int li = g_pCurrentModel->osd_params.layout;
            if ( li >= 0 && li < MODEL_MAX_OSD_PROFILES )
            if ( g_pCurrentModel->osd_params.osd_flags2[li] & OSD_FLAG2_SHOW_LOCAL_VERTICAL_SPEED )
            {
               if ( s_TimeLastMAVLink_Altitude == 0 )
               {
                  s_TimeLastMAVLink_Altitude = g_TimeNow;
                  s_LastMAVLink_Altitude = ((long)pdpfct->altitude) - 100000;
                  pdpfct->vspeed = 100000;
               }
               else
               {
                  long alt = ((long)pdpfct->altitude) - 100000;
                  if ( g_TimeNow > s_TimeLastMAVLink_Altitude )
                  {
                     long dTime = g_TimeNow - s_TimeLastMAVLink_Altitude; 
                     float vspeed = (float)(alt - s_LastMAVLink_Altitude)*1000.0/(float)dTime;
                     //log_line("alt: %d - %d, %d, %f, dt: %d", alt, s_LastMAVLink_Altitude, (long)vspeed, vspeed, dTime);
                     pdpfct->vspeed = (u32)(vspeed + 100000);
                  }
                  s_TimeLastMAVLink_Altitude = g_TimeNow;
                  s_LastMAVLink_Altitude = alt;
               }
            }
         }
         pdpfct->heading = mavlink_msg_global_position_int_get_hdg(&msgMav) / 100.0f;

         pdpfct->latitude = mavlink_msg_global_position_int_get_lat(&msgMav);
         pdpfct->longitude = mavlink_msg_global_position_int_get_lon(&msgMav);
         s_bHasReceivedGPSPos = true;
         #ifdef DEBUG_MAV
         log_line("MAV Pos Int: alt absolute: %f", pdpfct->altitude_abs/10.0f);
         printf("MAV Pos Int: alt absolute: %f\n", pdpfct->altitude_abs/10.0f);
         #endif
         break;

     case MAVLINK_MSG_ID_GPS_RAW_INT:
         pdpfct->gps_fix_type = mavlink_msg_gps_raw_int_get_fix_type(&msgMav);
         pdpfct->satelites = mavlink_msg_gps_raw_int_get_satellites_visible(&msgMav);
         pdpfct->hdop = mavlink_msg_gps_raw_int_get_eph(&msgMav);
         pdpfct->latitude = mavlink_msg_gps_raw_int_get_lat(&msgMav);
         pdpfct->longitude = mavlink_msg_gps_raw_int_get_lon(&msgMav);
         //uTmp32 = mavlink_msg_gps_raw_int_get_alt(&msgMav)/1000.0f / 10.0 + 100000;
         //if ( pdpfct->gps_fix_type >= GPS_FIX_TYPE_3D_FIX )
         //   pdpfct->altitude_abs = uTmp32;

         s_bHasReceivedGPSInfo = true;
         #ifdef DEBUG_MAV
         log_line("MAV GPS Raw: satelites: %d, fix type: %d, hdop: %f, %.7f, %.7f", pdpfct->satelites, pdpfct->gps_fix_type, pdpfct->hdop/100.0f, pdpfct->longitude/10000000.0f, pdpfct->latitude/10000000.0f);
         printf("MAV GPS Raw: satelites: %d, fix type: %d, hdop: %f\n", pdpfct->satelites, pdpfct->gps_fix_type, pdpfct->hdop/100.0f);
         #endif
         break;

      case MAVLINK_MSG_ID_GPS2_RAW:
      {
         pdpfct->extra_info[1] = mavlink_msg_gps2_raw_get_satellites_visible(&msgMav);
         pdpfct->extra_info[2] = mavlink_msg_gps2_raw_get_fix_type(&msgMav);
         u16 hdop = mavlink_msg_gps2_raw_get_eph(&msgMav);
         pdpfct->extra_info[3] = (hdop >> 8);
         pdpfct->extra_info[4] = (hdop & 0xFF);
         s_bHasReceivedGPSInfo = true;
         break;
      }
      
      case MAVLINK_MSG_ID_VFR_HUD: 
         pdpfct->throttle = mavlink_msg_vfr_hud_get_throttle(&msgMav); 
         //pdpfct->altitude = mavlink_msg_vfr_hud_get_alt(&msgMav)*100 + 100000;

         if ( g_pCurrentModel->osd_params.layout < 0 || g_pCurrentModel->osd_params.layout >= MODEL_MAX_OSD_PROFILES ||
              (!(g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_LOCAL_VERTICAL_SPEED)) )
            pdpfct->vspeed = mavlink_msg_vfr_hud_get_climb(&msgMav)*100 + 100000; 
         pdpfct->hspeed = mavlink_msg_vfr_hud_get_groundspeed(&msgMav) * 100.0f + 100000;

         tmp32= mavlink_msg_vfr_hud_get_airspeed(&msgMav) * 100.0f + 100000;
         pdpfct->aspeed = tmp32;
         #ifdef DEBUG_MAV
         log_line("MAV HUD: alt: %f, vspeed: %f", pdpfct->altitude/100.0f, pdpfct->vspeed/100.0f);
         printf("MAV HUD: alt: %f, vspeed: %f\n", pdpfct->altitude/100.0f, pdpfct->vspeed/100.0f);
         #endif
         break;

      case MAVLINK_MSG_ID_ATTITUDE: 
         pdpfct->flags = pdpfct->flags | FC_TELE_FLAGS_HAS_ATTITUDE;
         pdpfct->roll = (mavlink_msg_attitude_get_roll(&msgMav) + 3.141592653589793)*5700.2958;
         pdpfct->pitch = (mavlink_msg_attitude_get_pitch(&msgMav) + 3.141592653589793)*5700.2958;
         #ifdef DEBUG_MAV
         log_line("MAV attitude: pitch: %.1f, roll: %.1f", pdpfct->pitch/100.0f, pdpfct->roll/100.0f);
         printf("MAV attitude: pitch: %.1f, roll: %.1f\n", pdpfct->pitch/100.0f, pdpfct->roll/100.0f);
         #endif
         break;

      case MAVLINK_MSG_ID_RC_CHANNELS_RAW:
         tmpi = (int)((u8)mavlink_msg_rc_channels_raw_get_rssi(&msgMav));
         
         if ( /*(tmpi != 255) &&*/ (NULL != pPHRTE) )
         {
            pdpfct->rc_rssi = (tmpi*100)/255;
            if ( ! (pPHRTE->flags & FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI) )
            {
               log_line("Received RC RSSI from FC through MAVLink, value: %d", pdpfct->rc_rssi);
               pPHRTE->flags |= FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI;
            }
            pPHRTE->uplink_mavlink_rc_rssi = pdpfct->rc_rssi;
         }
         //if ( NULL != pPHRTE && (pPHRTE->flags & FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI) && (tmpi == 255) )
         //   pPHRTE->uplink_mavlink_rc_rssi = 255;

         s_MAVLinkRCChannels[0] = mavlink_msg_rc_channels_raw_get_chan1_raw(&msgMav);
         s_MAVLinkRCChannels[1] = mavlink_msg_rc_channels_raw_get_chan2_raw(&msgMav);
         s_MAVLinkRCChannels[2] = mavlink_msg_rc_channels_raw_get_chan3_raw(&msgMav);
         s_MAVLinkRCChannels[3] = mavlink_msg_rc_channels_raw_get_chan4_raw(&msgMav);
         s_MAVLinkRCChannels[4] = mavlink_msg_rc_channels_raw_get_chan5_raw(&msgMav);
         s_MAVLinkRCChannels[5] = mavlink_msg_rc_channels_raw_get_chan6_raw(&msgMav);
         s_MAVLinkRCChannels[6] = mavlink_msg_rc_channels_raw_get_chan7_raw(&msgMav);
         s_MAVLinkRCChannels[7] = mavlink_msg_rc_channels_raw_get_chan8_raw(&msgMav);
         break;

      case MAVLINK_MSG_ID_RC_CHANNELS:
         tmpi = (int)((u8)mavlink_msg_rc_channels_get_rssi(&msgMav));
         
         if ( /*(tmpi != 255) &&*/ (NULL != pPHRTE) )
         {
            pdpfct->rc_rssi = (tmpi*100)/255;
            if ( ! (pPHRTE->flags & FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI) )
            {
               log_line("Received RC RSSI from FC through MAVLink, value: %d", pdpfct->rc_rssi);
               pPHRTE->flags |= FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI;
            }
            pPHRTE->uplink_mavlink_rc_rssi = pdpfct->rc_rssi;
         }
         //if ( NULL != pPHRTE && (pPHRTE->flags & FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RC_RSSI) && (tmpi == 255) )
         //   pPHRTE->uplink_mavlink_rc_rssi = 255;

         s_MAVLinkRCChannels[0] = mavlink_msg_rc_channels_get_chan1_raw(&msgMav);
         s_MAVLinkRCChannels[1] = mavlink_msg_rc_channels_get_chan2_raw(&msgMav);
         s_MAVLinkRCChannels[2] = mavlink_msg_rc_channels_get_chan3_raw(&msgMav);
         s_MAVLinkRCChannels[3] = mavlink_msg_rc_channels_get_chan4_raw(&msgMav);
         s_MAVLinkRCChannels[4] = mavlink_msg_rc_channels_get_chan5_raw(&msgMav);
         s_MAVLinkRCChannels[5] = mavlink_msg_rc_channels_get_chan6_raw(&msgMav);
         s_MAVLinkRCChannels[6] = mavlink_msg_rc_channels_get_chan7_raw(&msgMav);
         s_MAVLinkRCChannels[7] = mavlink_msg_rc_channels_get_chan8_raw(&msgMav);
         s_MAVLinkRCChannels[8] = mavlink_msg_rc_channels_get_chan9_raw(&msgMav);
         s_MAVLinkRCChannels[9] = mavlink_msg_rc_channels_get_chan10_raw(&msgMav);
         s_MAVLinkRCChannels[10] = mavlink_msg_rc_channels_get_chan11_raw(&msgMav);
         s_MAVLinkRCChannels[11] = mavlink_msg_rc_channels_get_chan12_raw(&msgMav);
         s_MAVLinkRCChannels[12] = mavlink_msg_rc_channels_get_chan13_raw(&msgMav);
         s_MAVLinkRCChannels[13] = mavlink_msg_rc_channels_get_chan14_raw(&msgMav);         
         break;

      case MAVLINK_MSG_ID_RADIO_STATUS:
         tmp8 = ((int)mavlink_msg_radio_status_get_rssi(&msgMav))*100/255;
         //if ( tmp8 != 0xFF )
         //   pdpfct->rc_rssi = tmp8;

         if ( NULL != pPHRTE )
         {
            if ( ! (pPHRTE->flags & FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RX_RSSI) )
            {
               log_line("Received RX RSSI from FC through MAVLink, value: %d", tmp8);
               pPHRTE->flags |= FLAG_RUBY_TELEMETRY_HAS_MAVLINK_RX_RSSI;
            }
            pPHRTE->uplink_mavlink_rx_rssi = tmp8;
         }
         break;

      case MAVLINK_MSG_ID_RAW_IMU:
         //log_line("MSG_RAW_IMU");
         break;

      case MAVLINK_MSG_ID_SCALED_IMU:
         //log_line("MSG_SCALED_IMU");
         break;

      case MAVLINK_MSG_ID_HIGH_LATENCY:
         {
            //log_line("MSG_HIGH_LAT");
            int iTemp = mavlink_msg_high_latency_get_temperature(&msgMav);
            if ( iTemp < 100 && iTemp > -100 )
               pdpfct->temperature = 100 + (int) iTemp;

            iTemp = mavlink_msg_high_latency_get_temperature_air(&msgMav);
            if ( iTemp < 100 && iTemp > -100 )
               pdpfct->temperature = 100 + (int) iTemp;
         }
         break;

      case MAVLINK_MSG_ID_HIGH_LATENCY2:
         {
            //log_line("MSG_HIGH_LAT2");
            int iTemp = mavlink_msg_high_latency2_get_temperature_air(&msgMav);
            if ( iTemp < 100 && iTemp > -100 )
               pdpfct->temperature = 100 + (int) iTemp;

            u16 uDir = 2 * mavlink_msg_high_latency2_get_wind_heading(&msgMav);
            uDir++;
            pdpfct->extra_info[7] = uDir >> 8;
            pdpfct->extra_info[8] = uDir & 0xFF;
             
            u16 uSpeed = 100 * mavlink_msg_high_latency2_get_windspeed(&msgMav) / 5;
            uSpeed++;
            pdpfct->extra_info[9] = uSpeed >> 8;
            pdpfct->extra_info[10] = uSpeed & 0xFF;
         }
         break;

      case MAVLINK_MSG_ID_SCALED_PRESSURE:
         {
            //log_line("SCALED PRESSURE");
            int iTemp = mavlink_msg_scaled_pressure_get_temperature(&msgMav);
            iTemp = iTemp/100;
            if ( iTemp < 100 && iTemp > -100 )
               pdpfct->temperature = 100 + (int) iTemp;
         }

      /*
      case MAVLINK_MSG_ID_WIND:
         {
            log_line("WIND");
            u16 uDir = mavlink_msg_wind_get_direction(&msgMav);
            pdpfct->extra_info[7] = uDir >> 8;
            pdpfct->extra_info[8] = uDir & 0xFF;
            float fSpeed = mavlink_msg_wind_get_speed(&msgMav);
            u16 uSpeed = (u16)(fSpeed*100.0);
            pdpfct->extra_info[9] = uSpeed >> 8;
            pdpfct->extra_info[10] = uSpeed & 0xFF;
         }
      */

      case MAVLINK_MSG_ID_WIND_COV:
       {
          //log_line("WIND_COV");
          float fWindX = mavlink_msg_wind_cov_get_wind_x(&msgMav);
          float fWindY = mavlink_msg_wind_cov_get_wind_x(&msgMav);
          //float fWindZ = mavlink_msg_wind_cov_get_wind_x(&msgMav);
          if ( fabs(fWindX) + fabs(fWindY) > 0.0001 )
          {
             float fLen = sqrtf(fWindX*fWindX + fWindY * fWindY);
             float fAngle = 3.1415*2.0*atan2f(fWindY, fWindX);
             fAngle -= pdpfct->heading;
             u16 uDir = (u16)fAngle;
             uDir++;
             pdpfct->extra_info[7] = uDir >> 8;
             pdpfct->extra_info[8] = uDir & 0xFF;

             u16 uSpeed = (u16)(fLen*100.0);
             uSpeed++;
             pdpfct->extra_info[9] = uSpeed >> 8;
             pdpfct->extra_info[10] = uSpeed & 0xFF;
          }
          else
          {
             pdpfct->extra_info[7] = 0;
             pdpfct->extra_info[8] = 0;
             pdpfct->extra_info[9] = 0;
             pdpfct->extra_info[10] = 0;
          }
       }
      case MAVLINK_MSG_ID_PARAM_VALUE:
         /*
         {
            char szBuff[32];
            szBuff[0] = '*';
            szBuff[1] = 0;
            mavlink_msg_param_value_get_param_id(&msgMav, szBuff);
            u8 type = mavlink_msg_param_value_get_param_type(&msgMav);
            //int val = (int)mavlink_msg_param_value_get_param_value(&msgMav);
            //log_line("recv param '%s' (%d of %d),  value: %d", szBuff, (int)mavlink_msg_param_value_get_param_index(&msgMav), (int)mavlink_msg_param_value_get_param_count(&msgMav), val);
            float val = (float)mavlink_msg_param_value_get_param_value(&msgMav);
            log_line("recv param '%s'(%d of %d), type: %d, value: %f", szBuff, (int)mavlink_msg_param_value_get_param_index(&msgMav), (int)mavlink_msg_param_value_get_param_count(&msgMav), type, val);
         }
         */
         break;
      
   }
}

bool parse_telemetry_from_fc( u8* buffer, int length, t_packet_header_fc_telemetry* pphfct, t_packet_header_ruby_telemetry_extended_v3* pPHRTE, u8 vehicleType, int telemetry_type )
{
   if ( telemetry_type == TELEMETRY_TYPE_LTM )
      return parse_telemetry_from_fc_ltm(buffer, length, pphfct, pPHRTE, vehicleType);

   //log_line("parsing %d bytes", length);
   //printf("\nparsing %d bytes", length);
   bool ret = false;
   uint8_t c;
   for( int i=0; i<length; i++)
   {
      c = *buffer;
      buffer++;
      if (mavlink_parse_char(0, c, &msgMav, &statusMav))
      {
         g_TimeLastMessageFromFC = g_TimeNow;
         ret = true;
         _process_mav_message(pphfct, pPHRTE, vehicleType);
      }
   }
   return ret;
}

bool has_received_gps_info()
{
   if ( g_bDebug )
      return true;
   return (s_bHasReceivedGPSInfo && s_bHasReceivedGPSPos);
}

bool has_received_flight_mode()
{
   if ( g_bDebug )
      return true;
   return s_bHasReceivedHeartbeat;
}

u32 get_last_message_time()
{
   if ( 0 == s_iNextMessageIndex )
      return 0;
   return s_uLastMessagesTimes[s_iNextMessageIndex-1];
}

char* get_last_message()
{
   if ( 0 == s_iNextMessageIndex )
      return NULL;
   return s_szLastMessages[s_iNextMessageIndex-1];
}
