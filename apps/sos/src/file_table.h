#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_

#include <sos.h>


typedef struct _fhandle_table {
    int file_descriptor[PROCESS_MAX_FILES]; /* file descriptor table */
} fhandle_table;

//TODO move this into the process struct. for now this is for just the single 
//user process
fhandle_table fdt;

#endif /*_FILE_TABLE_H_*/
