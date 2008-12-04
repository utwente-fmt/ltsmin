#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

/**
\file dynamic-array.h

Provides dynamically resizing arrays.
*/

/**
\brief Opaque type array manager.
*/
typedef struct array_manager *array_manager_t;

/**
\brief Create an array manager.
*/
extern array_manager_t create_manager(int block_size);

/**
\brief Add an array to be managed.

This function will give warnings when it is compiled.
*/
extern void add_array(array_manager_t man,void**ar,int e_size);

/**
\brief Add an array to be managed.

This macro wraps a call to add_array in such a way that no warnings are produced.
*/
#define ADD_ARRAY(man,array_var,element_type) {element_type**ptr=&array_var;add_array(man,(void**)ptr,sizeof(element_type));}

/**
\brief Ensure that all managed array are big enough for the given index to be valid.
*/
extern void ensure_access(array_manager_t man,int index);

/**
\brief Return the current size of the managed arrays.
*/
extern int array_size(array_manager_t man);

#endif

