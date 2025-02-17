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
#include "../base/hardware.h"
#include "../base/hw_procs.h"
#include "../base/radio_utils.h"
#include "../base/models.h"
#include "../base/ctrl_interfaces.h"
#include "../base/ctrl_settings.h"
#include "../common/string_utils.h"

Model* g_pModelVehicle = NULL;

bool configure_radios_succeeded = false;

int init_Radios()
{
   char szBuff[1024];
   char szOutput[2048];
   configure_radios_succeeded = false;

   log_line("=================================================================");
   log_line("Configuring radios: START.");

   if( access( "tmp/ruby/conf_radios", R_OK ) != -1 )
   {
      log_line("Radios already configured. Do nothing.");
      log_line("=================================================================");
      configure_radios_succeeded = true;
      return 1;
   }
   
   u32 delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   bool isStation = hardware_is_station();

   if ( isStation )
   {
      log_line("Configuring radios: we are station.");
      load_Preferences();
      load_ControllerInterfacesSettings();

      Preferences* pP = get_Preferences();
      if ( NULL != pP )
         delayMs = (u32) pP->iDebugWiFiChangeDelay;
      if ( delayMs<1 || delayMs > 200 )
         delayMs = DEFAULT_DELAY_WIFI_CHANGE;
   }
   else
   {
      log_line("Configuring radios: we are vehicle.");
      reset_ControllerInterfacesSettings();
   }
   //system("iw reg set DE");
   //hw_execute_bash_command("iw reg set DE", NULL);

   system("iw reg set BO");
   hw_execute_bash_command("iw reg set BO", NULL);

   if ( 0 == hardware_get_radio_interfaces_count() )
   {
      log_error_and_alarm("No network cards found. Nothing to configure!");
      log_line("=================================================================");
      return 0;
   }

   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
      {
         log_error_and_alarm("Failed to get radio info for interface %d.", i+1);
         continue;
      }
      if ( hardware_radio_is_sik_radio(pRadioHWInfo) )
      {
         log_line("Configuring radio interface %d: radio interface is SiK radio. Skipping it.", i+1);
         continue;
      }
      if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
      {
         log_softerror_and_alarm("Radio interface %d is unknow type. Skipping it.", i+1);
         continue;
      }
      log_line( "Configuring radio interface %d: %s ...", i+1, pRadioHWInfo->szName );
      if ( ! pRadioHWInfo->isSupported )
      {
         log_softerror_and_alarm("Found unsupported radio: %s, skipping.",pRadioHWInfo->szName);
         continue;
      }

      pRadioHWInfo->iCurrentDataRate = 0;
      sprintf(szBuff, "ifconfig %s mtu 2304 2>&1", pRadioHWInfo->szName );
      hw_execute_bash_command(szBuff, szOutput);
      if ( 0 != szOutput[0] )
         log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
      hardware_sleep_ms(delayMs);

      if ( ((pRadioHWInfo->typeAndDriver) & 0xFF) == RADIO_TYPE_ATHEROS )
      {
         //sprintf(szBuff, "ifconfig %s down", pRadioHWInfo->szName );
         //execute_bash_command(szBuff, NULL);
         //hardware_sleep_ms(delayMs);
         //sprintf(szBuff, "iw dev %s set type managed", pRadioHWInfo->szName );
         //execute_bash_command(szBuff, NULL);
         //hardware_sleep_ms(delayMs);

         sprintf(szBuff, "ifconfig %s up", pRadioHWInfo->szName );
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);
         int dataRateMb = DEFAULT_RADIO_DATARATE_VIDEO_ATHEROS/1000/1000;
         if ( isStation )
            dataRateMb = controllerGetCardDataRate(pRadioHWInfo->szMAC);
         else if ( NULL != g_pModelVehicle )
         {
            if ( g_pModelVehicle->radioInterfacesParams.interface_link_id[i] >= 0 )
            if ( g_pModelVehicle->radioInterfacesParams.interface_link_id[i] < g_pModelVehicle->radioLinksParams.links_count )
            {

               dataRateMb = g_pModelVehicle->radioLinksParams.link_datarate_video_bps[g_pModelVehicle->radioInterfacesParams.interface_link_id[i]];
               if ( dataRateMb > 0 )
                  dataRateMb /= 1000 * 1000;
            }
         }
         if ( dataRateMb == 0 )
            dataRateMb = DEFAULT_RADIO_DATARATE_VIDEO;
         if ( dataRateMb > 0 )
            sprintf(szBuff, "iw dev %s set bitrates legacy-2.4 %d lgi-2.4", pRadioHWInfo->szName, dataRateMb );
         else
            sprintf(szBuff, "iw dev %s set bitrates ht-mcs-2.4 %d lgi-2.4", pRadioHWInfo->szName, -dataRateMb-1 );
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);
         sprintf(szBuff, "ifconfig %s down 2>&1", pRadioHWInfo->szName );
         hw_execute_bash_command(szBuff, NULL);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         hardware_sleep_ms(delayMs);
         
         sprintf(szBuff, "iw dev %s set monitor none", pRadioHWInfo->szName);
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);

         sprintf(szBuff, "iwconfig %s txpower 27", pRadioHWInfo->szName);
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);
         
         sprintf(szBuff, "ifconfig %s up", pRadioHWInfo->szName );
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);
         //printf(szBuff, "iw dev %s set monitor fcsfail", pRadioHWInfo->szName);
         //execute_bash_command(szBuff, NULL);
         //hardware_sleep_ms(delayMs);

         pRadioHWInfo->iCurrentDataRate = dataRateMb;
      }
      //if ( ((pRadioHWInfo->type) & 0xFF) == RADIO_TYPE_RALINK )
      else
      {
         sprintf(szBuff, "ifconfig %s down 2>&1", pRadioHWInfo->szName );
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         hardware_sleep_ms(delayMs);

         sprintf(szBuff, "iw dev %s set monitor none 2>&1", pRadioHWInfo->szName);
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         hardware_sleep_ms(delayMs);

         sprintf(szBuff, "iwconfig %s txpower 27", pRadioHWInfo->szName);
         hw_execute_bash_command(szBuff, NULL);
         hardware_sleep_ms(delayMs);
         
         sprintf(szBuff, "ifconfig %s up 2>&1", pRadioHWInfo->szName );
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         hardware_sleep_ms(delayMs);

         //sprintf(szBuff, "iw dev %s set bitrates legacy-2.4 %d lgi-2.4", pRadioHWInfo->szName, 18 );
         //hw_execute_bash_command(szBuff, NULL);
         //hardware_sleep_ms(delayMs);
      }

      if ( hardware_radioindex_supports_frequency(i, DEFAULT_FREQUENCY58) )
      {
         sprintf(szBuff, "iw dev %s set freq %d 2>&1", pRadioHWInfo->szName, DEFAULT_FREQUENCY58/1000);
         pRadioHWInfo->uCurrentFrequencyKhz = DEFAULT_FREQUENCY58;
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         pRadioHWInfo->lastFrequencySetFailed = 0;
         pRadioHWInfo->uFailedFrequencyKhz = 0;
      }
      else if ( hardware_radioindex_supports_frequency(i, DEFAULT_FREQUENCY) )
      {
         sprintf(szBuff, "iw dev %s set freq %d 2>&1", pRadioHWInfo->szName, DEFAULT_FREQUENCY/1000);
         pRadioHWInfo->uCurrentFrequencyKhz = DEFAULT_FREQUENCY;
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
            log_softerror_and_alarm("Unexpected result: [%s]", szOutput);
         pRadioHWInfo->lastFrequencySetFailed = 0;
         pRadioHWInfo->uFailedFrequencyKhz = 0;
      }
      else
      {
         pRadioHWInfo->uCurrentFrequencyKhz = 0;
         pRadioHWInfo->lastFrequencySetFailed = 1;
         pRadioHWInfo->uFailedFrequencyKhz = DEFAULT_FREQUENCY;
      }
      //if ( isStation && controllerIsCardDisabled(pRadioHWInfo->szMAC) )
      //{
      //   sprintf(szBuff, "ifconfig %s down", pRadioHWInfo->szName );
      //   execute_bash_command(szBuff, NULL);
      //}
      hardware_sleep_ms(2*delayMs);

      for( int k=0; k<3; k++ )
      {
         sprintf(szBuff, "ifconfig | grep %s", pRadioHWInfo->szName);
         hw_execute_bash_command(szBuff, szOutput);
         if ( 0 != szOutput[0] )
         {
            log_line("Radio interface %s state: [%s]", pRadioHWInfo->szName, szOutput);
            break;
         }
         hardware_sleep_ms(50);
      }
   }

   FILE* fd = fopen("tmp/ruby/conf_radios", "w");
   fprintf(fd, "done");
   fclose(fd);
   
   hardware_save_radio_info();
   log_line("Configuring radios COMPLETED.");
   log_line("=================================================================");
   log_line("Radio interfaces and frequencies assigned:");
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      log_line("   * Radio interface %d: %s on port %s, %s %s %s: %s", i+1, str_get_radio_card_model_string(pRadioHWInfo->iCardModel), pRadioHWInfo->szUSBPort, pRadioHWInfo->szName, pRadioHWInfo->szMAC, pRadioHWInfo->szDescription, str_format_frequency(pRadioHWInfo->uCurrentFrequencyKhz));
   }
   log_line("=================================================================");
   configure_radios_succeeded = true;
   return 1;
}


int main(int argc, char *argv[])
{
   if ( strcmp(argv[argc-1], "-ver") == 0 )
   {
      printf("%d.%d (b%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR/10, SYSTEM_SW_BUILD_NUMBER);
      return 0;
   }
   

   log_init("RubyRadioInit");

   char szOutput[1024];
   hw_execute_bash_command_raw("ls /sys/class/net/", szOutput);
   log_line("Network devices found: [%s]", szOutput);

   hardware_enumerate_radio_interfaces();

   int retry = 0;
   while ( 0 == hardware_get_radio_interfaces_count() && retry < 10 )
   {
      hardware_sleep_ms(200);
      char szComm[256];
      sprintf(szComm, "rm -rf %s", FILE_CURRENT_RADIO_HW_CONFIG);
      hw_execute_bash_command(szComm, NULL);
      hardware_reset_radio_enumerated_flag();
      hardware_enumerate_radio_interfaces();
      retry++;
   }

   if ( 0 == hardware_get_radio_interfaces_count() )
   {
      log_error_and_alarm("There are no radio interfaces (2.4/5.8 wlans) on this device.");
      return -1;
   }    

   g_pModelVehicle = new Model();
   if ( ! g_pModelVehicle->loadFromFile(FILE_CURRENT_VEHICLE_MODEL) )
   {
      log_softerror_and_alarm("Can't load current model vehicle.");
      g_pModelVehicle = NULL;
   }
   
   init_Radios();

   log_line("Ruby Init Radio process completed.");
   return (0);
} 