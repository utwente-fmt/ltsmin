#include <ctype.h>
#include "chunk_support.h"
#include "runtime.h"

#define VALID_IDX(i,c) if (i>=c.len) Fatal(1,error,"chunk overflow")

static const char HEX[16]="0123456789ABCDEF";

void chunk_encode_copy(chunk dst,chunk src,char esc){
	chunk_len k=0;
	for(chunk_len i=0;i<src.len;i++){
		if (isprint(src.data[i])) {
			if (src.data[i]==esc){
				VALID_IDX(k+1,dst);
				dst.data[k]=esc;
				dst.data[k+1]=esc;
				k+=2;
				VALID_IDX(k,dst);
			} else {
				VALID_IDX(k,dst);
				dst.data[k]=src.data[i];
				k++;
			}
		} else {
			VALID_IDX(k+2,dst);
			dst.data[k]=esc;
			dst.data[k+1]=HEX[(src.data[i]>>4)&(char)0x0F];
			dst.data[k+2]=HEX[src.data[i]&(char)0x0F];
			k+=3;
		}
	}
	if(k<dst.len) {
		dst.data[k]=0;
	}
}

static int hex_decode(char c){
	switch(c){
	case '0'...'9': return (c-'0');
	case 'a'...'f': return (10+c-'a');
	case 'A'...'F': return (10+c-'A');
	default: Fatal(1,error,"%c is not a hex digit",c); return 0;
	}
}

void chunk_decode_copy(chunk dst,chunk src,char esc){
	chunk_len i=0,j=0;
	while(i<src.len){
		if (src.data[i]==0) break;
		VALID_IDX(j,dst);
		if (src.data[i]==esc){
			if (src.data[i+1]==esc){
				dst.data[j]=esc;
				i+=2;
			} else {
				dst.data[j]=(hex_decode(src.data[i+1])<<4)+hex_decode(src.data[i+2]);
				i+=3;
			}
		} else {
			dst.data[j]=src.data[i];
			i++;
		}
		j++;
	}
	dst.len=j;
}

