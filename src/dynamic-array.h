#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

typedef struct array_manager *array_manager_t;

extern array_manager_t create_manager(int block_size);

extern void add_array(array_manager_t man,void**ar,int e_size);

extern void ensure_access(array_manager_t man,int index);

extern int array_size(array_manager_t man);

#endif
