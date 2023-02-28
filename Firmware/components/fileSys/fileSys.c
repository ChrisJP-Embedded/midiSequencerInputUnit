#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "include/fileSys.h"
#include "string.h"
#include "dirent.h"
#include "sys/stat.h"
#include "esp_vfs.h"
#include "errno.h"


#define LOG_TAG "FileSystem"
#define AUTO_CLOSE_PREV_FILE_ON_FILE_OPEN   1
#define AUTO_CLOSE_PREV_FILE_ON_UNMOUNT     1
#define BASE_PATH                           "/littlefs"
#define PARTITION_LABEL                     "fileSys"
#define MAX_FILESYS_RETRIES                 3
#define SUCCESS                             0


//---- Private ----//
static uint8_t fileSys_openFileRW(char * fileName, bool createNew);
static uint8_t fileSys_closeFile(void);
static uint8_t fileSys_refreshLocalData(void);
static uint8_t fileSys_mount(void);
static uint8_t fileSys_unmount(void);


//This struct holds all runtime data relating to the file system
struct fileSys_PrivateRuntimeDataCache
{
    esp_vfs_littlefs_conf_t conf;   //LittleFS port provided configuartion for the target file system
    bool isPartitionMounted;        //Keep a record of whether there is a file system currently mounted
    uint8_t numFilesOnPartition;    //Number of file currently existing on the target file system
    size_t partitionTotalBytes;   
    size_t partitionUsedBytes;
    char localFilenames[MAX_NUM_FILES][MAX_FILENAME_CHARS];
    char openFilePath[MAX_FILEPATH_CHARS];
    uint32_t openFileSize;          //Size of currently open file
    FILE * fileHandle;              //Handle to currently open file, NULL if no file open
} g_FileSysPrivateData;






//---- Public
FileSysPublicData_t fileSys_init(void)
{
    //This function mounts the file system if not already mounted.
    //It populates a structure containing a set pointers that act to provide
    //read-only access to partition data and the current file system state.

    assert(g_FileSysPrivateData.isPartitionMounted == false);

    FileSysPublicData_t FileSysDataInterface =
    {

        &g_FileSysPrivateData.isPartitionMounted,
        &g_FileSysPrivateData.numFilesOnPartition, 
        {NULL} //Init each ptr in array to NULL
    };

    //Now iterate through and set pointers to
    //filename cache which is local to this module
    for(uint8_t a = 0; a < MAX_NUM_FILES; ++a)
    {
        FileSysDataInterface.filenamesPtr[a] = g_FileSysPrivateData.localFilenames[a];
    }

    memset(&g_FileSysPrivateData, 0, sizeof(g_FileSysPrivateData));
    fileSys_mount();

    return FileSysDataInterface;
}


//---- Public
void fileSys_deinit(void)
{
    assert(g_FileSysPrivateData.isPartitionMounted == true);
    fileSys_unmount();
}


//---- Public
uint8_t fileSys_writeFile(char * fileName, uint8_t * data, uint32_t numBytes, bool createFileIfDoesntExist)
{
    uint32_t bytesWritten;

    assert(g_FileSysPrivateData.isPartitionMounted == true);

    fileSys_openFileRW(fileName, createFileIfDoesntExist);

    //Make sure the partition has enough free space available
    if((g_FileSysPrivateData.partitionUsedBytes + numBytes) >= g_FileSysPrivateData.partitionTotalBytes)
    {
        ESP_LOGE(LOG_TAG, "Error: Requested write op would exceed remaining partition size");
        return 1;
    }
    else //If parition DOES have enough free space
    {
        //Ensure the max file size is not exceeded before saving
        if((g_FileSysPrivateData.openFileSize + numBytes) >= MAX_FILE_SIZE_IN_BYTES)
        {
            ESP_LOGE(LOG_TAG, "Error: Requested write operation would exceed system max file size");
            return 1;
        }
        else //Checks complete, perform file save operation
        {
            if((bytesWritten = fwrite(data, sizeof(uint8_t), numBytes, g_FileSysPrivateData.fileHandle)) != numBytes)
            {
                ESP_LOGE(LOG_TAG, "Error: fileWrite operation failed %ld bytes written out of %ld. Errno: %d", bytesWritten, numBytes, errno);
                return 1;
            }   
            ESP_LOGI(LOG_TAG, "%ld bytes successfully written to file", numBytes);
            g_FileSysPrivateData.openFileSize += numBytes;
            fflush(g_FileSysPrivateData.fileHandle);
        }
    }

    fileSys_closeFile(); //Operation complete, close file

    return 0;
}


//---- Public
uint32_t fileSys_readFile(char *fileName, uint8_t * dataBuffer, uint16_t numBytes, bool readEntireFile)
{
    //This function performs a read on the currently open file,
    //proceeding from the current file pointer position in file.
    //numBytes of data is written to the address pointed at by
    //'dataBuffer' (which should have been allocated from PSRAM)

    //RETURNS: The number of bytes successfully read from file

    size_t numBytesRead;
    size_t numBytesInFile;

    assert(g_FileSysPrivateData.isPartitionMounted == true);
    assert(dataBuffer != NULL);
    assert(fileName != NULL);

    //Open target file for read op
    fileSys_openFileRW(fileName, false); 

    if(g_FileSysPrivateData.fileHandle == NULL) 
    {
        //The file should have been opened via a previous call to 'fileSys_openFileRW'
        //If the module file pointer is NULL then a system fault has occured.
        ESP_LOGE(LOG_TAG, "Error: Attempted to read from file when no file open");
        assert(0);
    }

    if(readEntireFile)
    {
        fseek(g_FileSysPrivateData.fileHandle, 0L, SEEK_END);
        numBytesInFile = ftell(g_FileSysPrivateData.fileHandle);
        rewind(g_FileSysPrivateData.fileHandle);

        //NEED TO ADD CHECK THAT MAX FILESIZE NOT EXCEEDED

        numBytesRead = fread(dataBuffer, sizeof(uint8_t), numBytesInFile, g_FileSysPrivateData.fileHandle);
        if(numBytesInFile != numBytesRead)
        {
            if(feof(g_FileSysPrivateData.fileHandle))
            {
                ESP_LOGE(LOG_TAG, "Error: Reached end of currently open file while reading");
                assert(0);
            }
        }
    }
    else
    {
        numBytesRead = fread(dataBuffer, sizeof(uint8_t), numBytes, g_FileSysPrivateData.fileHandle);
        if(numBytes != numBytesRead)
        {
            if(feof(g_FileSysPrivateData.fileHandle))
            {
                ESP_LOGE(LOG_TAG, "Error: Reached end of currently open file while reading");
                assert(0);
            }
        }
    }

    fileSys_closeFile(); //Operation complete, close file

    return (uint32_t)numBytesRead;
}


//---- Public
uint8_t fileSys_deleteFile(char * fileName)
{
    char scratch[MAX_FILEPATH_CHARS] = {0}; //------------------REMOVE THIS AND STRCAT THE BASE ONTO FULLFILEPATH--------------------
    char * fullFilePath = NULL; //Used to store constructed file path
    bool fileExists = false; //Used to indicate whether target file exists

    assert(g_FileSysPrivateData.isPartitionMounted == true);

    //Construct full path of the target file.
    strcpy(scratch, BASE_PATH); //Copy partition root path
    fullFilePath = strcat(scratch, "/"); //Add target filename to path
    fullFilePath = strcat(fullFilePath, fileName); //Add target filename to path

    //Find the target file for deletion. Iterate through
    //all files looking for target filename - a local record
    //was created when the file system was first mounted
    for (uint8_t i = 0; i < g_FileSysPrivateData.numFilesOnPartition; ++i)
    {
        //Compare the current file name with target
        if(strcmp(fileName, &g_FileSysPrivateData.localFilenames[i][0]) == 0)
        {
            //Target file found!
            fileExists = true;
            break;
        }
    }

    ESP_LOGI(LOG_TAG, "Attempting to delete file with path: %s", fullFilePath);

    if(fileExists) //If the target file exists.
    {
        if(remove(fullFilePath) != 0) //Delete the target file!
        {
            ESP_LOGE(LOG_TAG, "Call to remove() failed. errno: %d", errno);
            return 1;
        }
    }
    else //If the file does NOT exist, abort the deletion operation
    {   
        ESP_LOGE(LOG_TAG, "Error: Failure to delete file - '%s' does not exist", fullFilePath);
        return 1;
    }

    fileSys_refreshLocalData(); //Ensure local record of file data is up to data.

    return 0;
}


//---- Private
static uint8_t fileSys_openFileRW(char * fileName, bool createNew)
{
    char scratch[MAX_FILEPATH_CHARS] = {0};
    struct stat fileInfo;
    char * fullFilePath = NULL;
    bool fileFound = false;

    assert(g_FileSysPrivateData.isPartitionMounted == true);
    assert(g_FileSysPrivateData.fileHandle == NULL);
    assert(!((g_FileSysPrivateData.numFilesOnPartition == 0) && (createNew == false)));

    //Scan through local record of filenames and confirm
    //that the target file exists before attempting to open
    for(uint8_t i = 0; i < g_FileSysPrivateData.numFilesOnPartition; ++i)
    {
        if(strcmp(&g_FileSysPrivateData.localFilenames[i][0], fileName) == 0)
        {
            createNew = false;
            fileFound = true;
            break;
        }
    }

    //If the target file could not be found on the file system and we 
    //dont have permission to create a new file, then abort function
    if((fileFound == false) && (createNew == false))
    {
        ESP_LOGE(LOG_TAG, "Error: Cannot open file '%s', and not authorized to create new file", fileName);
        return 1;
    }
    else if(fileFound == false) //Target file not found, but we are authorized to create new files, so create a new file
    {
        //If creating a new file exceeds the max
        //num files allowed, then abort function
        if(g_FileSysPrivateData.numFilesOnPartition >= MAX_NUM_FILES) 
        {
            ESP_LOGE(LOG_TAG, "Error: Cannot create new file as max numer of files reached");
            return 1;
        }
    }

    //------------------------------------------------------------
    //If we reach here then either a new file has been created
    //or the target file already existsed on the fileSys partition
    //------------------------------------------------------------

    //Construct full path of the target file (or file to be created)
    strcpy(scratch, BASE_PATH); //Copy partition root path
    fullFilePath = strcat(scratch, "/"); //Add target filename to path
    fullFilePath = strcat(fullFilePath, fileName); //Add target filename to path

    //ESP_LOGI(LOG_TAG, "Just generated FullFilePath: %s", fullFilePath);

    if(fileFound == true) g_FileSysPrivateData.fileHandle = fopen(fullFilePath, "r+"); //open with read/write access (file must exist)
    else g_FileSysPrivateData.fileHandle = fopen(fullFilePath, "w+"); //create file and open with read/write access

    //If the previous file open operation
    //failed we must abort the function
    if(g_FileSysPrivateData.fileHandle == NULL)
    {
        ESP_LOGE(LOG_TAG, "Call to fopen returned NULL, could not open file");
        ESP_LOGE(LOG_TAG, "errno: %d", errno);
        return 1;
    }
    else    //---- File opened or created sucessfully ----//
    {
        //Clear local file path storage bytes
        memset(g_FileSysPrivateData.openFilePath, 0, MAX_FILEPATH_CHARS);
        //Update local file path record
        strcpy(g_FileSysPrivateData.openFilePath, fullFilePath);

        if(fileFound == false) //If the requested file didn't exist (meaning we just created it)
        {
            g_FileSysPrivateData.numFilesOnPartition++; //We just created a file, so increment number of files

            //Manually add this new file name to the local record of file names
            memset(&g_FileSysPrivateData.localFilenames[g_FileSysPrivateData.numFilesOnPartition - 1][0], 0, MAX_FILEPATH_CHARS);
            strcpy(&g_FileSysPrivateData.localFilenames[g_FileSysPrivateData.numFilesOnPartition - 1][0], fileName);

            ESP_LOGI(LOG_TAG, "Created new file: %s, with file path: %s", 
                &g_FileSysPrivateData.localFilenames[g_FileSysPrivateData.numFilesOnPartition - 1][0], g_FileSysPrivateData.openFilePath);
        }
    }

    //Need to keep a record of the number of bytes in the file
    if (stat(g_FileSysPrivateData.openFilePath, &fileInfo) == 0)
    {
	    g_FileSysPrivateData.openFileSize = (uint32_t)fileInfo.st_size;
        //Only show for previously existing files
        if(createNew == false) ESP_LOGI(LOG_TAG, "fileSize = %ld bytes", g_FileSysPrivateData.openFileSize);
    }
    else
    {   
        ESP_LOGE(LOG_TAG, "Error - littleFs didnt populate stat struct for file info - errno: %d", errno);
        return 1;
    }

    ESP_LOGI(LOG_TAG, "Successfully opened file: %s", g_FileSysPrivateData.openFilePath);

    return 0;
}


//---- Private
static uint8_t fileSys_closeFile(void)
{
    assert(g_FileSysPrivateData.isPartitionMounted == true);
    assert(g_FileSysPrivateData.fileHandle != NULL);

    if(fclose(g_FileSysPrivateData.fileHandle) != 0) //Close operation failed
    {
        ESP_LOGE(LOG_TAG, "Call to fclose() failed. errno: %d", errno);
        return 1;
    } 
        
    //--------------------------------------//
    //------ FILE CLOSED SUCCESSFULLY ------//
    //--------------------------------------//

    //Update local data cache
    g_FileSysPrivateData.fileHandle = NULL;
    memset(g_FileSysPrivateData.openFilePath, 0, MAX_FILEPATH_CHARS);
    g_FileSysPrivateData.openFileSize = 0;

    return 0;
}


//---- Private
static uint8_t fileSys_refreshLocalData(void)
{
    //This function scans for files on a mounted file system. 
    //The number of files present and their respective filenames are stored locally.

    DIR * dirPtr = NULL;                    //Directory pointer, required for directory operations
    struct dirent * dirItemInfoPtr = NULL;  //Used to store the data relating to an item in a directory 
    uint8_t numFiles = 0;                   //Temp store for number of files found in a directory

    assert(g_FileSysPrivateData.isPartitionMounted == true);

    //Clear out any previous filename data
    memset(g_FileSysPrivateData.localFilenames, 0, sizeof(g_FileSysPrivateData.localFilenames));

    //Must open directory to get list of files
    dirPtr = opendir(BASE_PATH);

    if (dirPtr != NULL) //Directory open SUCCESS
    {
        //Generate list of file names present
        while ((dirItemInfoPtr = readdir(dirPtr)) != NULL)
        {
            size_t size = strlen(dirItemInfoPtr->d_name);
            assert(size < MAX_FILENAME_CHARS);
            strcpy(&g_FileSysPrivateData.localFilenames[numFiles][0], dirItemInfoPtr->d_name);
            ESP_LOGI(LOG_TAG, "Found file: %s", &g_FileSysPrivateData.localFilenames[numFiles][0]);
            numFiles++;

            //ADD GETOUT
        }

        g_FileSysPrivateData.numFilesOnPartition = numFiles;
        ESP_LOGI(LOG_TAG, "Num files: %d", numFiles);

        if(closedir(dirPtr) != 0)
        {
            ESP_LOGE(LOG_TAG, "Error: Call to closedir() failed. Errno: %d", errno);
            return 1;
        }
    }
    else //Directory open FAILURE
    {
        ESP_LOGE(LOG_TAG, "Error: Call to opendir() failed. Errno: %d", errno);
        return 1;
    }

    return 0;
}


//---- Private
static uint8_t fileSys_mount(void)
{
    //This function attempts to mount the file system partition

    esp_err_t ret;

    //Required configuration for third party littleFS port
    g_FileSysPrivateData.conf.base_path = BASE_PATH;
    g_FileSysPrivateData.conf.partition_label = PARTITION_LABEL;
    g_FileSysPrivateData.conf.format_if_mount_failed = true; //REMOVE LATER, PROVIDE OPTION IN SYSTEM UI MENU?
    g_FileSysPrivateData.conf.dont_mount = false;
    
    //Attempt to mount littleFS flash partition based on confg
    ret = esp_vfs_littlefs_register(&g_FileSysPrivateData.conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Error: file system mount attempt FAILED. Problem: %s", esp_err_to_name(ret));
        return 1;
    }

    //---- If we reached here, the file system was mounted successfully ----//

    //Get partition usage data
    ret = esp_littlefs_info(g_FileSysPrivateData.conf.partition_label, &g_FileSysPrivateData.partitionTotalBytes, &g_FileSysPrivateData.partitionUsedBytes);

    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Error: Call to esp_littlefs_info() failed. Problem: %s ", esp_err_to_name(ret));
        return 1;
    }

    g_FileSysPrivateData.isPartitionMounted = true;

    //Update local fileSys data - filenames, num files etc.
    fileSys_refreshLocalData(); 

    return 0;
}


//---- Private
static uint8_t fileSys_unmount(void)
{
    //This function attempts to unmount the currently mounted

    esp_err_t ret;

    assert(g_FileSysPrivateData.isPartitionMounted == true);
    assert(g_FileSysPrivateData.fileHandle == NULL);

    //Attempt to unmount littleFS partition
    ret = esp_vfs_littlefs_unregister(g_FileSysPrivateData.conf.partition_label);

    if (ret != ESP_OK)
    {
        //Shouldnt get here
        ESP_LOGE(LOG_TAG, "Error: FileSys unmount attempt FAILED. Problem: %s", esp_err_to_name(ret));
        return 1;
    } 

    //Reset the local data cache
    memset(&g_FileSysPrivateData, 0, sizeof(g_FileSysPrivateData));

    return 0;
}