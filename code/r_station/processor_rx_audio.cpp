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
#include "../base/hw_procs.h"
#include "processor_rx_audio.h"

#include "../radio/radiopackets2.h"
#include "../radio/radiolink.h"
#include "../radio/radiopacketsqueue.h"
#include "../radio/fec.h" 
#include "packets_utils.h"

#include "shared_vars.h"
#include "timers.h"

bool s_bHasAudioOutputDevice = false;
int s_fPipeAudio = -1;

u32 s_uCurrentRxAudioBlockIndex = MAX_U32;

u32 s_AudioPacketsPerBlock = 0;
u32 s_AudioFECPerBlock = 0;
u32 s_AudioPacketSize = 0;

u8 s_BufferAudioPackets[MAX_BUFFERED_AUDIO_PACKETS][MAX_PACKET_PAYLOAD];
bool s_BufferAudioPacketsReceived[MAX_BUFFERED_AUDIO_PACKETS];
u32 s_uReceivedDataPacketsForCurrentBlock = 0;
u32 s_uReceivedECPacketsForCurrentBlock = 0;

u32 s_uLastOutputedBlockIndex = MAX_U32;
u32 s_uLastOutputedPacketIndex = MAX_U32;

unsigned int fec_decode_audio_missing_packets[MAX_TOTAL_PACKETS_IN_BLOCK];
unsigned int fec_decode_audio_fec_indexes[MAX_TOTAL_PACKETS_IN_BLOCK];
u8* fec_decode_audio_data_packets[MAX_TOTAL_PACKETS_IN_BLOCK];
u8* fec_decode_audio_fec_packets[MAX_TOTAL_PACKETS_IN_BLOCK];
unsigned int missing_audio_packets_count_for_fec = 0;

int s_iAudioRecordingSegment = 0;
FILE* s_pFileAudioRecording = NULL;
int s_iTokenPositionToCheck = 0;
char s_szAudioToken[24];

FILE* s_pFileRawStream = NULL;

void _stop_audio_player_and_pipe()
{
   if ( (NULL == g_pCurrentModel) || (! g_pCurrentModel->audio_params.has_audio_device) )
   {
      log_line("[AudioRx] No audio capture device on current vehicle, nothing to stop.");
      return;
   }

   if ( -1 != s_fPipeAudio )
      close(s_fPipeAudio);
   s_fPipeAudio = -1;

   char szComm[256];
   char szOutput[128];

   hw_execute_bash_command("pidof aplay", szOutput);
   if ( strlen(szOutput) > 2 )
   {
      sprintf(szComm, "kill -9 %s 2>/dev/null", szOutput);
      hw_execute_bash_command( szComm, NULL); 
   }
}

void _start_audio_player_and_pipe()
{
   if ( g_bSearching )
   {
      log_line("[AudioRx] Audio not started, controller is in search mode.");
      return;
   }

   if ( (NULL == g_pCurrentModel) || (! g_pCurrentModel->audio_params.has_audio_device) )
   {
      log_line("[AudioRx] No audio capture device on current vehicle.");
      return;
   }

   if ( (NULL == g_pCurrentModel) || (! g_pCurrentModel->audio_params.enabled) )
   {
      log_line("[AudioRx] Audio is disabled on current vehicle.");
      return;
   }

   char szComm[128];
   sprintf(szComm, "aplay -c 1 --rate 44100 --format S16_LE %s 2>/dev/null &", FIFO_RUBY_AUDIO1);
   hw_execute_bash_command(szComm, NULL);

   log_line("[AudioRx] Opening audio pipe write endpoint: %s", FIFO_RUBY_AUDIO1);
   s_fPipeAudio = open(FIFO_RUBY_AUDIO1, O_WRONLY);
   if ( s_fPipeAudio < 0 )
   {
      log_error_and_alarm("[AudioRx] Failed to open audio pipe write endpoint: %s",FIFO_RUBY_AUDIO1);
      return;
   }
   log_line("[AudioRx] Opened successfully audio pipe write endpoint: %s", FIFO_RUBY_AUDIO1);
}

void _reset_current_audio_rx_block()
{
   for( int i=0; i<MAX_BUFFERED_AUDIO_PACKETS; i++ )
      s_BufferAudioPacketsReceived[i] = false;
   s_uReceivedDataPacketsForCurrentBlock = 0;
   s_uReceivedECPacketsForCurrentBlock = 0;
}

void _output_audio_block(u32 uBlockIndex, u32 uPacketIndex, u32 uAudioSize)
{
   s_uLastOutputedBlockIndex = uBlockIndex;
   s_uLastOutputedPacketIndex = uPacketIndex;

   int iBreakFoundPosition = -1;
   u8* pData = &s_BufferAudioPackets[uPacketIndex][0];

   for( int i=0; i<(int)uAudioSize; i++ )
   {
      if ( (*pData) != s_szAudioToken[s_iTokenPositionToCheck] )
      {
         s_iTokenPositionToCheck = 0;
         pData++;
         continue;
      }
      pData++;
      s_iTokenPositionToCheck++;
      if ( s_szAudioToken[s_iTokenPositionToCheck] == 0 )
      {
         iBreakFoundPosition = i+1;
         break;
      }
   }

   if ( iBreakFoundPosition == -1 )
   {
      if ( s_fPipeAudio != -1 )
         write(s_fPipeAudio, &s_BufferAudioPackets[uPacketIndex][0], uAudioSize);
      #ifdef FEATURE_LOCAL_AUDIO_RECORDING
      if ( NULL != s_pFileAudioRecording )
        fwrite(&s_BufferAudioPackets[uPacketIndex][0], 1, uAudioSize, s_pFileAudioRecording);
      #endif
      return;
   }

   #ifdef FEATURE_LOCAL_AUDIO_RECORDING
   if ( NULL != s_pFileAudioRecording )
   {
      fclose(s_pFileAudioRecording);
      s_pFileAudioRecording = NULL;
   }
   #endif

   if ( iBreakFoundPosition >= 0 )
   {
      _stop_audio_player_and_pipe();
      _start_audio_player_and_pipe();

      int iToOutput = (int)uAudioSize - iBreakFoundPosition;

      if ( (iToOutput > 0) && (s_fPipeAudio != -1) )
         write(s_fPipeAudio, &s_BufferAudioPackets[uPacketIndex][iBreakFoundPosition], iToOutput);
   
      #ifdef FEATURE_LOCAL_AUDIO_RECORDING
      s_iAudioRecordingSegment++;
      char szBuff[256];
      sprintf(szBuff, "%s%d", FILE_TMP_AUDIO_RECORDING, s_iAudioRecordingSegment);
      s_pFileAudioRecording = fopen(szBuff, "wb");

      if ( NULL != s_pFileAudioRecording )
      if ( iToOutput > 0 )
         fwrite(&s_BufferAudioPackets[uPacketIndex][iBreakFoundPosition], 1, iToOutput, s_pFileAudioRecording);
      #endif
   }
}


void _try_reconstruct_and_output_audio()
{
   if ( -1 == s_fPipeAudio )
      return;

   if ( s_uReceivedDataPacketsForCurrentBlock + s_uReceivedECPacketsForCurrentBlock < s_AudioPacketsPerBlock )
      return;

   // Reconstruct block

   // Add existing data packets, mark and count the ones that are missing

   missing_audio_packets_count_for_fec = 0;
   for( u32 i=0; i<s_AudioPacketsPerBlock; i++ )
   {
      fec_decode_audio_data_packets[i] = &s_BufferAudioPackets[i][0];
      if ( ! s_BufferAudioPacketsReceived[i] )
      {
         fec_decode_audio_missing_packets[missing_audio_packets_count_for_fec] = i;
         missing_audio_packets_count_for_fec++;
      }
   }

   // Add the needed FEC packets to the list
   unsigned int pos = 0;
   for( u32 i=0; i<s_AudioFECPerBlock; i++ )
   {
      if ( s_BufferAudioPacketsReceived[i+s_AudioPacketsPerBlock] )
      {
         fec_decode_audio_fec_packets[pos] = &s_BufferAudioPackets[i+s_AudioPacketsPerBlock][0];
         fec_decode_audio_fec_indexes[pos] = i;
         pos++;
         if ( pos == missing_audio_packets_count_for_fec )
            break;
      }
   }

   fec_decode(s_AudioPacketSize, fec_decode_audio_data_packets, (unsigned int) s_AudioPacketsPerBlock, fec_decode_audio_fec_packets, fec_decode_audio_fec_indexes, fec_decode_audio_missing_packets, missing_audio_packets_count_for_fec );

   for( u32 i=0; i<s_AudioPacketsPerBlock; i++ )
      _output_audio_block(s_uCurrentRxAudioBlockIndex, (u32)i, s_AudioPacketSize);
}

void init_processing_audio()
{
   s_uCurrentRxAudioBlockIndex = MAX_U32;
   _reset_current_audio_rx_block();

   s_uLastOutputedBlockIndex = MAX_U32;
   s_uLastOutputedPacketIndex = MAX_U32;

   s_AudioPacketsPerBlock = 4;
   s_AudioFECPerBlock = 2;
   if ( NULL != g_pCurrentModel )
   {
      s_AudioPacketsPerBlock = g_pCurrentModel->audio_params.flags & 0xFF;
      s_AudioFECPerBlock = (g_pCurrentModel->audio_params.flags >> 8) & 0xFF;
   }

   s_fPipeAudio = -1;
   s_bHasAudioOutputDevice = false;

   if ( (NULL != g_pCurrentModel) && (! g_pCurrentModel->audio_params.has_audio_device) )
   {
      log_line("[AudioRx] Current vehilce has no audio device. Do not enable audio rx.");
      return;
   }

   char szOutput[4096];
   hw_execute_bash_command_raw("aplay -l 2>&1", szOutput );
   if ( 0 == szOutput[0] || NULL != strstr(szOutput, "no soundcards") || NULL != strstr(szOutput, "no sound") )
   {
      log_softerror_and_alarm("[AudioRx] No output audio devices/soundcards on the controller. Audio output is disabled.");
      s_bHasAudioOutputDevice = false;
   }
   else
      s_bHasAudioOutputDevice = true;


   s_iAudioRecordingSegment = 0;

   s_iTokenPositionToCheck = 0;
   strcpy(s_szAudioToken, "0123456789");
   s_szAudioToken[10] = 10;
   s_szAudioToken[11] = 0;
  
   _start_audio_player_and_pipe();

   s_pFileRawStream = fopen("raw_audio_in_stream.data", "wb");

}

void uninit_processing_audio()
{
   _stop_audio_player_and_pipe();


   if ( NULL != s_pFileRawStream )
      fclose(s_pFileRawStream);
   s_pFileRawStream = NULL;
}

void process_received_audio_packet(u8* pPacketBuffer)
{
   t_packet_header* pPH = (t_packet_header*)pPacketBuffer;
   u8* pData = pPacketBuffer + sizeof(t_packet_header);
   u32 uAudioBlockSegmentIndex = 0;

   if ( NULL != s_pFileRawStream )
      fwrite(pPacketBuffer, 1, pPH->total_length, s_pFileRawStream);

   memcpy((u8*)&uAudioBlockSegmentIndex, pData, sizeof(u32));
   u32 uBlockIndex = uAudioBlockSegmentIndex>>8;
   u32 uPacketIndex = uAudioBlockSegmentIndex & 0xFF;
   pData += sizeof(u32);

   u32 uAudioSize = pPH->total_length-sizeof(u32);
   if ( pPH->packet_flags & PACKET_FLAGS_BIT_EXTRA_DATA )
   {
      u8 size = *(((u8*)pPH) + pPH->total_length-1);
      uAudioSize -= size;
   }

   s_AudioPacketSize = uAudioSize;
   
   if ( uPacketIndex >= MAX_BUFFERED_AUDIO_PACKETS )
      return;

   // First time we try to process audio packets?

   if ( s_uCurrentRxAudioBlockIndex == MAX_U32 )
   {
      // Wait for the begining of the next block
      if ( uPacketIndex >= s_AudioFECPerBlock )
         return;
  
      s_uCurrentRxAudioBlockIndex = uBlockIndex;
      _reset_current_audio_rx_block();
   }

   if ( uBlockIndex < s_uCurrentRxAudioBlockIndex )
      return;

   if ( uBlockIndex != s_uCurrentRxAudioBlockIndex )
   {
      // Try reconstruction of last block if needed
      if ( s_uLastOutputedPacketIndex < s_AudioPacketsPerBlock-1 )
      if ( s_uReceivedDataPacketsForCurrentBlock + s_uReceivedECPacketsForCurrentBlock >= s_AudioPacketsPerBlock )
      {
         _try_reconstruct_and_output_audio();
      }

      s_uLastOutputedBlockIndex = s_uCurrentRxAudioBlockIndex;
      s_uLastOutputedPacketIndex = s_AudioPacketsPerBlock-1;

      _reset_current_audio_rx_block();
      s_uCurrentRxAudioBlockIndex = uBlockIndex;

      // Wait for begining of the next block
      if ( uPacketIndex >= s_AudioFECPerBlock )
         return;
   }

   if ( s_BufferAudioPacketsReceived[uPacketIndex] )
      return;

   if ( uPacketIndex < s_AudioPacketsPerBlock )
      s_uReceivedDataPacketsForCurrentBlock++;
   else
      s_uReceivedECPacketsForCurrentBlock++;

   s_BufferAudioPacketsReceived[uPacketIndex] = true;
   memcpy(&s_BufferAudioPackets[uPacketIndex][0], pData, uAudioSize);

   u32 uNextBlockToOutput = s_uLastOutputedBlockIndex;
   u32 uNextPacketToOutput = s_uLastOutputedPacketIndex;

   uNextPacketToOutput++;
   if ( uNextPacketToOutput >= s_AudioPacketsPerBlock )
   {
      uNextBlockToOutput++;
      uNextPacketToOutput = 0;
   }

   if ( uPacketIndex < s_AudioPacketsPerBlock )
   if ( (uBlockIndex == uNextBlockToOutput) && (uPacketIndex == uNextPacketToOutput) )
   {
      _output_audio_block(uBlockIndex, uPacketIndex, uAudioSize);
   }
}
