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
#include "hardware.h"
#include "hw_procs.h"
#include "../radio/radiopackets2.h"
#include "ctrl_preferences.h"
#include <ctype.h>

#define PREFERENCES_SETTINGS_STAMP_ID "vIV.3"
Preferences s_Preferences;


void reset_Preferences()
{
   memset(&s_Preferences, 0, sizeof(s_Preferences));
   s_Preferences.iMenusStacked = 1;
   s_Preferences.iInvertColorsOSD = 0;
   s_Preferences.iInvertColorsMenu = 0;
   s_Preferences.iActionQuickButton1 = quickActionVideoRecord;
   s_Preferences.iActionQuickButton2 = quickActionCycleOSD;
   s_Preferences.iActionQuickButton3 = quickActionTakePicture;
   s_Preferences.iOSDScreenSize = 1;
   s_Preferences.iScaleOSD = 0;
   s_Preferences.iScaleAHI = 0;
   s_Preferences.iScaleMenus = 0;
   s_Preferences.iAddOSDOnScreenshots = 1;
   s_Preferences.iShowLogWindow = 0;
   s_Preferences.iMenuDismissesAlarm = 0;
   s_Preferences.iVideoDestination = prefVideoDestination_Disk;
   s_Preferences.iStartVideoRecOnArm = 0;
   s_Preferences.iStopVideoRecOnDisarm = 0;
   s_Preferences.iShowControllerCPUInfo = 1;
   s_Preferences.iShowBigRecordButton = 0;
   s_Preferences.iSwapUpDownButtons = 1;
   s_Preferences.iSwapUpDownButtonsValues = 0;

   s_Preferences.iAHIToSides = 0;
   s_Preferences.iAHIShowAirSpeed = 0;
   s_Preferences.iAHIStrokeSize = 0;

   s_Preferences.iUnits = prefUnitsMetric;

   s_Preferences.iColorOSD[0] = 255;
   s_Preferences.iColorOSD[1] = 250;
   s_Preferences.iColorOSD[2] = 220;
   s_Preferences.iColorOSD[3] = 100; // 100%
   s_Preferences.iColorOSDOutline[0] = 185;
   s_Preferences.iColorOSDOutline[1] = 185;
   s_Preferences.iColorOSDOutline[2] = 155;
   s_Preferences.iColorOSDOutline[3] = 70; // 70%
   s_Preferences.iColorAHI[0] = 208;
   s_Preferences.iColorAHI[1] = 238;
   s_Preferences.iColorAHI[2] = 214;
   s_Preferences.iColorAHI[3] = 100; // 100%
   s_Preferences.iOSDOutlineThickness = 0;
   s_Preferences.iRecordingLedAction = 2; // blink

   s_Preferences.iDebugMaxPacketSize = MAX_PACKET_PAYLOAD;
   s_Preferences.iDebugSBWS = 2;
   s_Preferences.iDebugRestartOnRadioSilence = 0;
   s_Preferences.iOSDFont = 1;
   s_Preferences.iPersistentMessages = 1;
   s_Preferences.nLogLevel = 0;
   s_Preferences.iDebugShowDevVideoStats = 0;
   s_Preferences.iDebugShowDevRadioStats = 0;
   s_Preferences.iDebugShowFullRXStats = 0;
   s_Preferences.iDebugShowVehicleVideoStats = 0;
   s_Preferences.iDebugShowVehicleVideoGraphs = 0;
   s_Preferences.iDebugShowVideoSnapshotOnDiscard = 0;
   s_Preferences.iDebugWiFiChangeDelay = DEFAULT_DELAY_WIFI_CHANGE;

   s_Preferences.iAutoExportSettings = 1;
   s_Preferences.iAutoExportSettingsWasModified = 0;

   s_Preferences.iShowProcessesMonitor = 0;
   s_Preferences.iShowCPULoad = 0;
   s_Preferences.uEnabledAlarms = 0xFFFFFFFF & (~ALARM_ID_CONTROLLER_CPU_LOOP_OVERLOAD) & (~ALARM_ID_CPU_RX_LOOP_OVERLOAD);
   s_Preferences.iShowOnlyPresentTxPowerCards = 1;
}

int save_Preferences()
{
   FILE* fd = fopen(FILE_PREFERENCES, "w");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to save preferences to file: %s",FILE_PREFERENCES);
      return 0;
   }

   fprintf(fd, "%s\n", PREFERENCES_SETTINGS_STAMP_ID);
   
   fprintf(fd, "%d %d %d\n", s_Preferences.iMenusStacked, s_Preferences.iInvertColorsOSD, s_Preferences.iInvertColorsMenu); 
   fprintf(fd, "%d %d %d %d\n", s_Preferences.iOSDScreenSize, s_Preferences.iScaleOSD, s_Preferences.iScaleMenus, s_Preferences.iScaleAHI); 
   fprintf(fd, "%d %d %d\n", s_Preferences.iActionQuickButton1, s_Preferences.iActionQuickButton2, s_Preferences.iActionQuickButton3);
   fprintf(fd, "%d %d\n", s_Preferences.iAddOSDOnScreenshots, s_Preferences.iStatsToggledOff);
   fprintf(fd, "%d\n", s_Preferences.iShowLogWindow);

   fprintf(fd, "%d\n", s_Preferences.iOSDFlipVertical);
   fprintf(fd, "%d\n", s_Preferences.iMenuDismissesAlarm);

   fprintf(fd, "%d %d %d\n", s_Preferences.iVideoDestination, s_Preferences.iStartVideoRecOnArm, s_Preferences.iStopVideoRecOnDisarm);

   fprintf(fd, "%d\n", s_Preferences.iShowControllerCPUInfo);
   fprintf(fd, "%d\n", s_Preferences.iShowBigRecordButton);
   fprintf(fd, "%d %d\n", s_Preferences.iSwapUpDownButtons, s_Preferences.iSwapUpDownButtonsValues);
   fprintf(fd, "%d %d %d\n", s_Preferences.iAHIToSides, s_Preferences.iAHIShowAirSpeed, s_Preferences.iAHIStrokeSize);

   fprintf(fd, "%d\n", s_Preferences.iUnits);

   fprintf(fd, "%d %d\n", s_Preferences.iDebugMaxPacketSize, s_Preferences.iDebugSBWS);

   fprintf(fd, "%d %d %d %d\n", s_Preferences.iColorOSD[0], s_Preferences.iColorOSD[1], s_Preferences.iColorOSD[2], s_Preferences.iColorOSD[3]);
   fprintf(fd, "%d %d %d %d\n", s_Preferences.iColorAHI[0], s_Preferences.iColorAHI[1], s_Preferences.iColorAHI[2], s_Preferences.iColorAHI[3]);

   fprintf(fd, "%d\n", s_Preferences.iDebugRestartOnRadioSilence);

   fprintf(fd, "%d %d %d %d\n", s_Preferences.iColorOSDOutline[0], s_Preferences.iColorOSDOutline[1], s_Preferences.iColorOSDOutline[2], s_Preferences.iColorOSDOutline[3]);
   fprintf(fd, "%d\n", s_Preferences.iOSDOutlineThickness);
   fprintf(fd, "%d\n", s_Preferences.iOSDFont);
   fprintf(fd, "%d %d\n", s_Preferences.iRecordingLedAction, s_Preferences.iPersistentMessages);
   fprintf(fd, "%d\n", s_Preferences.nLogLevel);
   fprintf(fd, "%d %d\n", s_Preferences.iDebugShowDevVideoStats, s_Preferences.iDebugShowDevRadioStats);
   fprintf(fd, "%d\n", s_Preferences.iDebugShowFullRXStats);
   fprintf(fd, "%d\n", s_Preferences.iDebugWiFiChangeDelay);

   // Extra values

   fprintf(fd, "%d\n", s_Preferences.iDebugShowVideoSnapshotOnDiscard);
   fprintf(fd, "%d %d\n", s_Preferences.iDebugShowVehicleVideoStats, s_Preferences.iDebugShowVehicleVideoGraphs);

   fprintf(fd, "%d %d %d\n", s_Preferences.iAutoExportSettings, s_Preferences.iAutoExportSettingsWasModified, s_Preferences.iShowProcessesMonitor);

   fprintf(fd, "%u\n", s_Preferences.uEnabledAlarms);

   fprintf(fd, "%d %d\n", s_Preferences.iShowCPULoad, s_Preferences.iShowOnlyPresentTxPowerCards);

   fclose(fd);
   log_line("Saved preferences to file: %s", FILE_PREFERENCES);
   return 1;
}

int load_Preferences()
{
   FILE* fd = fopen(FILE_PREFERENCES, "r");
   if ( NULL == fd )
   {
      reset_Preferences();
      log_softerror_and_alarm("Failed to load preferences from file: %s (missing file)",FILE_PREFERENCES);
      return 0;
   }

   int failed = 0;

   char szBuff[256];
   szBuff[0] = 0;
   if ( 1 != fscanf(fd, "%s", szBuff) )
      failed = 1;
   if ( 0 != strcmp(szBuff, PREFERENCES_SETTINGS_STAMP_ID) )
      failed = 1;

   if ( failed )
   {
      reset_Preferences();
      log_softerror_and_alarm("Failed to load preferences from file: %s (invalid config file version)",FILE_PREFERENCES);
      fclose(fd);
      return 0;
   }

   if ( 3 != fscanf(fd, "%d %d %d", &s_Preferences.iMenusStacked, &s_Preferences.iInvertColorsOSD, &s_Preferences.iInvertColorsMenu) )
      failed = 1;

   if ( 4 != fscanf(fd, "%d %d %d %d", &s_Preferences.iOSDScreenSize, &s_Preferences.iScaleOSD, &s_Preferences.iScaleMenus, &s_Preferences.iScaleAHI) )
      failed = 1;

   if ( 3 != fscanf(fd, "%d %d %d", &s_Preferences.iActionQuickButton1, &s_Preferences.iActionQuickButton2, &s_Preferences.iActionQuickButton3) )
      failed = 1;

   if ( s_Preferences.iActionQuickButton1 < 0 || s_Preferences.iActionQuickButton1 >= quickActionLast )
      s_Preferences.iActionQuickButton1 = quickActionCycleOSD;
   if ( s_Preferences.iActionQuickButton2 < 0 || s_Preferences.iActionQuickButton2 >= quickActionLast )
      s_Preferences.iActionQuickButton2 = quickActionTakePicture;
   if ( s_Preferences.iActionQuickButton3 < 0 || s_Preferences.iActionQuickButton3 >= quickActionLast )
      s_Preferences.iActionQuickButton3 = quickActionToggleAllOff;

   if ( 2 != fscanf(fd, "%d %d", &s_Preferences.iAddOSDOnScreenshots, &s_Preferences.iStatsToggledOff) )
      failed = 1;

   if ( 1 != fscanf(fd, "%d", &s_Preferences.iShowLogWindow) )
      failed = 1;

   if ( 1 != fscanf(fd, "%d", &s_Preferences.iOSDFlipVertical) )
      failed = 1;

   if ( 1 != fscanf(fd, "%d", &s_Preferences.iMenuDismissesAlarm) )
      failed = 1;

   if ( (!failed) && 3 != fscanf(fd, "%d %d %d", &s_Preferences.iVideoDestination, &s_Preferences.iStartVideoRecOnArm, &s_Preferences.iStopVideoRecOnDisarm) )
      failed = 1;

   if ( s_Preferences.iVideoDestination != 0 && s_Preferences.iVideoDestination != 1 )
      s_Preferences.iVideoDestination = 0;

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iShowControllerCPUInfo) )
      failed = 1;
   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iShowBigRecordButton) )
      failed = 1;

   if ( (!failed) && 2 != fscanf(fd, "%d %d", &s_Preferences.iSwapUpDownButtons, &s_Preferences.iSwapUpDownButtonsValues) )
      failed = 1;

   if ( (!failed) && 3 != fscanf(fd, "%d %d %d", &s_Preferences.iAHIToSides, &s_Preferences.iAHIShowAirSpeed, &s_Preferences.iAHIStrokeSize) )
      failed = 1;

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iUnits) )
      failed = 1;

   if ( (!failed) && 2 != fscanf(fd, "%d %d", &s_Preferences.iDebugMaxPacketSize, &s_Preferences.iDebugSBWS) )
      failed = 1;

   if ( (!failed) && 4 != fscanf(fd, "%d %d %d %d", &s_Preferences.iColorOSD[0], &s_Preferences.iColorOSD[1], &s_Preferences.iColorOSD[2], &s_Preferences.iColorOSD[3]) )
      failed = 1;
   if ( (!failed) && 4 != fscanf(fd, "%d %d %d %d", &s_Preferences.iColorAHI[0], &s_Preferences.iColorAHI[1], &s_Preferences.iColorAHI[2], &s_Preferences.iColorAHI[3]) )
      failed = 1;

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iDebugRestartOnRadioSilence) )
      failed = 1;

   if ( (!failed) && 4 != fscanf(fd, "%d %d %d %d", &s_Preferences.iColorOSDOutline[0], &s_Preferences.iColorOSDOutline[1], &s_Preferences.iColorOSDOutline[2], &s_Preferences.iColorOSDOutline[3]) )
      failed = 1;

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iOSDOutlineThickness) )
      failed = 1;

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.iOSDFont) )
   {
      s_Preferences.iOSDFont = 1;
      failed = 1;
   }
   if ( (!failed) && (1 != fscanf(fd, "%d", &s_Preferences.iRecordingLedAction)) )
      { s_Preferences.iRecordingLedAction = 2; failed = 1; }

   if ( (!failed) && (1 != fscanf(fd, "%d", &s_Preferences.iPersistentMessages)) )
      { s_Preferences.iPersistentMessages = 1; failed = 1; }

   if ( (!failed) && 1 != fscanf(fd, "%d", &s_Preferences.nLogLevel) )
      failed = 1;

   int bOk = 1;
   if ( bOk && 2 != fscanf(fd, "%d %d", &s_Preferences.iDebugShowDevVideoStats, &s_Preferences.iDebugShowDevRadioStats) )
   {
      s_Preferences.iDebugShowDevVideoStats = 1;
      s_Preferences.iDebugShowDevRadioStats = 1;
      bOk = 0;
   }

   if ( bOk && 1 != fscanf(fd, "%d", &s_Preferences.iDebugShowFullRXStats) )
   {
      s_Preferences.iDebugShowFullRXStats = 0;
      bOk = 0;
   }

   if ( bOk && 1 != fscanf(fd, "%d", &s_Preferences.iDebugWiFiChangeDelay) )
   {
      s_Preferences.iDebugShowFullRXStats = 0;
      bOk = 0;
   }

   // Extra fields

   if ( bOk && 1 != fscanf(fd, "%d", &s_Preferences.iDebugShowVideoSnapshotOnDiscard) )
   {
      s_Preferences.iDebugShowVideoSnapshotOnDiscard = 0;
   }

   if ( bOk && 2 != fscanf(fd, "%d %d", &s_Preferences.iDebugShowVehicleVideoStats, &s_Preferences.iDebugShowVehicleVideoGraphs) )
   {
      s_Preferences.iDebugShowVehicleVideoStats = 0;
      s_Preferences.iDebugShowVehicleVideoGraphs = 0;
   }

   if ( bOk && 2 != fscanf(fd, "%d %d", &s_Preferences.iAutoExportSettings, &s_Preferences.iAutoExportSettingsWasModified) )
   {
      s_Preferences.iAutoExportSettings = 1;
      s_Preferences.iAutoExportSettingsWasModified = 0;
   }

   if ( bOk && 1 != fscanf(fd, "%d", &s_Preferences.iShowProcessesMonitor) )
      s_Preferences.iShowProcessesMonitor = 0;

   if ( bOk && 1 != fscanf(fd, "%u", &s_Preferences.uEnabledAlarms) )
      s_Preferences.uEnabledAlarms = 0xFFFFFFFF;

   if ( bOk && 2 != fscanf(fd, "%d %d", &s_Preferences.iShowCPULoad, &s_Preferences.iShowOnlyPresentTxPowerCards) )
      s_Preferences.iShowCPULoad = 0;

   // End reading file;
   // Validate settings

   if ( s_Preferences.iColorOSD[3] == 0 )
   {
      s_Preferences.iColorOSD[0] = 255;
      s_Preferences.iColorOSD[1] = 250;
      s_Preferences.iColorOSD[2] = 220;
      s_Preferences.iColorOSD[3] = 100; // 100%
   }

   if ( s_Preferences.iColorOSDOutline[3] == 0 )
   {
      s_Preferences.iColorOSDOutline[0] = 185;
      s_Preferences.iColorOSDOutline[1] = 185;
      s_Preferences.iColorOSDOutline[2] = 155;
      s_Preferences.iColorOSDOutline[3] = 70; // 70%
   }

   if ( s_Preferences.iColorAHI[3] == 0 )
   {
      s_Preferences.iColorAHI[0] = 208;
      s_Preferences.iColorAHI[1] = 238;
      s_Preferences.iColorAHI[2] = 214;
      s_Preferences.iColorAHI[3] = 100; // 100%
   }

   if ( s_Preferences.iDebugMaxPacketSize < 100 ||
        s_Preferences.iDebugMaxPacketSize > MAX_PACKET_PAYLOAD )
      s_Preferences.iDebugMaxPacketSize = MAX_PACKET_PAYLOAD;

   if ( s_Preferences.iDebugSBWS < 2 || s_Preferences.iDebugSBWS > 16 )
      s_Preferences.iDebugSBWS = 2;

   if ( failed )
   {
      log_softerror_and_alarm("Failed to load preferences from file: %s (invalid config file)",FILE_PREFERENCES);
      fclose(fd);
      return 0;
   }
   fclose(fd);
   log_line("Loaded preferences from file: %s", FILE_PREFERENCES);
   return 1;
}

Preferences* get_Preferences()
{
   return &s_Preferences;
}

