/*
 * Copyright (c) 2020 - Romulo Fernandes Machado Leitao
 * See LICENSE for details.
 *
 * Romulo Fernandes Machado Leitao <abra185@gmail.com>
 */

#include <yaul.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "block.h"
//#include "filesystem.h"

static FilesystemData sCdFilesystemData;
static FilesystemHeaderTable sCdHeaderTable;

// wait for A button press.
void getinput()
{
    static smpc_peripheral_digital_t _digital;
    bool held = false;

    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &_digital);

    held = ((_digital.pressed.raw & PERIPHERAL_DIGITAL_A) != 0x0000);

    while(true)
    {
        dbgio_flush();
        vdp_sync();

        if(((_digital.pressed.raw & PERIPHERAL_DIGITAL_A) != 0x0000) &&
           held == false)
        {
            break;
        }

        smpc_peripheral_process();
        smpc_peripheral_digital_port(1, &_digital);

        if(held)
        {
            held = ((_digital.pressed.raw & PERIPHERAL_DIGITAL_A) != 0x0000);
        }
    }
}

void clearscreen()
{
    dbgio_puts("[H[2J");
}

static void _vblank_out_handler(void *);

void initializeFilesystem() {
  // CDBlock Initialization.
  const int stat = initializeCDBlock();
  assert(stat == 0);

  readFilesystem(&sCdFilesystemData);

  // Create cd entries table (necessary for looking for files).
  const uint32_t tableSize = getHeaderTableSize(&sCdFilesystemData);

  sCdHeaderTable.entries = (FilesystemEntry*) malloc(tableSize);

  fillHeaderTable(&sCdFilesystemData, &sCdHeaderTable);
}

void hardwareInit() {
  vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A,
      VDP2_TVMD_VERT_224);

  vdp2_scrn_back_screen_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
      COLOR_RGB1555(1, 0, 3, 15));

  cpu_intc_mask_set(0);

  vdp2_tvmd_display_set();
  
  vdp_sync_vblank_out_set(_vblank_out_handler);
}

void printFileContents(const char* filename)
{
  char readbuffer[250];
  FilesystemEntry* fsEntry = NULL;
  FilesystemEntryCursor fileCursor;
  getFileEntry(&sCdHeaderTable, getFilenameHash(filename, strlen(filename)), &fsEntry);

  assert(fsEntry != NULL);

  initFileEntryCursor(fsEntry, &fileCursor);
  int readAmount = sizeof(readbuffer);
  int remainder = fsEntry->size;

  do
  {
    if(readAmount > remainder)
    {
      readAmount = remainder;
    }

    int retCode = readFileCursor(fsEntry, &fileCursor, readbuffer, readAmount);
    assert(retCode == 0);

    remainder -= readAmount;

    clearscreen();
    dbgio_printf("%.*s", readAmount, readbuffer);
    dbgio_printf("\nBytes read: %d\nBytes remaining: %d\nPress A to continue...", readAmount, remainder);
    getinput();

  } while (remainder > 0);
}

int main(void) {
  hardwareInit();

  dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
  dbgio_dev_font_load();
  dbgio_dev_font_load_wait();

  // Start filesystem
  initializeFilesystem();
  
  // test a file in a directory
  printFileContents("A_FOLDER/DIRTEST.TXT");

  // test a really big file
  printFileContents("SHERLOCK.TXT");
  
  clearscreen();
  dbgio_printf("All done!");

  dbgio_flush();
  vdp_sync();

  while (true)
    ;
}



static void
_vblank_out_handler(void *work __unused)
{
  smpc_peripheral_intback_issue();
}

