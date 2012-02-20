// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef RAF_OBJECT_H
#define RAF_OBJECT_H

#include <hre-io/user.h>

struct raf_object {
    void(*read)(raf_t raf,void*buf,size_t len,off_t ofs);
    void(*write)(raf_t raf,void*buf,size_t len,off_t ofs);
    off_t(*size)(raf_t raf);
    void(*resize)(raf_t raf,off_t size);
    void(*close)(raf_t *raf);
    char*name;
};

extern void raf_illegal_read(raf_t raf,void*buf,size_t len,off_t ofs);
extern void raf_illegal_write(raf_t raf,void*buf,size_t len,off_t ofs);
extern off_t raf_illegal_size(raf_t raf);
extern void raf_illegal_resize(raf_t raf,off_t size);
extern void raf_illegal_close(raf_t *raf);
extern void raf_init(raf_t raf,char*name);

#endif


