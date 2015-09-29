#include <string.h>
#include <stdlib.h>
#include <sos.h>
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

#define verbose 5

vnode* vnode_list = NULL;

extern fhandle_t mnt_point;
fattr_t *mnt_attr = NULL;

int o_to_fm[] = {FM_READ, FM_WRITE, FM_READ + FM_WRITE};

void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
int file_close(vnode *vn);

void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr);
void mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);

void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
void file_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count);
void file_write_cb(int pid, seL4_CPtr reply_cap, void* args);
void vfs_stat_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);

void vfs_getdirent_cb(uintptr_t token
                     ,nfs_stat_t status
                     ,int num_files
                     ,char *file_names[]
                     ,nfscookie_t nfscookie
                     );


typedef struct _file_open_args file_open_args;
typedef struct _file_read_args file_read_args;
typedef struct _file_write_nfs_args file_write_nfs_args;
typedef struct _vfs_stat_args vfs_stat_args;
typedef struct _getdirent_args getdirent_args;
typedef struct _file_write_args file_write_args;

int copy_page (seL4_Word dst, int count, seL4_Word src, int pid);

vnode_ops file_ops = 
{
    .vfs_write  = &file_write, 
    .vfs_read   = &file_read,
    .vfs_close  = &file_close
};

struct _file_open_args {
    vnode* vn;
    int pid;
    seL4_CPtr reply_cap;
};

struct _file_read_args {
    vnode *vn;
    seL4_CPtr reply_cap;
    seL4_Word buf;
    int *offset;
    int pid;
    size_t nbyte;
    size_t bytes_read;
    seL4_Word to_read;
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

struct _vfs_stat_args {
    seL4_CPtr reply_cap;
    seL4_Word buf;
    int pid;
};

struct _getdirent_args {
    seL4_CPtr reply_cap;
    seL4_Word to_get;
    seL4_Word entries_received;
    seL4_Word buf;
    int pid;
    size_t nbyte;
};

vnode_ops nfs_ops;

void nfs_timeout_wrapper(uint32_t id, void* data) {
    nfs_timeout();
}

void vfs_init(void) {
    con_init();
    int err = register_tic(100000, &nfs_timeout_wrapper, NULL);
    assert(err);
}

void vnode_insert(vnode *vn) {
    if (vn != NULL) {
        vn->next = vnode_list;
        vnode_list = vn;
    }
}

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
        return vn;
    }
    *err = VFS_OK; 
    if (strcmp(path, "console") == 0) {         
        vn = console_open(mode, err);
    } else if (strcmp(path, "null") == 0) {
        vn = nul_open(mode, err);
    } else {
        vn = malloc(sizeof(vnode) + strlen(path) + 1);

        if (vn == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
        file_open_args *args = malloc(sizeof(file_open_args));
        args->vn = vn;
        args->pid = pid;
        args->reply_cap = reply_cap;
        //set fields
        vn->fmode = mode;
        //insert into linked list
        vn->next = vnode_list;
        vnode_list = vn;
        // Set the ops type
        vn->ops = &file_ops;
        //set the name of the file
        strcpy(vn->name, path);

        //defer to the nfs
        int status = nfs_lookup(&mnt_point, vn->name, file_open_cb, (uintptr_t)args);
        if (status != RPC_OK) {
            *err = VFS_ERR;
            free(vn);
            return NULL;
        }
        *err = VFS_CALLBACK;
    }
    return vn;
}

// Set up vnode and filetable
void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    file_open_args *args = (file_open_args*) token;
    vnode* vn = args->vn;
    if (status == NFS_OK) { //file found
        if (o_to_fm[vn->fmode] != (o_to_fm[vn->fmode] & fattr->mode)) {//if permissions don't match
            send_seL4_reply(args->reply_cap, -1);
            vnode_remove(vn);
            free(args);
            return; 
        }
        vn->fs_data = malloc(sizeof(fhandle_t));
        memcpy(vn->fs_data, fh, sizeof(fhandle_t));
        vn->size = fattr->size;
        vn->ctime = fattr->ctime;
        vn->atime = fattr->atime;
        int fd = add_fd(vn, args->pid);
        /* Do filetable setup */
        send_seL4_reply((seL4_CPtr)args->reply_cap, fd);
        free(args);
        return;
    } else if (status == NFSERR_NOENT && vn->fmode != O_RDONLY) {
        //open file as read/write
        if (mnt_attr != NULL) {
            timestamp_t cur_time = time_stamp();
            sattr_t sattr = {.mode = 0666
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
            int status = nfs_create(&mnt_point, vn->name, &sattr, file_open_cb, (uintptr_t) args);
            if (status != RPC_OK) {
                send_seL4_reply(args->reply_cap, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
        } else {
            //printf("First mnt attr get!\n");
            mnt_attr = malloc(sizeof(fattr_t));
            if (mnt_attr == NULL) {
                send_seL4_reply(args->reply_cap, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
            int status = nfs_lookup(&mnt_point, ".", mnt_lookup_cb, (uintptr_t) args);
            if (status != RPC_OK) {
                send_seL4_reply(args->reply_cap, -1);
                vnode_remove(vn);
                free(args);
                return;
            }
            //printf("First mnt attr get end!\n");
        }
    } else { 
        send_seL4_reply(args->reply_cap, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
}

void mnt_lookup_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    file_open_args *args = (file_open_args *) token;
    vnode* vn = args->vn;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        vnode_remove(vn);
        free(args);
        return;
    }

    memcpy(mnt_attr, fattr, sizeof(fattr_t));
    timestamp_t cur_time = time_stamp();
    sattr_t sattr = {.mode = 0666
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
    int ret = nfs_create(&mnt_point, vn->name, &sattr, file_open_cb, (uintptr_t) args);
    if (ret != RPC_OK) {
        send_seL4_reply(args->reply_cap, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
}

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

/* Takes a user pointer */
void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid) {
    if (vn->fmode == O_WRONLY) {
        printf("couldn't open file in read due to permissions.\n");
        send_seL4_reply(reply_cap, 0);
        return;
    }

    file_read_args *args = malloc(sizeof(file_read_args));
    args->vn = vn;
    args->reply_cap = reply_cap;
    args->buf = (seL4_Word) buf;
    args->offset = offset;
    args->pid = pid;
    args->nbyte = nbyte;
    args->bytes_read = 0;
    args->to_read = nbyte;
    seL4_Word start_addr = (seL4_Word) buf;
    seL4_Word end_addr = start_addr + args->to_read;
    if ((end_addr & PAGE_MASK) != (start_addr & PAGE_MASK)) {
        args->to_read = PAGE_SIZE - (start_addr & ~PAGE_MASK);
    }
    int status = nfs_read(vn->fs_data, *offset, args->to_read, file_read_cb, (uintptr_t)args);
    if (status != RPC_OK) {
        free(args);
        printf("couldn't open file in read\n");
        send_seL4_reply(reply_cap, -1);
    }
}

void file_read_cb(uintptr_t token
                 ,nfs_stat_t status
                 ,fattr_t *fattr
                 ,int count
                 ,void *data
                 )
{
    file_read_args* args = (file_read_args*) token;
    vnode* vn = args->vn;
    //printf("Read cb: usr ptr %p, read: %d\n", args->buf, count);

    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, args->bytes_read);
        free(args);
        return;
    }
    vn->atime = fattr->atime;  
    copy_page(args->buf, count, (seL4_Word) data, args->pid);
    *(args->offset) += count;
    args->bytes_read += count;
    args->buf += count; //need to increment this pointer

    if (args->bytes_read == args->nbyte || count < args->to_read) {
        //printf("read done, bytes_read = %d\n", args->bytes_read);
        send_seL4_reply((seL4_CPtr)args->reply_cap, args->bytes_read);
        free(args); 
    } else {
        args->to_read = args->nbyte - args->bytes_read;
        if ((args->buf & ~PAGE_MASK) + args->to_read  > PAGE_SIZE) {
            args->to_read = PAGE_SIZE - (args->buf & ~PAGE_MASK);
        }
        //printf("starting up new cb. ");
        int status = nfs_read(vn->fs_data
                ,*(args->offset)
                ,args->to_read
                ,file_read_cb
                ,token
                );
        if (status != RPC_OK) {
            send_seL4_reply(args->reply_cap, args->bytes_read);
            free(args);  
        }
    }
}

void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid) {
    if (vn->fmode == O_RDONLY) {
        send_seL4_reply(reply_cap, 0);
        return;
    }
    //printf("File Write called \n");
    file_write_nfs_args *nfs_args = malloc(sizeof(file_write_nfs_args));
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
    write_args->vn = vn;
    write_args->buf = (seL4_Word) buf;
    write_args->offset = offset;
    write_args->nfs_args = nfs_args;

    if ((end_addr & PAGE_MASK) != (start_addr & PAGE_MASK)) {
        nfs_args->to_write = (start_addr & PAGE_MASK) - start_addr + PAGE_SIZE ;
    }

    printf("file_write calling map_if_valid\n");
    int err = map_if_valid(nfs_args->buf & PAGE_MASK, pid, file_write_cb, write_args, reply_cap);
    if (err) {
        //printf("couldn't map buffer\n");
        free(nfs_args);
        free(write_args);
        send_seL4_reply(reply_cap, 0);
        return;
    }
    //printf("write callback set up\n");
}

void file_write_cb(int pid, seL4_CPtr reply_cap, void* args) {
    printf("Write Callback\n");
    file_write_args* write_args = (file_write_args*) args;
    vnode *vn = write_args->vn;
    int *offset = write_args->offset;
    file_write_nfs_args* nfs_args = write_args->nfs_args;

    seL4_Word kptr = user_to_kernel_ptr(nfs_args->buf, pid);
    
    int status = nfs_write(vn->fs_data
            ,*offset
            ,nfs_args->to_write
            ,(const void*)kptr
            ,file_write_nfs_cb
            ,(uintptr_t)nfs_args
            );
    free(args);
    if (status != RPC_OK) {
        send_seL4_reply(reply_cap, -1);
        free(nfs_args);
        return;
    }
}

void file_write_nfs_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count) {
    printf("NFS Callback\n");
    file_write_nfs_args *args = (file_write_nfs_args*) token;
    vnode *vn = args->vn;
    //printf("write: got status from nfs: %d\n", status);
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, args->bytes_written + count);
        free(args);
        return;
    }
    vn->atime = fattr->atime;  

    //the write was done by the nfs
    *(args->offset) += count;
    args->bytes_written += count;
    args->buf += count; //need to increment this pointer

    if (args->bytes_written == args->nbyte) {

        send_seL4_reply((seL4_CPtr)args->reply_cap, args->bytes_written);
        free(args);
    } else {
        printf("file_write_cb calling map_if_valid\n");
        int err = map_if_valid(args->buf & PAGE_MASK, args->pid, NULL, NULL, 0);
        if (err) {
            send_seL4_reply(args->reply_cap, args->bytes_written);
            free(args);
            return;
        }

        seL4_Word kptr = user_to_kernel_ptr(args->buf, args->pid);

        args->to_write = args->nbyte - args->bytes_written;
        if ((kptr & ~PAGE_MASK) + args->to_write > PAGE_SIZE) {
            args->to_write = PAGE_SIZE - (kptr & ~(PAGE_MASK));
        }
        int status = nfs_write(vn->fs_data
                ,*args->offset
                ,args->to_write
                ,(const void*)kptr
                ,&file_write_nfs_cb
                ,(uintptr_t) token
                );
        if (status != RPC_OK) {
            send_seL4_reply(args->reply_cap, args->bytes_written);
            free(args);
        }
    } 

}

void vfs_stat(const char *path, seL4_Word buf, seL4_CPtr reply_cap, int pid) {
    vfs_stat_args *args = malloc(sizeof(vfs_stat_args));
    args->reply_cap = reply_cap;
    args->buf = buf;
    args->pid = pid;
    nfs_lookup(&mnt_point, path, vfs_stat_cb, (uintptr_t)args);
}

void vfs_stat_cb(uintptr_t token
                ,nfs_stat_t status
                ,fhandle_t *fh
                ,fattr_t *fattr
                ) 
{
    vfs_stat_args *args = (vfs_stat_args*) token;

    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        free(args);
        return;
    }

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

    copy_out(args->buf, (seL4_Word) &temp, sizeof(sos_stat_t), args->pid);
    send_seL4_reply(args->reply_cap, 0);
    free(args);
}

void vfs_getdirent(int pos
                  ,char *buf
                  ,size_t nbyte
                  ,seL4_CPtr reply_cap
                  ,int pid
                  ) 
{
    getdirent_args *args = malloc(sizeof(getdirent_args));
    args->reply_cap = reply_cap;
    args->to_get = pos;
    args->entries_received = 0;
    args->buf = (seL4_Word) buf;
    args->nbyte = nbyte;
    args->pid = pid;
    nfs_readdir(&mnt_point, 0, vfs_getdirent_cb, (uintptr_t)args);
}

void vfs_getdirent_cb(uintptr_t token
                     ,nfs_stat_t status
                     ,int num_files
                     ,char *file_names[]
                     ,nfscookie_t nfscookie
                     ) 
{
    getdirent_args *args = (getdirent_args *)token;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        free(args);
    } else if (num_files == 0) {
        // If the entry requested is equal to the next free entry, return 0
        if (args->to_get == args->entries_received) {
            send_seL4_reply(args->reply_cap, 0);
            free(args);
        } else {
            send_seL4_reply(args->reply_cap, -1);
            free(args);
        }
    } else if (args->entries_received + num_files > args->to_get) {
        int index = args->to_get - args->entries_received;
        args->entries_received += num_files;

        int len = strlen(file_names[index]) + 1;
        if (args->nbyte > len) {
            args->nbyte = len;
        }

        copy_out(args->buf, (seL4_Word) file_names[index], args->nbyte, args->pid);

        send_seL4_reply(args->reply_cap, args->nbyte);
        free(args);
    } else {
        args->entries_received += num_files;
        nfs_readdir(&mnt_point, nfscookie, vfs_getdirent_cb, (uintptr_t)args);
    }
}

void vfs_stat_wrapper (int pid, seL4_CPtr reply_cap, void* args) {
    copy_in_args *copy_args = (copy_in_args *)args;
    char *path = (char *) copy_args->cb_arg_1;
    seL4_Word buf = copy_args->cb_arg_2;
    vfs_stat(path, buf, reply_cap, pid); 
}