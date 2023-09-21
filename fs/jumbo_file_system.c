#include "jumbo_file_system.h"
#include "string.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static block_num_t current_dir;
static unsigned int allocated_blocks;

static int is_dir(block_num_t block_num) {
    struct block block;
    read_block(block_num, &block);
    return block.is_dir == 0;
}

static struct block create_directory_block(block_num_t block_num, block_num_t prev) {
    struct block block;
    block.is_dir = (uint32_t)0;
    block.contents.dirnode.num_entries = 0;
    // char back[3] = "..";
    // char here[2] = ".";
    // strcpy(block.contents.dirnode.entries[1].name, back);
    // block.contents.dirnode.entries[1].block_num = prev;
    // strcpy(block.contents.dirnode.entries[0].name, here);
    // block.contents.dirnode.entries[0].block_num = block_num;
    return block;
}

static struct block create_inode_block() {
    struct block block;
    block.is_dir = (uint32_t)1;
    block.contents.inode.file_size = 0;
    return block;
}


/* jfs_mount
 *   prepares the DISK file on the _real_ file system to have file system
 *   blocks read and written to it.  The application _must_ call this function
 *   exactly once before calling any other jfs_* functions.  If your code
 *   requires any additional one-time initialization before any other jfs_*
 *   functions are called, you can add it here.
 * filename - the name of the DISK file on the _real_ file system
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_mount(const char* filename) {
    int ret = bfs_mount(filename);
    current_dir = 1; // root directory
    if (ret != 0) return ret;

    // write root directory to block 1
    struct block root = create_directory_block(1, 1);
    ret = write_block(1, &root);

    allocated_blocks = 2; // root and superblock
    return ret;
}


/* jfs_mkdir
 *   creates a new subdirectory in the current directory
 * directory_name - name of the new subdirectory
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_mkdir(const char* directory_name) {

    if (strlen(directory_name) > MAX_NAME_LENGTH) {
        return E_MAX_NAME_LENGTH;
    }

    if (allocated_blocks + 1 > NUM_BLOCKS) {
        return E_DISK_FULL;
    }

    struct block cur;
    read_block(current_dir, &cur);

    if (cur.contents.dirnode.num_entries == MAX_DIR_ENTRIES) {
        return E_MAX_DIR_ENTRIES;
    }

    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (!strcmp(directory_name, cur.contents.dirnode.entries[i].name)) {
            return E_EXISTS;
        }
    }

    // create new directory block
    block_num_t block_num = allocate_block();
    allocated_blocks++;
    struct block new_block = create_directory_block(block_num, current_dir);

    // copy directory blocknum and name to curdir
    cur.contents.dirnode.entries[cur.contents.dirnode.num_entries].block_num = block_num;
    strcpy(cur.contents.dirnode.entries[cur.contents.dirnode.num_entries].name, directory_name);
    cur.contents.dirnode.num_entries++;

    // write changes
    write_block(block_num, &new_block);
    // printf("MAX_DIR_ENTRIES: %d\nDIR_ENTRIES: %d\n", MAX_DIR_ENTRIES, cur.contents.dirnode.num_entries);
    write_block(current_dir, &cur);

    return E_SUCCESS;
}


/* jfs_chdir
 *   changes the current directory to the specified subdirectory, or changes
 *   the current directory to the root directory if the directory_name is NULL
 * directory_name - name of the subdirectory to make the current
 *   directory; if directory_name is NULL then the current directory
 *   should be made the root directory instead
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR
 */
int jfs_chdir(const char* directory_name) {
    if (!directory_name) {
        current_dir = 1; // change to root directory
        return E_SUCCESS;
    }

    struct block block;
    assert(read_block(current_dir, &block) == 0);
    for (int i = 0; i < block.contents.dirnode.num_entries; i++) {
        char* str = block.contents.dirnode.entries[i].name;
        if (strcmp(str, directory_name) == 0) {
            if (!is_dir(block.contents.dirnode.entries[i].block_num)) {
                return E_NOT_DIR;
            }

            // change curdir to directory_name
            current_dir = block.contents.dirnode.entries[i].block_num;
            return E_SUCCESS;
        }
    }
    return E_NOT_EXISTS;
}


/* jfs_ls
 *   finds the names of all the files and directories in the current directory
 *   and writes the directory names to the directories argument and the file
 *   names to the files argument
 * directories - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * file - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * returns 0 on success or one of the following error codes on failure:
 *   (this function should always succeed)
 */
int jfs_ls(char* directories[MAX_DIR_ENTRIES+1], char* files[MAX_DIR_ENTRIES+1]) {
    struct block cur;
    read_block(current_dir, &cur);
    // printf("MAX_DIR_ENTRIES: %d\nDIR_ENTRIES: %d\n", MAX_DIR_ENTRIES, cur.contents.dirnode.num_entries);

    int dir_count = 0, file_count = 0;
    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        char* str = (char *) malloc(MAX_NAME_LENGTH + 1);
        strncpy(str, cur.contents.dirnode.entries[i].name, sizeof(cur.contents.dirnode.entries[i].name));
        if (is_dir(cur.contents.dirnode.entries[i].block_num)) { // add to dirs
            directories[dir_count] = str;
            dir_count++;
        }
        else { // add to files
            files[file_count] = str;
            file_count++;
        }
    }
    // put null after in both arrays
    directories[dir_count] = NULL;
    files[file_count] = NULL;

    return E_SUCCESS;
}


/* jfs_rmdir
 *   removes the specified subdirectory of the current directory
 * directory_name - name of the subdirectory to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR, E_NOT_EMPTY
 */
int jfs_rmdir(const char* directory_name) {
    struct block cur;
    read_block(current_dir, &cur);
    int flag = 0, pos = 0;
    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (strcmp(cur.contents.dirnode.entries[i].name, directory_name) == 0) {
            if (!is_dir(cur.contents.dirnode.entries[i].block_num)) {
                return E_NOT_DIR;
            }

            struct block block;
            read_block(cur.contents.dirnode.entries[i].block_num, &block);
            if (block.contents.dirnode.num_entries != 0) {
                return E_NOT_EMPTY;
            }

            // remove dirblock
            release_block(cur.contents.dirnode.entries[i].block_num);
            allocated_blocks--;
            cur.contents.dirnode.num_entries--;
            flag = 1;
            pos = i;
            break;
        }
    }
    if (flag) { // if removed
        // fix curdir entries list
        for (int i = pos; i < cur.contents.dirnode.num_entries; i++) {
            cur.contents.dirnode.entries[i].block_num = cur.contents.dirnode.entries[i + 1].block_num;
            strcpy(cur.contents.dirnode.entries[i].name, cur.contents.dirnode.entries[i + 1].name);
        }
        // write changes
        write_block(current_dir, &cur);
        return E_SUCCESS;
    }
    return E_NOT_EXISTS;
}


/* jfs_creat
 *   creates a new, empty file with the specified name
 * file_name - name to give the new file
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_creat(const char* file_name) {
    if (strlen(file_name) > MAX_NAME_LENGTH) {
        return E_MAX_NAME_LENGTH;
    }

    struct block cur;
    read_block(current_dir, &cur);
    if (cur.contents.dirnode.num_entries == MAX_DIR_ENTRIES) {
        return E_MAX_DIR_ENTRIES;
    }

    if (allocated_blocks == NUM_BLOCKS) {
        return E_DISK_FULL;
    }

    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (!strcmp(cur.contents.dirnode.entries[i].name, file_name)) {
            return E_EXISTS;
        }
    }

    // create inode
    block_num_t block_num = allocate_block();
    allocated_blocks++;

    // add inode to entries list
    cur.contents.dirnode.entries[cur.contents.dirnode.num_entries].block_num = block_num;
    strcpy(cur.contents.dirnode.entries[cur.contents.dirnode.num_entries].name, file_name);
    cur.contents.dirnode.num_entries++;

    // write changes
    write_block(current_dir, &cur);
    struct block inode = create_inode_block();
    write_block(block_num, &inode);

    return E_SUCCESS;
}


/* jfs_remove
 *   deletes the specified file and all its data (note that this cannot delete
 *   directories; use rmdir instead to remove directories)
 * file_name - name of the file to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */
int jfs_remove(const char* file_name) {
    struct block cur;
    read_block(current_dir, &cur);

    int flag = 0, pos = 0;
    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (!strcmp(cur.contents.dirnode.entries[i].name, file_name)) {
            if (is_dir(cur.contents.dirnode.entries[i].block_num)) {
                return E_IS_DIR;
            }

            struct block found;
            read_block(cur.contents.dirnode.entries[i].block_num, &found);

            // release inode
            release_block(cur.contents.dirnode.entries[i].block_num);
            allocated_blocks--;
            cur.contents.dirnode.num_entries--;

            int num_blocks = found.contents.inode.file_size / BLOCK_SIZE;
            if (num_blocks * BLOCK_SIZE < found.contents.inode.file_size) {
                num_blocks++;
            }
            // release data blocks
            for (int i = 0; i < num_blocks; i++) {
                release_block(found.contents.inode.data_blocks[i]);
                allocated_blocks--;
            }

            flag = 1;
            pos = i;
            break;
        }
    }
    if (!flag) {
        return E_NOT_EXISTS;
    }
    // fix curdir entries
    for (int i = pos; i < cur.contents.dirnode.num_entries; i++) {
        cur.contents.dirnode.entries[i].block_num = cur.contents.dirnode.entries[i + 1].block_num;
        strcpy(cur.contents.dirnode.entries[i].name, cur.contents.dirnode.entries[i + 1].name);
    }
    // write curdir changes
    write_block(current_dir, &cur);
    return E_SUCCESS;
}


/* jfs_stat
 *   returns the file or directory stats (see struct stat for details)
 * name - name of the file or directory to inspect
 * buf  - pointer to a struct stat (already allocated by the caller) where the
 *   stats will be written
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS
 */
int jfs_stat(const char* name, struct stats* buf) {
    struct block cur;
    read_block(current_dir, &cur);

    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (strcmp(cur.contents.dirnode.entries[i].name, name) == 0) {
            struct block found;
            read_block(cur.contents.dirnode.entries[i].block_num, &found);
            buf->is_dir = found.is_dir;
            strcpy(buf->name, name);
            buf->block_num = cur.contents.dirnode.entries[i].block_num;

            if (buf->is_dir) { // it is a file
                buf->file_size = found.contents.inode.file_size;
                buf->num_data_blocks  = found.contents.inode.file_size / BLOCK_SIZE;
                if (found.contents.inode.file_size % BLOCK_SIZE) {
                    buf->num_data_blocks++;
                }
            }

            return E_SUCCESS;
        }
    }
    return E_NOT_EXISTS;
}


/* jfs_write
 *   appends the data in the buffer to the end of the specified file
 * file_name - name of the file to append data to
 * buf - buffer containing the data to be written (note that the data could be
 *   binary, not text, and even if it is text should not be assumed to be null
 *   terminated)
 * count - number of bytes in buf (write exactly this many)
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR, E_MAX_FILE_SIZE, E_DISK_FULL
 */
int jfs_write(const char* file_name, const void* buf, unsigned short count) {
    struct block cur;
    read_block(current_dir, &cur);
    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (strcmp(cur.contents.dirnode.entries[i].name, file_name) == 0) {
            if (is_dir(cur.contents.dirnode.entries[i].block_num)) {
                return E_IS_DIR;
            }

            struct block found;
            read_block(cur.contents.dirnode.entries[i].block_num, &found);

            if (found.contents.inode.file_size + count > MAX_FILE_SIZE) {
                return E_MAX_FILE_SIZE;
            }

            char block[64];
            if (found.contents.inode.file_size != 0) { // old file
                int pos_last_block = found.contents.inode.file_size / BLOCK_SIZE;
                int to_fill = BLOCK_SIZE - (found.contents.inode.file_size % BLOCK_SIZE);

                if (count <= to_fill) { // fill last block only
                    read_block(found.contents.inode.data_blocks[pos_last_block], &block);
                    memcpy(block + found.contents.inode.file_size % BLOCK_SIZE, buf, count);
                    write_block(found.contents.inode.data_blocks[pos_last_block], &block);
                    found.contents.inode.file_size += count;
                    write_block(cur.contents.dirnode.entries[i].block_num, &found);
                    return E_SUCCESS;
                }

                // fill last block
                read_block(found.contents.inode.data_blocks[pos_last_block], &block);
                memcpy(block + found.contents.inode.file_size % BLOCK_SIZE, buf, to_fill);
                write_block(found.contents.inode.data_blocks[pos_last_block], &block);
                found.contents.inode.file_size += to_fill;

                // add additional blocks
                count = count - to_fill;
                found.contents.inode.file_size += count;
                if (count <= BLOCK_SIZE) { // just one block
                    if (allocated_blocks == MAX_DATA_BLOCKS) {
                        return E_DISK_FULL;
                    }
                    block_num_t block_num = allocate_block();
                    allocated_blocks++;
                    found.contents.inode.data_blocks[pos_last_block + 1] = block_num;
                    memcpy(block, buf, count);
                    write_block(block_num, block);
                }
                else {
                    int num_blocks = count / BLOCK_SIZE;
                    if (count % BLOCK_SIZE != 0) num_blocks++;
                    if (num_blocks + allocated_blocks > MAX_DATA_BLOCKS) {
                        return E_DISK_FULL;
                    }
                    int i;
                    for (i = 0; i < num_blocks - 1; i++) {
                        block_num_t block_num = allocate_block();
                        allocated_blocks++;
                        found.contents.inode.data_blocks[i + pos_last_block + 1] = block_num;
                        memcpy(block, buf + i * BLOCK_SIZE + to_fill, BLOCK_SIZE);
                        write_block(block_num, block);
                    }
                    // write last block
                    int remainder = count % BLOCK_SIZE;
                    if (remainder) {
                        block_num_t block_num = allocate_block();
                        allocated_blocks++;
                        found.contents.inode.data_blocks[i] = block_num;
                        memcpy(block, buf + i * BLOCK_SIZE + to_fill, remainder + 1);
                        write_block(block_num, block);
                    }
                }
                write_block(cur.contents.dirnode.entries[i].block_num, &found);
                return E_SUCCESS;
            }


            // new file
            found.contents.inode.file_size += count;
            if (count <= BLOCK_SIZE) { // just one block
                if (allocated_blocks == MAX_DATA_BLOCKS) {
                    return E_DISK_FULL;
                }
                block_num_t block_num = allocate_block();
                allocated_blocks++;
                found.contents.inode.data_blocks[0] = block_num;
                memcpy(block, buf, count);
                write_block(block_num, block);
            }
            else {
                int num_blocks = count / BLOCK_SIZE;
                if (count % BLOCK_SIZE != 0) num_blocks++;
                if (num_blocks + allocated_blocks > MAX_DATA_BLOCKS) {
                    return E_DISK_FULL;
                }
                int i;
                for (i = 0; i < num_blocks - 1; i++) {
                    block_num_t block_num = allocate_block();
                    allocated_blocks++;
                    found.contents.inode.data_blocks[i] = block_num;
                    memcpy(block, buf + i * BLOCK_SIZE, BLOCK_SIZE);
                    write_block(block_num, block);
                }
                // write last block
                int remainder = count % BLOCK_SIZE;
                if (remainder) {
                    block_num_t block_num = allocate_block();
                    allocated_blocks++;
                    found.contents.inode.data_blocks[i] = block_num;
                    memcpy(block, buf + i * BLOCK_SIZE, remainder);
                    write_block(block_num, block);
                }
            }


            write_block(cur.contents.dirnode.entries[i].block_num, &found);
            return E_SUCCESS;
            return E_UNKNOWN;
        }
    }
    return E_NOT_EXISTS;
}


/* jfs_read
 *   reads the specified file and copies its contents into the buffer, up to a
 *   maximum of *ptr_count bytes copied (but obviously no more than the file
 *   size, either)
 * file_name - name of the file to read
 * buf - buffer where the file data should be written
 * ptr_count - pointer to a count variable (allocated by the caller) that
 *   contains the size of buf when it's passed in, and will be modified to
 *   contain the number of bytes actually written to buf (e.g., if the file is
 *   smaller than the buffer) if this function is successful
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */
int jfs_read(const char* file_name, void* buf, unsigned short* ptr_count) {
    struct block cur;
    read_block(current_dir, &cur);
    for (int i = 0; i < cur.contents.dirnode.num_entries; i++) {
        if (strcmp(cur.contents.dirnode.entries[i].name, file_name) == 0) {
            if (is_dir(cur.contents.dirnode.entries[i].block_num)) {
                return E_IS_DIR;
            }
            struct block inode;
            read_block(cur.contents.dirnode.entries[i].block_num, &inode);
            if (*ptr_count > inode.contents.inode.file_size) {
                *ptr_count = inode.contents.inode.file_size;
            }

            int i;
            int num_blocks = *ptr_count / BLOCK_SIZE;
            struct block data_block;
            for (i = 0; i < num_blocks; i++) {
                read_block(inode.contents.inode.data_blocks[i], &data_block);
                memcpy(buf + i * BLOCK_SIZE, &data_block, BLOCK_SIZE);
            }
            int remainder = *ptr_count - i * BLOCK_SIZE;
            if (remainder != 0) { // write last block
                read_block(inode.contents.inode.data_blocks[i], &data_block);
                memcpy(buf + i * BLOCK_SIZE, &data_block, remainder);
            }

            return E_SUCCESS;
        }
    }
    return E_NOT_EXISTS;
}


/* jfs_unmount
 *   makes the file system no longer accessible (unless it is mounted again).
 *   This should be called exactly once after all other jfs_* operations are
 *   complete; it is invalid to call any other jfs_* function (except
 *   jfs_mount) after this function complete.  Basically, this closes the DISK
 *   file on the _real_ file system.  If your code requires any clean up after
 *   all other jfs_* functions are done, you may add it here.
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_unmount() {
  int ret = bfs_unmount();
  return ret;
}
