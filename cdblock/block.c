/*
 * Copyright (c) 2020 - Romulo Fernandes Machado Leitao
 * See LICENSE for details.
 *
 * Romulo Fernandes Machado Leitao <abra185@gmail.com>
 */

#include "block.h"
#include <cd-block.h>
#include <ctype.h>

// Debugging Functions
// #define DEBUG_CDBLOCK

char* identifierPtr(DirectoryRecord* rec) { 
  char* ptr = (char*) &rec->identifierLength;
  ptr++;

  return ptr;
}

DirectoryRecord *nextDir(DirectoryRecord* rec) {
  uint8_t *dirPtr = (uint8_t*) rec;
  dirPtr += rec->length;

  return (DirectoryRecord*) dirPtr;
}

void binarySearch(FilesystemEntry* entries, uint32_t entriesLength, 
  FilesystemEntry searchElement, FilesystemEntry** foundEntry) {

  const uint32_t pivotIndex = entriesLength / 2;
  FilesystemEntry* pivot = &entries[pivotIndex];

  if (pivot->filenameHash == searchElement.filenameHash) {
    *foundEntry = pivot;

  } else if (entriesLength > 1) {
    if (pivot->filenameHash > searchElement.filenameHash) {
      binarySearch(entries, pivotIndex, searchElement, foundEntry);

    } else {
      binarySearch(&entries[pivotIndex],
        entriesLength - pivotIndex, searchElement, foundEntry);
    }
  }
}


void quickSort(FilesystemEntry* entries, int32_t left, int32_t right) {

  // Already sorted.
  if (right <= left)
    return;

  int32_t pivotIndex = left + (right - left) / 2;
  int32_t l = left - 1;
  int32_t r = right + 1;
  
  const FilesystemEntry pivot = entries[pivotIndex];
  for (;;) {
    do {
      l++;
    } while (entries[l].filenameHash < pivot.filenameHash);
    
    do {
      r--;
    } while (entries[r].filenameHash > pivot.filenameHash);

    if (l >= r) {
      pivotIndex = r;
      break;
    }
  
    // Swap
    const FilesystemEntry tmp = entries[r];
    entries[r] = entries[l];
    entries[l] = tmp;
  }

  quickSort(entries, left, pivotIndex);
  quickSort(entries, pivotIndex + 1, right);
}

void navigateDirectory(DirectoryRecord* record, 
  int level, RecordFunction recordFunction, void *userData, 
  bool continueReading) {

  assert(record != NULL);

  // Skip empty entries.
  if (record->length == 0)
    return;
  
  // Skip '.'
  DirectoryRecord* dir = record;
  if (!continueReading) {
    dir = nextDir(record);
    assert(dir->length != 0);
  
    // Skip '..'
    dir = nextDir(dir);
  }
      
  // Visit every entry on the directory.
  while (dir->length != 0) {
    if (recordFunction != NULL)
      recordFunction(dir, level, userData);

    // Visit sub-directory recursively.
    if (isDirectory(dir)) {
      uint32_t extraLevels = dir->extentLength.b / 2048;
      if (dir->extentLength.b % 2048)
        extraLevels++;

      for (uint32_t level = 0; level < extraLevels; ++level) {
        Sector sector;
        const int stat = cd_block_read_data(LBA2FAD(dir->extentLocation.b) +
          level, 2048, sector.data);
        
        assert(stat == 0);
  
        DirectoryRecord* newRecord = (DirectoryRecord*) sector.data;
        navigateDirectory(newRecord, level + 1, recordFunction, userData, 
          level > 0);
      }
    }

    dir = nextDir(dir);
  }
}

/**
 * Fill a filesystem entry and then jump to the next entry. In case of a
 * child, the parent hash will be passed to generate the child hash.
 */
void fillHeaderTableEntry(DirectoryRecord* record, uint32_t parentHash, 
  uint32_t parentPrime, FilesystemEntry** entry, 
  FilesystemHeaderTable* headerTable, bool continueReading) {

  assert(record != NULL);
  assert(headerTable != NULL);

  // Skip empty entries.
  if (record->length == 0)
    return;

  DirectoryRecord* dir = record;
  if (!continueReading) {

    // Skip '.'
    dir = nextDir(record);
    assert( dir->length != 0 );

    // Skip '..'
    dir = nextDir(dir);
  }
      
  // Visit every entry on the directory.
  while (dir->length != 0) {

    // -2 takes into account ';1'
    uint32_t identifierSize = dir->identifierLength;
    if (isDirectory(dir) == 0 && identifierSize > 2)
      identifierSize -= 2;

    uint32_t lastPrime = 0;
    
    uint32_t hash = generateHash(identifierPtr(dir), identifierSize,
      parentHash, parentPrime, HASH_PRIME, &lastPrime);

#ifdef DEBUG_CDBLOCK
    char identifierName[256];
    memcpy(identifierName, dir->identifierPtr(), identifierSize);
    identifierName[identifierSize] = 0;

    char tmpBuffer[1024];
    sprintf(tmpBuffer, "Added %s (%lu) to header table\n", identifierName, hash);
    dbgio_puts(tmpBuffer);
    dbgio_flush();
#endif

    if (isDirectory(dir)) {
      // Add '/'
      hash += HASH_CHAR('/') * lastPrime;
      hash %= HASH_CUT_NUMBER;
      lastPrime *= HASH_PRIME;

      // Visit children.
      uint32_t extraLevels = dir->extentLength.b / 2048;
      if (dir->extentLength.b % 2048)
        extraLevels++;
      
      for (uint32_t level = 0; level < extraLevels; ++level) {
        Sector sector;

        const int stat = cd_block_read_data(LBA2FAD(dir->extentLocation.b) + 
          level, 2048, sector.data);

        assert(stat == 0);

        DirectoryRecord* newRecord = (DirectoryRecord*) sector.data;
        fillHeaderTableEntry(newRecord, hash, lastPrime, entry, 
          headerTable, level > 0);
      }

    } else {

      // Add file entry.
      (*entry)->filenameHash = hash;
      (*entry)->lba = dir->extentLocation.b;
      (*entry)->size = dir->extentLength.b;
      
      if (dir->extentLength.b == 0) {
        char identifierName[256];
        memcpy(identifierName, identifierPtr(dir), identifierSize);
        identifierName[identifierSize] = 0;

        char tmpBuffer[1024];
        sprintf(tmpBuffer, "Invalid 0 byte file detected: %s\n", identifierName);
        dbgio_puts(tmpBuffer);
        dbgio_flush();

        assert(false);
      }

      headerTable->numEntries += 1;
      (*entry) += 1;
    } 

    dir = nextDir(dir);
  }
}


int initializeCDBlock() {
  assert(sizeof(VolumeDescriptorSet) == 2048 && "VolumeDescriptorSet size mismatch.");

  assert(sizeof(PrimaryVolumeDescriptor) == 2048 && "PrimaryVolumeDescriptor size mismatch.");

  int returnCode;
  if ((returnCode = cd_block_init(0x0002)) != 0)
    return returnCode;

  if (cd_block_cmd_is_auth(NULL) == 0) {
    if ((returnCode = cd_block_bypass_copy_protection()) != 0)
      return returnCode;
  }

  return 0;
}

int readFilesystem(FilesystemData* fsData) {
  assert(fsData != NULL);

  // Skip the first 16 sectors dedicated to IP.BIN
  uint8_t startFAD = LBA2FAD(16);
  VolumeDescriptorSet tempSet;

  // Find Primary Volume Descriptor.
  int cdBlockRet = 0;
  do {
    cdBlockRet = cd_block_read_data(startFAD, 2048, (uint8_t*) &tempSet);

    if (cdBlockRet != 0)
      return cdBlockRet;
    else if (tempSet.common.type != VD_PRIMARY)
      startFAD++;

  } while (tempSet.common.type != VD_PRIMARY);

  PrimaryVolumeDescriptor* primaryDescriptor = (PrimaryVolumeDescriptor*)&tempSet;

  // Jump to root sector and retrieve it.
  const int stat = cd_block_read_data(
    LBA2FAD(primaryDescriptor->rootDirectoryRecord.root.extentLocation.b), 
      2048, (uint8_t*) &fsData->rootSector);

  assert(stat == 0);
  return 0;
}

void navigateFilesystem(FilesystemData* fsData, 
  RecordFunction recordFunction, void *userData) {

  assert(fsData != NULL);
  navigateDirectory((DirectoryRecord*)&fsData->rootSector, 0, recordFunction, userData, false);
}

void tallyFiles(DirectoryRecord* dir, int unused __unused, void* length) {
  uint32_t *lengthData = (uint32_t*) length;
  if (!isDirectory(dir))
    (*lengthData)++;
}

uint32_t getHeaderTableSize(FilesystemData* fsData) {
  assert(fsData != NULL);

  uint32_t numEntries = 0;
  navigateDirectory((DirectoryRecord*)&fsData->rootSector, 0, tallyFiles, &numEntries, false);

  return (numEntries * sizeof(FilesystemEntry));
}

void fillHeaderTable(FilesystemData* fsData, 
  FilesystemHeaderTable* headerTable) {

  assert(fsData != NULL);
  assert(headerTable != NULL);
  assert(headerTable->entries != NULL);

  headerTable->numEntries = 0;

  FilesystemEntry* iterEntry = headerTable->entries;
  fillHeaderTableEntry((DirectoryRecord*)&fsData->rootSector, 0, HASH_PRIME, 
    &iterEntry, headerTable, false);

  quickSort(headerTable->entries, 0, headerTable->numEntries - 1);
}

void getFileEntry(FilesystemHeaderTable* headerTable, 
  uint32_t filenameHash, FilesystemEntry** resultingEntry) {

  assert(headerTable != NULL);
  assert(resultingEntry != NULL);
  FilesystemEntry searchEntry = {};
  searchEntry.filenameHash = filenameHash;
  searchEntry.lba = 0;
  searchEntry.size = 0;

  binarySearch(headerTable->entries, headerTable->numEntries, 
    searchEntry, resultingEntry);
}

int getFileContents(FilesystemEntry* entry, void* buffer) {
  FilesystemEntryCursor tmpFileCursor;
  tmpFileCursor.cursor = NULL;
  tmpFileCursor.lba = entry->lba;
  return readFileCursor(entry, &tmpFileCursor, buffer, entry->size);  
}

void initFileEntryCursor(FilesystemEntry* entry, FilesystemEntryCursor* cursor)
{
  assert(entry != NULL);
  assert(cursor != NULL);

  memset(cursor, 0, sizeof(*cursor));
  cursor->lba = entry->lba;
}

int readFileCursor(FilesystemEntry* entry, FilesystemEntryCursor* fileCursor, void* buffer, uint32_t length)
{
  assert(entry != NULL);
  assert(fileCursor != NULL);
  assert(buffer != NULL);

  uint8_t* dstBuffer = (uint8_t*) buffer;
  uint32_t missingBytes = length;

  if(fileCursor->cursor)
  {
    uint32_t remainingBuffer = fileCursor->cursor - fileCursor->sector.data;
    remainingBuffer = sizeof(fileCursor->sector.data) - remainingBuffer;
    
    if(missingBytes <= remainingBuffer)
    {
      memcpy(dstBuffer, fileCursor->cursor, missingBytes);
      fileCursor->cursor += missingBytes;
      return 0;
    }
    else
    {
      // if remainingBuffer is 0, then this memcopy does nothing
      // and we prepare for a normal read from a fresh sector.
      memcpy(dstBuffer, fileCursor->cursor, remainingBuffer);
      dstBuffer += remainingBuffer;
      missingBytes -= remainingBuffer;
      fileCursor->cursor = NULL;

      // Jump to next LBA.
      fileCursor->lba++;
    }      
  }

  while (missingBytes > 0) {
    int ret = cd_block_read_data(LBA2FAD(fileCursor->lba), 2048, fileCursor->sector.data);
    if (ret != 0)
      return ret;

    if (missingBytes > 2048) {
      memcpy(dstBuffer, fileCursor->sector.data, 2048);
      dstBuffer += 2048;
      missingBytes -= 2048;
      fileCursor->cursor = NULL;

      // Jump to next LBA.
      fileCursor->lba++;
    } else {
      memcpy(dstBuffer, fileCursor->sector.data, missingBytes);
      fileCursor->cursor = fileCursor->sector.data + missingBytes;
      missingBytes = 0;
    }
  }

  return 0;
}

uint32_t generateHash(const char* filename, uint32_t length, 
  uint32_t startingHash, uint32_t firstPrime, uint32_t primeFactor, 
  uint32_t *lastPrime) {

  assert(filename != NULL);

  uint32_t hash = startingHash;
  uint32_t prime = firstPrime;
  for (uint32_t i = 0; i < length; ++i) {
    hash += HASH_CHAR(filename[i]) * prime;
    hash %= HASH_CUT_NUMBER;
    prime *= primeFactor;
  }
    
  if (lastPrime != NULL)
    *lastPrime = prime;

  return hash;
}

uint32_t getFilenameHash(const char *filename, uint32_t length) {
  return generateHash(filename, length, 0, HASH_PRIME, 
    HASH_PRIME, NULL);
}
