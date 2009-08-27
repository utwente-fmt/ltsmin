

#ifndef SORTCOUNT_H
#define SORTCOUNT_H

void simplesort(int*a, int n);
void quicksort(int* a, int first, int last);
void bucketsort(int* a, int n);


void sortpiece(int* a, int* b, int first, int last);
 // sorteaza un tablou "twins"
 // (a_i, b_i) < (a_j, b_j) iff a_i < a_j sau a_i=a_j, b_i < b_j
void sort2(int* a, int* b, int n, int na, int* abegin);
 // sorts the triple <a,b> on "a" and fills in abegin
 // n = length of a,b
 // na = number of different elements in "a"
 // doesn't free and alloc
void sort3(int** a, int** b, int** c, int n, int na, int* abegin);

void ComputeCount(int* a, int na, int* count);
void Count2EndIndex(int* a, int n);
void EndIndex2BeginIndex(int* a, int n);
void Count2BeginIndex(int* a, int n);
void SortArray(int** a, int na, int* key, int* index, int ni);
void SortArray_copy(int* a, int na, int* key, int* index, int ni);

#endif
