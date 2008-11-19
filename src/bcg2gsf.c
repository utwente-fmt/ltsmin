
#include "archive.h"
#include "stream.h"
#include "bcg_user.h"
#include "runtime.h"
#include "ltsman.h"

int main(int argc,char**argv){
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
	bcg_type_state_number bcg_s1;
	BCG_TYPE_C_STRING bcg_comment;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	bcg_type_state_number bcg_s2;
	int i,N,invis;
	archive_t gsf;
	stream_t ds;

	runtime_init_args(&argc,&argv);
	if(argc!=2) Fatal(1,error,"usage bcg2gsf [options] input");
	char*bcg=argv[1];
	BCG_INIT();
	BCG_OT_READ_BCG_BEGIN (bcg, &bcg_graph, 0);
	char*out=prop_get_S("out",NULL);
	if(out){
		gsf=arch_gsf_write(file_output(out));
	} else {
		gsf=arch_gsf_write(stream_output(stdout));
	}
	lts_t lts=lts_new();
	lts_set_segments(lts,1);
	lts_set_states(lts,0,BCG_OT_NB_STATES (bcg_graph));
	lts_set_trans(lts,0,0,BCG_OT_NB_EDGES (bcg_graph));
	lts_set_root(lts,0,BCG_OT_INITIAL_STATE (bcg_graph));
	BCG_READ_COMMENT (BCG_OT_GET_FILE (bcg_graph), &bcg_comment);
	ds=arch_write(gsf,"info",NULL,0);
	lts_write_info(lts,ds,LTS_INFO_PACKET);
	DSclose(&ds);
	N=BCG_OT_NB_LABELS (bcg_graph);
	ds=arch_write(gsf,"actions",NULL,0);
	invis=0;
	for(i=0;i<N;i++){
		if (BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			DSwriteS(ds, BCG_OT_LABEL_STRING (bcg_graph,i));
		} else {
			if (invis) Fatal(1,error,"more than one invisible label");
			DSwriteS(ds,"tau");
			invis++;
		}
	}
	DSclose(&ds);
	stream_t src=arch_write(gsf,"src-0-0",NULL,0);
	stream_t lbl=arch_write(gsf,"label-0-0",NULL,0);
	stream_t dst=arch_write(gsf,"dest-0-0",NULL,0);
	BCG_OT_ITERATE_PLN (bcg_graph,bcg_s1,bcg_label_number,bcg_s2){
		DSwriteU32(src,bcg_s1);
		DSwriteU32(lbl,bcg_label_number);
		DSwriteU32(dst,bcg_s2);
	} BCG_OT_END_ITERATE;
	BCG_OT_READ_BCG_END (&bcg_graph);
	DSclose(&src);
	DSclose(&lbl);
	DSclose(&dst);
	arch_close(&gsf);
	return 0;
}

