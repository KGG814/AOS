#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include <serial/serial.h>
#include <cspace/cspace.h>

#include "vfs.h"
#include "syscalls.h"
#include "file_table.h"
#include "pagetable.h"

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

#define READ_CB_DELAY 100000
#define CONSOLE_BUFFER_SIZE 4096

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
void file_close(vnode *vn);

void con_read_reply_cb(seL4_Uint32 id, void *data);
void file_open_cb(uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr);
void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
typedef struct _con_read_args con_read_args;
typedef struct _file_open_args file_open_args;
typedef struct _file_read_args file_read_args;

int copy_by_page (seL4_Word buf, int count, void *data, addr_space *as);

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
    .vfs_read   = &file_read
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
};

vnode_ops nfs_ops;


void vfs_init(void) {
    serial_handle = serial_init();
    serial_register_handler(serial_handle, serial_cb); 

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
        printf("TEST: %p\n", &mnt_point);
        //nfs_lookup(&mnt_point, vn->name, file_open_cb, (uintptr_t)args);
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


int vfs_getdirent(int pos, const char *name, size_t nbyte, seL4_CPtr reply_cap) {
    send_seL4_reply(reply_cap, 1);
    return VFS_ERR_NOT_DIR;
}

int vfs_stat(const char *path, sos_stat_t *buf, seL4_CPtr reply_cap) {
    /*if (strcmp(path, "console") == 0) {
        buf->st_type = ST_SPECIAL;
        buf->st_mode = FM_WRITE | FM_READ;
        buf->st_size = 0;
        buf->st_ctime
    }*/
    send_seL4_reply(reply_cap, 1);
    return VFS_ERR;
}

void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as) {
    //assert(!"trying to read!");
    if (vn == NULL || (vn->fmode == O_WRONLY)) {
        send_seL4_reply(reply_cap, VFS_ERR);
    }
    
    int bytes = 0;
    char *cur = (char *) buf;
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


void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, addr_space *as) {
    file_read_args *args = malloc(sizeof(file_read_args));
    args->vn = vn;
    args->reply_cap = reply_cap;
    args->buf = (seL4_Word) buf;
    args->offset = offset;
    args->as = as;
    nfs_read(vn->fs_data, *offset, nbyte, file_read_cb, (uintptr_t)args);
}

void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
    /* 9242_TODO Callback */
}

// Set up vnode and filetable
void file_open_cb (uintptr_t token, nfs_stat_t status, fhandle_t *fh, fattr_t *fattr) {
    printf("Open callback\n");
    file_open_args *args = (file_open_args*) token;
    vnode* vn = args->vn;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        return;
    }
    vn->fs_data = fh;
    vn->size = fattr->size;
    vn->ctime = fattr->ctime;
    vn->atime = fattr->atime;
    int fd = add_fd(vn, args->as);
    /* Do filetable setup */
    send_seL4_reply((seL4_CPtr)vn->fs_data, fd);

}

void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data) {
    file_read_args* args = (file_read_args*) token;
    vnode* vn = args->vn;
    addr_space* as = args->as;
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        return;
    }
    vn->atime = fattr->atime;  
    /* 9242_TODO Error check this */
    copy_by_page(args->buf, count, data, as);
    
    *(args->offset) += count;
    send_seL4_reply((seL4_CPtr)vn->fs_data, count);
}

/* 9242_TODO */
int copy_by_page (seL4_Word buf, int count, void *data, addr_space *as) {
    seL4_Word end_addr = (seL4_Word) (buf + count);
    seL4_Word next_addr;
    int err = 0;
    int data_count = 0;
    while (buf < end_addr) {
        next_addr = (buf & PAGE_MASK) + PAGE_SIZE;
        if (next_addr > end_addr) {
            next_addr = end_addr;
        }
        err = map_if_valid(buf & PAGE_MASK, as);
        if (err) {
            return err;
        }
        memcpy((void *)buf, data + data_count, next_addr - buf);
        data_count += (next_addr - buf);
        buf = next_addr;
    }
    return 0;
}
