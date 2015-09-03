#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include <serial/serial.h>
#include <cspace/cspace.h>

#include "vfs.h"
#include "syscalls.h"
#include "file_table.h"

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
fhandle_t root_directory;
//linked list for vnodes
//TODO change this to something sensible

//console specific stuff
void serial_cb(struct serial* s, char c);
void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
void con_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset);
void con_read_reply_cb(seL4_Uint32 id, void *data);
void file_open_cb (uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr);
void file_read_cb(uintptr_t token, nfs_stat_t status, fattr_t *fattr, int count, void *data);
typedef struct _con_read_args con_read_args;
typedef struct _file_open_args file_open_args;
typedef struct _file_read_args file_read_args;

vnode_ops console_ops = 
{
    .vfs_write  = &con_write, 
    .vfs_read   = &con_read
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
    vnode* vn;
    seL4_CPtr reply_cap;
    char* buf;
    int* offset;
};

vnode_ops nfs_ops;


void vfs_init(struct serial *s) {
    serial_handle = s;
    serial_register_handler(s, serial_cb); 
}
    
vnode* vfs_open(const char* path, fmode_t mode, addr_space *as, seL4_CPtr reply_cap) {
    vnode *vn = NULL;
    mode &= O_ACCMODE;
    if (strcmp(path, "console") == 0) {         
        if (mode == O_RDONLY || mode == O_RDWR) { 
            if (console_status == CONSOLE_READ_OPEN) {
                return NULL;
            } 

            console_status = CONSOLE_READ_OPEN;
        }

        //console wasn't open. make a new vnode
        vn = malloc(sizeof(vnode) + strlen("console") + 1);

        if (vn == NULL) {
            return vn;
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
    } else {
        vn = (vnode *)CALLBACK;
        //console wasn't open. make a new vnode
        vnode* vn_callback = malloc(sizeof(vnode) + strlen(path) + 1);

        if (vn_callback == NULL) {
            return NULL;
        }
        file_open_args *args = malloc(sizeof(file_open_args));
        args->vn = vn_callback;
        args->as = as;
        args->reply_cap = reply_cap;
        //set fields
        vn_callback->fmode = mode;
        //insert into linked list
        vn_callback->next = vnode_list;
        vnode_list = vn_callback;
        // Set the ops type
        vn_callback->ops = &file_ops;
        //set the name of the file
        strcpy(vn_callback->name, path);
        //9242_TODO Do the callback
        nfs_lookup(&root_directory, path, file_open_cb, (uintptr_t)args);
    }

    return vn;
}

int vfs_close(vnode *vn) {
    if (vn == NULL) {
        return -1;
    }
    if (strcmp(vn->name, "console") == 0 && (vn->fmode & FM_READ)) {
        console_status = CONSOLE_READ_CLOSE;
    }
    //actually need to close the vnode 
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

void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
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


void file_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
    file_read_args* args = malloc(sizeof(file_read_args));
    args->vn = vn;
    args->reply_cap = reply_cap;
    args->buf = buf;
    args->offset = offset;
    nfs_read(vn->fs_data, *offset, nbyte, file_read_cb, (uintptr_t)args);
}

void file_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset) {
    /* 9242_TODO Callback */
}

// Set up vnode and filetable
void file_open_cb (uintptr_t token, nfs_stat_t status, fhandle_t* fh, fattr_t* fattr) {
    file_open_args* args = (file_open_args*) token;
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
    if (status != NFS_OK) {
        send_seL4_reply(args->reply_cap, -1);
        return;
    }
    vn->atime = fattr->atime;
    memcpy(args->buf, data, count);
    *(args->offset) += count;
    send_seL4_reply((seL4_CPtr)vn->fs_data, count);
}
