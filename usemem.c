#include <malloc.h>

int main(int argc,char *argv[]){
	long long int size=atoll(argv[1]);
	int i,j;
	int* data[256];
	for(i=0;i<256;i++){
		data[i]=calloc(4,size);
	}
	for(i=0;i<256;i++){
		for(j=0;j<size;j++){
			data[i][j]++;
		}
	}
	return 0;
}

