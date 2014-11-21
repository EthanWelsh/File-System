#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/* * * * * * * * * * * * * * *

           DEFINES

 * * * * * * * * * * * * * * */
//size of a disk block
#define    BLOCK_SIZE 512

//we'll use 8.3 filenames
#define    MAX_FILENAME 8
#define    MAX_EXTENSION 3

//How many files can there be in one directory?
#define    MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
    ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define    MAX_DATA_IN_BLOCK BLOCK_SIZE

//How many pointers in an inode?
#define NUM_POINTERS_IN_INODE ((BLOCK_SIZE - sizeof(unsigned int) - sizeof(unsigned long)) / sizeof(unsigned long))


/* * * * * * * * * * * * * * *

            STRUCTS

 * * * * * * * * * * * * * * */
struct cs1550_directory_entry
{
    char dname[MAX_FILENAME + 1]; //the directory name (plus space for a null)
    int nFiles; //How many files are in this directory (needs to be less than MAX_FILES_IN_DIR)

    struct cs1550_file_directory
    {
        char fname[MAX_FILENAME + 1];    //filename (plus space for nul)
        char fext[MAX_EXTENSION + 1];    //extension (plus space for nul)
        size_t fsize;            //file size
        long nStartBlock;        //where the first block is on disk
    } files[MAX_FILES_IN_DIR];        //There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
    //And all of the space in the block can be used for actual data
    //storage.
    char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */

cs1550_directory_entry *dirs;
int dirCount;



/* * * * * * * * * * * * * * *

        HELPER FUNCTIONS

 * * * * * * * * * * * * * * */


// Prints the binary representation of a byte
void printByte(char byte)
{
    unsigned int bit;

    for(int i = 0; i < 8; i++)
    {
        bit = getBitFromByte(byte, i);
        printf("%d", bit);
    }
    printf("\n");
}


// Given a block, mark said block as taken
int markTaken(int blockNum)
{
    FILE *fp;
    fp = fopen("disk", "r+");

    int byteToSeekTo;
    int indexIntoByte;

    blockToByteTranslation(blockNum, &byteToSeekTo, &indexIntoByte);

    char orMask = 0x80;

    orMask = orMask >> indexIntoByte;

    char byteFromFile;

    fseek(fp, byteToSeekTo, SEEK_SET);
    fread(&byteFromFile, 1, 1, fp);

    byteFromFile = byteFromFile | orMask;

    fseek(fp, byteToSeekTo, SEEK_SET);
    fwrite(&byteFromFile, 1, 1, fp);

    fclose(fp);

    return 0;
}


// Given a block, marks said block as free
int markFree(int blockNum)
{
    FILE *fp;
    fp = fopen("disk", "r+");

    int byteToSeekTo;
    int indexIntoByte;

    blockToByteTranslation(blockNum, &byteToSeekTo, &indexIntoByte);

    char byteFromFile;

    fseek(fp, byteToSeekTo, SEEK_SET);
    fread(&byteFromFile, 1, 1, fp);
    unsigned char orMask = 0x80;

    orMask = orMask >> indexIntoByte;

    orMask = ~orMask;

    byteFromFile = byteFromFile&orMask;

    fseek(fp, byteToSeekTo, SEEK_SET);
    fwrite(&byteFromFile, 1, 1, fp);

    fclose(fp);

    return 0;
}


// Determines if the given block is free or taken
int blockStatus(int blockNum)
{
    FILE *fp;
    fp = fopen("disk", "r+");

    int byteToSeekTo;
    int indexIntoByte;

    blockToByteTranslation(blockNum, &byteToSeekTo, &indexIntoByte);

    char byteFromFile;

    fseek(fp, byteToSeekTo, SEEK_SET);
    fread(&byteFromFile, 1, 1, fp);

    int bit = getBitFromByte(byteFromFile, indexIntoByte);

    fclose(fp);

    return bit;
}


// Given a block number will return which byte that block can be found at in our file (and the index into our byte)
void blockToByteTranslation(int blockNum, int *byteIndex, int *indexIntoByte)
{
    int i = 0;
    int byte = 0;
    int offset = 0;

    for(i = 0; i < blockNum; i++)
    {
        if(offset == 7)
        {
            byte++;
            offset = 0;
        }
        else offset++;
    }

    *byteIndex = byte;
    *indexIntoByte = offset;
}


// Return a specific bit from a given byte.
unsigned int getBitFromByte(char byte, int indexInByte) // NOTE: index left to right
{
    byte = byte << (indexInByte);
    unsigned char mask = 0x80;

    unsigned int bit = mask & byte;

    if(bit == 128) return 1;
    else return 0;
}


// Given a block will return how many consecutive free blocks can be found after this block.
int countFreeRun(int blockNum)
{
    int i;

    int freeBlocksSoFar = 0;

    if(blockStatus(blockNum) == 1) return -1;


    for(i = blockNum; i < 2048; i++)
    {
        if(blockStatus(i) == 0) freeBlocksSoFar++;
        else return freeBlocksSoFar;
    }

    return freeBlocksSoFar;
}


// Detects the next sequence of blocks that a file of the given size can fit in, then returns the first block number in that run.
int nextFreeRunFit(int sizeOfTargetRun)
{
    int i;

    for(i = 5; i < 2048; i++) // TODO should i start at 4?
    {
        if(countFreeRun(i) >= sizeOfTargetRun) return i;
    }

    return -1;
}

// Given a file, will move said file into memory and mark the bitmap appropriately
int moveFileToMemory(void * data, int size)
{
    int tempSize = size;
    int blockCount = 0;

    while(tempSize > 0)
    {
        tempSize = tempSize - BLOCK_SIZE;
        blockCount++;
    }

    int startBlock = nextFreeRunFit(blockCount);

    if(startBlock == -1)
    {
        printf("No more space left!!!\n");
        return -1;
    }

    int offsetInBytes = startBlock * BLOCK_SIZE;

    FILE *fp;
    fp = fopen("disk", "r+");

    fseek(fp, offsetInBytes, SEEK_SET);
    fwrite(data, 1, size, fp);
    fclose(fp);

    int i;

    for(i = startBlock; i < startBlock + blockCount; i++)
    {
        markTaken(i);
    }


    return 0;
}


// Marks a given region in memory as free
int removeFileFromMemory(int startBlockNum, int blockCount)
{
    return 0;
}



/* * * * * * * * * * * * * * *

     FILESYSTEM FUNCTIONS

 * * * * * * * * * * * * * * */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    printf("==========GETATTR START==========\n");
    printf("%s\n", path);
    //printf("PATH: %s\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    //is path the root dir?
    if (strcmp(path, "/") == 0)
    {
        printf("FOUND ROOT\n");
        //Check if name is subdirectory
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        res = 0;
    }
    else
    {
        char directory[MAX_FILENAME + 1] = {0};
        char filename[MAX_FILENAME + 1] = {0};
        char extension[MAX_EXTENSION] = {0};

        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

        printf("Directory: %s\n", directory);
        printf("File Name: %s\n", filename);
        printf("Extension: %s\n", extension);


        if(strcmp(directory, ""))
        {
            printf("GETTING DIRECTORY\n");
            cs1550_directory_entry targetDir;
            if(getDir(directory, &targetDir))
            { // If the file could be found...
                printf("Found Directory. Name is %s\n", directory);
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 2;
                res = 0; // no error
            }
            else
            { // If the file could NOT be found...
                printf("%s could not be found \n", directory);
                printf("==========GETATTR END==========\n");
                return -ENOENT;
            }
        }
        else if(strcmp(filename, ""))
        { // TODO handle files
            printf("FILE?!?!?!?\n");
            printf("==========GETATTR END==========\n");
            return -ENOENT;
        }
        //TODO Else return that path doesn't exist for FILES
        // res = -ENOENT;
    }
    printf("==========GETATTR END==========\n");
    return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    //Since we're building with -Wall (all warnings reported) we need
    //to "use" every parameter, so let's just cast them to void to
    //satisfy the compiler
    (void) offset;
    (void) fi;

    //TODO support files in directories

    printf("==========READDIR START==========\n");

    char directory[MAX_FILENAME + 1] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory);

    printf("Reading from Directory: %s\n", directory);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    printf("PATH: %s\n", path);
    printf("R: %d\n", strcmp(path, "/"));
    printf("R: %d\n", strcmp(path, "fdsfdfd/"));

    //This line assumes we have no subdirectories, need to change TODO
    if (strcmp(path, "/") != 0)
    {
        return -ENOENT;
    }

    printf("DIRCOUNT: %d\n", dirCount);

    int i;
    for(i = 0; i < dirCount; i++)
    {
        printf("FILLING IN %s\n", dirs[i].dname);
        filler(buf, dirs[i].dname, NULL, 0);
    }
    printf("==========READDIR END==========\n");

    return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
    printf("==========MKDIR START==========\n");
    printf("PATH: %s\n", path);

    (void) mode;

    FILE * fp;
    fp = fopen (".directories", "r");

    int dirCount = 0;

    if(fp != NULL)
    {
        printf(".directories file opened sucessfully\n");

        // Read the first thing in the file, the number of dirs in the file.
        fread(&dirCount, 1, sizeof(int), fp);

        // Allocate space for all directories, plus an additional one which will be the one we add
        dirs = (cs1550_directory_entry *) malloc(sizeof(cs1550_directory_entry) * (dirCount+1));

        // Read all the directories from our file into our array of dir structures.
        fread(dirs, dirCount, sizeof(cs1550_directory_entry) * dirCount, fp);

        cs1550_directory_entry *newDir = &dirs[dirCount];

        strcpy(newDir->dname, path+1);
        newDir->nFiles = 0;

        dirCount++;
        printf("Changing dir count from %d to %d\n", dirCount - 1, dirCount);

        fp = fopen(".directories", "w");

        fseek(fp, SEEK_SET, 0);

        fwrite(&dirCount, 1, sizeof(int), fp);
        fwrite(dirs, 1, sizeof(cs1550_directory_entry) * dirCount, fp);
    }
    else
    {
        printf(".directories file NOT opened sucessfully\n");
        printf("Creating new DIR\n");

        fp = fopen(".directories", "w");

        int one = 1;

        dirs = (cs1550_directory_entry *) malloc(sizeof(cs1550_directory_entry));

        strcpy(dirs[0].dname, path + 1);
        dirs[0].nFiles = 0;

        fwrite(&one, 1, sizeof(int), fp);
        fwrite(dirs, 1, sizeof(cs1550_directory_entry), fp);

        printf("WROTE FILE\n");
    }

    fclose(fp);

    printf("==========MKDIR END==========\n");
    return 0;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
    (void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void) mode;
    (void) dev;
    (void) path;
    return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;

    //check to make sure path exists
    //check that size is > 0
    //check that offset is <= to the file size
    //read in data
    //set size and return, or error

    size = 0;

    return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void) buf;
    (void) offset;
    (void) fi;
    (void) path;



    //check to make sure path exists
    //check that size is > 0
    //check that offset is <= to the file size
    //write data
    //set size (should be same as input) and return, or error

    return size;
}










/******************************************************************************
*
*  DO NOT MODIFY ANYTHING BELOW THIS LINE
*
*****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
    (void) path;
    (void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;

    return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
        .getattr    = cs1550_getattr,
        .readdir    = cs1550_readdir,
        .mkdir    = cs1550_mkdir,
        .rmdir = cs1550_rmdir,
        .read    = cs1550_read,
        .write    = cs1550_write,
        .mknod    = cs1550_mknod,
        .unlink = cs1550_unlink,
        .truncate = cs1550_truncate,
        .flush = cs1550_flush,
        .open    = cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &hello_oper, NULL);
}

#pragma clang diagnostic pop