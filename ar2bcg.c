#include "ltsmeta.h"
#include "archive.h"
#include "stream.h"
#include "bcg_user.h"
#include "runtime.h"

int main(int argc,char**argv){
	lts_t lts;
	archive_t dir=NULL;
	stream_t ds;
	runtime_init_args(&argc,&argv);
	char *ar;
	int blocksize=prop_get_U32("bs",4096);
	if ((ar=prop_get_S("gcf",NULL))){
		raf_t raf=raf_unistd(ar);
		dir=arch_gcf_read(raf);
	}
	if (dir==NULL && (ar=prop_get_S("dir",NULL))){
		dir=arch_dir(ar,blocksize);
	}
	if (dir==NULL && (ar=prop_get_S("fmt",NULL))){
		dir=arch_fmt(ar,file_input,file_output,blocksize);
	}
	if (dir==NULL) {
		Fatal(1,error,"please specify input with gcf=... , dir=... or fmt=...");
	}
	char*bcg;
	if (!(bcg=prop_get_S("bcg",NULL))){
		Fatal(1,error,"please specify output with bcg=...");
	}
	Warning(info,"copying %s to %s",ar,bcg);
	BCG_INIT();
	Warning(info,"reading info");
	ds=arch_read(dir,"info",NULL);
	int decode;
	lts=lts_read(ds,&decode);
	DSclose(&ds);
	BCG_IO_WRITE_BCG_BEGIN (bcg,lts_get_root(lts),1,lts_get_comment(lts),0);
	int i,N;
	N=lts_get_labels(lts);
	Warning(info,"reading %d labels",N);
	ds=arch_read(dir,"TermDB",decode?"auto":NULL);
	char **label=RTmalloc(N*sizeof(char*));
	for(i=0;i<N;i++){
		//Warning(info,"reading label %d",i);
		label[i]=DSreadLN(ds);
	}
	DSclose(&ds);
	Warning(info,"copying transitions");
	N=lts_get_trans(lts);
	stream_t src=arch_read(dir,"src-0-0",decode?"auto":NULL);
	stream_t lbl=arch_read(dir,"label-0-0",decode?"auto":NULL);
	stream_t dst=arch_read(dir,"dest-0-0",decode?"auto":NULL);
	for(i=0;i<N;i++){
		int s=DSreadU32(src);
                int l=DSreadU32(lbl);
		int d=DSreadU32(dst);
		BCG_IO_WRITE_BCG_EDGE (s,(strcmp(label[l],"tau"))?(label[l]):"i",d);
	}
	DSclose(&src);
	DSclose(&lbl);
	DSclose(&dst);
	BCG_IO_WRITE_BCG_END ();
	arch_close(&dir);
	return 0;
}


