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

#include "base.h"
#include "config.h"
#include "ctrl_settings.h"
#include "hardware.h"
#include "hw_procs.h"
#include "flags.h"

ControllerSettings s_CtrlSettings;
int s_CtrlSettingsLoaded = 0;

void reset_ControllerSettings()
{
   memset(&s_CtrlSettings, 0, sizeof(s_CtrlSettings));
   s_CtrlSettings.iUseBrokenVideoCRC = 0;
   s_CtrlSettings.iTXPower = DEFAULT_RADIO_TX_POWER_CONTROLLER;
   s_CtrlSettings.iTXPowerAtheros = DEFAULT_RADIO_TX_POWER_CONTROLLER;
   s_CtrlSettings.iTXPowerRTL = DEFAULT_RADIO_TX_POWER_CONTROLLER;
   s_CtrlSettings.iTXPowerSiK = DEFAULT_RADIO_SIK_TX_POWER;
   s_CtrlSettings.iMaxTXPower = MAX_TX_POWER;
   s_CtrlSettings.iMaxTXPowerAtheros = MAX_TX_POWER;
   s_CtrlSettings.iMaxTXPowerRTL = MAX_TX_POWER;
   s_CtrlSettings.iHDMIBoost = 6;
   s_CtrlSettings.iOverVoltage = 0;
   s_CtrlSettings.iFreqARM = 0;
   s_CtrlSettings.iFreqGPU = 0;

   s_CtrlSettings.iNiceRouter = DEFAULT_PRIORITY_PROCESS_ROUTER;
   s_CtrlSettings.ioNiceRouter = DEFAULT_IO_PRIORITY_ROUTER;
   s_CtrlSettings.iNiceCentral = DEFAULT_PRIORITY_PROCESS_CENTRAL;
   s_CtrlSettings.iNiceRXVideo = DEFAULT_PRIORITY_PROCESS_VIDEO_RX;
   s_CtrlSettings.ioNiceRXVideo = DEFAULT_IO_PRIORITY_VIDEO_RX;
   s_CtrlSettings.iVideoForwardUSBType = 0;
   s_CtrlSettings.iVideoForwardUSBPort = 5001;
   s_CtrlSettings.iVideoForwardUSBPacketSize = 1024;
   s_CtrlSettings.nVideoForwardETHType = 0;
   s_CtrlSettings.nVideoForwardETHPort = 5010;
   s_CtrlSettings.nVideoForwardETHPacketSize = 1024;
   s_CtrlSettings.iTelemetryForwardUSBType = 0;
   s_CtrlSettings.iTelemetryForwardUSBPort = 5002;
   s_CtrlSettings.iTelemetryForwardUSBPacketSize = 128;
   s_CtrlSettings.iTelemetryOutputSerialPortIndex = -1;
   s_CtrlSettings.iTelemetryInputSerialPortIndex = -1;
   s_CtrlSettings.iMAVLinkSysIdController = DEFAULT_MAVLINK_SYS_ID_CONTROLLER;

   s_CtrlSettings.iDisableHDMIOverscan = 0;
   s_CtrlSettings.iDeveloperMode = 0;
   s_CtrlSettings.iRenderFPS = 10;
   s_CtrlSettings.iShowVoltage = 1;
   s_CtrlSettings.nRetryRetransmissionAfterTimeoutMS = DEFAULT_VIDEO_RETRANS_RETRY_TIME;
   s_CtrlSettings.nRequestRetransmissionsOnVideoSilenceMs = DEFAULT_VIDEO_RETRANS_REQUEST_ON_VIDEO_SILENCE_MS;
   s_CtrlSettings.nUseFixedIP = 0;
   s_CtrlSettings.uFixedIP = (192<<24) | (168<<16) | (1<<8) | 20;
   s_CtrlSettings.nAutomaticTxCard = 1;
   s_CtrlSettings.nRotaryEncoderFunction = 1; // 0 - none, 1 - menu, 2 - camera
   s_CtrlSettings.nRotaryEncoderSpeed = 0; // 0 - normal, 1 - slow
   s_CtrlSettings.nRotaryEncoderFunction2 = 2; // 0 - none, 1 - menu, 2 - camera
   s_CtrlSettings.nRotaryEncoderSpeed2 = 0; // 0 - normal, 1 - slow
   s_CtrlSettings.nPingClockSyncFrequency = DEFAULT_PING_FREQUENCY;
   s_CtrlSettings.nGraphRadioRefreshInterval = 100;
   s_CtrlSettings.nGraphVideoRefreshInterval = 100;
   s_CtrlSettings.iDisableRetransmissionsAfterControllerLinkLostMiliseconds = DEFAULT_CONTROLLER_LINK_MILISECONDS_TIMEOUT_TO_DISABLE_RETRANSMISSIONS;
   s_CtrlSettings.iVideoDecodeStatsSnapshotClosesOnTimeout = 1;
   s_CtrlSettings.iQAButtonRelaySwitching = -1;
   s_CtrlSettings.iFreezeOSD = 0;
   s_CtrlSettings.iDevSwitchVideoProfileUsingQAButton = -1;
   s_CtrlSettings.iShowControllerAdaptiveInfoStats = 0;
   s_CtrlSettings.iShowVideoStreamInfoCompact = 0;

   s_CtrlSettings.iSearchSiKAirRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
   s_CtrlSettings.iSearchSiKECC = 0;
   s_CtrlSettings.iSearchSiKLBT = 0;
   s_CtrlSettings.iSearchSiKMCSTR = 0;

   s_CtrlSettings.iAudioOutputDevice = 1;
   s_CtrlSettings.iAudioOutputVolume = 100;

   s_CtrlSettings.iDevRxLoopTimeout = DEFAULT_MAX_RX_LOOP_TIMEOUT_MILISECONDS;
   s_CtrlSettings.uShowBigRxHistoryInterface = 0;
   s_CtrlSettings.iSiKPacketSize = DEFAULT_SIK_PACKET_SIZE;

   log_line("Reseted controller settings.");
}

int save_ControllerSettings()
{
   FILE* fd = fopen(FILE_CONTROLLER_SETTINGS, "w");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to save controller settings to file: %s",FILE_CONTROLLER_SETTINGS);
      return 0;
   }
   fprintf(fd, "%s\n", CONTROLLER_SETTINGS_STAMP_ID);
   fprintf(fd, "%d %d %d\n", s_CtrlSettings.iDeveloperMode, s_CtrlSettings.iUseBrokenVideoCRC, s_CtrlSettings.iHDMIBoost);
   fprintf(fd, "%d %d %d %d %d %d\n", s_CtrlSettings.iTXPower, s_CtrlSettings.iTXPowerAtheros, s_CtrlSettings.iTXPowerRTL, s_CtrlSettings.iMaxTXPower, s_CtrlSettings.iMaxTXPowerAtheros, s_CtrlSettings.iMaxTXPowerRTL);

   fprintf(fd, "%d %d %d\n", s_CtrlSettings.iOverVoltage, s_CtrlSettings.iFreqARM, s_CtrlSettings.iFreqGPU);

   fprintf(fd, "%d %d %d\n%d %d\n", s_CtrlSettings.iNiceRouter, s_CtrlSettings.iNiceCentral, s_CtrlSettings.iNiceRXVideo, s_CtrlSettings.ioNiceRouter, s_CtrlSettings.ioNiceRXVideo);

   fprintf(fd, "video_usb: %d %d %d\n", s_CtrlSettings.iVideoForwardUSBType, s_CtrlSettings.iVideoForwardUSBPort, s_CtrlSettings.iVideoForwardUSBPacketSize);
   fprintf(fd, "telem_usb: %d %d %d\n", s_CtrlSettings.iTelemetryForwardUSBType, s_CtrlSettings.iTelemetryForwardUSBPort, s_CtrlSettings.iTelemetryForwardUSBPacketSize);
   fprintf(fd, "%d %d\n", s_CtrlSettings.iTelemetryOutputSerialPortIndex, s_CtrlSettings.iTelemetryInputSerialPortIndex);

   fprintf(fd, "%d %d\n", s_CtrlSettings.iMAVLinkSysIdController, s_CtrlSettings.iDisableHDMIOverscan);
   fprintf(fd, "%d %d\n", s_CtrlSettings.iRenderFPS, s_CtrlSettings.iShowVoltage);

   fprintf(fd, "%d %d\n", s_CtrlSettings.nRetryRetransmissionAfterTimeoutMS, s_CtrlSettings.nRequestRetransmissionsOnVideoSilenceMs);

   fprintf(fd, "%d %u\n", s_CtrlSettings.nUseFixedIP, s_CtrlSettings.uFixedIP);

   fprintf(fd, "video_eth: %d %d %d\n", s_CtrlSettings.nVideoForwardETHType, s_CtrlSettings.nVideoForwardETHPort, s_CtrlSettings.nVideoForwardETHPacketSize);

   fprintf(fd, "%d\n", s_CtrlSettings.nAutomaticTxCard);
   fprintf(fd, "%d %d\n", s_CtrlSettings.nRotaryEncoderFunction, s_CtrlSettings.nRotaryEncoderSpeed);
   fprintf(fd, "%d %d %d\n", s_CtrlSettings.nPingClockSyncFrequency, s_CtrlSettings.nGraphRadioRefreshInterval, s_CtrlSettings.nGraphVideoRefreshInterval);

   // Extra params

   fprintf(fd, "%d %d\n", s_CtrlSettings.iDisableRetransmissionsAfterControllerLinkLostMiliseconds, s_CtrlSettings.iVideoDecodeStatsSnapshotClosesOnTimeout);
   fprintf(fd, "%d %d\n", s_CtrlSettings.nRotaryEncoderFunction2, s_CtrlSettings.nRotaryEncoderSpeed2);
   fprintf(fd, "%d %d\n", s_CtrlSettings.iQAButtonRelaySwitching, s_CtrlSettings.iFreezeOSD);
   fprintf(fd, "%d %d\n", s_CtrlSettings.iDevSwitchVideoProfileUsingQAButton, s_CtrlSettings.iShowControllerAdaptiveInfoStats);
   fprintf(fd, "%d\n", s_CtrlSettings.iShowVideoStreamInfoCompact);
   fprintf(fd, "%d\n", s_CtrlSettings.iTXPowerSiK);

   fprintf(fd, "%d %d %d %d\n", s_CtrlSettings.iSearchSiKAirRate, s_CtrlSettings.iSearchSiKECC, s_CtrlSettings.iSearchSiKLBT, s_CtrlSettings.iSearchSiKMCSTR);
   fprintf(fd, "%d %d\n", s_CtrlSettings.iAudioOutputDevice, s_CtrlSettings.iAudioOutputVolume);
   fprintf(fd, "%d %u\n", s_CtrlSettings.iDevRxLoopTimeout, s_CtrlSettings.uShowBigRxHistoryInterface);
   fprintf(fd, "%d\n", s_CtrlSettings.iSiKPacketSize);
   fclose(fd);

   log_line("Saved controller settings to file: %s", FILE_CONTROLLER_SETTINGS);
   return 1;
}

int load_ControllerSettings()
{
   s_CtrlSettingsLoaded = 1;
   FILE* fd = fopen(FILE_CONTROLLER_SETTINGS, "r");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to load controller settings from file: %s (missing file). Resetted controller settings to default.",FILE_CONTROLLER_SETTINGS);
      reset_ControllerSettings();
      save_ControllerSettings();
      return 0;
   }

   int failed = 0;
   char szBuff[256];
   szBuff[0] = 0;
   if ( 1 != fscanf(fd, "%s", szBuff) )
      failed = 1;
   if ( 0 != strcmp(szBuff, CONTROLLER_SETTINGS_STAMP_ID) )
      failed = 2;

   if ( failed )
   {
      fclose(fd);
      log_softerror_and_alarm("Failed to load controller settings from file: %s (invalid config file version)",FILE_CONTROLLER_SETTINGS);
      reset_ControllerSettings();
      save_ControllerSettings();
      return 0;
   }

   if ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iDeveloperMode) )
      s_CtrlSettings.iDeveloperMode = 0;
   if ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iUseBrokenVideoCRC) )
      s_CtrlSettings.iUseBrokenVideoCRC = 0;
   if ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iHDMIBoost) )
      s_CtrlSettings.iHDMIBoost = 5;

   if ( 3 != fscanf(fd, "%d %d %d", &s_CtrlSettings.iTXPower, &s_CtrlSettings.iTXPowerAtheros, &s_CtrlSettings.iTXPowerRTL) )
   {
      s_CtrlSettings.iTXPower = DEFAULT_RADIO_TX_POWER_CONTROLLER;
      s_CtrlSettings.iTXPowerAtheros = DEFAULT_RADIO_TX_POWER_CONTROLLER;
      s_CtrlSettings.iTXPowerRTL = DEFAULT_RADIO_TX_POWER_CONTROLLER;
      hardware_set_radio_tx_power_atheros(DEFAULT_RADIO_TX_POWER_CONTROLLER);
      hardware_set_radio_tx_power_rtl(DEFAULT_RADIO_TX_POWER_CONTROLLER);
   }

   if ( 3 != fscanf(fd, "%d %d %d", &s_CtrlSettings.iMaxTXPower, &s_CtrlSettings.iMaxTXPowerAtheros, &s_CtrlSettings.iMaxTXPowerRTL) )
   {
      s_CtrlSettings.iMaxTXPower = MAX_TX_POWER;
      s_CtrlSettings.iMaxTXPowerAtheros = MAX_TX_POWER;
      s_CtrlSettings.iMaxTXPowerRTL = MAX_TX_POWER;
   }

   if ( 3 != fscanf(fd, "%d %d %d", &s_CtrlSettings.iOverVoltage, &s_CtrlSettings.iFreqARM, &s_CtrlSettings.iFreqGPU) )
      { s_CtrlSettings.iOverVoltage = 0; s_CtrlSettings.iFreqARM = 0; s_CtrlSettings.iFreqGPU = 0; }

   if ( 5 != fscanf(fd, "%d %d %d %d %d", &s_CtrlSettings.iNiceRouter, &s_CtrlSettings.iNiceCentral, &s_CtrlSettings.iNiceRXVideo, &s_CtrlSettings.ioNiceRouter, &s_CtrlSettings.ioNiceRXVideo) )
      { log_softerror_and_alarm("Failed to load controller settings (line nice)"); }


   if ( (!failed) && (3 != fscanf(fd, "%*s %d %d %d", &s_CtrlSettings.iVideoForwardUSBType, &s_CtrlSettings.iVideoForwardUSBPort, &s_CtrlSettings.iVideoForwardUSBPacketSize)) )
      { s_CtrlSettings.iVideoForwardUSBType = 0; s_CtrlSettings.iVideoForwardUSBPort = 0; s_CtrlSettings.iVideoForwardUSBPacketSize = 1024; failed = 4; }

   if ( (!failed) && (3 != fscanf(fd, "%*s %d %d %d", &s_CtrlSettings.iTelemetryForwardUSBType, &s_CtrlSettings.iTelemetryForwardUSBPort, &s_CtrlSettings.iTelemetryForwardUSBPacketSize)) )
      { s_CtrlSettings.iTelemetryForwardUSBType = 0; s_CtrlSettings.iTelemetryForwardUSBPort = 0; s_CtrlSettings.iTelemetryForwardUSBPacketSize = 1024; failed = 5; }

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.iTelemetryOutputSerialPortIndex, &s_CtrlSettings.iTelemetryInputSerialPortIndex)) )
   {
      s_CtrlSettings.iTelemetryOutputSerialPortIndex = -1;
      s_CtrlSettings.iTelemetryInputSerialPortIndex = -1;
      failed = 6;
   }
   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.iMAVLinkSysIdController, &s_CtrlSettings.iDisableHDMIOverscan)) )
   {
       s_CtrlSettings.iMAVLinkSysIdController = DEFAULT_MAVLINK_SYS_ID_CONTROLLER;
       s_CtrlSettings.iDisableHDMIOverscan = 0;
       failed = 7;
   }
   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.iRenderFPS)) )
      { s_CtrlSettings.iRenderFPS = 10; failed = 8; }

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.iShowVoltage)) )
      { s_CtrlSettings.iShowVoltage = 1; failed = 9; }

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.nRetryRetransmissionAfterTimeoutMS, &s_CtrlSettings.nRequestRetransmissionsOnVideoSilenceMs)) )
   {
      s_CtrlSettings.nRetryRetransmissionAfterTimeoutMS = DEFAULT_VIDEO_RETRANS_RETRY_TIME;
      s_CtrlSettings.nRequestRetransmissionsOnVideoSilenceMs = DEFAULT_VIDEO_RETRANS_REQUEST_ON_VIDEO_SILENCE_MS;
      failed = 10;
   }
   
   if ( (!failed) && (2 != fscanf(fd, "%d %u", &s_CtrlSettings.nUseFixedIP, &s_CtrlSettings.uFixedIP)) )
   {
      s_CtrlSettings.nUseFixedIP = 0;
      s_CtrlSettings.uFixedIP = (192<<24) | (168<<16) | (1<<8) | 20;
      failed = 11;
   }

   if ( (!failed) && (3 != fscanf(fd, "%*s %d %d %d", &s_CtrlSettings.nVideoForwardETHType, &s_CtrlSettings.nVideoForwardETHPort, &s_CtrlSettings.nVideoForwardETHPacketSize)) )
      { s_CtrlSettings.nVideoForwardETHType = 0; s_CtrlSettings.nVideoForwardETHPort = 5010; s_CtrlSettings.nVideoForwardETHPacketSize = 1024; failed = 12; }

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.nAutomaticTxCard)) )
   {
      s_CtrlSettings.nAutomaticTxCard = 1;
      failed = 13;
   }

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.nRotaryEncoderFunction, &s_CtrlSettings.nRotaryEncoderSpeed)) )
   {
      s_CtrlSettings.nRotaryEncoderFunction = 1;
      s_CtrlSettings.nRotaryEncoderSpeed = 0;
      failed = 14;
   }

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.nPingClockSyncFrequency, &s_CtrlSettings.nGraphRadioRefreshInterval)) )
   {
      s_CtrlSettings.nPingClockSyncFrequency = DEFAULT_PING_FREQUENCY;
      s_CtrlSettings.nGraphRadioRefreshInterval = 100;
      failed = 15;
   }

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.nGraphVideoRefreshInterval)) )
      s_CtrlSettings.nGraphVideoRefreshInterval = 100;

   // Extended values

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.iDisableRetransmissionsAfterControllerLinkLostMiliseconds)) )
      s_CtrlSettings.iDisableRetransmissionsAfterControllerLinkLostMiliseconds = DEFAULT_CONTROLLER_LINK_MILISECONDS_TIMEOUT_TO_DISABLE_RETRANSMISSIONS;

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.iVideoDecodeStatsSnapshotClosesOnTimeout)) )
      s_CtrlSettings.iVideoDecodeStatsSnapshotClosesOnTimeout = 1;

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.nRotaryEncoderFunction2, &s_CtrlSettings.nRotaryEncoderSpeed2)) )
   {
      s_CtrlSettings.nRotaryEncoderFunction2 = 2;
      s_CtrlSettings.nRotaryEncoderSpeed2 = 0;
   }

   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iQAButtonRelaySwitching)) )
      s_CtrlSettings.iQAButtonRelaySwitching = -1;
     
   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iFreezeOSD)) )
      s_CtrlSettings.iFreezeOSD = 0;

   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iDevSwitchVideoProfileUsingQAButton)) )
      s_CtrlSettings.iDevSwitchVideoProfileUsingQAButton = -1;
   
   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iShowControllerAdaptiveInfoStats)) )
      s_CtrlSettings.iShowControllerAdaptiveInfoStats = 0;

   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iShowVideoStreamInfoCompact)) )
      s_CtrlSettings.iShowVideoStreamInfoCompact = 0;

   if ( (!failed) && ( 1 != fscanf(fd, "%d", &s_CtrlSettings.iTXPowerSiK)) )
      s_CtrlSettings.iTXPowerSiK = DEFAULT_RADIO_SIK_TX_POWER;

   if ( (!failed) && ( 4 != fscanf(fd, "%d %d %d %d", &s_CtrlSettings.iSearchSiKAirRate, &s_CtrlSettings.iSearchSiKECC, &s_CtrlSettings.iSearchSiKLBT, &s_CtrlSettings.iSearchSiKMCSTR)) )
   {
      s_CtrlSettings.iSearchSiKAirRate = DEFAULT_RADIO_DATARATE_SIK_AIR;
      s_CtrlSettings.iSearchSiKECC = 0;
      s_CtrlSettings.iSearchSiKLBT = 0;
      s_CtrlSettings.iSearchSiKMCSTR = 0;
   }

   if ( (!failed) && (2 != fscanf(fd, "%d %d", &s_CtrlSettings.iAudioOutputDevice, &s_CtrlSettings.iAudioOutputVolume)) )
   {
      s_CtrlSettings.iAudioOutputDevice = 1;
      s_CtrlSettings.iAudioOutputVolume = 100;
      failed = 15;
   }

   if ( (!failed) && (2 != fscanf(fd, "%d %u", &s_CtrlSettings.iDevRxLoopTimeout, &s_CtrlSettings.uShowBigRxHistoryInterface)) )
   {
      s_CtrlSettings.iDevRxLoopTimeout = DEFAULT_MAX_RX_LOOP_TIMEOUT_MILISECONDS;
      s_CtrlSettings.uShowBigRxHistoryInterface = 0;
      failed = 16;
   }

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_CtrlSettings.iSiKPacketSize)) )
      { s_CtrlSettings.iSiKPacketSize = DEFAULT_SIK_PACKET_SIZE; }

   fclose(fd);

   // Validate settings

   if ( s_CtrlSettings.iMAVLinkSysIdController <= 0 || s_CtrlSettings.iMAVLinkSysIdController > 255 )
      s_CtrlSettings.iMAVLinkSysIdController = DEFAULT_MAVLINK_SYS_ID_CONTROLLER;
   if ( s_CtrlSettings.iVideoForwardUSBType < 0 || s_CtrlSettings.iVideoForwardUSBType > 1 || s_CtrlSettings.iVideoForwardUSBPacketSize == 0 || s_CtrlSettings.iVideoForwardUSBPort == 0 )
      { s_CtrlSettings.iVideoForwardUSBType = 0; s_CtrlSettings.iVideoForwardUSBPort = 0; s_CtrlSettings.iVideoForwardUSBPacketSize = 1024; }

   if ( s_CtrlSettings.iTelemetryForwardUSBType < 0 || s_CtrlSettings.iTelemetryForwardUSBType > 1 ||  s_CtrlSettings.iTelemetryForwardUSBPort == 0 ||  s_CtrlSettings.iTelemetryForwardUSBPacketSize == 0 )
      { s_CtrlSettings.iTelemetryForwardUSBType = 0; s_CtrlSettings.iTelemetryForwardUSBPort = 0; s_CtrlSettings.iTelemetryForwardUSBPacketSize = 1024; }

   if ( s_CtrlSettings.iNiceRouter < -18 || s_CtrlSettings.iNiceRouter > 0 )
      s_CtrlSettings.iNiceRouter = DEFAULT_PRIORITY_PROCESS_ROUTER;
   if ( s_CtrlSettings.iNiceRXVideo < -18 || s_CtrlSettings.iNiceRXVideo > 0 )
      s_CtrlSettings.iNiceRXVideo = DEFAULT_PRIORITY_PROCESS_VIDEO_RX;
   if ( s_CtrlSettings.iNiceCentral < -16 || s_CtrlSettings.iNiceCentral > 0 )
      s_CtrlSettings.iNiceCentral = DEFAULT_PRIORITY_PROCESS_CENTRAL;

   if ( s_CtrlSettings.iTXPowerSiK < 1 )
      s_CtrlSettings.iTXPowerSiK = DEFAULT_RADIO_SIK_TX_POWER;
   if ( s_CtrlSettings.iTXPowerSiK > 20 )
      s_CtrlSettings.iTXPowerSiK = DEFAULT_RADIO_SIK_TX_POWER;
     
   if ( s_CtrlSettings.iRenderFPS < 10 || s_CtrlSettings.iRenderFPS > 30 )
      s_CtrlSettings.iRenderFPS = 10;

   if ( s_CtrlSettings.nRotaryEncoderFunction < 0 || s_CtrlSettings.nRotaryEncoderFunction > 2 )
      s_CtrlSettings.nRotaryEncoderFunction = 1;
   if ( s_CtrlSettings.nRotaryEncoderSpeed < 0 || s_CtrlSettings.nRotaryEncoderSpeed > 1 )
      s_CtrlSettings.nRotaryEncoderSpeed = 0;

   if ( s_CtrlSettings.nPingClockSyncFrequency < 1 || s_CtrlSettings.nPingClockSyncFrequency > 50 )
      s_CtrlSettings.nPingClockSyncFrequency = DEFAULT_PING_FREQUENCY;

   if ( (s_CtrlSettings.iSiKPacketSize < 10) || (s_CtrlSettings.iSiKPacketSize > 250 ) )
      s_CtrlSettings.iSiKPacketSize = DEFAULT_SIK_PACKET_SIZE;
   if ( failed )
   {
      log_line("Incomplete/Invalid settings file %s, error code: %d. Reseted to default.", FILE_CONTROLLER_SETTINGS, failed);
      reset_ControllerSettings();
      save_ControllerSettings();
   }
   else
      log_line("Loaded controller settings from file: %s", FILE_CONTROLLER_SETTINGS);
   return 1;
}

ControllerSettings* get_ControllerSettings()
{
   if ( ! s_CtrlSettingsLoaded )
      load_ControllerSettings();
   return &s_CtrlSettings;
}

u32 compute_ping_interval_ms(u32 uModelFlags, u32 uRxTxSyncType, u32 uCurrentVideoFlags)
{
   u32 ping_interval_ms = 1000/DEFAULT_PING_FREQUENCY;
   if ( s_CtrlSettings.nPingClockSyncFrequency != 0 )
      ping_interval_ms = 1000/s_CtrlSettings.nPingClockSyncFrequency;

   if ( uModelFlags & MODEL_FLAG_PRIORITIZE_UPLINK )
      ping_interval_ms /= 2;

   #ifdef FEATURE_VEHICLE_COMPUTES_ADAPTIVE_VIDEO
   if ( uCurrentVideoFlags & ENCODING_EXTRA_FLAG_ENABLE_ADAPTIVE_VIDEO_LINK_PARAMS )
   if ( uCurrentVideoFlags & ENCODING_EXTRA_FLAG_ADAPTIVE_VIDEO_LINK_USE_CONTROLLER_INFO_TOO )
   {
      if ( ping_interval_ms > 100 )
      {
         ping_interval_ms = 100;
         if ( uModelFlags & MODEL_FLAG_PRIORITIZE_UPLINK )
            ping_interval_ms = 70;
      }
   }
   #endif
   return ping_interval_ms;
}