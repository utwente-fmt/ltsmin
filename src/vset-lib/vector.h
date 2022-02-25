/*
 * vector.h
 *
 *  Created on: Jul 11, 2019
 *      Author: lieuwe
 */

#ifndef SRC_VSET_LIB_VECTOR_H_
#define SRC_VSET_LIB_VECTOR_H_

/* A vector implementation for C
 * By Edd Mann
 * Copied from https://eddmann.com/posts/implementing-a-dynamic-vector-array-in-c/
 *
 */


#define VECTOR_INIT_CAPACITY 4

#define VECTOR_INIT(vec) vector vec; vector_init(&vec)
#define VECTOR_ADD(vec, item) vector_add(&vec, (void *) item)
#define VECTOR_SET(vec, id, item) vector_set(&vec, id, (void *) item)
#define VECTOR_GET(vec, type, id) (type) vector_get(&vec, id)
#define VECTOR_DELETE(vec, id) vector_delete(&vec, id)
#define VECTOR_TOTAL(vec) vector_total(&vec)
#define VECTOR_FREE(vec) vector_free(&vec)

typedef struct vector {
    void **items;
    int capacity;
    int total;
} vector;

void vector_init(vector *);
int vector_total(vector *);
static void vector_resize(vector *, int);
void vector_add(vector *, void *);
void vector_set(vector *, int, void *);
void *vector_get(vector *, int);
void vector_delete(vector *, int);
void vector_free(vector *);


#endif /* SRC_VSET_LIB_VECTOR_H_ */
