#include <string.h>
#include <stdlib.h>
#include <sos.h>
#include <assert.h>

#include "vfs.h"

#define CONSOLE_READ_OPEN   1
#define CONSOLE_READ_CLOSE  0

int console_status = CONSOLE_READ_CLOSE;

//linked list for vnodes
//TODO change this to something sensible
vnode* vnode_list = NULL;

int con_read(vnode *vn, const char *buf, size_t nbyte);
int con_write(vnode *vn, const char *buf, size_t nbyte);

vnode_ops console_ops = {&con_write, &con_read};
vnode_ops nfs_ops;

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
    int bytes = 0;
    /**/
    return bytes;
}

int con_write(vnode *vn, const char *buf, size_t nbyte) {
    int bytes = 0;
    return 0;//sos_write(buf, nbyte);
}
