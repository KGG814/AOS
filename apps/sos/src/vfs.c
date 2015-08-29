#include "vfs.h"

//linked list for vnodes
//TODO change this to something sensible
vnode* vnode_list;

vnode_ops console_ops;
vnode_ops nfs_ops;

vnode* vfs_open(const char* path, fmode_t mode) {
    //currently unimplemented
    return NULL;
}


