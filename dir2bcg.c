#include "ltsmeta.h"
#include "archive.h"
#include "data_io.h"
#include "bcg_user.h"
#include "runtime.h"

int main(int argc,char**argv){
	lts_t lts;
	archive_t dir;
	data_stream_t ds;
	runtime_init();
	set_label("bcg2gsf");
	BCG_INIT();
	dir=arch_dir(argv[1],4096);
	Warning(info,"reading info");
	ds=DScreate(arch_read(dir,"info"),SWAP_NETWORK);
	lts=lts_read(ds);
	DSclose(&ds);
	BCG_IO_WRITE_BCG_BEGIN (argv[2],lts_get_root(lts),2,lts_get_comment(lts),0);
	int i,N;
	N=lts_get_labels(lts);
	Warning(info,"reading %d labels",N);
	ds=DScreate(arch_read(dir,"TermDB"),SWAP_NETWORK);
	char **label=RTmalloc(N*sizeof(char*));
	for(i=0;i<N;i++){
		//Warning(info,"reading label %d",i);
		label[i]=DSreadLN(ds);
	}
	DSclose(&ds);
	Warning(info,"copying transitions");
	N=lts_get_trans(lts);
	data_stream_t src=DScreate(arch_read(dir,"src-0-0"),SWAP_NETWORK);
	data_stream_t lbl=DScreate(arch_read(dir,"label-0-0"),SWAP_NETWORK);
	data_stream_t dst=DScreate(arch_read(dir,"dest-0-0"),SWAP_NETWORK);
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
	return 0;
}


