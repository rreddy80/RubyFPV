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
#include <ctype.h>


u32 channels433[] = {425000, 426000, 427000, 428000, 429000, 430000, 431000, 432000, 433000, 434000, 435000, 435000, 437000, 438000, 439000, 440000, 441000, 442000, 443000, 444000, 445000};
u32 channels868[] = {858000, 859000, 860000, 861000, 862000, 863000, 864000, 865000, 866000, 867000, 868000, 869000, 870000, 871000, 872000, 873000, 874000, 875000};
u32 channels915[] = {910000,911000,912000,913000,914000,915000,916000,917000,918000,919000,920000};

u32 channels23[] = { 2312000, 2317000, 2322000, 2327000, 2332000, 2337000, 2342000, 2347000, 2352000, 2357000, 2362000, 2367000, 2372000, 2377000, 2382000, 2387000, 2392000, 2397000, 2402000, 2407000 };
u32 channels24[] = { 2412000, 2417000, 2422000, 2427000, 2432000, 2437000, 2442000, 2447000, 2452000, 2457000, 2462000, 2467000, 2472000, 2484000 };
u32 channels25[] = { 2487000, 2489000, 2492000, 2494000, 2497000, 2499000, 2512000, 2532000, 2572000, 2592000, 2612000, 2632000, 2652000, 2672000, 2692000, 2712000 };
u32 channels58[] = { 5180000, 5200000, 5220000, 5240000, 5260000, 5280000, 5300000, 5320000, 5500000, 5520000, 5540000, 5560000, 5580000, 5600000, 5620000, 5640000, 5660000, 5680000, 5700000, 5745000, 5765000, 5785000, 5805000, 5825000, 5845000, 5865000, 5885000 };

// in 1 Mb increments, in bps
int s_WiFidataRates[] = {2000000, 6000000, 9000000, 12000000, 18000000, 24000000, 36000000, 48000000, 54000000};
int s_SiKdataRates[] = {2000, 4000, 8000, 16000, 19000, 24000, 32000, 48000, 64000, 96000, 128000, 192000, 250000};

u32* getChannels433() { return channels433; }
int getChannels433Count() { return sizeof(channels433)/sizeof(channels433[0]); }
u32* getChannels868() { return channels868; }
int getChannels868Count() { return sizeof(channels868)/sizeof(channels868[0]); }
u32* getChannels915() { return channels915; }
int getChannels915Count() { return sizeof(channels915)/sizeof(channels915[0]); }

u32* getChannels24() { return channels24; }
int getChannels24Count() { return sizeof(channels24)/sizeof(channels24[0]); }
u32* getChannels23() { return channels23; }
int getChannels23Count() { return sizeof(channels23)/sizeof(channels23[0]); }
u32* getChannels25() { return channels25; }
int getChannels25Count() { return sizeof(channels25)/sizeof(channels25[0]); }
u32* getChannels58() { return channels58; }
int getChannels58Count() { return sizeof(channels58)/sizeof(channels58[0]); }


int* getSiKAirDataRates()
{
   return s_SiKdataRates;
}

int getSiKAirDataRatesCount()
{
   return sizeof(s_SiKdataRates)/sizeof(s_SiKdataRates[0]);
}


int getBand(u32 freqKhz)
{
   if ( freqKhz < 10000 ) // Old format
   {
      if ( freqKhz < 2700 )
         return RADIO_HW_SUPPORTED_BAND_24;
      else
         return RADIO_HW_SUPPORTED_BAND_58;
   }
   if ( freqKhz < 500000 )
      return RADIO_HW_SUPPORTED_BAND_433;
   if ( freqKhz < 900000 )
      return RADIO_HW_SUPPORTED_BAND_868;
   if ( freqKhz < 950000 )
      return RADIO_HW_SUPPORTED_BAND_915;
   if ( freqKhz < 2412000 )
      return RADIO_HW_SUPPORTED_BAND_23;
   if ( freqKhz < 2487000 )
      return RADIO_HW_SUPPORTED_BAND_24;
   if ( freqKhz < 5000000 )
      return RADIO_HW_SUPPORTED_BAND_25;
   if ( freqKhz > 5000000 )
      return RADIO_HW_SUPPORTED_BAND_58;

   return 0;
}

int getChannelIndexForFrequency(u32 nBand, u32 freqKhz)
{
   int nChannel = -1;

   if ( nBand == RADIO_HW_SUPPORTED_BAND_433 )
      for( int i=0; i<sizeof(channels433[0])/sizeof(channels433); i++ )
         if ( freqKhz == channels433[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_868 )
      for( int i=0; i<sizeof(channels868[0])/sizeof(channels868); i++ )
         if ( freqKhz == channels868[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_915 )
      for( int i=0; i<sizeof(channels915[0])/sizeof(channels915); i++ )
         if ( freqKhz == channels915[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_23 )
      for( int i=0; i<getChannels23Count(); i++ )
         if ( freqKhz == getChannels23()[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_24 )
      for( int i=0; i<getChannels24Count(); i++ )
         if ( freqKhz == getChannels24()[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_25 )
      for( int i=0; i<getChannels25Count(); i++ )
         if ( freqKhz == getChannels25()[i] )
         {
            nChannel = i;
            break;
         }

   if ( nBand == RADIO_HW_SUPPORTED_BAND_58 )
      for( int i=0; i<getChannels58Count(); i++ )
         if ( freqKhz == getChannels58()[i] )
         {
            nChannel = i;
            break;
         }
   return nChannel;
}

int isFrequencyInBands(u32 freqKhz, u8 bands)
{
   if ( freqKhz < 500000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_433 )
         return 1;
      return 0;
   }
   if ( freqKhz < 890000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_868 )
         return 1;
      return 0;
   }
   if ( freqKhz < 950000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_915 )
         return 1;
      return 0;
   }
   if ( freqKhz < 2412000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_23 )
         return 1;
      return 0;
   }
   if ( freqKhz < 2487000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_24 )
         return 1;
      return 0;
   }
   if ( freqKhz < 5000000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_25 )
         return 1;
      return 0;
   }
   if ( freqKhz > 5000000 )
   {
      if ( bands & RADIO_HW_SUPPORTED_BAND_58 )
         return 1;
      return 0;
   }
   return 0;
}

int getSupportedChannels(u32 supportedBands, int includeSeparator, u32* pOutChannels, int maxChannels)
{
   if ( NULL == pOutChannels || 0 == maxChannels )
      return 0;

   int countSupported = 0;

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_433 )
   {
      for( int i=0; i<getChannels433Count(); i++ )
      {
         *pOutChannels = getChannels433()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_868 )
   {
      for( int i=0; i<getChannels868Count(); i++ )
      {
         *pOutChannels = getChannels868()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_915 )
   {
      for( int i=0; i<getChannels915Count(); i++ )
      {
         *pOutChannels = getChannels915()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_23 )
   {
      for( int i=0; i<getChannels23Count(); i++ )
      {
         *pOutChannels = getChannels23()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_24 )
   {
      for( int i=0; i<getChannels24Count(); i++ )
      {
         *pOutChannels = getChannels24()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_25 )
   {
      for( int i=0; i<getChannels25Count(); i++ )
      {
         *pOutChannels = getChannels25()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   if ( supportedBands & RADIO_HW_SUPPORTED_BAND_58 )
   {
      for( int i=0; i<getChannels58Count(); i++ )
      {
         *pOutChannels = getChannels58()[i];
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
      if ( includeSeparator )
      {
         *pOutChannels = -1;
         pOutChannels++;
         countSupported++;
         if ( countSupported >= maxChannels )
            return countSupported;
      }
   }

   return countSupported;
}

int *getDataRatesBPS() { return s_WiFidataRates; }
int getDataRatesCount() { return sizeof(s_WiFidataRates)/sizeof(s_WiFidataRates[0]); }

u32 getRealDataRateFromMCSRate(int mcsIndex)
{
   if ( 0 == mcsIndex )
      return 6000000;
   if ( 1 == mcsIndex )
      return 13000000;
   if ( 2 == mcsIndex )
      return 19000000;
   if ( 3 == mcsIndex )
      return 26000000;
   if ( 4 == mcsIndex )
      return 39000000;
   if ( 5 == mcsIndex )
      return 52000000;
   if ( 6 == mcsIndex )
      return 58000000;
   return 58000000;
}

u32 getRealDataRateFromRadioDataRate(int dataRateBPS)
{
   if ( dataRateBPS < 0 )
      return getRealDataRateFromMCSRate(-dataRateBPS-1);
   else if ( dataRateBPS <= 56 )
      return ((u32)(dataRateBPS))*1000*1000;
   else
      return (u32)dataRateBPS;
}

void getSystemVersionString(char* p, u32 swversion)
{
   if ( NULL == p )
      return;
   u32 val = swversion;
   u32 major = (val >> 8) & 0xFF;
   u32 minor = val & 0xFF;
   sprintf(p, "%u.%u", major, minor);
   int len = strlen(p);

   if ( len > 2 )
   {
      if ( p[len-1] == '0' && p[len-2] != '.' )
         p[len-1] = 0;
   }

   if ( minor > 10 && ((minor % 10) != 0) )
      sprintf(p, "%u.%u.%u", major, minor/10, minor%10);
}

int config_file_get_value(const char* szPropName)
{
   char szComm[1024];
   char szOut[1024];
   char* szTmp = NULL;
   int value = 0;

   sprintf(szComm, "grep '#%s!=' /boot/config.txt", szPropName);
   hw_execute_bash_command_silent(szComm, szOut);
   if ( strlen(szOut) > 5 )
   {
      szTmp = strchr(szOut, '=');
      if ( NULL != szTmp )
         sscanf(szTmp+1, "%d", &value);
      if ( value > 0 )
         value = -value;
   }

   sprintf(szComm, "grep '%s=' /boot/config.txt", szPropName);
   hw_execute_bash_command_silent(szComm, szOut);
   if ( strlen(szOut) > 5 )
   {
      szTmp = strchr(szOut, '=');
      if ( NULL != szTmp )
         sscanf(szTmp+1, "%d", &value);
      if ( value < 0 )
         value = -value;
   }
   return value;
}


void config_file_add_value(const char* szFile, const char* szPropName, int value)
{
   char szComm[1024];
   char szOutput[1024];
   sprintf(szComm, "cat %s | grep %s", szFile, szPropName);
   hw_execute_bash_command(szComm, szOutput);
   if ( strlen(szOutput) >= strlen(szPropName) )
      return;

   u8 data[4096];
   FILE* fd = fopen(szFile, "r");
   if ( NULL == fd )
      return;
   int nSize = fread(data, 1, 4095, fd);
   fclose(fd);
   if ( nSize <= 0 )
      return;

   fd = fopen(szFile, "w");
   if ( NULL == fd )
      return;

   fprintf(fd, "%s=%d\n", szPropName, value);
   fwrite(data, 1, nSize, fd);
   fclose(fd);
}


void config_file_set_value(const char* szFile, const char* szPropName, int value)
{
   char szComm[1024];
   if ( value <= 0 )
   {
      sprintf(szComm, "sed -i 's/#%s!=[-0-9]*/#%s!=%d/g' %s", szPropName, szPropName, value, szFile);
      hw_execute_bash_command(szComm, NULL);

      sprintf(szComm, "sed -i 's/%s=[-0-9]*/#%s!=%d/g' %s", szPropName, szPropName, value, szFile);
      hw_execute_bash_command(szComm, NULL);
   }
   else
   {
      sprintf(szComm, "sed -i 's/#%s!=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
      hw_execute_bash_command(szComm, NULL);

      sprintf(szComm, "sed -i 's/%s=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
      hw_execute_bash_command(szComm, NULL);
   }
}

void config_file_force_value(const char* szFile, const char* szPropName, int value)
{
   char szComm[1024];
   sprintf(szComm, "sed -i 's/#%s!=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
   hw_execute_bash_command(szComm, NULL);

   sprintf(szComm, "sed -i 's/#%s=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
   hw_execute_bash_command(szComm, NULL);

   sprintf(szComm, "sed -i 's/%s!=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
   hw_execute_bash_command(szComm, NULL);

   sprintf(szComm, "sed -i 's/%s=[-0-9]*/%s=%d/g' %s", szPropName, szPropName, value, szFile);
   hw_execute_bash_command(szComm, NULL);
}


void save_simple_config_fileU(const char* fileName, u32 value)
{
   FILE* fd = fopen(fileName, "w");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to simple configuration to file: %s",fileName);
      return;
   }
   fprintf(fd, "%u\n", value);
   fclose(fd);
   log_line("Saved value %u to file: %s", value, fileName);
}

u32 load_simple_config_fileU(const char* fileName, u32 defaultValue)
{
   u32 returnValue = defaultValue;

   FILE* fd = fopen(fileName, "r");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to load simple configuration from file: %s (missing file)",fileName);
      return defaultValue;
   }
   if ( 1 != fscanf(fd, "%u", &returnValue) )
   {
      log_softerror_and_alarm("Failed to load simple configuration from file: %s (invalid config file)",fileName);
      return defaultValue;
   }   
   
   fclose(fd);
   log_line("Loaded value %u from file: %s", returnValue, fileName);
   return returnValue;
}

void save_simple_config_fileI(const char* fileName, int value)
{
   FILE* fd = fopen(fileName, "w");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to save simple configuration to file: %s",fileName);
      return;
   }
   fprintf(fd, "%d\n", value);
   fclose(fd);
}

int load_simple_config_fileI(const char* fileName, int defaultValue)
{
   int returnValue = defaultValue;

   FILE* fd = fopen(fileName, "r");
   if ( NULL == fd )
   {
      log_softerror_and_alarm("Failed to load simple configuration from file: %s (missing file)",fileName);
      return defaultValue;
   }
   if ( 1 != fscanf(fd, "%d", &returnValue) )
   {
      log_softerror_and_alarm("Failed to load simple configuration from file: %s (invalid config file)",fileName);
      log_line("Saving a default value (%d) and file.", defaultValue);
      save_simple_config_fileI(fileName, defaultValue);
      return defaultValue;
   }   
   
   fclose(fd);
   return returnValue;
}

void get_Ruby_BaseVersion(int* pMajor, int* pMinor)
{
   int iMajor = 0;
   int iMinor = 0;

   char szVersion[32];
   szVersion[0] = 0;

   FILE* fd = fopen(FILE_INFO_VERSION, "r");
   if ( NULL == fd )
      fd = fopen("ruby_ver.txt", "r");
   if ( NULL != fd )
   {
      if ( 1 != fscanf(fd, "%s", szVersion) )
         szVersion[0] = 0;
      fclose(fd);
   }

   if ( 0 != szVersion[0] )
   {
      char* p = &szVersion[0];
      while ( *p )
      {
         if ( isdigit(*p) )
           iMajor = iMajor * 10 + ((*p)-'0');
         if ( (*p) == '.' )
           break;
         p++;
      }
      if ( 0 != *p )
      {
         p++;
         while ( *p )
         {
            if ( isdigit(*p) )
               iMinor = iMinor * 10 + ((*p)-'0');
            if ( (*p) == '.' )
              break;
            p++;
         }
      }
      if ( iMinor > 9 )
         iMinor = iMinor/10;
   }
   if ( NULL != pMajor )
      *pMajor = iMajor;
   if ( NULL != pMinor )
      *pMinor = iMinor;
}

void get_Ruby_UpdatedVersion(int* pMajor, int* pMinor)
{
   int iMajor = 0;
   int iMinor = 0;

   char szVersion[32];
   szVersion[0] = 0;

   FILE* fd = fopen(FILE_INFO_LAST_UPDATE, "r");
   if ( NULL != fd )
   {
      if ( 1 != fscanf(fd, "%s", szVersion) )
         szVersion[0] = 0;
      fclose(fd);
   }

   if ( 0 != szVersion[0] )
   {
      char* p = &szVersion[0];
      while ( *p )
      {
         if ( isdigit(*p) )
           iMajor = iMajor * 10 + ((*p)-'0');
         if ( (*p) == '.' )
           break;
         p++;
      }
      if ( 0 != *p )
      {
         p++;
         while ( *p )
         {
            if ( isdigit(*p) )
               iMinor = iMinor * 10 + ((*p)-'0');
            if ( (*p) == '.' )
              break;
            p++;
         }
      }
      if ( iMinor > 9 )
         iMinor = iMinor/10;
   }
   if ( NULL != pMajor )
      *pMajor = iMajor;
   if ( NULL != pMinor )
      *pMinor = iMinor;
}
