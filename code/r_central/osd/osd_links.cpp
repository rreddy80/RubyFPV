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
#include "../../base/hardware.h"
#include "../../base/hardware_radio.h"
#include "../../base/config.h"
#include "../../base/hdmi_video.h"
#include "../../base/models.h"
#include "../../base/ctrl_interfaces.h"
#include "../../common/string_utils.h"

#include "../../renderer/render_engine.h"

#include "osd.h"
#include "osd_common.h"
#include "../colors.h"
#include "osd_links.h"
#include "../shared_vars.h"
#include "../timers.h"

float _osd_get_radio_link_new_height()
{
   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();

   float fHeightLink = osd_get_link_bars_height(1.0);

   if ( g_pCurrentModel->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_RADIO_INTERFACES_INFO )
      fHeightLink += height_text_small;

   if ( g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_RADIO_LINK_INTERFACES_EXTENDED )
      fHeightLink += height_text_small;
   return fHeightLink;
}


float _osd_render_local_radio_link_tag_new(float xPos, float yPos, int iLocalRadioLinkId, bool bHorizontal, bool bDraw)
{
   char szBuff1[32];
   char szBuff2[32];
   char szBuff3[32];

   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();

   int iVehicleRadioLinkId = g_SM_RadioStats.radio_links[iLocalRadioLinkId].matchingVehicleRadioLinkId;
   sprintf(szBuff1, "Link-%d", iVehicleRadioLinkId+1);

   strcpy(szBuff2, str_format_frequency_no_sufix(g_pCurrentModel->radioLinksParams.link_frequency_khz[iVehicleRadioLinkId]));
   
   szBuff3[0] = 0;
   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
   {
      strcpy(szBuff3, "RELAY");
      strcpy(szBuff2, str_format_frequency_no_sufix(g_pCurrentModel->relay_params.uRelayFrequencyKhz));
   }
   float fWidthLink = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff1);
   float fWidthFreq = g_pRenderEngine->textWidth(g_idFontOSD, szBuff2);
   float fWidthRelay = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff3);

   if ( fWidthFreq > fWidthLink )
      fWidthLink = fWidthFreq;
   if ( fWidthRelay > fWidthLink )
      fWidthLink = fWidthRelay;

   fWidthLink += 0.5*height_text_small;
   float fHeight = 1.1 * height_text_small + height_text;
   
   if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      fHeight += 1.0*height_text_small;

   if ( bDraw )
   {
      g_pRenderEngine->drawBackgroundBoundingBoxes(false);

      double* pC = get_Color_OSDText();
      g_pRenderEngine->setFill(pC[0], pC[1], pC[2], 0.3);
      g_pRenderEngine->setStroke(0,0,0,0);
      g_pRenderEngine->drawRoundRect(xPos+1.0*g_pRenderEngine->getPixelWidth(), yPos + 1.0*g_pRenderEngine->getPixelHeight(), fWidthLink, fHeight, 0.003*osd_getScaleOSD());

      osd_set_colors();

      yPos += 0.07 * height_text_small;
      float fWidth = g_pRenderEngine->textWidth(g_idFontOSD, szBuff2);
      g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSD, szBuff2);

      yPos += height_text*0.9;
      fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff1);
      g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos,  g_idFontOSDSmall, szBuff1);

      yPos += height_text_small*0.9;

      // Third line
      if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
      {
         fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, "DIS");
         g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSDSmall, "DIS");
      }
      else if ( g_pCurrentModel->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
      {
         fWidth = g_pRenderEngine->textWidth(g_idFontOSDSmall, szBuff3);
         g_pRenderEngine->drawText(xPos+(fWidthLink-fWidth)*0.5, yPos, g_idFontOSDSmall, szBuff3);
      }

      if ( g_pCurrentModel->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_BACKGROUND_ON_TEXTS_ONLY )
         g_pRenderEngine->drawBackgroundBoundingBoxes(true);
   }

   if ( bHorizontal )
      return fWidthLink;
   else
      return fHeight;
}

float _osd_show_radio_bars_info(float xPos, float yPos, int iLastRxDeltaTime, int iMaxRxQuality, int dbm, int iDataRateBps, bool bShowBars, bool bShowPercentages, bool bUplink, bool bHorizontal, bool bDraw)
{
   if ( ! bShowPercentages )
   if ( ! bShowBars )
      return 0.0;

   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();

   float fTotalWidth = 0.0;
   float fTotalHeight = osd_get_link_bars_height(1.0);

   bool bShowRed = false;
   if ( iLastRxDeltaTime > 1000 )
      bShowRed = true;
   if ( iMaxRxQuality <= 0 )
      bShowRed = true;

   if ( bShowBars )
   {
       float fSize = osd_get_link_bars_width(1.0);
       if ( bDraw )
          osd_show_link_bars(xPos+fSize,yPos, iLastRxDeltaTime, iMaxRxQuality, 1.0);
       xPos += fSize;
       fTotalWidth += fSize;
   }

   float fIconHeight = fTotalHeight;
   float fIconWidth = 0.5*fIconHeight/g_pRenderEngine->getAspectRatio();

   if ( bDraw )
   {
      if ( bUplink )
         g_pRenderEngine->drawIcon(xPos-3.0*g_pRenderEngine->getPixelWidth(),yPos, fIconWidth, fIconHeight, g_idIconArrowUp);
      else
         g_pRenderEngine->drawIcon(xPos-3.0*g_pRenderEngine->getPixelWidth(),yPos, fIconWidth, fIconHeight, g_idIconArrowDown);
   }

   xPos += fIconWidth*1.1;
   fTotalWidth += fIconWidth*1.1;

   char szDBM[32];
   if ( dbm < -110 )
      strcpy(szDBM, "--- dBm");
   else
      sprintf(szDBM, "%d dBm", dbm);

   if ( bShowPercentages )
   {
      char szBuff[32];
      char szBuffDR[32];
      
      sprintf(szBuff, "%d%%", iMaxRxQuality);
      
      str_format_bitrate(iDataRateBps, szBuffDR);
      if ( strlen(szBuffDR) > 4 )
      if ( szBuffDR[strlen(szBuffDR)-2] == 'p' )
         szBuffDR[strlen(szBuffDR)-2] = 0;

      osd_set_colors();
      float fWidthPercentage = g_pRenderEngine->textWidth(g_idFontOSDSmall, "1999%");
      float fWidthDBM = g_pRenderEngine->textWidth(g_idFontOSDSmall, "-110 dBm");
      if ( fWidthDBM > fWidthPercentage )
         fWidthPercentage = fWidthDBM;
      if ( bDraw )
      {
         if ( iMaxRxQuality >= 0 )
            g_pRenderEngine->drawText(xPos, yPos, g_idFontOSDSmall, szBuff);
         g_pRenderEngine->drawText(xPos, yPos+height_text_small, g_idFontOSDSmall, szDBM);
      }
      fTotalWidth += fWidthPercentage;
   }
   if ( bHorizontal )
      return fTotalWidth;
   else
      return fTotalHeight;
}


float osd_show_local_radio_link_new(float xPos, float yPos, int iLocalRadioLinkId, bool bHorizontal)
{
   float fMarginY = 0.5*osd_getSpacingV();
   float fMarginX = 0.5*osd_getSpacingH();

   if ( ! bHorizontal )
      xPos += 4.0*g_pRenderEngine->getPixelWidth();
   float xStart = xPos;
   float yStart = yPos;

   float height_text = osd_getFontHeight();
   float height_text_small = osd_getFontHeightSmall();
   float height_text_extra_small = g_pRenderEngine->textHeight(g_idFontOSDExtraSmall);

   float fWidthTag = _osd_render_local_radio_link_tag_new(xPos,yPos, iLocalRadioLinkId, true, false);
   float fHeightTag = _osd_render_local_radio_link_tag_new(xPos,yPos, iLocalRadioLinkId, false, false);

   float fTotalWidthLink = 0.0;
   float fTotalHeightLink = fHeightTag;
   float fHeightLink = _osd_get_radio_link_new_height();

   if ( ! bHorizontal )
      fTotalHeightLink = 0.0;

   if ( bHorizontal )
   if ( fHeightLink > fTotalHeightLink )
      fTotalHeightLink = fHeightLink;

   int iVehicleRadioLinkId = g_SM_RadioStats.radio_links[iLocalRadioLinkId].matchingVehicleRadioLinkId;
   
   int iRuntimeInfoToUse = 0; // Main vehicle
   Model* pModelToUse = g_pCurrentModel;
   // int iRuntimeInfoToUse = osd_get_current_data_source_vehicle_index;
   //Model* pModelToUse = osd_get_current_data_source_vehicle_model();
   
   bool bShowVehicleSide = false;
   bool bShowControllerSide = false;
   bool bShowBars = false;
   bool bShowPercentages = false;

   bool bShowSummary = true;
   bool bShowInterfaces = false;
   bool bShowInterfacesExtended = false;

   if ( pModelToUse->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_RADIO_LINKS )
      bShowControllerSide = true;

   if ( pModelToUse->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_VEHICLE_RADIO_LINKS )
      bShowVehicleSide = true;

   if ( pModelToUse->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_RADIO_LINK_QUALITY_BARS )
      bShowBars = true;

   if ( pModelToUse->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_RADIO_LINK_QUALITY_PERCENTAGE )
      bShowPercentages = true;

   if ( pModelToUse->osd_params.osd_flags[g_pCurrentModel->osd_params.layout] & OSD_FLAG_SHOW_RADIO_INTERFACES_INFO )
   {
      bShowInterfaces = true;
      bShowSummary = false;
   }

   if ( pModelToUse->osd_params.osd_flags2[g_pCurrentModel->osd_params.layout] & OSD_FLAG2_SHOW_RADIO_LINK_INTERFACES_EXTENDED )
   {
      bShowInterfaces = true;
      bShowInterfacesExtended = true;
      bShowSummary = false;    
   }

   if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
   {
      bShowVehicleSide = true;
      bShowControllerSide = false;
   }

   if ( bHorizontal )
   {
      xPos -= fMarginX;
      yPos += fMarginY;
   }
   else
   {
      xPos += fMarginX;
      yPos += fMarginY;
   }

   if ( ! bHorizontal )
   {
      _osd_render_local_radio_link_tag_new(xStart,yStart, iLocalRadioLinkId, bHorizontal, true);
      fTotalHeightLink += fHeightTag;
      yPos += fHeightTag;
   }

   float dySignalBars = 0.0;
   if ( (! bShowInterfaces) && (! bShowInterfacesExtended) )
       dySignalBars = height_text_small*0.3;
   else
      dySignalBars = height_text_small*0.1;

   // Show vehicle side

   if ( bShowVehicleSide )
   {
      int nRxQuality = 0;
      int iDataRateUplinkBPS = 0;
      int iLastRxDeltaTime = 100000;
      int dbm = -200;
      
      for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( pModelToUse->radioInterfacesParams.interface_link_id[i] != iVehicleRadioLinkId )
            continue;
         if ( g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i] > nRxQuality )
            nRxQuality = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i];
         if ( pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId] < 0 )
             iDataRateUplinkBPS = pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId];
         else if ( g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_datarate_bps[i] > iDataRateUplinkBPS )
            iDataRateUplinkBPS = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_datarate_bps[i];
      
         if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
            iDataRateUplinkBPS = pModelToUse->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];

         if ( ! pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
         if ( (g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200) > dbm )
            dbm = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200;
      }

      int iVehicleRadioCard = -1;
      for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( pModelToUse->radioInterfacesParams.interface_link_id[i] == iVehicleRadioLinkId )
         {
            iVehicleRadioCard = i;
            break;
         }
      }
      if ( iVehicleRadioCard != -1 )
          iLastRxDeltaTime = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[iVehicleRadioLinkId].timeNow - g_VehiclesRuntimeInfo[osd_get_current_data_source_vehicle_index()].SMVehicleRxStats[iVehicleRadioLinkId].timeLastRxPacket;
            
      if ( bShowSummary )
      {
         bool bAsUplink = true;
         if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
            bAsUplink = false;
         float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateUplinkBPS, bShowBars, bShowPercentages, bAsUplink, bHorizontal, false);
         if ( bHorizontal )
         {
            xPos -= fSize;
            fTotalWidthLink += fSize;
         }
         else if ( fSize > fTotalWidthLink )
            fTotalWidthLink = fSize;

         _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateUplinkBPS, bShowBars, bShowPercentages, bAsUplink, bHorizontal, true);
         if ( ! bHorizontal )
         {
             yPos += fSize;
             fTotalHeightLink += fSize;
         }
      }
      else // Vehicle side, not summary
      {
         int iCountShown = 0;
         for( int i=0; i<pModelToUse->radioInterfacesParams.interfaces_count; i++ )
         {
            if ( pModelToUse->radioInterfacesParams.interface_link_id[i] != iVehicleRadioLinkId )
               continue;
            nRxQuality = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_link_quality[i];
            iDataRateUplinkBPS = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_datarate_bps[i];
            iLastRxDeltaTime = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[i].timeNow - g_VehiclesRuntimeInfo[iRuntimeInfoToUse].SMVehicleRxStats[i].timeLastRxPacket;
            dbm = g_VehiclesRuntimeInfo[iRuntimeInfoToUse].headerRubyTelemetryExtended.uplink_rssi_dbm[i]-200;

            if ( pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId] < 0 )
                iDataRateUplinkBPS = pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId];
            if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
            {
               iDataRateUplinkBPS = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];
               dbm = -200;
            }
            if ( iCountShown > 0 )
            {
               if ( bHorizontal )
               {
                  xPos -= osd_getSpacingH()*0.5;
                  fTotalWidthLink += osd_getSpacingH()*0.5;
               }
               else
               {
                  yPos += osd_getSpacingV()*0.5;
                  fTotalHeightLink += osd_getSpacingV()*0.5;
               }
            }

            bool bAsUplink = true;
            if ( pModelToUse->radioLinksParams.link_capabilities_flags[iVehicleRadioLinkId] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
               bAsUplink = false;
         
            float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateUplinkBPS, bShowBars, bShowPercentages, bAsUplink, bHorizontal, false);
            if ( bHorizontal )
            {
               xPos -= fSize;
               fTotalWidthLink += fSize;
            }
            else if ( fSize > fTotalWidthLink )
               fTotalWidthLink = fSize;
            _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateUplinkBPS, bShowBars, bShowPercentages, bAsUplink, bHorizontal, true);
            if ( ! bHorizontal )
            {
                yPos += fSize;
                fTotalHeightLink += fSize;
            }

            char szLine1[128];
            char szBuffDR[32];
            bool bShowLine1AsError = false;
            sprintf(szLine1, "%s: RX/TX ", pModelToUse->radioInterfacesParams.interface_szPort[i] );
            
            str_format_bitrate(iDataRateUplinkBPS, szBuffDR);
            if ( strlen(szBuffDR) > 4 )
            if ( szBuffDR[strlen(szBuffDR)-2] == 'p' )
               szBuffDR[strlen(szBuffDR)-2] = 0;
            
            strcat(szLine1, szBuffDR);

            if ( bShowLine1AsError )
               g_pRenderEngine->setColors(get_Color_IconError());

            float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
            int iFontLine1 = g_idFontOSDSmall;
            float dyLine1 = 0.0;
            if ( fWidthLine1 > fSize*0.8 )
            {
               iFontLine1 = g_idFontOSDExtraSmall;
               dyLine1 = (height_text_small - height_text_extra_small)*0.6;
            }
            fWidthLine1 = g_pRenderEngine->textWidth(iFontLine1, szLine1);
            
            if ( bHorizontal )
               g_pRenderEngine->drawText(xPos + (fSize - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars + osd_get_link_bars_height(1.0), iFontLine1, szLine1);
            else
            {
               g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars, iFontLine1, szLine1);
               yPos += height_text_small;
               fTotalHeightLink += height_text_small;             
            }
            if ( bShowLine1AsError )
               osd_set_colors();

            iCountShown++;
         }
      }
   }

   // Separator between controller and vehicle side

   if ( bShowVehicleSide && bShowControllerSide )
   {
       if ( bHorizontal )
       {
          xPos -= osd_getSpacingH()*0.5;
          fTotalWidthLink += osd_getSpacingH()*0.5;
       }
       else
       {
          yPos += osd_getSpacingV()*0.6;
          fTotalHeightLink += osd_getSpacingV()*0.6;
       }
   }

   // Controller side

   if ( bShowControllerSide )
   {
      int iCountInterfacesForCurrentLink = 0;
      if ( iLocalRadioLinkId >= 0 )
      {
         for( int k=0; k<g_SM_RadioStats.countLocalRadioInterfaces; k++ )
            if ( g_SM_RadioStats.radio_interfaces[k].assignedLocalRadioLinkId == iLocalRadioLinkId )
               iCountInterfacesForCurrentLink++;
      }

      int nRxQuality = 0;
      int iDataRateVideo = 0;
      int iDataRateData = 0;
      int iLastRxDeltaTime = 100000;
      int dbm = -200;
      for( int i=0; i<g_SM_RadioStats.countLocalRadioInterfaces; i++ )
      {
         if ( g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId != iLocalRadioLinkId )
            continue;
         if ( g_SM_RadioStats.radio_interfaces[i].rxQuality > nRxQuality )
            nRxQuality = g_SM_RadioStats.radio_interfaces[i].rxQuality;
         if ( pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId] < 0 )
             iDataRateVideo = pModelToUse->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId];
         else
         {
            if ( g_SM_RadioStats.radio_interfaces[i].lastDataRateVideo > iDataRateVideo )
              iDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastDataRateVideo;
            if ( g_SM_RadioStats.radio_interfaces[i].lastDataRateData > iDataRateData )
              iDataRateData = g_SM_RadioStats.radio_interfaces[i].lastDataRateData;
         }
         int iRxDeltaTime = g_TimeNow - g_SM_RadioStats.radio_interfaces[i].timeLastRxPacket;
         if ( iRxDeltaTime < iLastRxDeltaTime )
            iLastRxDeltaTime = iRxDeltaTime;

         if ( g_fOSDDbm[i] > dbm )
            dbm = g_fOSDDbm[i];
      }

      if ( pModelToUse->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
      {
         iDataRateVideo = pModelToUse->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];
         iDataRateData = iDataRateVideo;
      }
      if ( iDataRateVideo == 0 )
         iDataRateVideo = iDataRateData;

      if ( bShowSummary )
      {
         float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateVideo, bShowBars, bShowPercentages, false, bHorizontal, false);
         if ( bHorizontal )
         {
            xPos -= fSize;
            fTotalWidthLink += fSize;
         }
         else if ( fSize > fTotalWidthLink )
            fTotalWidthLink = fSize;

         _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, dbm, iDataRateVideo, bShowBars, bShowPercentages, false, bHorizontal, true);
         if ( ! bHorizontal )
         {
             yPos += fSize;
             fTotalHeightLink += fSize;
         }
      }
      else // Controller side, not summary
      {
         int iCountShown = 0;
         for( int i=0; i<g_SM_RadioStats.countLocalRadioInterfaces; i++ )
         {
            if ( g_SM_RadioStats.radio_interfaces[i].assignedLocalRadioLinkId != iLocalRadioLinkId )
               continue;
            nRxQuality = g_SM_RadioStats.radio_interfaces[i].rxQuality;
            iLastRxDeltaTime = g_TimeNow - g_SM_RadioStats.radio_interfaces[i].timeLastRxPacket;
            iDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastDataRateVideo;
            if ( 0 == iDataRateVideo )
               iDataRateVideo = g_SM_RadioStats.radio_interfaces[i].lastDataRateData;
              
            if ( g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId] < 0 )
                iDataRateVideo = g_pCurrentModel->radioLinksParams.link_datarate_video_bps[iVehicleRadioLinkId];

            if ( g_pCurrentModel->radioLinkIsSiKRadio(iVehicleRadioLinkId) )
               iDataRateVideo = g_pCurrentModel->radioLinksParams.link_datarate_data_bps[iVehicleRadioLinkId];

            bool bIsTxCard = false;
            if ( (iLocalRadioLinkId >= 0) && (g_SM_RadioStats.radio_links[iLocalRadioLinkId].lastTxInterfaceIndex == i) && (1 < g_SM_RadioStats.countLocalRadioInterfaces) )
               bIsTxCard = true;
      
            if ( iCountShown > 0 )
            {
               if ( bHorizontal )
               {
                  xPos -= osd_getSpacingH()*0.5;
                  fTotalWidthLink += osd_getSpacingH()*0.5;
               }
               else
               {
                  yPos += osd_getSpacingV()*0.5;
                  fTotalHeightLink += osd_getSpacingV()*0.5;
               }
            }
            float fSize = _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, g_fOSDDbm[i], iDataRateVideo, bShowBars, bShowPercentages, false, bHorizontal, false);
            if ( bHorizontal )
            {
               xPos -= fSize;
               fTotalWidthLink += fSize;
            }
            else if ( fSize > fTotalWidthLink )
               fTotalWidthLink = fSize;
            _osd_show_radio_bars_info(xPos, yPos+dySignalBars, iLastRxDeltaTime, nRxQuality, g_fOSDDbm[i], iDataRateVideo, bShowBars, bShowPercentages, false, bHorizontal, true);
            
            if ( bIsTxCard && (1<iCountInterfacesForCurrentLink) )
               g_pRenderEngine->drawIcon(xPos-height_text*0.1, yPos+dySignalBars, height_text*0.9/g_pRenderEngine->getAspectRatio() , height_text*0.9, g_idIconUplink2);

            if ( ! bHorizontal )
            {
                yPos += fSize;
                fTotalHeightLink += fSize;
            }

            char szLine1[128];
            bool bShowLine1AsError = false;
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);

            if ( NULL != pRadioHWInfo )
            {
               if ( g_SM_RadioStats.radio_links[iLocalRadioLinkId].lastTxInterfaceIndex == i )
                  sprintf(szLine1, "%s: RX/TX ", pRadioHWInfo->szUSBPort );
               else if ( g_SM_RadioStats.radio_interfaces[i].openedForRead )
                  sprintf(szLine1, "%s: RX ", pRadioHWInfo->szUSBPort );
               else if ( g_SM_RadioStats.radio_interfaces[i].openedForWrite )
                  sprintf(szLine1, "%s: -- ", pRadioHWInfo->szUSBPort );
               else
                  sprintf(szLine1, "%s: N/A ", pRadioHWInfo->szUSBPort );
            }
            else
               strcpy(szLine1, "N/A ");

            char szBuffDR[32];
            str_format_bitrate(iDataRateVideo, szBuffDR);
            if ( strlen(szBuffDR) > 4 )
            if ( szBuffDR[strlen(szBuffDR)-2] == 'p' )
               szBuffDR[strlen(szBuffDR)-2] = 0;

            strcat(szLine1, szBuffDR);
            
            if ( bShowLine1AsError )
               g_pRenderEngine->setColors(get_Color_IconError());

            float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
            int iFontLine1 = g_idFontOSDSmall;
            float dyLine1 = 0.0;
            if ( fWidthLine1 > fSize*0.8 )
            {
               iFontLine1 = g_idFontOSDExtraSmall;
               dyLine1 = (height_text_small - height_text_extra_small)*0.6;
            }
            fWidthLine1 = g_pRenderEngine->textWidth(iFontLine1, szLine1);
            
            if ( bHorizontal )
               g_pRenderEngine->drawText(xPos + (fSize - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars + osd_get_link_bars_height(1.0), iFontLine1, szLine1);
            else
            {
               g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dyLine1 + dySignalBars, iFontLine1, szLine1);
               yPos += height_text_small;
               fTotalHeightLink += height_text_small;             
            }
            if ( bShowLine1AsError )
               osd_set_colors();

            if ( bShowInterfacesExtended )
            {
               char szCardName[128];
               controllerGetCardUserDefinedNameOrType(pRadioHWInfo, szCardName);
          
               if ( NULL != pRadioHWInfo )
                  sprintf(szLine1, "%s%s", (controllerIsCardInternal(pRadioHWInfo->szMAC)?"":"(Ext) "), szCardName );
               else
                  strcpy(szLine1, szCardName);
                 
               float fWidthLine1 = g_pRenderEngine->textWidth(g_idFontOSDExtraSmall, szLine1);
               if ( bHorizontal )
                  g_pRenderEngine->drawText(xPos + (fSize - fWidthLine1)*0.5, yPos + dySignalBars + osd_get_link_bars_height(1.0) + height_text_small, g_idFontOSDExtraSmall, szLine1);
               else
               {
                  g_pRenderEngine->drawText(xStart + (osd_getVerticalBarWidth() - fWidthLine1)*0.5, yPos + dySignalBars, g_idFontOSDExtraSmall, szLine1);
                  yPos += height_text_extra_small;
                  fTotalHeightLink += height_text_extra_small;             
               }
            }
            iCountShown++;
         }
      }
   }

   if ( bHorizontal )
   {
      xPos -= fWidthTag;
      fTotalWidthLink += fWidthTag;
      _osd_render_local_radio_link_tag_new(xStart-fTotalWidthLink-2.0*fMarginX,yStart, iLocalRadioLinkId, bHorizontal, true);
   }

   double* pC = get_Color_OSDText();
   g_pRenderEngine->setFill(0,0,0,0.1);
   g_pRenderEngine->setStroke(pC[0], pC[1], pC[2], 0.7);
   //g_pRenderEngine->setStroke(255,50,50,1);
   g_pRenderEngine->setStrokeSize(2.0);

   if ( bHorizontal )
      g_pRenderEngine->drawRoundRect(xStart-fTotalWidthLink-2.0*fMarginX, yStart, fTotalWidthLink + 2.0*fMarginX, fTotalHeightLink + 2.0*fMarginY, 0.003*osd_getScaleOSD());
   else
      g_pRenderEngine->drawRoundRect(xStart, yStart, osd_getVerticalBarWidth() - 8.0*g_pRenderEngine->getPixelWidth(), fTotalHeightLink + 2.0*fMarginY, 0.003*osd_getScaleOSD());
   osd_set_colors();

   if ( bHorizontal )
      return fTotalWidthLink + 2.0 * fMarginX;
   else
      return fTotalHeightLink + 2.0 * fMarginY;
}

