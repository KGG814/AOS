#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include <serial/serial.h>
#include <cspace/cspace.h>

#include "vfs.h"

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

#define READ_CB_DELAY 100000
#define CONSOLE_BUFFER_SIZE 4096

//console stuff. possibly move this to its own file
int console_status = CONSOLE_READ_CLOSE;

char console_buf[CONSOLE_BUFFER_SIZE];
volatile int console_data_size = 0;
char *console_data_start = console_buf;
char *console_data_end = console_buf;
const char *console_buf_end = console_buf + CONSOLE_BUFFER_SIZE - 1;

void console_cb(struct serial* s, char c) {
    if (console_data_start == console_data_end && console_data_size != 0) {
        return; //buffer full
    }
    *console_data_end++ = c;
    console_data_size++;
    if (console_data_end == console_buf_end) {
        console_data_end = console_buf;
    }
} 


//linked list for vnodes
//TODO change this to something sensible
vnode* vnode_list = NULL;

void con_read(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap);
int con_write(vnode *vn, const char *buf, size_t nbyte);

vnode_ops console_ops = 
{
    .vfs_write  = &con_write, 
    .vfs_read   = &con_read
};
vnode_ops nfs_ops;

struct serial *serial_handle = NULL;

void vfs_init(struct serial *s) {
    serial_handle = s;
    serial_register_handler(s, console_cb); 
}
    
vnode* vfs_open(const char* path, fmode_t mode) {
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
        vn->ctime = time_stamp();
        vn->atime = vn->ctime;

        //the console doesn't need the fs_data 
        vn->fs_data = NULL;

        //insert into linked list
        vn->next = vnode_list;
        vnode_list = vn;

        //set the vnode_ops 
        vn->ops = &console_ops;

        //set the name of the console 
        strcpy(vn->name, "console");
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


int vfs_getdirent(int pos, const char *name, size_t nbyte) {
    return VFS_ERR_NOT_DIR;
}

int vfs_stat(const char *path, sos_stat_t *buf) {
    /*if (strcmp(path, "console") == 0) {
        buf->st_type = ST_SPECIAL;
        buf->st_mode = FM_WRITE | FM_READ;
        buf->st_size = 0;
        buf->st_ctime
    }*/
    return VFS_ERR;
}

typedef struct _con_read_args {
    char *buf;
    size_t nbyte;
    seL4_CPtr reply_cap;
} con_read_args;

void read_reply_cb(seL4_Uint32 id, void *data) {
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
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, bytes);
        seL4_Send(reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        free(args);
    } else {
        register_timer(READ_CB_DELAY, &read_reply_cb, args);
    }

}

void con_read(vnode *vn, const char *buf, size_t nbyte, seL4_CPtr reply_cap) {
    //assert(!"trying to read!");
    if (vn == NULL || (vn->fmode == O_WRONLY)) {
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, VFS_ERR);
        seL4_Send(reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
    }
    
    int bytes = 0;
    char *cur = (char *) buf;
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
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, bytes);
        seL4_Send(reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
    } else {
        con_read_args *args = malloc(sizeof(con_read_args));
        if (args == NULL) {
            seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
            seL4_SetMR(0, bytes);
            seL4_Send(reply_cap, reply);
            cspace_free_slot(cur_cspace, reply_cap);
        }
        args->buf = cur;
        args->nbyte = nbyte;
        args->reply_cap = reply_cap;

        register_timer(READ_CB_DELAY, &read_reply_cb, args);
    }
}

int con_write(vnode *vn, const char *buf, size_t nbyte) {
    if (vn == NULL || (vn->fmode == O_RDONLY)) {
        return VFS_ERR;
    }
    char *c = (char *) buf;
    int bytes = serial_send(serial_handle, c, nbyte);
    return bytes;//sos_write(buf, nbyte);
}
