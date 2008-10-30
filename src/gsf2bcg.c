#include "archive.h"
#include "stream.h"
#include "bcg_user.h"
#include "runtime.h"
#include "stringindex.h"

int main(int argc,char**argv){
	archive_t gsf=NULL;
	stream_t ds;
	runtime_init_args(&argc,&argv);
	int blocksize=prop_get_U32("bs",4096);
        char*in=prop_get_S("in",NULL);
        if(in){
                gsf=arch_gsf_read(file_input(in));
        } else {
                gsf=arch_gsf_read(stream_input(stdin));
        }
	char*bcg;
	if (!(bcg=prop_get_S("bcg",NULL))){
		Fatal(1,error,"please specify output with bcg=...");
	}
	Warning(info,"copying %s to %s",in,bcg);
	BCG_INIT();
	Warning(info,"reading header");
	ds=arch_read(gsf,"header",NULL);
	Warning(info,"got it");
	uint32_t segments=DSreadU32(ds);
	if(segments!=1) Fatal(1,error,"cannot deal with %u segments",segments);
	Warning(info,"segs ok");
	uint32_t root_seg=DSreadU32(ds);
	if(root_seg!=0) Fatal(1,error,"input corrupted");
	Warning(info,"root seg ok");
	uint32_t root_ofs=DSreadU32(ds);
	Warning(info,"root is %u",root_ofs);
	char *comment=DSreadSA(ds);
	Warning(info,"comment is %s",comment);
	DSclose(&ds);
	BCG_IO_WRITE_BCG_BEGIN (bcg,root_ofs,1,comment,0);
	string_index_t act=SIcreate();
	ds=arch_read(gsf,"actions",NULL);
	int N=0;
	Warning(info,"copying transitions");
	stream_t src=arch_read(gsf,"src-0-0",NULL);
	stream_t lbl=arch_read(gsf,"label-0-0",NULL);
	stream_t dst=arch_read(gsf,"dest-0-0",NULL);
	for(;;){
		if (DSempty(src)) break;
		int s=DSreadU32(src);
                int l=DSreadU32(lbl);
		int d=DSreadU32(dst);
		while(l>=N){
			char a[1024];
			DSreadS(ds,a,1024);
			SIputAt(act,a,N);
			N++;
		}
		char*ls=SIget(act,l);
		BCG_IO_WRITE_BCG_EDGE (s,(strcmp(ls,"tau"))?ls:"i",d);
	}
	BCG_IO_WRITE_BCG_END ();
	DSclose(&src);
	DSclose(&lbl);
	DSclose(&dst);
	DSclose(&ds);
	arch_close(&gsf);
	return 0;
}


