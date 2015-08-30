#include "vfs.h"
#include <string.h>
#include <stdlib.h>

#define CONSOLE_OPEN_READ ((void*) 1)

//linked list for vnodes
//TODO change this to something sensible
vnode* vnode_list;


int con_read(int file, const char *buf, size_t nbyte);

vnode_ops console_ops = {NULL, &con_read, NULL, NULL};
vnode_ops nfs_ops;


vnode* find_vnode(const char* path);

vnode* vfs_open(const char* path, fmode_t mode) {
    vnode *vn = NULL;
    if (strcmp(path, "console") == 0) {         
        vn = find_vnode(path);
        if (vn != NULL) {
            if (mode & FM_READ) {
                if (vn->fs_data == CONSOLE_OPEN_READ) {
                    //tried to open console twice for reading
                    return NULL;
                }
                vn->fs_data = CONSOLE_OPEN_READ;
            }

            vn->ref_count++;
            vn->atime = time_stamp();
            return vn;
        }

        //console wasn't open. make a new vnode
        vn = malloc(sizeof(vnode) + strlen("console") + 1);
        
        if (vn == NULL) {
            return vn;
        }
        //set fields
        vn->fmode = FM_READ | FM_WRITE;
        vn->size = 0;
        vn->ctime = time_stamp();
        vn->atime = vn->ctime;
        vn->ref_count = 1;

        //the console doesn't need the fs_data 
        vn->fs_data = (mode & FM_READ ? CONSOLE_OPEN_READ : NULL);

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

int vfs_close(vnode *vn, fmode_t mode) {
    if (vn == NULL) {
        return -1;
    }
    if (strcmp(vn->name, "console") == 0) {
        vn->fs_data = NULL;
    }
    vn->ref_count--;
    if (vn->ref_count == 0) {
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
    }

    return 0;
}

//traverse the linked list and see if the file has been opened by the vfs.
vnode* find_vnode(const char* path) {
    vnode *vn = vnode_list; 
    while (vn != NULL && strcmp(path, vn->name) != 0) {
        vn = vn->next;
    }
    return vn;
}

int con_read(int file, const char *buf, size_t nbyte) {
    int bytes = 0;
    /**/
    return bytes;
}
