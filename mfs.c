#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

#define WHITESPACE " \t\n"          // We want to split our command line up into tokens
                                    // so we need to define what delimits our tokens.
                                    // In this case  white space
                                    // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255        // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5         // Mav shell only supports five arguments

#define NUM_BLOCKS 4226             // Maximum number of blocks allocated
#define BLOCK_SIZE 8192             // Maximum block size
#define MAX_FILE_SIZE 10240000      // Maximum file size
#define MAX_FILE 125                // Maximum number of files/inodes
#define MAX_BLOCKS_PER_FILE 1250    // Maxiumum blocks per file
#define MAX_FILENAME 32             // Maximum filename length

// array used to store files in blocks
// NOTE: actual data blocks start at index 130
void *data_blocks[NUM_BLOCKS];

// free inodes array 
uint8_t *free_inode_map;

// free blocks array 
uint8_t *free_block_map;

// entry struct used to store directory file data
struct directory_entry {
    char *name;
    int valid;
    int inode_idx;
    int h;
    int r;
};
struct directory_entry *directory_array_ptr;

// entry struct for inode data
struct inode {
    time_t date;
    int size;
    int valid;
    int blocks[MAX_BLOCKS_PER_FILE];
};
struct inode *inode_array_ptr[MAX_FILE];

// keep track if a file system image is opened or not
int opened = 0;
char *opened_image = NULL;

/*
// image struct that will store image data
typedef struct file_system_image {
    unsigned char *file_data;
    char *image_name;
} image_t;
*/

/*
    Name: trim
    Parameters: string
    Return: void
    Description: trims the input string by replacing the newline character with a null byte
*/
void trim(char *string) {
    if (string[strlen(string)-1] == '\n')
        string[strlen(string)-1] = 0;
}

/*
    Name: cleanup
    Parameters: array of strings, size of the array, and a string
    Return: void
    Description: frees processed strings and sets all strings in the array to NULL
*/
void cleanup(char *arr[], int size, char *string) {
    free(string);
    for (int i = 0; i < size; i++) {
        free(arr[i]);
        arr[i] = NULL;
    }
}

/*
    Name: close_image()
    Parameters: None
    Return: Void
    Description: closes opened image by freeing directory names and data blocks
*/
void close_image() {
    // free directory file names
    for (int i = 0; i < MAX_FILE; i++) {
        free(directory_array_ptr[i].name);
    }

    // free data blocks
    for (int i = 0; i < NUM_BLOCKS; i++) {
        free(data_blocks[i]);
    }

    // freeing data, so image is closed
    opened = 0;
}

/*
    Name: init
    Parameters: None
    Return: void
    Description: sets up new file system image that is empty
*/
void init() {
    // if file image already opened, do cleanup before initializing new one
    if (opened) {
        close_image();
    }

    // allocate memory for data blocks and clear them
    for (int i = 0; i < NUM_BLOCKS; i++) {
        data_blocks[i] = calloc(BLOCK_SIZE, sizeof(char));
    }

    // store directores in block 0 (enough space that block 1 isn't used)
    directory_array_ptr = (struct directory_entry *) data_blocks[0];
    for (int i = 0; i < MAX_FILE; i++) {
        directory_array_ptr[i].name = NULL;
        directory_array_ptr[i].valid = 0;
        directory_array_ptr[i].inode_idx = -1;
        directory_array_ptr[i].h = 0;
        directory_array_ptr[i].r = 0;
    }

    // store inodes in blocks 5-129
    for (int i = 5; i < MAX_FILE + 5; i++) {
        inode_array_ptr[i - 5] = (struct inode *) data_blocks[i];
        inode_array_ptr[i - 5]->date = 0;
        inode_array_ptr[i - 5]->size = 0;
        inode_array_ptr[i - 5]->valid = 0;
        // set all blocks of each inode to invalid index
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
            inode_array_ptr[i - 5]->blocks[j] = -1;
        }
    }

    // store free inode map in block 2
    free_inode_map = (uint8_t *) data_blocks[2];
    for (int i = 0; i < MAX_FILE; i++) {
        free_inode_map[i] == 0;
    }

    // store free block map in block 3
    free_block_map = (uint8_t *) data_blocks[3];
    for (int i = 0; i < NUM_BLOCKS - 130; i++) {
        free_block_map[i] == 0;
    }

    // new image created, so set open to true
    opened = 1;
}

/*
    Name: savefs
    Parameters: file pointer to file being written into
    Return: void
    Description: save contents of file system image into the file specified
*/
void savefs(FILE *fp) {
    // save contents of directory block into file
    for (int i = 0; i < MAX_FILE; i++) {
        // save names of files on image into file
        if (directory_array_ptr[i].name != NULL) {
            // keep track of length of filename so we can read from it later
            int len = strlen(directory_array_ptr[i].name);
            fwrite(&len, sizeof(int), 1, fp);
            fwrite(directory_array_ptr[i].name, sizeof(char), len, fp);
        }
        // no file name at directory index
        else {
            // write 0 into file to indicate no filename to be read later
            int len = 0;
            fwrite(&len, sizeof(int), 1, fp);
        }

        // then save remaining fields
        fwrite(&(directory_array_ptr[i].valid), sizeof(int), 1, fp);
        fwrite(&(directory_array_ptr[i].inode_idx), sizeof(int), 1, fp);
        fwrite(&(directory_array_ptr[i].h), sizeof(int), 1, fp);
        fwrite(&(directory_array_ptr[i].r), sizeof(int), 1, fp);
    }

    // save contents of free inode map into file
    fwrite(free_inode_map, sizeof(uint8_t), MAX_FILE, fp);

    // save contents of free block map into file
    fwrite(free_block_map, sizeof(uint8_t), NUM_BLOCKS - 130, fp);

    // save contents of inode blocks into file
    for (int i = 0; i < MAX_FILE; i++) {
        fwrite(&(inode_array_ptr[i]->date), sizeof(time_t), 1, fp);
        fwrite(&(inode_array_ptr[i]->size), sizeof(int), 1, fp);
        fwrite(&(inode_array_ptr[i]->valid), sizeof(int), 1, fp);
        // also save contents of block array for each inode
        fwrite(inode_array_ptr[i]->blocks, sizeof(int), MAX_BLOCKS_PER_FILE, fp);
    }

    // save contents of data blocks into file
    for (int i = 130; i < NUM_BLOCKS; i++) {
        fwrite(data_blocks[i], BLOCK_SIZE, 1, fp);
    }
}

/*
    Name: open
    Parameters: file pointer to file being read from
    Return: void
    Description: read contents of file and save it into the image
*/
void open(FILE *fp) {
    // read directories and save into directory pointer array
    for (int i = 0; i < MAX_FILE; i++) {
        // read length of filename
        int len;
        fread(&len, sizeof(int), 1, fp);

        // if len > 0, allocate space for filename and read into it
        if (len) {
            directory_array_ptr[i].name = malloc(sizeof(char) * len);
            fread(directory_array_ptr[i].name, sizeof(char), len, fp);
        }
        // if len == 0, no filename to read, so set name to NULL
        else {
            directory_array_ptr[i].name = NULL;
        }

        // then read remaining fields
        fread(&(directory_array_ptr[i].valid), sizeof(int), 1, fp);
        fread(&(directory_array_ptr[i].inode_idx), sizeof(int), 1, fp);
        fread(&(directory_array_ptr[i].h), sizeof(int), 1, fp);
        fread(&(directory_array_ptr[i].r), sizeof(int), 1, fp);
    }

    // read free inode map values from file
    fread(free_inode_map, sizeof(uint8_t), MAX_FILE, fp);

    // read free block map values from file
    fread(free_block_map, sizeof(uint8_t), NUM_BLOCKS - 130, fp);

    // read inodes and save into inode array pointer
    for (int i = 0; i < MAX_FILE; i++) {
        fread(&(inode_array_ptr[i]->date), sizeof(time_t), 1, fp);
        fread(&(inode_array_ptr[i]->size), sizeof(int), 1, fp);
        fread(&(inode_array_ptr[i]->valid), sizeof(int), 1, fp);
        // also read contents of blocks array for each inode
        fread(inode_array_ptr[i]->blocks, sizeof(int), MAX_BLOCKS_PER_FILE, fp);
    }

    // read contents of data blocks into the corresponding data blocks
    for (int i = 130; i < NUM_BLOCKS; i++) {
        fread(data_blocks[i], BLOCK_SIZE, 1, fp);
    }
}

/*
    Name: df
    Parameters: None
    Return: int
    Description: iterates over free block list and gets size of free blocks
*/
int df() {
    // keep count of number of free blocks
    int count = 0;
    
    for (int i = 0; i < NUM_BLOCKS - 130; i++) {
        // increment count if a block is not in use
        if (free_block_map[i] == 0) {
            count++;
        }
    }

    // to get bytes free, multiply count of free blocks by block size
    return count * BLOCK_SIZE;
}

/*
    Name: find_free_directory_entry
    Parameters: none
    Return: int
    Description: searches directory array for first free entry
*/
int find_free_directory_entry() {
    int retval = -1;

    // search directory array for a free entry
    for (int i = 0; i < MAX_FILE; i++) {
        // get i value of first free entry
        if (directory_array_ptr[i].valid == 0) {
            retval = i;
            break;
        }
    }

    return retval;
}

/*
    Name: find_free_inode
    Parameters: none
    Return: int
    Description: searches inode array for first free entry
*/
int find_free_inode() {
    int retval = -1;

    // search inode array for a free entry
    for (int i = 0; i < MAX_FILE; i++) {
        // get i value of first free entry
        if (inode_array_ptr[i]->valid == 0) {
            retval = i;
            break;
        }
    }

    return retval;
}

/*
    Name: find_free_block
    Parameters: none
    Return: int
    Description: searches free block map for first free entry
*/
int find_free_block() {
    int retval = -1;

    // search free block map for a free entry
    for (int i = 0; i < NUM_BLOCKS - 130; i++) {
        // get i value of first free entry
        if (free_block_map[i] == 0) {
            retval = i;
            break;
        }
    }

    // return the index in the map + 130 to get the index in data_blocks
    return retval + 130;
}

/*
    Name: find_free_inode_block_entry
    Parameters: index of an entry in the inode array
    Return: int
    Description: searches blocks array in an inode for the first free entry
*/
int find_free_inode_block_entry(int inode_idx) {
    int retval = -1;

    // search the blocks array in the inode pointed to by the index for a free entry
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
        // get i value of first free entry
        if (inode_array_ptr[inode_idx]->blocks[i] == -1) {
            retval = i;
            break;
        }
    }

    return retval;
}

/*
    Name: put
    Parameters: filename of file being put into image
    Return: void
    Description: will open file and try to read the file into file image system
*/
void put(char *filename) {
    // create stat struct and try reading file into it
    struct stat buf;
    int status = stat(filename, &buf);

    // if invalid name entered, print error message
    if (status == -1) {
        printf("put error: File not found\n");
        return;
    }

    // check if file size is greater than amount of free space on image
    if (buf.st_size > df()) {
        printf("put error: Not enough disk space\n");
        return;
    }

    // check if file size is greater than supported max file size
    if (buf.st_size > MAX_FILE_SIZE) {
        printf("put error: File size too big\n");
        return;
    }

    // try to find a free directory entry
    int dir_idx = find_free_directory_entry();

    // if -1 returned, no space in directory array, so print error message
    if (dir_idx == -1) {
        printf("put error: Not enough disk space\n");
        return;
    }

    // populate directory entry fields
    directory_array_ptr[dir_idx].name = strdup(filename);
    directory_array_ptr[dir_idx].valid = 1;

    // try to find a free inode
    int inode_idx = find_free_inode();

    // if -1 returned, no inode available, so print error message
    if (inode_idx == -1) {
        printf("put error: Not enough disk space\n");
        return;
    }

    directory_array_ptr[dir_idx].inode_idx = inode_idx;
    directory_array_ptr[dir_idx].h = 0;
    directory_array_ptr[dir_idx].r = 0;

    // populate inode entry fields
    inode_array_ptr[inode_idx]->date = time(NULL);
    inode_array_ptr[inode_idx]->size = buf.st_size;
    inode_array_ptr[inode_idx]->valid = 1;

    // update free inode map
    free_inode_map[inode_idx] = 1;

    // open file now to read into data blocks
    FILE *fp = fopen(filename, "r");

    // Save off the size of the input file since we'll use it in a couple of places and 
    // also initialize our index variables to zero. 
    int copy_size = buf.st_size;

    // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
    // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
    // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
    int offset = 0;

    // copy_size is initialized to the size of the input file so each loop iteration we
    // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
    // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
    // we have copied all the data from the input file.
    while (copy_size >= BLOCK_SIZE) {
        // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
        // memory pool. Why? We are simulating the way the file system stores file data in
        // blocks of space on the disk. block_index will keep us pointing to the area of
        // the area that we will read from or write to.
        int block_idx = find_free_block();

        // if -1 returned (never should), print error message and cleanup directory/inode entry
        if (block_idx == -1) {
            printf("put error: Not enough disk space\n");

            free(directory_array_ptr[dir_idx].name);
            directory_array_ptr[dir_idx].name = NULL;
            directory_array_ptr[dir_idx].valid = 0;
            directory_array_ptr[dir_idx].inode_idx = -1;
            directory_array_ptr[dir_idx].h = 0;
            directory_array_ptr[dir_idx].r = 0;

            inode_array_ptr[inode_idx]->date = 0;
            inode_array_ptr[inode_idx]->size = 0;
            inode_array_ptr[inode_idx]->valid = 0;
            for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
                inode_array_ptr[inode_idx]->blocks[i] = -1;
            }

            free_inode_map[inode_idx] = 1;

            return;
        } 

        // Index into the input file by offset number of bytes.  Initially offset is set to
        // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
        // then increase the offset by BLOCK_SIZE and continue the process.  This will
        // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
        fseek(fp, offset, SEEK_SET);
    
        // Read BLOCK_SIZE number of bytes from the input file and store them in our
        // data array. 
        int bytes = fread(data_blocks[block_idx], BLOCK_SIZE, 1, fp);

        // After reading in data into the block, set that block in the free block map to in use
        // index - 130 used to map from index in data_blocks to the index in the free map
        free_block_map[block_idx - 130] = 1;

        // Also record the block used into the blocks array of the inode
        int inode_block_entry = find_free_inode_block_entry(inode_idx);

        // if -1 returned, print error message and cleanup directory/inode entry
        if (inode_block_entry == -1) {
            printf("put error: Not enough disk space\n");

            free(directory_array_ptr[dir_idx].name);
            directory_array_ptr[dir_idx].name = NULL;
            directory_array_ptr[dir_idx].valid = 0;
            directory_array_ptr[dir_idx].inode_idx = -1;
            directory_array_ptr[dir_idx].h = 0;
            directory_array_ptr[dir_idx].r = 0;

            inode_array_ptr[inode_idx]->date = 0;
            inode_array_ptr[inode_idx]->size = 0;
            inode_array_ptr[inode_idx]->valid = 0;
            for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
                inode_array_ptr[inode_idx]->blocks[i] = -1;
            }

            free_inode_map[inode_idx] = 1;

            return;
        } 

        inode_array_ptr[inode_idx]->blocks[inode_block_entry] = block_idx;

        // If bytes == 0 and we haven't reached the end of the file then something is 
        // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
        // It means we've reached the end of our input file.
        if (bytes == 0 && !feof(fp)) {
            printf("An error occured reading from the input file.\n");
            return;
        }

        // Clear the EOF file flag.
        clearerr(fp);

        // Reduce copy_size by the BLOCK_SIZE bytes.
        copy_size -= BLOCK_SIZE;
        
        // Increase the offset into our input file by BLOCK_SIZE.  This will allow
        // the fseek at the top of the loop to position us to the correct spot.
        offset += BLOCK_SIZE;

        // Increment the index into the block array 
        block_idx++;
    }

    // handle remainder
    if (copy_size > 0) {
        // try to find a free block
        int block_idx = find_free_block();

        // if -1 returned (never should), print error message and cleanup directory/inode entry
        if (block_idx == -1) {
            printf("put error: Not enough disk space\n");

            free(directory_array_ptr[dir_idx].name);
            directory_array_ptr[dir_idx].name = NULL;
            directory_array_ptr[dir_idx].valid = 0;
            directory_array_ptr[dir_idx].inode_idx = -1;
            directory_array_ptr[dir_idx].h = 0;
            directory_array_ptr[dir_idx].r = 0;

            inode_array_ptr[inode_idx]->date = 0;
            inode_array_ptr[inode_idx]->size = 0;
            inode_array_ptr[inode_idx]->valid = 0;

            free_inode_map[inode_idx] = 1;

            return;
        } 

        // read remainder into the block
        fseek(fp, offset, SEEK_SET);
        int bytes = fread(data_blocks[block_idx], copy_size, 1, fp);

        // After reading in data into the block, set that block in the free block map to in use
        // index - 130 used to map from index in data_blocks to the index in the free map
        free_block_map[block_idx - 130] = 1;

        // Also record the block used into the blocks array of the inode
        int inode_block_entry = find_free_inode_block_entry(inode_idx);

        // if -1 returned, print error message and cleanup directory/inode entry
        if (inode_block_entry == -1) {
            printf("put error: Not enough disk space\n");

            free(directory_array_ptr[dir_idx].name);
            directory_array_ptr[dir_idx].name = NULL;
            directory_array_ptr[dir_idx].valid = 0;
            directory_array_ptr[dir_idx].inode_idx = -1;
            directory_array_ptr[dir_idx].h = 0;
            directory_array_ptr[dir_idx].r = 0;

            inode_array_ptr[inode_idx]->date = 0;
            inode_array_ptr[inode_idx]->size = 0;
            inode_array_ptr[inode_idx]->valid = 0;
            for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
                inode_array_ptr[inode_idx]->blocks[i] = -1;
            }

            free_inode_map[inode_idx] = 1;

            return;
        } 

        inode_array_ptr[inode_idx]->blocks[inode_block_entry] = block_idx;
    }

    // close file pointer
    fclose(fp);
}

/*
    Name: get
    Parameters: filename of file in image and filename of file getting written to
    Return: void
    Description: retrieve file from image and write it into a file in the curent working directory
*/
void get(char *image_filename, char *out_filename) {
    // first, see if the image file actually exists
    int dir_idx = -1;
    for (int i = 0; i < MAX_FILE; i++) {
        // if NULL name, skip loop iteration
        if (directory_array_ptr[i].name == NULL) {
            continue;
        }

        // check if directory entry name is equal to image filename
        // must be a file that has not deleted in the image
        if (directory_array_ptr[i].valid && !strcmp(directory_array_ptr[i].name, image_filename)) {
            dir_idx = i;
            break;
        }
    }

    // if dir_idx is -1, the file cold not be found, so print error message
    if (dir_idx == -1) {
        printf("get error: File not found\n");
        return;
    }

    // if no output filename given, set it equal to the image filename
    if (!out_filename) {
        out_filename = image_filename;
    }

    // try opening output filename for writing
    FILE *fp = fopen(out_filename, "w");
    if (!fp) {
        printf("get error: File not found\n");
        return;
    }

    // get inode index using directory index
    int inode_idx = directory_array_ptr[dir_idx].inode_idx;

    // Initialize our offsets and pointers just we did above when reading from the file.
    int copy_size = inode_array_ptr[inode_idx]->size;
    int offset = 0;

    // Now that we have the inode of the file in the image, we can iterate through its block array
    // and copy the blocks into the file
    for (int i = 0; inode_array_ptr[inode_idx]->blocks[i] != -1; i++) {
        int block_idx = inode_array_ptr[inode_idx]->blocks[i];

        // Index into the input file by offset number of bytes.  Initially offset is set to
        // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
        // then increase the offset by BLOCK_SIZE and continue the process.  This will
        // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, ..., the remainder
        fseek(fp, offset, SEEK_SET);

        // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
        // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
        // end up with garbage at the end of the file.
        int num_bytes;
        if (copy_size < BLOCK_SIZE) {
            num_bytes = copy_size;
        }
        else {
            num_bytes = BLOCK_SIZE;
        }

        // Write num_bytes number of bytes from our data array into our output file.
        fwrite(data_blocks[block_idx], num_bytes, 1, fp);

        // Reduce the amount of bytes remaining to copy, increase the offset into the file
        copy_size -= num_bytes;
        offset += num_bytes;
    }

    // close file pointer
    fclose(fp);
}

/*
    Name: list
    Parameters: a flag that indicates if the user wants to also list hidden files
    Return: void
    Description: looks through the directory array and prints valid entries
*/
void list(int list_hidden) {
    // keep count of files listed
    int count = 0;

    // iterate through directory array for valid entries
    for (int i = 0; i < MAX_FILE; i++) {
        // only process valid entries
        if (directory_array_ptr[i].valid) {
            // if file is hidden and flag is not set, skip loop iteration
            if (directory_array_ptr[i].h && !list_hidden) {
                continue;
            }

            // get inode index of entry
            int inode_idx = directory_array_ptr[i].inode_idx;

            // get date and size of file from inode
            time_t date = inode_array_ptr[inode_idx]->date;
            int size = inode_array_ptr[inode_idx]->size;

            // ctime returns a string with a newline so strip it
            char *date_string = strdup(ctime(&date));
            trim(date_string);

            // print out list information
            printf("%-8d %s %s\n", size, date_string, directory_array_ptr[i].name);

            // because we allocated memory for date_string, deallocate it
            free(date_string);

            // increment count
            count++;
        }
    }

    // if count is 0, no files were listed
    if (count == 0) {
        printf("list: No files found.\n");
    }
}

/*
    Name: attrib
    Parameters: flags that see which attribute to set and the filename of the file that the
    user wants to set the attribute for
    Return: void
    Description: looks for the file in the directory entry and sets according attribute
*/
void attrib(int set_h, int set_r, char *filename) {
    // first, look for file in directory array
    int dir_idx = -1;
    for (int i = 0; i < MAX_FILE; i++) {
        // if entry is invalid, skip
        if (!directory_array_ptr[i].valid) {
            continue;
        }

        // if filename matches, get its index
        if (!strcmp(directory_array_ptr[i].name, filename)) {
            dir_idx = i;
            break;
        }
    }

    // if file not found, output error message
    if (dir_idx == -1 ) {
        printf("attrib error: File not found\n");
        return;
    }

    // if set_h flag is -1, user wants to set read-only instead
    if (set_h == -1) {
        directory_array_ptr[dir_idx].r = set_r;
    }
    // if set_r flag is -1, user wants to set hidden instead
    else if (set_r == -1) {
        directory_array_ptr[dir_idx].h = set_h;
    }
}

/*
    Name: del
    Parameters: filename of file to delete
    Return: void
    Description: deletes the provided file from the file system image
*/
void del(char *filename) {
    // first, look for file in directory array
    int dir_idx = -1;
    for (int i = 0; i < MAX_FILE; i++) {
        // skip entry if invalid or read-only
        if (!directory_array_ptr[i].valid || directory_array_ptr[i].r) {
            continue;
        }

        // if filename matches, get its index
        if (!strcmp(directory_array_ptr[i].name, filename)) {
            dir_idx = i;
            break;
        }
    }

    // if file not found, output error message
    if (dir_idx == -1 ) {
        printf("attrib error: File not found\n");
        return;
    }

    // get inode index of entry
    int inode_idx = directory_array_ptr[dir_idx].inode_idx;

    // clear directory entry
    free(directory_array_ptr[dir_idx].name);
    directory_array_ptr[dir_idx].name = NULL;
    directory_array_ptr[dir_idx].valid = 0;
    directory_array_ptr[dir_idx].inode_idx = -1;
    directory_array_ptr[dir_idx].h = 0;
    directory_array_ptr[dir_idx].r = 0;

    // clear inode entry and set its value in inode map to not in use
    inode_array_ptr[inode_idx]->date = 0;
    inode_array_ptr[inode_idx]->size = 0;
    inode_array_ptr[inode_idx]->valid = 0;
    free_inode_map[inode_idx] = 0;

    // clear blocks array in inode entry and set corresponding blocks in free block map to not in use
    for (int i = 0; inode_array_ptr[inode_idx]->blocks[i] != -1; i++) {
        // grab block index (offset 130) for free block map
        int block_idx = inode_array_ptr[inode_idx]->blocks[i] - 130;

        // set block in free map block to not in use
        free_block_map[block_idx] = 0;

        // clear entry in blocks array of inode
        inode_array_ptr[inode_idx]->blocks[i] = -1;
    }
}

int main()
{
    char cmd_str[MAX_COMMAND_SIZE] = {0};

    while (1) {
        // Print out the mfs prompt
        printf("mfs> ");

        // Read the command from the commandline.  The
        // maximum command that will be read is MAX_COMMAND_SIZE
        // This while command will wait here until the user
        // inputs something since fgets returns NULL when there
        // is no input
        while (!fgets(cmd_str, MAX_COMMAND_SIZE, stdin));
        trim(cmd_str);

        /* Parse input */
        char *token[MAX_NUM_ARGUMENTS] = {0};
        int token_count = 0;                                 
                                                            
        // Pointer to point to the token
        // parsed by strsep
        char *arg_ptr;                                         
                                                            
        char *working_str = strdup(cmd_str);                

        // we are going to move the working_str pointer so
        // keep track of its original value so we can deallocate
        // the correct amount at the end
        char *working_root = working_str;

        // strtok is used instead of strsep, because strsep returns an empty
        // string for every pair of delimiters in a row
        // Start first call of strtok and save it into the first postion of token.
        // If no input or only delimters, skip parsing and continue at top of loop
        // after deallocating everything (just in case)
        arg_ptr = strtok(working_str, WHITESPACE);
        if (arg_ptr == NULL) {
            cleanup(token, MAX_NUM_ARGUMENTS, working_root);
            continue;
        }
        token[token_count++] = strndup(arg_ptr, MAX_COMMAND_SIZE);

        // Tokenize the input strings with whitespace used as the delimiter.
        // Will ONLY read up to 5 strings (command + args)
        while (((arg_ptr = strtok(NULL, WHITESPACE)) != NULL) && 
              (token_count < MAX_NUM_ARGUMENTS)) {
            token[token_count++] = strndup(arg_ptr, MAX_COMMAND_SIZE);
        }

        /*
        for (int token_index = 0; token_index < token_count; token_index++ ) {
            printf("token[%d] = %s\n", token_index, token[token_index] );  
        }
        */
        
        // implement command functionality

        // if user enters createfs command
        if (!strcmp(token[0], "createfs")) {
            // if no filename given
            if (token[1] == NULL) {
                // print error message, clean parsing variables, and skip loop
                printf("createfs error: File not found\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // if filename given 
            else {
                // free old file image name and replace with new one
                free(opened_image);
                opened_image = strdup(token[1]);
                // initialize new file system image
                init();
            }
        }
        // if user enters savefs command
        else if (!strcmp(token[0], "savefs")) {
            // check if an image is currently not open
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("savefs error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // file image currently open
            else {
                // try opening file to write into
                FILE *fp = fopen(opened_image, "wb");
                if (!fp) {
                    printf("savefs error: File not found\n");
                    cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                    continue;
                }

                // save image into opened file
                savefs(fp);
                fclose(fp);
            }
        }
        // if user enters open command
        else if (!strcmp(token[0], "open")) {
            // if no filename given
            if (token[1] == NULL) {
                // print error message, clean parsing variables, and skip loop
                printf("open error: File not found\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // if filename given
            else {
                // try opening file to write from
                FILE *fp = fopen(token[1], "rb");
                if (!fp) {
                    printf("open error: File not found\n");
                    cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                    continue;
                }

                // free old file image name and replace with new one
                free(opened_image);
                opened_image = strdup(token[1]);
                // initialize a new image to read data into
                init();
                // read data into new image
                open(fp);
                fclose(fp);
            }
        }
        // if user enters close command
        else if (!strcmp(token[0], "close")) {
            // if no opened file image, cleanup and skip
            if (!opened) {
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // file image currently opened
            else {
                // free image name and close image
                free(opened_image);
                opened_image = NULL;
                close_image();
            }
        }
        // if user enters quit command
        else if (!strcmp(token[0], "quit")) {
            // if no image opened, clean parsing variables and return from program
            if (!opened) {
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                break;
            }
            // if image opened
            else {
                // clean parsing variables
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                // free image name and close image
                free(opened_image);
                opened_image = NULL;
                close_image();
                // return from program
                break;
            }
        }
        // if user enters df command
        else if (!strcmp(token[0], "df")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("df error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // if image currently opened
            else {
                // print result of df
                printf("%d bytes free.\n", df());
            }
        }
        // if user enters put command
        else if (!strcmp(token[0], "put")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("put error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // if no filename given
            if (token[1] == NULL) {
                // print error message, clean parsing variables, and skip loop
                printf("put error: File not found\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // if filename given, but too long
            else if (strlen(token[1]) > MAX_FILENAME) {
                // print error message, clean parsing variables, and skip loop
                printf("put error: File name too long\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            else {
                put(token[1]);
            }
        }
        // if user enters get command
        else if (!strcmp(token[0], "get")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("get error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // if no filename given
            if (token[1] == NULL) {
                // print error message, clean parsing variables, and skip loop
                printf("get error: File not found\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // try getting image file
            else {
                get(token[1], token[2]);
            }
        }
        // if user enters list command
        else if (!strcmp(token[0], "list")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("list error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // if user wants to also list hidden files
            if (token[1] != NULL && !strcmp(token[1], "-h")) {
                list(1);
            }
            // else just list unhidden files
            else {
                list(0);
            }
        }
        // if user enters attrib command
        else if (!strcmp(token[0], "attrib")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("attrib error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // if no attribute/filename given, print error message, clean parsing variables, and skip loop
            if (token[1] == NULL || token[2] == NULL) {
                printf("attrib error: Incorrect command usage\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // user wants to set file to hidden
            if (!strcmp(token[1], "+h")) {
                attrib(1, -1, token[2]);
            }
            // user wants to set file to unhidden
            else if (!strcmp(token[1], "-h")) {
                attrib(0, -1, token[2]);
            }
            // user wants to set file to read-only
            else if (!strcmp(token[1], "+r")) {
                attrib(-1, 1, token[2]);
            }
            // user wants to set file to not read-only
            else if (!strcmp(token[1], "-r")) {
                attrib(-1, 0, token[2]);
            }
            // user enters invalid attribute argumemt
            else {
                printf("attrib error: Incorrect command usage\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
        }
        // if user enters del command
        else if (!strcmp(token[0], "del")) {
            // if no image currently opened
            if (!opened) {
                // print error message, clean parsing variables, and skip loop
                printf("del error: No file system image currently open\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }

            // if no filename given
            if (token[1] == NULL) {
                // print error message, clean parsing variables, and skip loop
                printf("del error: File not found\n");
                cleanup(token, MAX_NUM_ARGUMENTS, working_root);
                continue;
            }
            // if filename given
            else {
                del(token[1]);
            }
        }

        // clean parsing variables for next loop iteration
        cleanup(token, MAX_NUM_ARGUMENTS, working_root);
    }

    return 0;
}
