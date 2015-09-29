#include "vfs.h"
#include "syscalls.h"
#include "devices.h"
#include "pagetable.h"
#include <serial/serial.h>
#include <string.h>

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

#define READ_CB_DELAY 100000
#define CONSOLE_BUFFER_SIZE 4096

//console stuff. possibly move this to its own file
struct serial *serial_handle = NULL;

int console_status = CONSOLE_READ_CLOSE;

char console_buf[CONSOLE_BUFFER_SIZE];
int console_data_size = 0;
char *console_data_start = console_buf;
char *console_data_end = console_buf;
const char *console_buf_end = console_buf + CONSOLE_BUFFER_SIZE - 1;

typedef struct _con_read_args con_read_args;
typedef struct _con_write_cb_args {
    seL4_Word buf;
    size_t nbyte;
    size_t bytes_written;
} con_write_args;

void con_write_cb(int pid, seL4_CPtr reply_cap, void *_args);
void con_read_reply_cb(seL4_Uint32 id, void *data);
//console specific stuff
void serial_cb(struct serial* s, char c);

//declarations for console ops 
void con_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
void con_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
int con_close(vnode *vn);

//declarations for nul ops 
void nul_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
void nul_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap, int *offset, int pid);
int nul_close(vnode *vn);

void con_init(void) {
    serial_handle = serial_init();
    serial_register_handler(serial_handle, serial_cb); 
}
//arguments for the console read callback 
struct _con_read_args {
    char *buf;
    size_t nbyte;
    seL4_CPtr reply_cap;
};
vnode_ops console_ops = 
{
    ._vfs_write  = &con_write, 
    ._vfs_read   = &con_read,
    ._vfs_close = &con_close
};

vnode_ops nul_ops = {
    ._vfs_read = &nul_read,
    ._vfs_write = &nul_write,
    ._vfs_close = &nul_close
};

vnode *console_open(fmode_t mode, int *err) {
    
    if (mode == O_RDONLY || mode == O_RDWR) { 
        if (console_status == CONSOLE_READ_OPEN) {
            *err = VFS_ERR;
            return NULL;
        } 

        console_status = CONSOLE_READ_OPEN;
    }

    //console wasn't open. make a new vnode
    vnode *vn = malloc(sizeof(vnode) + strlen("console") + 1);

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
    vnode_insert(vn);
    //set the vnode_ops 
    vn->ops = &console_ops;
    //set the name of the console 
    strcpy(vn->name, "console");

    return vn;

}

vnode *nul_open(fmode_t mode, int *err) {

    vnode *vn = malloc(sizeof(vnode) + strlen("null") + 1);

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

    vnode_insert(vn);

    vn->ops = &nul_ops;
    strcpy(vn->name, "null");

    return vn;
}

//null device stuff
void nul_read(vnode *vn, char *buf, size_t nbyte, seL4_CPtr reply_cap
        ,int *offset, int pid)
{ 
    send_seL4_reply(reply_cap, 0);
}
void nul_write(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap
        ,int *offset, int pid)
{ 
    send_seL4_reply(reply_cap, 0);
}
int nul_close(vnode *vn)
{ 
    vnode_remove(vn);
    return 0;
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


void con_read(vnode *vn
             ,char *buf
             ,size_t nbyte
             ,seL4_CPtr reply_cap
             ,int *offset
             ,int pid
             ) 
{
    //assert(!"trying to read!");
    char* cur = (char *)user_to_kernel_ptr((seL4_Word)buf, pid);
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


void con_write(vnode *vn
              ,const char *buf
              ,size_t nbyte
              ,seL4_CPtr reply_cap
              ,int *offset
              ,int pid
              ) 
{
    ////printf("Con write\n");
    if (vn == NULL || (vn->fmode == O_RDONLY) || nbyte == 0) {
        send_seL4_reply(reply_cap, 0);
        return;
    }

    //9242_TODO make this page by page
    con_write_args *args = malloc(sizeof(con_write_args));
    args->buf = (seL4_Word) buf;
    args->nbyte = nbyte;
    args->bytes_written = 0;

    int err = map_if_valid(args->buf & PAGE_MASK
                          ,pid
                          ,con_write_cb
                          ,args 
                          ,reply_cap
                          );

    if (err) {
        send_seL4_reply(reply_cap, 0);
        free(args);   
    }
}

void con_write_cb(int pid, seL4_CPtr reply_cap, void *_args) {
    con_write_args *args = (con_write_args*) _args;
    int to_write = args->nbyte - args->bytes_written; 
    if ((args->buf & ~PAGE_MASK) + to_write > PAGE_SIZE) {
        to_write = PAGE_SIZE - (args->buf & ~PAGE_MASK);
    }
    seL4_Word src = user_to_kernel_ptr(args->buf, pid);
    serial_send(serial_handle, (char *) src, to_write);
    args->bytes_written += to_write;
    args->buf += to_write;
    
    if (args->bytes_written == args->nbyte) {
        send_seL4_reply(reply_cap, args->nbyte);
        free(args);
        return;
    }

    int err = map_if_valid(args->buf & PAGE_MASK
                          ,pid
                          ,con_write_cb
                          ,args
                          ,reply_cap
                          );
    if (err) {
        send_seL4_reply(reply_cap, args->bytes_written);
        free(args);
    }
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


