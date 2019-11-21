// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

#include<stdlib.h>

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
extern array_manager_t create_manager(size_t block_size);

/**
\brief Destroy an array manager.
*/
extern void destroy_manager(array_manager_t);

/**
\brief type of the resize callback.
 */
typedef void(*array_resize_cb)(void*arg,void*old_array,size_t old_size,void*new_array,size_t new_size);

/**
\brief Add an array to be managed.

This function will give warnings when it is compiled.
*/
extern void add_array(array_manager_t man,void**ar,int e_size,array_resize_cb callback,void*cbarg);

/**
\brief Add an array to be managed.

This macro wraps a call to add_array in such a way that no warnings are produced.
*/
#define ADD_ARRAY(man,array_var,element_type) {element_type**ptr=&array_var;add_array(man,(void**)ptr,sizeof(element_type),NULL,NULL);}

/**
\brief Add an array to be managed and set callbacks to be called during resizing.

This macro wraps a call to add_array in such a way that no warnings are produced.
*/
#define ADD_ARRAY_CB(man, array_var, element_type, cb, cbarg) { \
    element_type **ptr = &array_var; \
    add_array(man, (void**)ptr, sizeof(element_type), (array_resize_cb)cb, cbarg); \
}

/**
\brief Ensure that all managed array are big enough for the given index to be valid.
*/
extern void ensure_access(array_manager_t man,size_t index);

/**
\brief Return the current size of the managed arrays.
*/
extern size_t array_size(array_manager_t man);

#endif

