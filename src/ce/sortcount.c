#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "sortcount.h"

/****************************************************


SORT FUNCTIONS ...


*****************************************************/

//****************************************************************************
// some help functions

void simplesort(int*a, int n){
 int i,j,aux;
 i=1;
 for(;i>0;){
	i=0;
	for(j=0;j<n-1;j++)
	 if (a[j]>a[j+1]){
		i++;
		aux=a[j+1];
		a[j+1]=a[j];
		a[j]=aux;
	 }
 }
}


void quicksort(int* a, int first, int last){

  int aux, pivot;
  int up, down;

  if (first >= last)
    return;
  pivot = a[first];
  up = first;
  down = last;
  while (up < down) {
    while ((up < down) && (a[down] > pivot)) down--;
    while ((up < down) && (a[up] <= pivot)) up++;
    if (up < down){
      aux = a[up];
      a[up] = a[down];
      a[down] = aux;
    }
  }
  aux = a[first];
  a[first] = a[down];
  a[down] = aux;

  quicksort(a, first, down-1);
  quicksort(a, down+1, last);
}


#define GT(a,b,a1,b1) (((a)>(a1)) || (((a)==(a1))&&((b)>(b1))))
#define LTE(a,b,a1,b1) (!GT(a,b,a1,b1))

void sortpiece(int* a, int* b, int first, int last){
 // sorteaza un tablou "twins"
 // (a_i, b_i) < (a_j, b_j) iff a_i < a_j sau a_i=a_j, b_i < b_j

  int aux, pivota, pivotb;
  int up, down;

  if (first >= last)
    return;
  pivota = a[first];
	pivotb = b[first];
  up = first;
  down = last;  
  while (up < down) {
    while ((up < down) && (GT(a[down], b[down], pivota, pivotb))) down--;
    while ((up < down) && (LTE(a[up], b[up], pivota, pivotb))) up++;
    if (up < down){
      aux = a[up];
      a[up] = a[down];
      a[down] = aux;
      aux = b[up];
      b[up] = b[down];
      b[down] = aux;
    }
  } 
  aux = a[first];
  a[first] = a[down];
  a[down] = aux;
	aux = b[first];
  b[first] = b[down];
  b[down] = aux;
 
  sortpiece(a, b, first, down-1);
  sortpiece(a, b, down+1, last);
}



void bucketsort(int* a, int n){
 int i,j, max;
 int* aux;
 max = 0; 
 for(i=0;i<n;i++) 
	if (a[i]>max) max = a[i]; 
 max++;
 if ((n>=max)||(n>=100000)){
	aux=(int*)calloc(max+1, sizeof(int));
	//	if (n>1000)
	//	 Warning(1,"********   max = %d ",max);
	ComputeCount(a,n,aux);
	//	if (n>1000)
	//	 Warning(1,"........   count computed");
	Count2BeginIndex(aux, max);
	//	if (n>1000)
	//	 Warning(1,"........   index computed");
	for(i=0;i<max;i++)
	 for(j=aux[i];j<aux[i+1];j++)
		a[j]=i;
	// if (n>1000)
	//Warning(1,"bucket-ed!! n %d max %d ",n,max);
	free(aux);
 }
 else {
	//Warning(1,"quick!! n %d max %d",n,max);
	quicksort(a,0,n-1);
	//simplesort(a,n);
 }
}

void sort2(int* a, int* b, int n, int na, int* abegin){
 // sorts the triple <a,b> on "a" and fills in abegin
 // n = length of a,b
 // na = number of different elements in "a"
 // doesn't free and alloc
 int i,j;
 int *aaux, *baux;
 for(i=0;i<n;i++)
	abegin[a[i]]++;
 for(i=1;i<=na;i++)
	abegin[i]+=abegin[i-1];
 aaux = (int*)calloc(n, sizeof(int)); 
 baux = (int*)calloc(n, sizeof(int));
 for(i=0;i<n;i++) {aaux[i] = a[i]; baux[i] = b[i];};
 for(i=0;i<n;i++){
	j = --abegin[a[i]];
	a[j] = aaux[i];
	b[j] = baux[i];
 }
 free(aaux); free(baux);
}

/*
void sort3(int* a, int* b, int* c, int n, int na, int* abegin){
 // sorts the triple <a,b,c> on "a" and fills in abegin
 // n = length of a,b,c
 // na = number of different elements in "a"
 // doesn't free and alloc

 int i,j;
 int *baux, *caux;
 for(i=0;i<n;i++){
	//	assert(a[i] < na);
	abegin[a[i]]++;
 }
 for(i=1;i<=na;i++)
	abegin[i]+=abegin[i-1];
 baux = (int*)calloc(n, sizeof(int)); 
 caux = (int*)calloc(n, sizeof(int));
 for(i=0;i<n;i++) {baux[i] = b[i]; caux[i] = c[i];};
 for(i=0;i<n;i++){
	j = --abegin[a[i]];
	b[j] = baux[i];
	c[j] = caux[i];
 }
 free(baux); free(caux);
 }
*/


void sort3(int** a, int** b, int** c, int n, int na, int* abegin){
 int i,j;
 int *baux, *caux;

 for(i=0;i<n;i++)
	abegin[(*a)[i]]++;

 for(i=1;i<=na;i++)
	abegin[i]+=abegin[i-1];
 baux = (int*)calloc(n, sizeof(int)); 
 caux = (int*)calloc(n, sizeof(int));
 for(i=0;i<n;i++){
	j = --abegin[(*a)[i]];
	baux[j] = (*b)[i];
	caux[j] = (*c)[i];
 }
 *b = baux;
 *c = caux;
 }


void ComputeCount(int* a, int na, int* count){
 int i;
 for(i=0;i<na;i++) count[a[i]]++;
}
void Count2EndIndex(int* a, int n){
 int i;
 for(i=1;i<=n;i++) a[i]+=a[i-1];
}

void EndIndex2BeginIndex(int* a, int n){
 int i;
 for(i=n;i>0;i--) a[i]=a[i-1];
 a[0]=0;
}
void Count2BeginIndex(int* a, int n){
 Count2EndIndex(a,n);
 EndIndex2BeginIndex(a,n);
}
void SortArray(int** a, int na, int* key, int* index, int ni){
 int i;
 int* newa;

 // for assertions
 int* aux;
 aux = (int*)calloc(ni+1, sizeof(int));
 for(i=0;i<=ni;i++) aux[i] = index[i]; 
 assert(index[ni] == na);
 // end for assertions

 newa = (int*)calloc(na, sizeof(int));
 
 for(i = 0; i < na; i++)
	newa[index[key[i]]++] = (*a)[i];
 
 if (*a != NULL) free(*a);

 *a = newa;
 EndIndex2BeginIndex(index,ni);

 // assert
 for(i=0;i<=ni;i++) assert(aux[i]==index[i]);
 // end assert
}


void SortArray_copy(int* a, int na, int* key, int* index, int ni){
 // copies the sorted array back to the original location
 int i;
 int* newa;
 // if (na==2)
 //	for(i=0;i<na;i++)
 //	 printf("--%d  ",key[i]);
 // for assertions
 int* aux;
 aux = (int*)calloc(ni+1, sizeof(int));
 for(i=0;i<=ni;i++) aux[i] = index[i]; 
 assert(index[ni] == na);
 // end for assertions
 newa = (int*)calloc(na, sizeof(int));
 for(i = 0; i < na; i++)
	newa[i] = a[i];
 for(i = 0; i < na; i++)
	a[index[key[i]]++] = newa[i];
 free(newa); 

 // if (na==2)
	//	for(i=0;i<=ni;i++)
 //	 printf("*%d*%d  ",aux[i],index[i]);
 EndIndex2BeginIndex(index,ni);
 // if (na==2)
 //	for(i=0;i<=ni;i++)
 //	 printf("**%d**%d  ",aux[i],index[i]);

 // assert
 for(i=0;i<=ni;i++) assert(aux[i]==index[i]);
 // end assert
}

