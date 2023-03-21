
#include <stdio.h>
#include <stdbool.h>

#define MAX_NUM_FILES           20
#define MAX_FILENAME_CHARS      20
#define MAX_FILEPATH_CHARS      30
#define MAX_FILE_SIZE_IN_BYTES  1024*1024 

typedef struct 
{
    //The host system requires access to an
    //up to date record of file system data.
    //These pointers provide read-only access
    bool * const isPartitionMountedPtr;
    uint8_t * const numFilesOnPartitionPtr;
    char *filenamesPtr[MAX_NUM_FILES];
} FileSysPublicData;


FileSysPublicData fileSys_init(void);
void fileSys_deinit(void);

uint8_t fileSys_writeFile(char * fileName, uint8_t * data, uint32_t numBytes, bool createFileIfDoesntExist);
uint32_t fileSys_readFile(char *fileName, uint8_t * dataBuffer, uint16_t numBytes, bool readEntireFile);
uint8_t fileSys_deleteFile(char * fileName);

