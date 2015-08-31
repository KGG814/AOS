#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include <serial/serial.h>

#include "vfs.h"

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

#define CONSOLE_BUFFER_SIZE 4096

//console stuff. possibly move this to its own file
int console_status = CONSOLE_READ_CLOSE;

char console_buf[CONSOLE_BUFFER_SIZE];
int console_data_size = 0;
char *console_data_start = console_buf;
char *console_data_end = console_buf;
const char *console_buf_end = console_buf + CONSOLE_READ_CLOSE - 1;

void console_cb(struct serial* s, char c); 

//linked list for vnodes
//TODO change this to something sensible
vnode* vnode_list = NULL;

int con_read(vnode *vn, const char *buf, size_t nbyte);
int con_write(vnode *vn, const char *buf, size_t nbyte);

vnode_ops console_ops = {&con_write, &con_read};
vnode_ops nfs_ops;

struct serial *serial_handle = NULL;

void vfs_init(struct serial *s) {
    serial_handle = s;
    serial_register_handler(s, &console_cb); 
}
    
vnode* vfs_open(const char* path, fmode_t mode) {
    vnode *vn = NULL;
    if (strcmp(path, "console") == 0) {         
        if (mode & FM_READ) { 
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

int con_read(vnode *vn, const char *buf, size_t nbyte) {
    if (vn == NULL || !(vn->fmode & FM_READ)) {
        return VFS_ERR;
    }
    
    int bytes = 0;
    char *cur = (char *) buf;
    while (bytes != nbyte) {
        while (!console_data_size); //wait for data to be entered
        *cur++ = *console_data_start++; 
        if (console_data_start == console_buf_end) {
            console_data_start = console_buf;
        } 
        --console_data_size;
    }

    return bytes;
}

int con_write(vnode *vn, const char *buf, size_t nbyte) {
    if (vn == NULL || !(vn->fmode & FM_WRITE)) {
        return VFS_ERR;
    }

    int bytes = serial_send(serial_handle, buf, nbyte);
    return bytes;//sos_write(buf, nbyte);
}

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

