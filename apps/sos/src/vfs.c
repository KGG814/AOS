#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include <serial/serial.h>
#include <cspace/cspace.h>
#include <sys/debug.h>
#include "vfs.h"
#include "syscalls.h"
#include "file_table.h"
#include "pagetable.h"

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

#define READ_CB_DELAY 100000
#define CONSOLE_BUFFER_SIZE 4096
#define verbose 5

vnode* vnode_list = NULL;

//console stuff. possibly move this to its own file
struct serial *serial_handle = NULL;

int console_status = CONSOLE_READ_CLOSE;

char console_buf[CONSOLE_BUFFER_SIZE];
int console_data_size = 0;
char *console_data_start = console_buf;
char *console_data_end = console_buf;
const char *console_buf_end = console_buf + CONSOLE_BUFFER_SIZE - 1;
extern fhandle_t mnt_point;
//linked list for vnodes
//9242_TODO change this to something sensible

//removes a vnode from the list
int vnode_remove(vnode *vn);

//null device stuff
void nul_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as)
{ 
    send_seL4_reply(reply_cap, 0);
}
void nul_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset)
{ 
    send_seL4_reply(reply_cap, 0);
}
int  nul_close(vnode *vn)
{ 
    vnode_remove(vn);
    return 0;
}

//console specific stuff
void serial_cb(struct serial* s, char c);
void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as);
void con_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
int con_close(vnode *vn);

void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as);
void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
int file_close(vnode *vn);

void con_read_reply_cb(seL4_Uint32 id, void *data);
void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr);
void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
void vfs_stat_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr);

typedef struct _con_read_args con_read_args;
typedef struct _file_open_args file_open_args;
typedef struct _file_read_args file_read_args;
typedef struct _vfs_stat_args vfs_stat_args;

int copy_page (seL4_Word dst, int count, seL4_Word src, addr_space *as);

vnode_ops console_ops = 
{
    .vfs_write  = &con_write, 
    .vfs_read   = &con_read,
    .vfs_close = &con_close
};

vnode_ops nul_ops = {
    .vfs_read = &nul_read,
    .vfs_write = &nul_write,
    .vfs_close = &nul_close
};

vnode_ops file_ops = 
{
    .vfs_write  = &file_write, 
    .vfs_read   = &file_read,
    .vfs_close  = &file_close
};

//arguments for the console read callback 
struct _con_read_args {
    char *buf;
    size_t nbyte;
    seL4_CPtr reply_cap;
};

struct _file_open_args {
    vnode* vn;
    addr_space* as;
    seL4_CPtr reply_cap;
};

struct _file_read_args {
    vnode *vn;
    seL4_CPtr reply_cap;
    seL4_Word buf;
    int *offset;
    addr_space *as;
    size_t nbyte;
    size_t bytes_read;
    seL4_Word to_read;
};

struct _vfs_stat_args {
    seL4_CPtr reply_cap;
    seL4_Word buf;
};

vnode_ops nfs_ops;

void nfs_timeout_wrapper(uint32_t id, void* data) {
    nfs_timeout();
}

void vfs_init(void) {
    serial_handle = serial_init();
    serial_register_handler(serial_handle, serial_cb); 
    dprintf(0, "registering timer. \n"); 
    assert(register_tic(100000, &nfs_timeout_wrapper, NULL));
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
               ,addr_space *as
               ,seL4_CPtr reply_cap
               ,int *err
               ) {
    vnode *vn = NULL;
    mode &= O_ACCMODE;
    *err = VFS_OK; 
    if (strcmp(path, "console") == 0) {         
        if (mode == O_RDONLY || mode == O_RDWR) { 
            if (console_status == CONSOLE_READ_OPEN) {
                *err = VFS_ERR;
                return NULL;
            } 

            console_status = CONSOLE_READ_OPEN;
        }

        //console wasn't open. make a new vnode
        vn = malloc(sizeof(vnode) + strlen("console") + 1);

        if (vn == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
        //set fields
        vn->fmode = mode;
        vn->size = 0;
        vn->ctime.seconds = 0;
        vn->ctime.useconds = 0;
        vn->atime.seconds = 0;
        vn->atime.useconds = 0;

        //the console doesn't need the fs_data 
        vn->fs_data = NULL;
        //insert into linked list
        vn->next = vnode_list;
        vnode_list = vn;
        //set the vnode_ops 
        vn->ops = &console_ops;
        //set the name of the console 
        strcpy(vn->name, "console");
    } else if (strcmp(path, "null") == 0) {
        vn = malloc(sizeof(vnode) + strlen("null") + 1);

        if (vn == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
    
        vn->fmode = mode;
        vn->size = 0;
        vn->ctime.seconds = 0;
        vn->ctime.useconds = 0;
        vn->atime.seconds = 0;
        vn->atime.useconds = 0;

        vn->fs_data = NULL;

        vn->next = vnode_list;
        vnode_list = vn;

        vn->ops = &nul_ops;
        strcpy(vn->name, "null");
    } else {
        printf("vfs opening a file\n");
        vn = malloc(sizeof(vnode) + strlen(path) + 1);

        if (vn == NULL) {
            *err = VFS_ERR;
            return NULL;
        }
        file_open_args *args = malloc(sizeof(file_open_args));
        args->vn = vn;
        args->as = as;
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
        //9242_TODO Do the callback
        nfs_lookup(&mnt_point, vn->name, file_open_cb, (uintptr_t)args);
        *err = VFS_CALLBACK;
    }
    return vn;
}



int con_close(vnode *vn) {
    if (vn == NULL) {
        return -1;
    }
    if (vn->fmode & FM_READ) {
        console_status = CONSOLE_READ_CLOSE;
    }

    //delete the vnode. The console doesn't currently hold any data so we can 
    //just clean it up
    if (vnode_remove(vn)) {
        return -1;
    }

    return 0;
}


void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as) {
    //assert(!"trying to read!");
    printf("trying to do a con read\n");
    char* cur = (char *)user_to_kernel_ptr((seL4_Word)buf, as);
    if (vn == NULL || (vn->fmode == O_WRONLY)) {
        send_seL4_reply(reply_cap, VFS_ERR);
    }
    int bytes = 0;
    if (console_data_size) { 
        while (bytes < nbyte && console_data_size) {
            *cur++ = *console_data_start++; 
            if (console_data_start == console_buf_end) {
                console_data_start = console_buf;
            } 
            --console_data_size;
            ++bytes;
        }
        send_seL4_reply(reply_cap, bytes);
    } else {
        con_read_args *args = malloc(sizeof(con_read_args));
        if (args == NULL) {
            send_seL4_reply(reply_cap, bytes);
        }
        args->buf = cur;
        args->nbyte = nbyte;
        args->reply_cap = reply_cap;

        register_timer(READ_CB_DELAY, &con_read_reply_cb, args);
    }
}

void con_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
    if (vn == NULL || (vn->fmode == O_RDONLY)) {
        send_seL4_reply(reply_cap, 0);
        return;
    }
    char *c = (char *) buf;
    int bytes = serial_send(serial_handle, c, nbyte);
    send_seL4_reply(reply_cap, bytes);
}

void serial_cb(struct serial* s, char c) {
    if (console_data_start == console_data_end && console_data_size != 0) {
        return; //buffer full
    }
    *console_data_end++ = c;
    console_data_size++;
    if (console_data_end == console_buf_end) {
        console_data_end = console_buf;
    }
} 

void con_read_reply_cb(seL4_Uint32 id, void *data) {
    int bytes = 0;
    con_read_args *args = (con_read_args *) data;
    char* cur = args->buf;
    size_t nbyte = args->nbyte;
    seL4_CPtr reply_cap = args->reply_cap;
    if (console_data_size) { 
        while (bytes < nbyte && console_data_size) {
            //assert(!"got into copy loop");
            *cur++ = *console_data_start++; 
            if (console_data_start == console_buf_end) {
                console_data_start = console_buf;
            } 
            --console_data_size;
            ++bytes;
        }
        send_seL4_reply(reply_cap, bytes);
        free(args);
    } else {
        register_timer(READ_CB_DELAY, &con_read_reply_cb, args);
    }

}

int vfs_getdirent(int pos, const char *name, size_t nbyte, seL4_CPtr reply_cap) {
    send_seL4_reply(reply_cap, 1);
    return VFS_ERR_NOT_DIR;
}

void vfs_stat(const char *path, seL4_Word buf, seL4_CPtr reply_cap) {
    vfs_stat_args *args = malloc(sizeof(vfs_stat_args));
    args->reply_cap = reply_cap;
    args->buf = buf;
    nfs_lookup(&mnt_point, path, vfs_stat_cb, (uintptr_t)args);
}

/* Takes a user pointer */
void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as) {
    printf("attempting a file_read\n");
    file_read_args *args = malloc(sizeof(file_read_args));
    args->vn = vn;
    args->reply_cap = reply_cap;
    args->buf = (seL4_Word) buf;
    args->offset = offset;
    args->as = as;
    args->nbyte = nbyte;
    args->bytes_read = 0;

    args->to_read = nbyte;
    seL4_Word start_addr = (seL4_Word) buf;
    seL4_Word end_addr = start_addr + args->to_read;
    if ((end_addr & PAGE_MASK) != (start_addr & PAGE_MASK)) {
        args->to_read = (start_addr & PAGE_MASK) - start_addr + PAGE_SIZE ;
    }
    int status = nfs_read(vn->fs_data, *offset, args->to_read, file_read_cb, (uintptr_t)args);
    if (status != RPC_OK) {
        printf("file_read: nfs_read returned: %d\n", status);
    }
}

void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
    /* 9242_TODO Callback */
}

int file_close(vnode *vn) {
    if (vn == NULL) {
        return -1;
    }

    //delete the vnode. The console doesn't currently hold any data so we can 
    //just clean it up
    if (vnode_remove(vn)) {
        return -1;
    }

    return 0;
}

// Set up vnode and filetable
void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    file_open_args *args = (file_open_args*) token;
    vnode* vn = args->vn;
    printf("open: status was %d\n", status);
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        vnode_remove(vn);
        free(args);
        return;
    }
    vn->fs_data = fh;
    vn->size = fattr->size;
    vn->ctime = fattr->ctime;
    vn->atime = fattr->atime;
    int fd = add_fd(vn, args->as);
    printf("add fd: returning %d\n", fd);
    /* Do filetable setup */
    send_seL4_reply((seL4_CPtr)args->reply_cap, fd);
    free(args);
}

void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
    file_read_args* args = (file_read_args*) token;
    vnode* vn = args->vn;
    printf("file_read_cb: status was %d\n", status);
    addr_space* as = args->as;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, 0);
        free(args);
        return;
    }

    vn->atime = fattr->atime;  
    /* 9242_TODO Error check this */
    copy_page(args->buf, count, (seL4_Word) data, as);
    *(args->offset) += count;
    args->bytes_read += count;
    args->buf += count; //need to increment this pointer
    if (args->bytes_read == args->nbyte || count < args->to_read) {
        printf("Sending sel4 reply\n");
        send_seL4_reply((seL4_CPtr)args->reply_cap, args->bytes_read);
        free(args); 
    } else {
        args->to_read = args->nbyte - args->bytes_read;
        if (args->to_read > PAGE_SIZE) {
            args->to_read = PAGE_SIZE;
        }
        nfs_read(vn->fs_data, *(args->offset), args->to_read, file_read_cb, (uintptr_t)args);
    } 
}

void vfs_stat_cb(uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    vfs_stat_args *args = (vfs_stat_args*) token;
    seL4_Word f_status = args->buf;
    sos_stat_t *stat = (sos_stat_t *)f_status;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, 1);
        return;
    }
    
    if (fattr->type == NFREG) {
        
        stat->st_type = ST_FILE;
    } else {
        stat->st_type = ST_SPECIAL;
    }
    
    stat->st_fmode = fattr->mode;
    stat->st_size = fattr->size;
    stat->st_ctime = fattr->ctime.seconds * 1000 + fattr->ctime.useconds / 1000;
    stat->st_atime = fattr->atime.seconds * 1000 + fattr->atime.useconds / 1000;
    send_seL4_reply(args->reply_cap, 0);

}

/* 9242_TODO */
int copy_page (seL4_Word dst, int count, seL4_Word src, addr_space *as) {
    int err = map_if_valid(dst & PAGE_MASK, as);
    if (err) {
        return err;
    }
    seL4_Word kptr = user_to_kernel_ptr(dst, as);
    memcpy((void *)kptr, (void *)src , count);
    return 0;
}
