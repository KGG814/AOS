#include <string.h>
#include <stdlib.h>
#include <sos/sos.h>
#include <assert.h>

#include <serial/serial.h>
#include <cspace/cspace.h>
#include <sys/debug.h>
#include "vfs.h"
#include "devices.h"
#include "syscalls.h"
#include "file_table.h"
#include "pagetable.h"
#include "proc.h"
#include "debug.h"

#define verbose 5

vnode* vnode_list = NULL;

extern fhandle_t mnt_point;
fattr_t *mnt_attr = NULL;

int o_to_fm[] = {FM_READ, FM_WRITE, FM_READ + FM_WRITE};

void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
void file_read_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
void file_read_nfs_cb_cont(int pid, seL4_CPtr reply_cap, void *args, int err);

int file_close(vnode *vn);

void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr);
void mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);

void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
void file_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);
void file_write_nfs_cb_continue(int pid, seL4_CPtr reply_cap, void *args, int err);
void file_write_cb(int pid, seL4_CPtr reply_cap, void* args, int err);

void vfs_stat_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);
void vfs_stat_reply(int pid, seL4_CPtr reply_cap, void *args, int err);

void vfs_getdirent_cb(uintptr_t token ,nfs_stat_t status
                     ,int num_files ,char *file_names[], nfscookie_t nfscookie);
void vfs_getdirent_reply(int pid, seL4_CPtr reply_cap, void *args, int err);

sattr_t get_new_file_attr(void);

typedef struct _vfs_open_args vfs_open_args;
typedef struct _file_read_args file_read_args;
typedef struct _file_write_nfs_args file_write_nfs_args;

typedef struct _file_write_args file_write_args;

vnode_ops file_ops = 
{
    ._vfs_write  = &file_write, 
    ._vfs_read   = &file_read,
    ._vfs_close  = &file_close
};

struct _vfs_open_args {
    vnode* vn;
    int pid;
    seL4_CPtr reply_cap;
};

struct _file_read_args {
    vnode *vn;
    seL4_CPtr reply_cap;
    seL4_Word buf;
    char * kbuf;
    int *offset;
    int pid;
    size_t nbyte;
    size_t bytes_read;
    seL4_Word to_read;
    int count;
};

struct _file_write_nfs_args {
    vnode *vn;
    seL4_CPtr reply_cap;
    seL4_Word buf;
    int *offset;
    int pid;
    size_t nbyte;
    size_t bytes_written;
    seL4_Word to_write;
};

struct _file_write_args {
    vnode *vn;
    seL4_Word buf;
    int *offset;
    file_write_nfs_args* nfs_args;
};



vnode_ops nfs_ops;

// A wrapper for the NFS timeout callbacks
void nfs_timeout_wrapper(uint32_t id, void* data) {
    nfs_timeout();
}

// Initialise the virtual file system
void vfs_init(void) {
    // Initialise the console
    con_init();
    // Register an NFS timeout call to happen every 100ms
    int err = register_tic(100000, &nfs_timeout_wrapper, NULL);
    assert(err);
}

// Insert a vnode into the vnode list
void vnode_insert(vnode *vn) {
    if (vn != NULL) {
        vn->next = vnode_list;
        vnode_list = vn;
    }
}

// Remove a vnode from the vnode list
int vnode_remove(vnode *vn) {
    if (vn == NULL) {
        return -1;
    }
    if (vnode_list == vn) {
        vnode_list = vnode_list->next;
    } else {
        vnode* cur = vnode_list;
        while (cur->next != vn) {
            cur = cur->next;
        }
        cur->next = vn->next;
    }
    free(vn);
    return 0;
}

// Open the given path and return a new vnode
vnode* vfs_open(const char* path
               ,fmode_t mode
               ,int pid
               ,seL4_CPtr reply_cap
               ,int *err
               ) 
{
    vnode *vn = NULL;
    mode &= O_ACCMODE;
    //invalid mode
    if (mode != O_RDONLY && mode != O_WRONLY && mode != O_RDWR) {
        // Return null on invalid mode
        return vn;
    }
    *err = VFS_OK; 
    // If we are opening console, call the console open function
    if (strcmp(path, "console") == 0) {         
        vn = console_open(mode, err);
        proc_table[pid]->reader_status = CURR_READ;
    // If we are opening null, call the null open function
    } else if (strcmp(path, "null") == 0) {
        vn = nul_open(mode, err);
    // Search for a file
    } else {
        // Allocate a vnode struct for the new file
        vn = malloc(sizeof(vnode) + strlen(path) + 1);
        // Check if malloc worked
        if (vn == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
        // Allocate arguments for file_open call
        vfs_open_args *args = malloc(sizeof(vfs_open_args));
        // Check if malloc worked
        if (args == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
        // Set the arguments for the file_open call
        args->vn = vn;
        args->pid = pid;
        args->reply_cap = reply_cap;
        // Set mode in vnode
        vn->fmode = mode;
        // Insert into vnode list
        vnode_insert(vn);
        // Set the ops type
        vn->ops = &file_ops;
        // Set the name of the file
        strcpy(vn->name, path);
        // Do an NFS lookup
        int status = nfs_lookup(&mnt_point, vn->name, file_open_cb, (uintptr_t)args);
        // check if the NFS call succeeded
        if (status != RPC_OK) {
            *err = VFS_ERR;
            free(vn);
            return NULL;
        }
        // Tell the calling process we are doing an NFS callback
        *err = VFS_CALLBACK;
    }
    return vn;
}

// NFS callback for the file_open function
// Sets up vnode and filetable
void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    // Get arguments from NFS token
    vfs_open_args *args = (vfs_open_args*) token;
    if (args == NULL || status != NFS_OK) {
        //9242_TODO Error handling, do a reply? 
        return;
    }
    // Get arguments we need
    seL4_CPtr reply_cap = args->reply_cap;
    seL4_CPtr pid = args->pid;
    // Get vnode
    vnode* vn = args->vn;
    // Check if vnode is valid
    if (vn == NULL) {
        eprintf("Error caught in file_open_cb\n");
        send_seL4_reply(reply_cap, pid, -1);
        free(args);
        return;
    }
    // Check if file was found
    if (status == NFS_OK) {
        // File was found
        // Check if permissions match
        if (o_to_fm[vn->fmode] != (o_to_fm[vn->fmode] & fattr->mode)) {
            // Permissions don't match
            assert(RTN_ON_FAIL);
            send_seL4_reply(reply_cap, pid, -1);
            vnode_remove(vn);
            free(args);
            return; 
        }
        // Allocate a file handle
        vn->fs_data = malloc(sizeof(fhandle_t));
        // Check if malloc worked
        if (vn->fs_data == NULL) {
            vnode_remove(vn);
            assert(RTN_ON_FAIL);
            send_seL4_reply(reply_cap, pid, -1);
            free(args);
            return; 
        }
        // Copy from the temporary NFS buffer to our buffer in the vnode
        memcpy(vn->fs_data, fh, sizeof(fhandle_t));
        // Set vnode variables
        vn->size = fattr->size;
        vn->ctime = fattr->ctime;
        vn->atime = fattr->atime;
        // Add a file descriptor to the process's file table
        int fd = add_fd(vn, pid);
        send_seL4_reply(reply_cap, pid, fd);
        free(args);
        return;
    } else if (status == NFSERR_NOENT && vn->fmode != O_RDONLY) {
        // Check if we have got the mount attributes yet from the NFS
        if (mnt_attr != NULL) {
            // mnt_attr have been set, continue
            // Get attributes for a new file
            sattr_t sattr = get_new_file_attr();
            // Do the NFS call to make a new file
            int status = nfs_create(&mnt_point
                                   ,vn->name
                                   ,&sattr
                                   ,file_open_cb
                                   ,(uintptr_t) args
                                   );
            // Check if the NFS call succeeded
            if (status != RPC_OK) {
                assert(RTN_ON_FAIL);
                send_seL4_reply(reply_cap, pid, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
        } else {
            // Get mnt_attr
            mnt_attr = malloc(sizeof(fattr_t));
            // Check if malloc failed
            if (mnt_attr == NULL) {
                assert(RTN_ON_FAIL);
                send_seL4_reply(reply_cap, pid, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
            // Do a lookup on current directory to get mnt_attr
            int status = nfs_lookup(&mnt_point, ".", mnt_lookup_cb, (uintptr_t) args);
            // Check if the NFS call succeeded
            if (status != RPC_OK) {
                assert(RTN_ON_FAIL);
                send_seL4_reply(args->reply_cap, pid, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
        }
    } else { 
        send_seL4_reply(args->reply_cap, pid, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
}


// NFS callback for looking up mount attributes
// Should only be called once, the first time we create a new file
void mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    // Get arguments from NFS token
    vfs_open_args *args = (vfs_open_args *) token;
    vnode* vn = args->vn;
    int pid = args->pid;
    // Check if the NFS call succeeded
    if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, pid, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
    // Save the mount atributes, for use whenever we create a file
    memcpy(mnt_attr, fattr, sizeof(fattr_t));
    // Continue with the open we were doing
    // Get new attributes for file
    sattr_t sattr = get_new_file_attr();
    // Do the NFS call to create a file
    int ret = nfs_create(&mnt_point, vn->name, &sattr, file_open_cb, (uintptr_t) args);
    // Check the NFS call worked
    if (ret != RPC_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, pid, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
}

// Free file data and close the file
int file_close(vnode *vn) {
    if (vn == NULL) {
        return -1;
    }
    if (vn->fs_data != NULL) {
        free(vn->fs_data);
    }
    //delete the vnode. The console doesn't currently hold any data so we can 
    //just clean it up
    if (vnode_remove(vn)) {
        return -1;
    }
    return 0;
}

// Read from a file over the NFS
void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid) {
    if(SOS_DEBUG) printf("file_read %s\n", vn->name);
    // Check if we are allowed to read from file
    if (vn->fmode == O_WRONLY) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, 0);
        return;
    }
    // Set the arguments for the read call
    file_read_args *args = malloc(sizeof(file_read_args));
    if (args == NULL) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, pid, -1);
    }
    args->vn = vn;
    args->reply_cap = reply_cap;
    args->buf = (seL4_Word) buf;
    args->kbuf = NULL;
    args->offset = offset;
    args->pid = pid;
    args->nbyte = nbyte;
    args->bytes_read = 0;
    args->to_read = nbyte;
    seL4_Word start_addr = (seL4_Word) buf;
    seL4_Word end_addr = start_addr + args->to_read;
    // If we go over a page boundary, set to read up to page boundary
    // Check if same page
    if ((end_addr & PAGE_MASK) != (start_addr & PAGE_MASK)) {
        // If not same page, then we have to read a page minus the offset into the page we are at
        args->to_read = PAGE_SIZE - (start_addr & ~PAGE_MASK);
    }
    // Do the NFS call to read
    int status = nfs_read(vn->fs_data, *offset, args->to_read, file_read_nfs_cb, (uintptr_t)args);
    // Check if NFS call worked
    if (status != RPC_OK) {
        free(args);
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, -1);
    }
    if(SOS_DEBUG) printf("file_read ended\n");
}

// Callback for the file read
void file_read_nfs_cb(uintptr_t token
                     ,nfs_stat_t status
                     ,fattr_t *fattr
                     ,int count
                     ,void *data
                     )
{
    if(SOS_DEBUG) printf("file_read_nfs_cb\n");
    // Get arguments from NFS token
    file_read_args* args = (file_read_args*) token;
    // Check if NFS call succeeded
    if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, args->pid, args->bytes_read);
        free(args);
        return;
    }
    vnode* vn = args->vn;
    // Set number of bytes read
    args->count = count;
    // Update access time
    vn->atime = fattr->atime;
    // Create a buffer to store bytes from temporary NFS buffer
    args->kbuf = malloc(sizeof(char)*count);  
    //9242_TODO check this malloc
    // Copy NFS buffer memory to our buffer 
    memcpy((void *)args->kbuf, data, count);
    // Copy from our buffer to a user provided buffer
    copy_page(args->buf, count, (seL4_Word) args->kbuf, args->pid, file_read_nfs_cb_cont, args, args->reply_cap);
    if(SOS_DEBUG) printf("file_read_nfs_cb ended\n");
}

// Continuation for NFS callback for file_read
void file_read_nfs_cb_cont(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if(SOS_DEBUG) printf("file_read_nfs_cb_cont\n");
    // Update read state
    file_read_args* read_args = args;
    vnode* vn = read_args->vn;
    *(read_args->offset) += read_args->count;
    read_args->bytes_read += read_args->count;
    read_args->buf += read_args->count;
    // Free the buffer used to copy NFS stuff
    free(read_args->kbuf);
    // If we have read everything we need to, or we read less than expected, return
    if (read_args->bytes_read == read_args->nbyte || read_args->count < read_args->to_read) {
        // Return to user
        send_seL4_reply(reply_cap, pid, read_args->bytes_read);
        free(read_args); 
    } else {
        // Need to do more reading, find out how much we have to read and do another callback
        read_args->to_read = read_args->nbyte - read_args->bytes_read;
        // If we go over a page boundary, set to read up to page boundary
        // Check if same page
        if ((read_args->buf & ~PAGE_MASK) + read_args->to_read  > PAGE_SIZE) {
            // If not same page, then we have to read a page minus the offset into the page we are at
            read_args->to_read = PAGE_SIZE - (read_args->buf & ~PAGE_MASK);
        }
        // Do the NFS read callback
        int status = nfs_read(vn->fs_data
                ,*(read_args->offset)
                ,read_args->to_read
                ,file_read_nfs_cb
                ,(uintptr_t) read_args
                );
        // Check if NFS call worked
        if (status != RPC_OK) {
            assert(RTN_ON_FAIL);
            send_seL4_reply(read_args->reply_cap, pid, read_args->bytes_read);
            free(read_args);  
        }
    }
    if(SOS_DEBUG) printf("file_read_nfs_cb_cont ended\n");
}

// Write to a file over NFS
void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid) {
    if(SOS_DEBUG) printf("file_write called \n");
    // Check permissions
    if (vn->fmode == O_RDONLY) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, 0);
        return;
    }
    // Set up args for call
    file_write_nfs_args *nfs_args = malloc(sizeof(file_write_nfs_args));
    //9242_TODO check this malloc

    nfs_args->vn = vn;
    nfs_args->reply_cap = reply_cap;
    nfs_args->buf = (seL4_Word) buf;
    nfs_args->offset = offset;
    nfs_args->pid = pid;
    nfs_args->nbyte = nbyte;
    nfs_args->bytes_written = 0;
    nfs_args->to_write = nbyte;
    seL4_Word start_addr = (seL4_Word) buf;
    seL4_Word end_addr = start_addr + nfs_args->to_write;

    file_write_args *write_args = malloc(sizeof(file_write_args));
    //9242_TODO check this malloc
    
    write_args->vn = vn;
    write_args->buf = (seL4_Word) buf;
    write_args->offset = offset;
    write_args->nfs_args = nfs_args;
    // If we go over a page boundary, set to write up to page boundary
    // Check if same page
    if ((end_addr & PAGE_MASK) != (start_addr & PAGE_MASK)) {
        nfs_args->to_write = (start_addr & PAGE_MASK) - start_addr + PAGE_SIZE ;
    }
    // Make sure the page is mapped in
    int err = map_if_valid(nfs_args->buf & PAGE_MASK, pid, file_write_cb, write_args, reply_cap);
    if (err) {
        //printf("couldn't map buffer\n");
        free(nfs_args);
        free(write_args);
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, 0);
        return;
    }
    if(SOS_DEBUG) printf("file_write ended\n");
}

// Callback for the file write
void file_write_cb(int pid, seL4_CPtr reply_cap, void* args, int err) {
    if(SOS_DEBUG) printf("file_write_cb\n");
    // Get the args we need
    file_write_args* write_args = (file_write_args*) args;
    vnode *vn = write_args->vn;
    int *offset = write_args->offset;
    file_write_nfs_args* nfs_args = write_args->nfs_args;
    // Convert the user buffer to a kernel buffer
    seL4_Word kptr = user_to_kernel_ptr(nfs_args->buf, pid);
    // Do the NFS write
    int status = nfs_write(vn->fs_data
            ,*offset
            ,nfs_args->to_write
            ,(const void*)kptr
            ,file_write_nfs_cb
            ,(uintptr_t)nfs_args
            );
    free(args);
    if (status != RPC_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, -1);
        free(nfs_args);
    }
    if(SOS_DEBUG) printf("file_write_cb ended\n");
}

// NFS callback for the file write
void file_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
    if(SOS_DEBUG) printf("file_write_nfs_cb\n");
    // Get the args from the NFS token
    file_write_nfs_args *args = (file_write_nfs_args*) token;
    vnode *vn = args->vn;
    int pid = args->pid;
    seL4_CPtr reply_cap = args->reply_cap;
    //printf("write: got status from nfs: %d\n", status);
    if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, args->bytes_written + count);
        free(args);
        return;
    }
    // Update access time
    vn->atime = fattr->atime;  
    // Update write state
    *(args->offset) += count;
    args->bytes_written += count;
    args->buf += count;
    // If we have written the expected number of bytes, return
    if (args->bytes_written == args->nbyte) {
        send_seL4_reply(reply_cap, pid, args->bytes_written);
        free(args);
    } else {
        // Check next page is mapped
        int err = map_if_valid(args->buf & PAGE_MASK, pid, file_write_nfs_cb_continue, args, 0);
        if (err) {
            assert(RTN_ON_FAIL);
            send_seL4_reply(reply_cap, pid, args->bytes_written);
            free(args);
            return;
        }
    } 
    if(SOS_DEBUG) printf("file_write_nfs_cb ended\n");
}

// Continuation for NFS callback for the file write
void file_write_nfs_cb_continue(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if(SOS_DEBUG) printf("file_write_nfs_cb_continue\n");
    // Get the args from the NFS token
    file_write_nfs_args *nfs_args = (file_write_nfs_args*) args;
    vnode *vn = nfs_args->vn;
    // Convert user ptr to kernel ptr
    seL4_Word kptr = user_to_kernel_ptr(nfs_args->buf, nfs_args->pid);
    // Update write state
    // If we go over a page boundary, set to write up to page boundary
    // Check if same page
    nfs_args->to_write = nfs_args->nbyte - nfs_args->bytes_written;
    if ((kptr & ~PAGE_MASK) + nfs_args->to_write > PAGE_SIZE) {
        nfs_args->to_write = PAGE_SIZE - (kptr & ~(PAGE_MASK));
    }
    // Do NFS write call
    int status = nfs_write(vn->fs_data
            ,*nfs_args->offset
            ,nfs_args->to_write
            ,(const void*)kptr
            ,&file_write_nfs_cb
            ,(uintptr_t) nfs_args
            );
    // Check if NFS call worked
    if (status != RPC_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(reply_cap, pid, nfs_args->bytes_written);
        free(args);
    }
    if(SOS_DEBUG) printf("file_write_nfs_cb_continue ended\n");
}

// Get status of file on NFS
void vfs_stat(const char *path, seL4_Word buf, seL4_CPtr reply_cap, int pid) {
    if(SOS_DEBUG) printf("vfs_stat\n");
    vfs_stat_args *args = malloc(sizeof(vfs_stat_args));
    //9242_TODO check this malloc

    args->reply_cap = reply_cap;
    args->buf = buf;
    args->pid = pid;
    nfs_lookup(&mnt_point, path, vfs_stat_cb, (uintptr_t)args);
    free(path);
    if(SOS_DEBUG) printf("vfs_stat ended\n");
}

// Callback for vfs_stat
void vfs_stat_cb(uintptr_t token
                ,nfs_stat_t status
                ,fhandle_t *fh
                ,fattr_t *fattr
                ) 
{
    if(SOS_DEBUG) printf("vfs_stat_cb\n");
    vfs_stat_args *args = (vfs_stat_args*) token;
    // Check if NFS call succeeded
    if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, args->pid, -1);
        free(args);
        return;
    }
    //9242_TODO Add malloc error check

    sos_stat_t temp = 
        {.st_type = 0
        ,.st_fmode = fattr->mode
        ,.st_size = fattr->size
        ,.st_ctime = fattr->ctime.seconds * 1000 + fattr->ctime.useconds / 1000
        ,.st_atime = fattr->atime.seconds * 1000 + fattr->atime.useconds / 1000
        };
    if (fattr->type == NFREG) {
        temp.st_type = ST_FILE;
    } else {
        temp.st_type = ST_SPECIAL;
    }
    // Set the return value to the file status
    ;
    // Prepare arguments
    //
    //9242_TODO check this malloc
    copy_out_args *copy_args = malloc(sizeof(copy_out_args));
    copy_args->src = (seL4_Word) malloc(sizeof(sos_stat_t));
    memcpy((void *)copy_args->src, &temp, sizeof(sos_stat_t));
    copy_args->usr_ptr = (seL4_Word) args->buf;
    copy_args->nbyte = sizeof(sos_stat_t);
    copy_args->count = 0;
    copy_args->cb = vfs_stat_reply;
    // Copy return value to user
    copy_out(args->pid, args->reply_cap, copy_args, 0);
    free(args);
    if(SOS_DEBUG) printf("vfs_stat_cb ended\n");
}

// Reply wrapper for stat
void vfs_stat_reply(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if(SOS_DEBUG) printf("vfs_stat_reply\n");
    send_seL4_reply(reply_cap, pid, 0);
    free(args);
    if(SOS_DEBUG) printf("vfs_stat_reply ended\n");
}

// Get directory entries
void vfs_getdirent(int pos
                  ,char *buf
                  ,size_t nbyte
                  ,seL4_CPtr reply_cap
                  ,int pid
                  ) 
{
    if(SOS_DEBUG) printf("vfs_getdirent\n");
    // Set up arguments
    getdirent_args *args = malloc(sizeof(getdirent_args));
    //9242_TODO check this malloc
    args->reply_cap = reply_cap;
    args->to_get = pos;
    args->entries_received = 0;
    args->buf = (seL4_Word) buf;
    args->nbyte = nbyte;
    args->pid = pid;
    // Do NFS call
    nfs_readdir(&mnt_point, 0, vfs_getdirent_cb, (uintptr_t)args);
    if(SOS_DEBUG) printf("vfs_getdirent ended\n");
}

// Callback for getdirent
void vfs_getdirent_cb(uintptr_t token
                     ,nfs_stat_t status
                     ,int num_files
                     ,char *file_names[]
                     ,nfscookie_t nfscookie
                     ) 
{
    if(SOS_DEBUG) printf("vfs_getdirent_cb\n");
    // Get arguments from NFS token
    getdirent_args *args = (getdirent_args *)token;
    int pid = args->pid;
    // Check if NFS call succeeded
    if (status != NFS_OK) {
        assert(RTN_ON_FAIL);
        send_seL4_reply(args->reply_cap, args->pid, -1);
        free(args);
    } else if (num_files == 0) {
        // If the entry requested is equal to the next free entry, return 0
        if (args->to_get == args->entries_received) {
            send_seL4_reply(args->reply_cap, pid, 0);
            free(args);
        } else {
            send_seL4_reply(args->reply_cap, pid, -1);
            free(args);
        }
    } else if (args->entries_received + num_files > args->to_get) {
        int index = args->to_get - args->entries_received;
        args->entries_received += num_files;

        int len = strlen(file_names[index]) + 1;
        if (args->nbyte > len) {
            args->nbyte = len;
        }
        //9242_TODO check this malloc
        copy_out_args *copy_args = malloc(sizeof(copy_out_args));
        copy_args->src = (seL4_Word) malloc(sizeof(char) * N_NAME);
        copy_args->usr_ptr = (seL4_Word) args->buf;
        // 9242_TODO Add malloc error checks
        
        strncpy((void *) copy_args->src, file_names[index], N_NAME);
        copy_args->nbyte = args->nbyte;
        copy_args->cb = vfs_getdirent_reply;
        copy_args->count = 0;
        copy_out(args->pid, args->reply_cap, copy_args, 0);
        free(args);
    } else {
        args->entries_received += num_files;
        nfs_readdir(&mnt_point, nfscookie, vfs_getdirent_cb, (uintptr_t)args);
    }
    if(SOS_DEBUG) printf("vfs_getdirent_cb ended\n");
}

// Reply wrapper for getdirent
void vfs_getdirent_reply(int pid, seL4_CPtr reply_cap, void *args, int err) {
    if(SOS_DEBUG) printf("vfs_getdirent_reply\n");
    copy_out_args *copy_args = (copy_out_args *)args;
    send_seL4_reply(reply_cap, pid, copy_args->nbyte);
    free(args);
    if(SOS_DEBUG) printf("vfs_getdirent_reply ended\n");
}

// Wrapper for vfs_stat call
void vfs_stat_wrapper (int pid, seL4_CPtr reply_cap, void* args, int err) {
    if(SOS_DEBUG) printf("vfs_stat_wrapper\n");
    vfs_stat_args *stat_args = (vfs_stat_args *)args; 
    if (err) {
        eprintf("Error caught in vfs_stat_wrapper\n");
        send_seL4_reply(reply_cap, pid, -1);
        return;
    }
    
    char *path = (char *) stat_args->kpath;
    seL4_Word buf = stat_args->buf;
    free(stat_args);
    vfs_stat(path, buf, reply_cap, pid); 
    if(SOS_DEBUG) printf("vfs_stat_wrapper_ended\n");
}

// Get attributes to set for a new file
sattr_t get_new_file_attr(void) {
    // Set the current time from the timer
    timestamp_t cur_time = time_stamp();
    // Set attributes for a new file
    sattr_t sattr = 
            {.mode = 0666
            ,.uid = mnt_attr->uid
            ,.gid = mnt_attr->gid
            ,.size = 0
            ,.atime = {.seconds = cur_time/1000000
                      ,.useconds = cur_time % 1000000 
                      }
            ,.mtime = {.seconds = cur_time/1000000
                      ,.useconds = cur_time % 1000000
                      }            
            };
    return sattr;
}
