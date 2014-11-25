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

#define    BLOCK_SIZE 512
#define    SIZE_OF_BITMAP 3
#define    NUM_OF_BLOCKS 10240





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


/* * * * * * * * * * * * * * *

      FUNCTION PROTOTYPES

* * * * * * * * * * * * * * */
int markFree(int);
int markTaken(int);
int blockStatus(int);
void blockToByteTranslation(int, int *, int *);
unsigned int getBitFromByte(char, int);
int moveFileToMemory(void *, int);
void removeFileFromMemory(int, int);
int countFreeRun(int);
int nextFreeRunFit(int);
void printByte(char);
int getBlockSize(size_t);
char getDir(const char *, cs1550_directory_entry *);
void addFileToDir(const char *, struct cs1550_file_directory);
/* * * * * * * * * * * * * * *

        HELPER FUNCTIONS

 * * * * * * * * * * * * * * */



// Prints the binary representation of a byte
void printByte(char byte)
{
    unsigned int bit;

    int i = 0;
    for(i = 0; i < 8; i++)
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
    fp = fopen(".disk", "r+");

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
    fp = fopen(".disk", "r+");

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
    fp = fopen(".disk", "r+");

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
    byte = byte << indexInByte;
    unsigned char mask = 0x80;

    unsigned int bit = mask & byte;

    if(bit == 128) return 1;
    else return 0;
}


// Given a block will return how many consecutive free blocks can be found after this block.
int countFreeRun(int blockNum)
{
    if(blockStatus(blockNum) == 1) return -1;
    if(blockNum <= SIZE_OF_BITMAP) return -1; // Don't allow allocation over our bitmap.

    int i;
    int freeBlocksSoFar = 0;

    for(i = blockNum; i < NUM_OF_BLOCKS; i++)
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

    for(i = SIZE_OF_BITMAP; i < NUM_OF_BLOCKS; i++) // TODO should be -1?
    {
        if(countFreeRun(i) >= sizeOfTargetRun) return i;
    }

    return -1;
}

// Given a file, will move said file into memory and mark the bitmap appropriately. Returns startblock.
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

    fp = fopen(".disk", "r+");

    fseek(fp, offsetInBytes, SEEK_SET);
    if(data != 0)
    {
        printf("Writing data to .disk\n");
        fwrite(data, 1, size, fp);

    }
    fclose(fp);

    int i;

    for(i = startBlock; i < startBlock + blockCount; i++)
    {
        markTaken(i);
    }

    return startBlock;
}


// Marks a given region in memory as free
void removeFileFromMemory(int startBlockNum, int blockCount)
{
    int i;

    for(i = startBlockNum; i < startBlockNum + blockCount; i++)
    {
        markFree(i);
    }
}


// Give a path, returns that path's directory.
char getDir(const char *path, cs1550_directory_entry *d)
{

    printf("===============GET DIR START=====================\n");
    FILE * fp;
    fp = fopen (".directories", "r");

    if(fp != NULL)
    {
        cs1550_directory_entry ret;
        while(fread(&ret, 1, sizeof(cs1550_directory_entry), fp) != 0)
        {
            if(strcmp(path, ret.dname) == 0)
            { // If you've found a directory with a name that matches the one passed in.
                *d = ret;
                return 1;
            }

        }
    }

    printf("Sorry, we couldn't find your file.\n");
    return 0;
}


// given a directory name, put a given file into that directory structure.
void addFileToDir(const char *path, struct cs1550_file_directory newFile)
{
    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    struct cs1550_directory_entry pop;
    getDir(directory, &pop);

    pop.files[pop.nFiles] = newFile;
}


// find out how many blocks you'll need to store a file of a given size.
int getBlockSize(size_t fsize)
{
    int blockCount = 0;
    while(fsize > 0)
    {
        fsize = fsize - BLOCK_SIZE;
        blockCount++;
    }

    return blockCount;
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


        if(strcmp(filename, "")) // If the filename isn't empty
        {
            printf("fIlE sTuFf\n");

            cs1550_directory_entry targetDir;
            if(getDir(directory, &targetDir))
            {
                int i;
                for(i = 0; i < targetDir.nFiles; i++)
                {
                    if(!strcmp(filename, targetDir.files[i].fname))
                    {
                        //regular file, probably want to be read and write
                        stbuf->st_mode = S_IFREG | 0666;
                        stbuf->st_nlink = 1; //file links
                        stbuf->st_size = targetDir.files[i].fsize;
                        printf("==========GETATTR END==========\n");
                        return 0;
                    }
                }
                printf("==========GETATTR END (FAIL 1)==========\n");
                return -ENOENT;
            }
        }

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
                printf("==========GETATTR END (FAIL 1)==========\n");
                return -ENOENT;
            }
        }


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

    printf("===================================== READDIR START =====================================\n");

    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    printf("Reading from Directory: %s\n", directory);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    printf("PATH: %s\n", path);

    //This line assumes we have no subdirectories, need to change TODO
    if (strcmp(path, "/") == 0)
    {
        FILE *fp;
        fp = fopen(".directories", "r");

        if(fp)
        {
            cs1550_directory_entry dir;
            while(fread(&dir, 1, sizeof(cs1550_directory_entry), fp) != 0)
            {
                filler(buf, dir.dname, NULL, 0);
            }
        }



        printf("===================================== READDIR END =====================================\n");

        return 0;
    }
    else
    {
        printf("HEREHE %s\n", path);
        cs1550_directory_entry mydir;

        if(getDir(directory, &mydir))
        {
            printf("Got DIR\n");
            printf("DIR NAME = %s\n", mydir.dname);
            printf("DIR NAME = %d\n", mydir.nFiles);

            int i;
            for (i = 0; i < mydir.nFiles; i++)
            {
                filler(buf, mydir.files[i].fname, NULL, 0);
            }
            printf("---~---\n");
        }
        else
        {
            printf("We could find your dir... SORRY!\n");
        }
    }
    printf("===================================== READDIR END =====================================\n");
    return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{

    (void) mode;

    printf("==========MKDIR START==========\n");

    FILE *fp;
    fp = fopen(".directories", "a");

    cs1550_directory_entry *dirs = (cs1550_directory_entry *) malloc(sizeof(cs1550_directory_entry));

    strcpy(dirs->dname, path + 1);
    dirs->nFiles = 0;

    fwrite(dirs, 1, sizeof(cs1550_directory_entry), fp);

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

    printf("===================================== MKNOD START =====================================\n");


    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    printf("DIRECTORY: %s\n", directory);
    printf("FILENAME: %s\n", filename);
    printf("EXTENSION: %s\n", extension);


    // Give a file a single block to start with.
    int startBlock = moveFileToMemory(0, 1);

    if(strcmp("/", directory) == 0)
    {
        printf("I'm sorry, but you can't create files in the root directory.\n");
        return -1;
    }

    FILE *fp;

    fp = fopen(".directories", "r+");

    if(fp)
    {
        cs1550_directory_entry searchDir;
        while(fread(&searchDir, 1, sizeof(cs1550_directory_entry), fp))
        {
            if(!strcmp(searchDir.dname, directory))
            {

                strcpy(searchDir.files[searchDir.nFiles].fname, filename);
                strcpy(searchDir.files[searchDir.nFiles].fext, extension);
                searchDir.files[searchDir.nFiles].fsize = 0;
                searchDir.files[searchDir.nFiles].nStartBlock = startBlock;

                searchDir.nFiles++;

                fseek(fp, -sizeof(cs1550_directory_entry), SEEK_CUR);
                fwrite(&searchDir, 1, sizeof(cs1550_directory_entry), fp);
                fclose(fp);
                printf("===================================== MKNOD END =====================================\n");
                return 0;
            }
        }
    }

    fclose(fp);
    printf("===================================== MKNOD END (FAIL) =====================================\n");
    return -1;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    cs1550_directory_entry *dir = NULL;
    getDir(directory, dir);

    int i;
    for(i = 0; i < dir->nFiles; i++)
    {
        if (strcmp(dir->files[i].fname, filename) == 0)
        {
            int startBlock = dir->files[i].nStartBlock;
            int sizeInBlocks = getBlockSize(dir->files[i].fsize);

            removeFileFromMemory(startBlock, sizeInBlocks);

            memset((void *)&dir->files[i], 0, sizeof(struct cs1550_file_directory));

            return 0;
        }
    }

    return -1;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{

    (void) fi;

    if(size <= 0)
    {
        printf("Size too small.\n");
        return -1;
    }
    if(offset > size)
    {
        printf("Your offset is larger than the file.\n");
        return -1;
    }

    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    cs1550_directory_entry *dir = NULL;

    getDir(directory, dir);

    //check to make sure path exists
    if(dir == NULL)
    {
        printf("Cannot find specified directory.\n");
        return -1;
    }

    int i;
    for(i = 0; i < dir->nFiles; i++)
    {
        if(strcmp(dir->files[i].fname, filename) == 0)
        {
            FILE *fp;
            fp = fopen(".disk", "r+");

            int startBlock = dir->files[i].nStartBlock;

            int offsetInBytes = startBlock * BLOCK_SIZE;

            fseek(fp, offsetInBytes + offset, SEEK_SET);
            fread(buf, 1, size, fp);

            fclose(fp);
            return size;
        }
    }
    return -1;
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

    printf("===================================== WRITE START =====================================\n");

    //check that size is > 0
    if(size <= 0)
    {
        printf("Size too small.\n");
        return -1;
    }
    if(offset > size)
    {
        printf("Your offset is larger than the file.\n");
        return -1;
    }

    char directory[MAX_FILENAME + 1] = {0};
    char filename[MAX_FILENAME + 1] = {0};
    char extension[MAX_EXTENSION] = {0};

    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    cs1550_directory_entry *dir = NULL;


    if(strcmp("/", directory) == 0)
    {
        printf("I'm sorry, but you can't create files in the root directory.\n");
        return -1;
    }

    getDir(directory, dir); // TODO should dir be a struct and not a pointer to a struct?

    //check to make sure path exists
    if(dir == NULL)
    {
        printf("Cannot find specified directory.\n");
        return -1;
    }

    int i;
    for(i = 0; i < dir->nFiles; i++)
    {
        if(strcmp(dir->files[i].fname, filename) == 0)
        {
            FILE *fp;
            fp = fopen(".disk", "r+");


            if(getBlockSize(size + offset) > getBlockSize(dir->files[i].fsize))
            { // If it is time to grow the file...

                // Calculate the current size of the file.
                int startBlock = dir->files[i].nStartBlock;
                int sizeInBlocks = getBlockSize(dir->files[i].fsize);

                // Remove the bitmap entries for this file
                removeFileFromMemory(startBlock, sizeInBlocks);


                FILE *fp;
                fp = fopen(".disk","r+");

                void *buffer = malloc(BLOCK_SIZE * sizeInBlocks);

                fseek(fp, startBlock * BLOCK_SIZE, SEEK_SET);

                // Read all the files blocks into a temporary buffer.
                fread(buffer, 1, BLOCK_SIZE * sizeInBlocks, fp);

                // Write the file into the disk and change the bitmap accordingly.
                moveFileToMemory(buffer, BLOCK_SIZE * sizeInBlocks);

                fclose(fp);
            }
            else
            { // If the write won't take us out of our current block...
                int startBlock = dir->files[i].nStartBlock;

                int offsetInBytes = startBlock * BLOCK_SIZE;

                fseek(fp, offsetInBytes + offset, SEEK_SET);
                fwrite(buf, 1, size, fp);

                fclose(fp);
                printf("===================================== WRITE END =====================================\n");
                return size;
            }
        }
    }

    return -1;
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