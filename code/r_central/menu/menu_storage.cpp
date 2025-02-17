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

#include "menu.h"
#include "menu_objects.h"
#include "menu_storage.h"
#include "menu_confirmation.h"

#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include "../media.h"
#include "../launchers_controller.h"


const char* s_szWarningFreeDiskSpace = "You are running low on free storage space. Move your media files to a USB memory stick.";

MenuStorage::MenuStorage(void)
:Menu(MENU_ID_STORAGE, "Media & Storage", NULL)
{
   m_Width = 0.58;
   m_Height = 0.62;
   m_xPos = menu_get_XStartPos(m_Width); m_yPos = 0.12;
   m_VideoInfoFilesCount = 0;
   m_PicturesFilesCount = 0;
   m_UIFilesPage = 0;
   m_UIFilesPerPage = 8;
   m_UIFilesColumns = 2;
   m_StaticMenuItemsCount = 0;
   m_StaticMenuItemsCountBeforeUIFiles = 0;
   m_ViewScreenShotIndex = -1;
   m_ScreenshotImageId = MAX_U32;
   m_pPopupProgress = NULL;
}

MenuStorage::~MenuStorage()
{
   if ( MAX_U32 != m_ScreenshotImageId )
      g_pRenderEngine->freeImage(m_ScreenshotImageId);
   m_ScreenshotImageId = MAX_U32;

   for( int i=0; i<m_VideoInfoFilesCount; i++ )
      free(m_szVideoInfoFiles[i]);
   m_VideoInfoFilesCount = 0;

   for( int i=0; i<m_PicturesFilesCount; i++ )
      free(m_szPicturesFiles[i]);
   m_PicturesFilesCount = 0;
}

void MenuStorage::valuesToUI()
{
}

void MenuStorage::onShow()
{
   char szBuff[1024];
   removeAllTopLines();
   removeAllItems();

   media_scan_files();

   if ( 1 == hw_execute_bash_command_raw("df -m /home/pi/ruby | grep root", szBuff) )
   {
      char szTemp[1024];
      long lb, lu, lf;
      sscanf(szBuff, "%s %ld %ld %ld", szTemp, &lb, &lu, &lf);
      m_MemUsed = lu;
      m_MemFree = lf;
   }

   ruby_signal_alive();
   buildFilesListPictures();
   buildFilesListVideo();

   m_UIFilesPage = 0;

   addMenuItem(new MenuItem("Expand File System", "Expands the file system to occupy the entire available SD card space."));
   addMenuItem(new MenuItem("Copy media files to USB memory stick", "Copy your screenshots and videos to an external USB memory stick."));
   addMenuItem(new MenuItem("Move media files to USB memory stick", "Move your screenshots and videos to an external USB memory stick."));
   addMenuItem(new MenuItem("Delete all files", "Deletes all videos, pictures from the SD Card."));
   m_IndexViewPictures = addMenuItem(new MenuItem("View Screenshots", "View the screenshots"));
   if ( 0 == media_get_screenshots_count() )
      m_pMenuItems[m_IndexViewPictures]->setEnabled(false);
   addMenuItem(new MenuItem("Prev Page",""));
   addMenuItem(new MenuItem("Next Page",""));

   m_StaticMenuItemsCountBeforeUIFiles = 5;
   m_StaticMenuItemsCount = 7;

   if ( m_VideoInfoFilesCount <= m_UIFilesPerPage )
   {
      m_pMenuItems[m_StaticMenuItemsCount-1]->setEnabled(false);
      m_pMenuItems[m_StaticMenuItemsCount-2]->setEnabled(false);
   }

   float height_text = g_pRenderEngine->textHeight(g_idFontMenu);
   m_ExtraItemsHeight = 0.5*height_text;
   m_ExtraItemsHeight += 2.4 * height_text *(1.0+MENU_ITEM_SPACING);

   if ( m_MemFree < 1000 )
      m_ExtraItemsHeight += height_text *(1.0+2.0*MENU_TEXTLINE_SPACING);

   m_ExtraItemsHeight += height_text *(1.2+MENU_ITEM_SPACING) * (m_UIFilesPerPage / m_UIFilesColumns);
   m_ExtraItemsHeight += height_text * 1.5;

   Menu::onShow();
}


void MenuStorage::Render()
{
   if ( -1 != m_ViewScreenShotIndex )
   {
      g_pRenderEngine->drawImage(0, 0, 1,1, m_ScreenshotImageId);
      g_pRenderEngine->setColors(get_Color_MenuText());
      char szBuff[128];
      sprintf(szBuff, "Screenshot %d of %d", m_ViewScreenShotIndex+1, media_get_screenshots_count() );
      g_pRenderEngine->drawText(0.05, 0.05, g_idFontMenu, szBuff );
      sprintf(szBuff, "Press [+/-] to navigate and [Back] to close");
      g_pRenderEngine->drawText(0.05, 0.08, g_idFontMenu, szBuff );
      return;
   }

   RenderPrepare();
   float y0 = RenderFrameAndTitle();
   float y = y0;
   float x = m_xPos+m_sfMenuPaddingX;
   float w = 0;
   float height_text = g_pRenderEngine->textHeight(g_idFontMenu);
   char szBuff[1024];
  
   g_pRenderEngine->setColors(get_Color_MenuText());
   g_pRenderEngine->setStrokeSize(0);

   if ( m_MemFree >= 1024 )
      sprintf(szBuff, "%d Gb ", (int)m_MemFree/1024);
   else
      sprintf(szBuff, "%d Mb ", (int)m_MemFree);
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, szBuff );
   w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff);
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, "free out of " );
   w += g_pRenderEngine->textWidth(g_idFontMenu, "free out of " );
   if ( m_MemFree+m_MemUsed >= 1024 )
      sprintf(szBuff, "%d Gb ", (int)(m_MemFree+m_MemUsed)/1024);
   else
      sprintf(szBuff, "%d Mb ", (int)(m_MemFree+m_MemUsed));
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, szBuff);
   w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff);
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, "total space." );
   w += g_pRenderEngine->textWidth(g_idFontMenu, "total space." );

   y += height_text *(1.0+MENU_ITEM_SPACING);

   if ( m_MemFree < 1000 )
   {
      y += height_text * MENU_TEXTLINE_SPACING;
      y += g_pRenderEngine->drawMessageLines(m_xPos+m_sfMenuPaddingX, y, s_szWarningFreeDiskSpace, MENU_TEXTLINE_SPACING, getUsableWidth(), g_idFontMenu);
      y += height_text * MENU_TEXTLINE_SPACING;
   }
   w = 0;
   sprintf(szBuff, "%d ", media_get_videos_count());
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, szBuff );
   w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff );
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, "videos and " );
   w += g_pRenderEngine->textWidth(g_idFontMenu, "videos and " );

   sprintf(szBuff, "%d ", media_get_screenshots_count());
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, szBuff );
   w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff );
   g_pRenderEngine->drawText(x+w, y, g_idFontMenu, "screenshots stored on persistent media storage." );
   w += g_pRenderEngine->textWidth(g_idFontMenu, "screenshots stored on persistent media storage." );

   y += height_text *(1.0+3.0*MENU_ITEM_SPACING);

   for( int i=0; i<m_StaticMenuItemsCountBeforeUIFiles; i++ )
      y += RenderItem(i,y)*1.1;

   y += height_text*0.6;

   float xpi = x;
   float ypi = y;
   float heightItem = height_text * (1.0+4.0*MENU_ITEM_SPACING);
   float widthItem = m_Width/m_UIFilesColumns - 2*m_sfMenuPaddingX;

   int index = m_UIFilesPerPage * m_UIFilesPage;
   for( int i=0; i<m_UIFilesPerPage; i++ )
   {
      if ( index >= m_VideoInfoFilesCount )
         break;
      if ( i == m_UIFilesPerPage/2 )
      {
         xpi = x + m_Width/m_UIFilesColumns - m_sfMenuPaddingX;
         ypi = y;
      }

      if ( m_SelectedIndex == i+m_StaticMenuItemsCountBeforeUIFiles )
      {
         g_pRenderEngine->setColors(get_Color_ItemSelectedBg());
         g_pRenderEngine->drawRoundRect(xpi-m_sfSelectionPaddingX, ypi-m_sfSelectionPaddingY, widthItem + 2.0*m_sfSelectionPaddingX, height_text+2.0*m_sfSelectionPaddingY, MENU_ROUND_MARGIN*m_sfMenuPaddingY);
         g_pRenderEngine->drawRoundRect(xpi+widthItem + 2.0 * m_sfSelectionPaddingX- 0.045*m_sfScaleFactor, ypi+height_text - 2.0*m_sfSelectionPaddingY, 0.045*m_sfScaleFactor, 1.2*height_text, 0.002*m_sfMenuPaddingY);
         g_pRenderEngine->setColors(get_Color_ItemSelectedText());
         g_pRenderEngine->drawTextLeft( xpi+widthItem + 2.0 * m_sfSelectionPaddingX -0.014*m_sfScaleFactor, ypi + height_text*0.8, g_idFontMenu, "Play" );
      }
      else
         g_pRenderEngine->setColors(get_Color_MenuText());

      w = 0.0;
      strcpy(szBuff, m_szVideoInfoFiles[index]);
      szBuff[strlen(szBuff)-5] = 0;
      g_pRenderEngine->drawText(xpi+w, ypi, g_idFontMenu, szBuff );
      w = g_pRenderEngine->textWidth(g_idFontMenu, szBuff );
      w += 0.5*height_text / g_pRenderEngine->getAspectRatio();
      sprintf(szBuff, "%02d:%02d",m_VideoFilesDuration[index]/60, m_VideoFilesDuration[index]%60);
      g_pRenderEngine->drawText(xpi+w, ypi, g_idFontMenu, szBuff );
      w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff );
      w += 0.5*height_text / g_pRenderEngine->getAspectRatio();
      sprintf(szBuff, "%dx%d, %d fps",m_VideoFilesWidth[index], m_VideoFilesHeight[index], m_VideoFilesFPS[index]);
      g_pRenderEngine->drawText(xpi+w, ypi, g_idFontMenu, szBuff );
      w += g_pRenderEngine->textWidth(g_idFontMenu, szBuff );
      ypi += heightItem;
      index++;
   }

   y += height_text *(1.2+MENU_ITEM_SPACING) * (m_UIFilesPerPage / m_UIFilesColumns);
   y += height_text * 1.5;

   g_pRenderEngine->setColors(get_Color_MenuText());

   float maxWidth = getUsableWidth();
   
   int maxMenuIndex = m_StaticMenuItemsCount-1;
   int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
   if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
      maxMenuIndex += m_UIFilesPerPage;
   else
      maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;

   sprintf(szBuff, "Page %d of %d", m_UIFilesPage+1,1+(m_VideoInfoFilesCount/m_UIFilesPerPage));
   g_pRenderEngine->drawTextLeft(x+maxWidth-2*m_sfMenuPaddingX, y+height_text*0.4, g_idFontMenu, szBuff); 

   m_pMenuItems[m_StaticMenuItemsCount-2]->Render(m_RenderXPos + m_sfMenuPaddingX, y, m_SelectedIndex == (maxMenuIndex-1), m_fSelectionWidth*0.34);
   m_pMenuItems[m_StaticMenuItemsCount-1]->Render(m_RenderXPos + 2.0*m_sfMenuPaddingX + m_fSelectionWidth*0.34, y, m_SelectedIndex == (maxMenuIndex), m_fSelectionWidth*0.34);

   g_pRenderEngine->setColors(get_Color_MenuText());
}


void MenuStorage::onMoveUp(bool bIgnoreReversion)
{
   if ( m_ViewScreenShotIndex != -1 )
   {
      m_ViewScreenShotIndex++;
      if ( m_ViewScreenShotIndex >= media_get_screenshots_count() )
         m_ViewScreenShotIndex = 0;

      char szFile[1024];
      sprintf(szFile, "%s%s", FOLDER_MEDIA, m_szPicturesFiles[m_ViewScreenShotIndex]); 
      g_pRenderEngine->freeImage(m_ScreenshotImageId);
      m_ScreenshotImageId = g_pRenderEngine->loadImage(szFile);

      return;
   }

   int maxMenuIndex = m_StaticMenuItemsCount-1;
   int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
   if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
      maxMenuIndex += m_UIFilesPerPage;
   else
      maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;

   if ( m_SelectedIndex > 0 )
      m_SelectedIndex--;
   else
      m_SelectedIndex = maxMenuIndex;
   
   onFocusedItemChanged();
}

void MenuStorage::onMoveDown(bool bIgnoreReversion)
{ 
   if ( m_ViewScreenShotIndex != -1 )
   {
      m_ViewScreenShotIndex--;
      if ( m_ViewScreenShotIndex <= 0 )
         m_ViewScreenShotIndex = media_get_screenshots_count()-1;

      char szFile[1024];
      sprintf(szFile, "%s%s", FOLDER_MEDIA, m_szPicturesFiles[m_ViewScreenShotIndex]); 
      g_pRenderEngine->freeImage(m_ScreenshotImageId);
      m_ScreenshotImageId = g_pRenderEngine->loadImage(szFile);

      return;
   }

   int maxMenuIndex = m_StaticMenuItemsCount-1;
   int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
   if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
      maxMenuIndex += m_UIFilesPerPage;
   else
      maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;

   if ( m_SelectedIndex < maxMenuIndex )
      m_SelectedIndex++;
   else
      m_SelectedIndex = 0;

   onFocusedItemChanged();
}

void MenuStorage::onMoveLeft(bool bIgnoreReversion)
{
}

void MenuStorage::onMoveRight(bool bIgnoreReversion)
{
}


void MenuStorage::onFocusedItemChanged()
{
   if ( 0 < m_ItemsCount && m_SelectedIndex >= 0 && m_SelectedIndex < m_ItemsCount && NULL != m_pMenuItems[m_SelectedIndex] )
      setTooltip( m_pMenuItems[m_SelectedIndex]->getTooltip() );
}


void MenuStorage::onReturnFromChild(int returnValue)
{
   Menu::onReturnFromChild(returnValue);
   if ( 1 != returnValue )
      return;

   if ( 5 == m_iConfirmationId )
   {
      log_line("Confirmed deletion of all media files.");
      char szComm[1024];
      sprintf(szComm, "rm -rf %s* /dev/null 2>&1", FOLDER_MEDIA);
      hw_execute_bash_command(szComm, NULL);   
      onShow();
      return;
   }

   if ( 10 == m_iConfirmationId )
   {
      pairing_stop();
      onEventReboot();

      // Expand FS
      hw_execute_bash_command("sudo raspi-config --expand-rootfs > /dev/null 2>&1", NULL);   
      hw_execute_bash_command("sudo reboot -f", NULL);
      exit(0);
      return;
   }
}

int MenuStorage::onBack()
{
   if ( g_bVideoPlaying )
   {
      stopVideoPlay();
      hardware_sleep_ms(50);
      return 1;
   }
   if ( m_ViewScreenShotIndex != -1 )
   {
      if ( MAX_U32 != m_ScreenshotImageId )
         g_pRenderEngine->freeImage(m_ScreenshotImageId);
      m_ScreenshotImageId = MAX_U32;

      m_ViewScreenShotIndex = -1;
      hardware_sleep_ms(50);
      return 1;
   }
   return 0;
}

void MenuStorage::onSelectItem()
{
   if ( m_ViewScreenShotIndex != -1 )
      return;

   if ( g_bVideoPlaying )
   {
      stopVideoPlay();
      return;
   }

   Menu::onSelectItem();

   if ( 0 == m_SelectedIndex )
   {
      m_iConfirmationId = 10;
      MenuConfirmation* pMC = new MenuConfirmation("Reboot","This operation requires a reboot. Do you want to continue?", m_iConfirmationId);
      add_menu_to_stack(pMC);
      return;
   }

   if ( 1 == m_SelectedIndex ) // copy
   {
      flowCopyMoveFiles(false);
      return;
   }
   if ( 2 == m_SelectedIndex ) // move
   {
      flowCopyMoveFiles(true);   
      return;
   }
   if ( 3 == m_SelectedIndex )
   {
      m_iConfirmationId = 5;
      MenuConfirmation* pMC = new MenuConfirmation("Confirmation","Are you sure you want to delete all files?",m_iConfirmationId);
      add_menu_to_stack(pMC);
      return;
   }

   if ( m_IndexViewPictures == m_SelectedIndex )
   {
      m_ViewScreenShotIndex = media_get_screenshots_count()-1;

      char szFile[1024];
      sprintf(szFile, "%s%s", FOLDER_MEDIA, m_szPicturesFiles[m_ViewScreenShotIndex]); 
      log_line("Menu: Loading screenshot: %s", szFile );
      m_ScreenshotImageId = g_pRenderEngine->loadImage(szFile);
      if ( 0 != m_ScreenshotImageId && MAX_U32 != m_ScreenshotImageId )
        log_line("Men: Image loaded ok");
      return;
   }

   int maxMenuIndex = m_StaticMenuItemsCount-1;
   int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
   if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
      maxMenuIndex += m_UIFilesPerPage;
   else
      maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;

   if ( m_SelectedIndex == (maxMenuIndex-1))
   {
      if ( m_UIFilesPage > 0 )
         m_UIFilesPage--;

      int maxMenuIndex = m_StaticMenuItemsCount-1;
      int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
      if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
         maxMenuIndex += m_UIFilesPerPage;
      else
         maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;
      m_SelectedIndex = maxMenuIndex-1;

      return;
   }
   if ( m_SelectedIndex == maxMenuIndex )
   {
      int indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
      if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
         m_UIFilesPage++;

      int maxMenuIndex = m_StaticMenuItemsCount-1;
      indexStartThisPage = m_UIFilesPerPage * m_UIFilesPage;
      if ( m_VideoInfoFilesCount > indexStartThisPage + m_UIFilesPerPage )
         maxMenuIndex += m_UIFilesPerPage;
      else
         maxMenuIndex += m_VideoInfoFilesCount - indexStartThisPage;
      m_SelectedIndex = maxMenuIndex;
      return;
   }

   char szFile[1024];
   char szBuff[1024];
   int index = m_UIFilesPerPage * m_UIFilesPage + m_SelectedIndex-m_StaticMenuItemsCountBeforeUIFiles;
   if ( index < 0 || index >= m_VideoInfoFilesCount )
      return;

   sprintf(szFile, "%s%s", FOLDER_MEDIA, m_szVideoInfoFiles[index]); 
   FILE* fd = fopen(szFile, "r");
   if ( NULL == fd )
      return;

   if ( 1 != fscanf(fd, "%s", szFile) )
   {
      fclose(fd);
      return;
   }
   fclose(fd);

   if ( pairing_isStarted() )
   {
      pairing_stop();
      m_bWasPairingStarted = true;
   }
   sprintf(szBuff, "./%s %s%s 30 &", VIDEO_PLAYER_OFFLINE, FOLDER_MEDIA, szFile);
   hw_execute_bash_command(szBuff,NULL);
   g_bVideoPlaying = true;
   g_uVideoPlayingStartTime = get_current_timestamp_ms();
   g_uVideoPlayingLengthSec = m_VideoFilesDuration[index];
}

void MenuStorage::buildFilesListPictures()
{
   DIR *d;
   struct dirent *dir;

   for( int i=0; i<m_PicturesFilesCount; i++ )
      free(m_szPicturesFiles[i]);
   m_PicturesFilesCount = 0;

   d = opendir(FOLDER_MEDIA);
   if (d)
   {
      while ((dir = readdir(d)) != NULL)
      {
         if ( strlen(dir->d_name) < 4 )
            continue;
         ruby_signal_alive();

         bool match = false;
         if ( strncmp(dir->d_name, FILE_FORMAT_SCREENSHOT, 6) == 0 )
            match = true;
         if ( ! match )
            continue;
         if ( m_PicturesFilesCount >= MAX_STORAGE_MENU_FILES )
            continue;
         m_szPicturesFiles[m_PicturesFilesCount] = (char*)malloc(strlen(dir->d_name)+1);
         if ( NULL == m_szPicturesFiles[m_PicturesFilesCount] )
            break;
         strcpy(m_szPicturesFiles[m_PicturesFilesCount], dir->d_name);
         m_PicturesFilesCount++;
      }
      closedir(d);
   }
   else
      log_softerror_and_alarm("Failed to open media dir to search for pictures.");
   log_line("Menu Storage: Found %d pictures on storage", m_PicturesFilesCount);
}

void MenuStorage::buildFilesListVideo()
{
   DIR *d;
   FILE* fd;
   struct dirent *dir;
   char szBuff[1024];

   for( int i=0; i<m_VideoInfoFilesCount; i++ )
      free(m_szVideoInfoFiles[i]);
   m_VideoInfoFilesCount = 0;

   d = opendir(FOLDER_MEDIA);
   if (d)
   {
      while ((dir = readdir(d)) != NULL)
      {
         if ( strlen(dir->d_name) < 4 )
            continue;

         ruby_signal_alive();

         bool match = false;
         if ( dir->d_name[strlen(dir->d_name)-4] == 'i' )
         if ( dir->d_name[strlen(dir->d_name)-3] == 'n' )
         if ( dir->d_name[strlen(dir->d_name)-2] == 'f' )
         if ( dir->d_name[strlen(dir->d_name)-1] == 'o' )
         if ( strncmp(dir->d_name, FILE_FORMAT_VIDEO_INFO, 5) == 0 )
            match = true;
         if ( ! match )
            continue;

         if ( m_VideoInfoFilesCount >= MAX_STORAGE_MENU_FILES )
            continue;
         m_szVideoInfoFiles[m_VideoInfoFilesCount] = (char*)malloc(strlen(dir->d_name)+1);
         if ( NULL == m_szVideoInfoFiles[m_VideoInfoFilesCount] )
         {
            log_softerror_and_alarm("Failed to allocate memory for video file info.");
            break;
         }
         strcpy(m_szVideoInfoFiles[m_VideoInfoFilesCount], dir->d_name);
         sprintf(szBuff, "%s%s", FOLDER_MEDIA, m_szVideoInfoFiles[m_VideoInfoFilesCount]);
         fd = fopen(szBuff, "r");
         if ( NULL == fd )
         {
            log_softerror_and_alarm("Failed to open video info file: [%s]", szBuff);
            continue;
         }
         if ( 3 != fscanf(fd, "%s %d %d", szBuff, &(m_VideoFilesFPS[m_VideoInfoFilesCount]), &(m_VideoFilesDuration[m_VideoInfoFilesCount])) )
         {
            log_softerror_and_alarm("Failed to read video info file: [%s], invalid format.", szBuff);
            fclose(fd);
            continue;
         }
         if ( 2 != fscanf(fd, "%d %d", &(m_VideoFilesWidth[m_VideoInfoFilesCount]), &(m_VideoFilesHeight[m_VideoInfoFilesCount])) )
         {
            log_softerror_and_alarm("Failed to read video info file: [%s], invalid format.", szBuff);
            fclose(fd);
            continue;
         }
         fclose(fd);

         m_VideoInfoFilesCount++;
      }
      closedir(d);
   }
   else
      log_softerror_and_alarm("Failed to open media dir to search for videos.");
   log_line("Menu Storage: Found %d videos on storage", m_VideoInfoFilesCount);
}

void MenuStorage::movePictures(bool bDelete)
{
   char szUSB[1024];
   char szBuff[1024];
   char szSrcFile[1024];
   char szCommand[1024];
   char szInfo[256];

   log_line("Moving pictures media files...");

   if ( bDelete )
      m_pPopupProgress->setTitle("Moving screenshots. Please wait...");
   else
      m_pPopupProgress->setTitle("Copying screenshots. Please wait...");

   render_all(get_current_timestamp_ms());

   buildFilesListPictures();

   sprintf(szBuff, "%s/%s/Ruby", FOLDER_RUBY, FOLDER_USB_MOUNT);
   sprintf(szCommand, "mkdir -p %s", szBuff );
   hw_execute_bash_command(szCommand, NULL);

   for( int i=0; i<m_PicturesFilesCount; i++ )
   {
      g_TimeNow = get_current_timestamp_ms();
      g_TimeNowMicros = get_current_timestamp_micros();
      ruby_signal_alive();
      ruby_processing_loop(true);
      render_all(get_current_timestamp_ms());

      strcpy(szSrcFile, FOLDER_MEDIA);
      strcat(szSrcFile, m_szPicturesFiles[i]);
      if ( -1 == access(szSrcFile, R_OK) )
         continue;

      sprintf(szCommand, "rm -rf %s/%s/Ruby/%s", FOLDER_RUBY, FOLDER_USB_MOUNT, m_szPicturesFiles[i]);
      hw_execute_bash_command(szCommand, NULL);

      if ( bDelete )
         sprintf(szCommand, "mv %s %s/%s/Ruby/%s", szSrcFile, FOLDER_RUBY, FOLDER_USB_MOUNT, m_szPicturesFiles[i]);
      else
         sprintf(szCommand, "cp %s %s/%s/Ruby/%s", szSrcFile, FOLDER_RUBY, FOLDER_USB_MOUNT, m_szPicturesFiles[i]);
      hw_execute_bash_command(szCommand, NULL);
   }

   //sprintf(szCommand, "cp /home/pi/ruby/logs/* %s/%s/Ruby/", FOLDER_RUBY, FOLDER_USB_MOUNT);
   //execute_bash_command(szCommand, NULL);
}

void MenuStorage::moveVideos(bool bDelete)
{
   char szUSB[1024];
   char szBuff[1024];
   char szSrcFile[1024];
   char szOutFile[1024];
   char szCommand[1024];
   char szInfo[256];
   FILE* fd = NULL;
   int fps = 0, duration = 0;

   log_line("Moving video media files...");

   if ( bDelete )
      sprintf(szBuff, "Moving videos to USB memory stick [%s]. Please wait...", hardware_get_mounted_usb_name());
   else
      sprintf(szBuff, "Copying videos to USB memory stick [%s]. Please wait...", hardware_get_mounted_usb_name());

   m_pPopupProgress->setTitle(szBuff);
   render_all(get_current_timestamp_ms());

   buildFilesListVideo();

   sprintf(szBuff, "%s/%s/Ruby", FOLDER_RUBY, FOLDER_USB_MOUNT);
   sprintf(szCommand, "mkdir -p %s", szBuff );
   hw_execute_bash_command(szCommand, NULL);

   for( int i=0; i<m_VideoInfoFilesCount; i++ )
   {
      sprintf(szInfo, "Processing video %d of %d. Please wait...", i+1, m_VideoInfoFilesCount);
      m_pPopupProgress->setTitle(szInfo);

      g_TimeNow = get_current_timestamp_ms();
      g_TimeNowMicros = get_current_timestamp_micros();
      ruby_signal_alive();
      ruby_processing_loop(true);
      render_all(get_current_timestamp_ms());

      sprintf(szBuff, "%s%s", FOLDER_MEDIA, m_szVideoInfoFiles[i]);
      fd = fopen(szBuff, "r");
      if ( NULL == fd )
         break;
      if ( 3 != fscanf(fd, "%s %d %d", szSrcFile, &fps, &duration ) )
      {
         fclose(fd);
         break;
      }
      fclose(fd);
      strcpy(szOutFile, szSrcFile);
      szOutFile[strlen(szOutFile)-4] = 'm';
      szOutFile[strlen(szOutFile)-3] = 'p';
      szOutFile[strlen(szOutFile)-2] = '4';
      szOutFile[strlen(szOutFile)-1] = 0;
      sprintf(szCommand, "rm -rf %s/%s/Ruby/%s", FOLDER_RUBY, FOLDER_USB_MOUNT, szOutFile);
      hw_execute_bash_command(szCommand, NULL);

      sprintf(szCommand, "./ruby_video_proc %s%s %s/%s/Ruby/%s", FOLDER_MEDIA, m_szVideoInfoFiles[i], FOLDER_RUBY, FOLDER_USB_MOUNT, szOutFile);
      log_line("Executing: %s", szCommand);
      hw_execute_bash_command(szCommand, NULL);
      //system(szCommand);
      log_line("Finished processing video %s", m_szVideoInfoFiles[i]);
      hardware_sleep_ms(100);
      if ( bDelete )
         sprintf(szInfo, "Moving video %d of %d to USB stick [%s]. Please wait...", i+1, m_VideoInfoFilesCount, hardware_get_mounted_usb_name());
      else
         sprintf(szInfo, "Copying video %d of %d to USB stick [%s]. Please wait...", i+1, m_VideoInfoFilesCount, hardware_get_mounted_usb_name());

      m_pPopupProgress->setTitle(szInfo);

      g_TimeNow = get_current_timestamp_ms();
      g_TimeNowMicros = get_current_timestamp_micros();
      ruby_signal_alive();
      ruby_processing_loop(true);
      render_all(get_current_timestamp_ms());

      hardware_sleep_ms(200);

      if ( bDelete )
      {
         sprintf(szCommand, "rm -rf %s%s", FOLDER_MEDIA, m_szVideoInfoFiles[i] );
         hw_execute_bash_command(szCommand, NULL);
         szCommand[strlen(szCommand)-4] = 'h';
         szCommand[strlen(szCommand)-3] = '2';
         szCommand[strlen(szCommand)-2] = '6';
         szCommand[strlen(szCommand)-1] = '4';
         hw_execute_bash_command(szCommand, NULL);
      }
   }

   //sprintf(szCommand, "cp /home/pi/ruby/logs/* %s/%s/Ruby/", FOLDER_RUBY, FOLDER_USB_MOUNT);
   //execute_bash_command(szCommand, NULL);
   ruby_signal_alive();
   sync();
   ruby_signal_alive();
   hardware_sleep_ms(100);
}

void MenuStorage::flowCopyMoveFiles(bool bDeleteToo)
{
   char szBuff[256];
   char szCommand[256];

   if ( NULL != m_pPopupProgress )
      popups_remove(m_pPopupProgress);
   m_pPopupProgress = new Popup("Mounting USB memory stick...",0.28, 0.32, 0.37, 300);
   m_pPopupProgress->setCentered();
   popups_add_topmost(m_pPopupProgress);

   sprintf(szCommand, "mkdir -p %s/%s", FOLDER_RUBY, FOLDER_USB_MOUNT); 
   hw_execute_bash_command(szCommand, NULL);

   ruby_pause_watchdog();

   if ( 0 == hardware_try_mount_usb() )
   {
      ruby_resume_watchdog();
      m_pPopupProgress->setTitle("No USB memory stick available.");
      m_pPopupProgress->setTimeout(5);
      return;
   }

   movePictures(bDeleteToo);
   moveVideos(bDeleteToo);

   if ( bDeleteToo )
   {
      onShow();
   }
   char szUSBDeviceName[128];
   strncpy(szUSBDeviceName, hardware_get_mounted_usb_name(), 127);

   hardware_unmount_usb();
   ruby_signal_alive();
   sync();
   ruby_signal_alive();
   ruby_resume_watchdog();
   invalidate();
   sprintf(szBuff, "Done. It's safe now to remove the USB memory stick [%s].", szUSBDeviceName);
   m_pPopupProgress->setTitle(szBuff);
   m_pPopupProgress->setTimeout(5);
}


void MenuStorage::stopVideoPlay()
{
   hw_stop_process(VIDEO_PLAYER_OFFLINE);
   g_bVideoPlaying = false;
   render_all(get_current_timestamp_ms(), true);
   if ( m_bWasPairingStarted )
      pairing_start_normal();
   render_all(get_current_timestamp_ms(), true);
}
