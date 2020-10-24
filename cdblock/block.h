/*
 * Copyright (c) 2020 - Romulo Fernandes Machado Leitao
 * See LICENSE for details.
 *
 * Romulo Fernandes Machado Leitao <abra185@gmail.com>
 */

#pragma once

#include <yaul.h>

#define DEBUG_FS 0

#define HASH_PRIME 31
#define HASH_CUT_NUMBER 1000000009
#define HASH_CHAR(X) ((X) - 31)

enum VolumeDescriptorTypes {
  VD_BOOT_RECORD = 0,
  VD_PRIMARY,
  VD_SUPPLEMENTARY,
  VD_PARTITION_DESCRIPTOR,

  VD_SET_TERMINATOR = 0xFF
};


// Use b for big endian on saturn.
typedef struct {
  uint32_t l;
  uint32_t b;
} __packed MultiEndianNumber32;

typedef struct {
  uint16_t l;
  uint16_t b;
} __packed MultiEndianNumber16;

typedef struct {
  uint8_t data[2048];
} __packed Sector;

typedef struct {
  uint8_t date[17];
} __packed Date;

typedef struct {
  uint8_t date[7];
} __packed RecordingDateTime;

#define FLAG_CDBLOCK_HIDDEN               (1 << 0)
#define FLAG_CDBLOCK_DIRECTORY            (1 << 1)
#define FLAG_CDBLOCK_ASSOCIATED_FILE      (1 << 2)
#define FLAG_CDBLOCK_EXT_FORMAT           (1 << 3)
#define FLAG_CDBLOCK_EXT_PERMISSIONS      (1 << 4)
#define FLAG_CDBLOCK_CONTINUE_NEXT_EXTENT (1 << 7)

typedef struct {
  uint8_t length;
  uint8_t extendedAttributeLength;
  MultiEndianNumber32 extentLocation;
  MultiEndianNumber32 extentLength;
  RecordingDateTime recordingDateTime;
  uint8_t flags;
  uint8_t unitSizeInterleavedMode;
  uint8_t gapSizeInterleavedMode;
  MultiEndianNumber16 volumeSequenceNumber;
  uint8_t identifierLength;
} __packed DirectoryRecord;

static inline bool __always_inline isDirectory(DirectoryRecord* rec) { return rec->flags & FLAG_CDBLOCK_DIRECTORY; }

typedef struct {
  DirectoryRecord root;
  uint8_t identifier;
} __packed RootDirectoryRecord;

typedef struct {
  uint8_t type;
  uint8_t identifier[5];
  uint8_t version;
} __packed VolumeDescriptorSetCommon;

static inline bool __always_inline isTerminator(VolumeDescriptorSetCommon* sec) { return sec->type == VD_SET_TERMINATOR; }

typedef struct {
  VolumeDescriptorSetCommon common;
  uint8_t data[2041];
} __packed VolumeDescriptorSet;

typedef struct {
  VolumeDescriptorSetCommon common;
  uint8_t unused;
  char systemIdentifier[32];
  char volumeIdentifier[32];
  uint8_t unused2[8];

  MultiEndianNumber32 volumeSpaceSize;
  uint8_t unused3[32];

  MultiEndianNumber16 volumeSetSize;
  MultiEndianNumber16 volumeSequenceNumber;
  MultiEndianNumber16 logicalBlockSize;
  MultiEndianNumber32 pathTableSize;

  int32_t locationPathTableLittle;
  int32_t locationOptionalPathTableLittle;

  int32_t locationPathTableBig;
  int32_t locationOptionalPathTableBig;

  RootDirectoryRecord rootDirectoryRecord;

  char volumeSetIdentifier[128];
  char publisherIdentifier[128];
  char dataPreparerIdentifier[128];
  char applicationIdentifier[128];
  char copyrightFileIdentifier[38];
  char abstractFileIdentifier[36];
  char bibliographicFileIdentifier[37];

  Date volumeCreationDateTime;
  Date volumeModificationDateTime;
  Date volumeExpirationDateTime;
  Date volumeEffectiveDateTime;

  int8_t fileStructureVersion;
  int8_t unused4;
  
  uint8_t applicationUsed[512];
  uint8_t isoReserved[653];
} __packed PrimaryVolumeDescriptor;

/**
 * ISO9660 Disk Data.
 */
typedef struct {
  // Root sector read from the filesystem.
  Sector rootSector;

  // Sector to operate temporary data.
  Sector tempSector;  
} FilesystemData;

/**
 * Cursor for reading sector by sector
 */
typedef struct {
  Sector sector;
  uint32_t lba;
  uint8_t* cursor;
} FilesystemEntryCursor;

/**
 * Entry in the file table.
 */
typedef struct {
  uint32_t filenameHash;
  uint32_t lba;
  uint32_t size;
} FilesystemEntry;

/**
 * Filesystem Header Table.
 */
typedef struct {
  uint32_t numEntries;

  // Entries will point to an user allocated memory region that will
  // store all entries. When deallocating this, user should free the
  // 'entries' pointer.
  FilesystemEntry *entries;
} FilesystemHeaderTable;

/**
 * Can be called by navigateDirectory when an entry is found. Parameters
 * are the directory entry first, navigation depth in filesystem and
 * and optional user data pointer that is passed to navigateDirectory.
 */
typedef void (*RecordFunction)(DirectoryRecord*, int, void*);

/**
 * initializeCDBlock CDBlock subsystem.
 */
extern int initializeCDBlock();

/**
 * Read the disk as a ISO9660 filesystem.
 *
 * @param fsData Pointer to where store the read FilesystemData.
 *
 * @return 0 If successful.
 */
extern int readFilesystem(FilesystemData *fsData);

/**
 * Navigate the filesystem recursively, applying the passed recordFunction
 * to every file/directory entry in the filesystem.
 *
 * @param fsData Pointer to initialized filesystem entry.
 *
 * @param recordFunction User specified function to be applied to every
 *                       read entry of the filesystem.
 *
 * @param userData Optinal user data pointer that can be passed to the
 *                 recordFunction.
 */
extern void navigateFilesystem(FilesystemData *fsData, 
  RecordFunction recordFunction, void *userData);

/**
 * Print (dbgio_buffer) every entry found in the filesystem.
 *
 * @param fsData Pointer to initialized filesystem entry.
 */
extern void printCdStructure(FilesystemData *fsData);

/**
 * Return the size in bytes required to store the Filesystem Header Table.
 */
extern uint32_t getHeaderTableSize(FilesystemData *fsData);

/**
 * Fill the passed Filesystem Header Table. The allocated header table 
 * pointer must point to a memory location with at least 
 * getHeaderTableSize() bytes available.
 */
extern void fillHeaderTable(FilesystemData *fsData, 
  FilesystemHeaderTable *headerTable);

/**
 * Return file entry.
 * @param headerTable Filesystem header table.
 * @param filenameHash Hash of the file to be searched. Use 
 *                     getFilenameHash for this.
 * @param resultingEntry If file is found, the resulting entry point to 
 *                       the file entry on the header table.
 */
extern void getFileEntry(FilesystemHeaderTable *headerTable, 
  uint32_t filenameHash, FilesystemEntry **resultingEntry);

/**
 * Return file contents from the specified entry.
 * @param entry A file entry in the header table.
 * @param buffer File contents will be returned in this buffer.
 *
 * @return 0 If reading was successful.
 */
extern int getFileContents(FilesystemEntry *entry, void *buffer);

/**
 * Initializes a cursor based on a given file entry
 * @param entry A file entry in the header table.
 * @param cursor [Output] File entry cursor to be initialized.
 *
 */
extern void initFileEntryCursor(FilesystemEntry* entry, FilesystemEntryCursor* cursor);

/**
 * Return file contents from the specified entry.
 * @param entry A file entry in the header table.
 * @param buffer File contents will be returned in this buffer.
 *
 * @return 0 If reading was successful.
 */
extern int readFileCursor(FilesystemEntry* entry, FilesystemEntryCursor* fileCursor, void* buffer, uint32_t length);

/**
 * Generate a hash based on passed parameters.
 *
 * @param filename Name of the file to generate the hash.
 * @param length Length of the filename string.
 * @param startingHash In case of appending to a hash, this is the 
 *                     starting hash.
 * @param firstPrime The first prime to be used in the sequence.
 * @param primeFactor The number we will multiply the prime each iteration.
 * @param lastPrime If not NULL, returns the last prime used.
 */
extern uint32_t generateHash(const char* filename, uint32_t length, uint32_t startingHash, uint32_t firstPrime, uint32_t primeFactor, uint32_t *lastPrime);

/**
 * Generate a cdblock filename hash.
 */
extern uint32_t getFilenameHash(const char *filename, uint32_t length);


