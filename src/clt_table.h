
#include <stdint.h>

#define R_BITS 28

typedef struct clt_dbs_s clt_dbs_t;


extern clt_dbs_t *clt_create (uint32_t ksize, uint32_t log_size);

extern void clt_free (clt_dbs_t* dbs);

extern int clt_find_or_put (const clt_dbs_t* dbs, uint64_t k);
