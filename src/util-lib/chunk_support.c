#include <hre/config.h>

#include <ctype.h>

#include <hre/user.h>
#include <util-lib/chunk_support.h>

#define VALID_IDX(i,c) if (i>=c.len) Abort("chunk overflow")

static const char HEX[16]="0123456789ABCDEF";

void chunk_encode_copy(chunk dst,chunk src,char esc){
	chunk_len k=0;
	for(chunk_len i=0;i<src.len;i++){
		if (isprint((unsigned char)src.data[i])) {
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
	default: Abort("%c is not a hex digit",c); return 0;
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

void
chunk2string (chunk src, size_t dst_size, char *dst) {
    Warning (debug, "encoding chunk of length %d", src.len);

    HREassert (dst_size >= sizeof "##...", "Chunk too small (sizeof '##...')");

    size_t k = 0;
    for (size_t i = 0; i < src.len; ++i) {
        if (!isprint((unsigned char)src.data[i]))
            goto hex;
    }

 /* quotable: */
    size_t dang_esc = 0;
    dst[k++] = '"';
    for (size_t i = 0; i < src.len && k+1 < dst_size; ++i) {
        switch (src.data[i]) {
        case '"': case '\\':
            dst[k++] = '\\';
            if (k == dst_size - sizeof "#...") dang_esc = 1;
            break;
        }
        dst[k++] = src.data[i];
    }
    if (k+2 < dst_size) {
        dst[k++] = '"';
    } else {
        Warning(info, "chunk overflow: truncating to %zu characters", dst_size-1);
        k = dst_size - sizeof "\"...";
        if (dang_esc) --k;          /* dangling escape: "\\"... */
        dst[k++] = '"'; dst[k++] = '.'; dst[k++] = '.'; dst[k++] = '.';
    }
    dst[k++] = '\0';
    return;
hex:
    dst[k++] = '#';
    for (size_t i = 0; i < src.len && k+1 < dst_size; ++i) {
        dst[k++] = HEX[(src.data[i]>>4)&(char)0x0F];
        dst[k++] = HEX[src.data[i]&(char)0x0F];
    }
    if (k+2 < dst_size) {
        dst[k++] = '#';
    } else {
        Warning(info, "chunk overflow: truncating to %zu characters", dst_size-1);
        k = dst_size - sizeof "#...";
        if (k % 2 == 0) --k;       /* dangling hex digit: #1#... */
        dst[k++] = '#'; dst[k++] = '.'; dst[k++] = '.'; dst[k++] = '.';
    }
    dst[k++] = '\0';
}

void string2chunk(char*src,chunk *dst){
	uint32_t len=strlen(src);
	Warning(debug,"decoding \"%s\" (%d)",src,len);
	if (src[0]=='#' && src[len-1]=='#') {
		Warning(debug,"hex");
		len=len/2 - 1;
		if (dst->len<len) Abort("chunk overflow");
		dst->len=len;
		for(uint32_t i=0;i<len;i++){
			dst->data[i]=(hex_decode(src[2*i+1])<<4)+hex_decode(src[2*i+2]);
		}
	} else if (src[0]=='"' && src[len-1]=='"') {
		Warning(debug,"quoted");
		len=len-2;
		if (dst->len<len) Abort("chunk overflow");
		dst->len=0;
#define PUT_CHAR(c) { dst->data[dst->len]=c; dst->len++ ; }
		for(uint32_t i=0;i<len;i++) {
			if (!isprint((unsigned char)src[i+1]))
			    Abort("non-printable character in source");
			if (src[i+1]=='"') Abort("unquoted \" in string");
			if (src[i+1]=='\\') {
				if (i+1==len) Abort("bad escape sequence");
				switch(src[i+2]){
				case '\\': PUT_CHAR('\\'); i++; continue;
				case '"': PUT_CHAR('"'); i++; continue;
				default: Abort("unknown escape sequence");
				}
			}
			PUT_CHAR(src[i+1]);
		}
#undef PUT_CHAR
	} else {
		Warning(debug,"verbatim");
		if (dst->len<len) Abort("chunk overflow");
		dst->len=len;
		for(uint32_t i=0;i<len;i++) {
			if (!isprint((unsigned char)src[i]))
			    Abort("non-printable character in source");
			if (isspace((unsigned char)src[i]))
			    Abort("white space in source");
			dst->data[i]=src[i];
		}
	}
	Warning(debug,"return length is %d",dst->len);
}



