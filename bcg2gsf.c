#include "ltsmeta.h"
#include "archive.h"
#include "data_io.h"
#include "bcg_user.h"
#include "runtime.h"

int main(int argc,char**argv){
	lts_t lts;
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
	bcg_type_state_number bcg_s1;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	bcg_type_state_number bcg_s2;
	int i,N,invis;
	archive_t gsf;
	data_stream_t ds;

	runtime_init();
	set_label("bcg2gsf");
	BCG_INIT();

	BCG_OT_READ_BCG_BEGIN (argv[1], &bcg_graph, 0);
	Warning(info," %d states %d transitions",BCG_OT_NB_STATES (bcg_graph),BCG_OT_NB_EDGES (bcg_graph));


	lts=lts_create(1);
	lts_set_states(lts,BCG_OT_NB_STATES (bcg_graph));
	lts_set_trans(lts,BCG_OT_NB_EDGES (bcg_graph));
	lts_set_root(lts,BCG_OT_INITIAL_STATE (bcg_graph));
	if(argc>2){
		gsf=arch_gsf_write(stream_output(fopen(argv[2],"w")));
	} else {
		gsf=arch_gsf_write(stream_output(stdout));
	}
	N=BCG_OT_NB_LABELS (bcg_graph);
	lts_set_labels(lts,N);
	invis=0;
	for(i=0;i<N;i++){
		if (!BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			invis++;
			lts_set_tau(lts,i);
		}
	}
	Warning(info,"found %d invisible label(s)",invis);
	ds=DScreate(arch_write(gsf,"info"),SWAP_NETWORK);
	lts_write_info(lts,ds,DIR_INFO);
	DSclose(&ds);
	ds=DScreate(arch_write(gsf,"TermDB"),SWAP_NETWORK);
	for(i=0;i<N;i++){
		if (BCG_OT_LABEL_VISIBLE (bcg_graph,i)){
			char *ln=BCG_OT_LABEL_STRING (bcg_graph,i);
			DSwrite(ds,ln,strlen(ln));
			DSwrite(ds,"\n",1);
		} else {
			DSwrite(ds,"tau\n",4);
		}
	}
	DSclose(&ds);
	data_stream_t src=DScreate(arch_write(gsf,"src-0-0"),SWAP_NETWORK);
	data_stream_t lbl=DScreate(arch_write(gsf,"label-0-0"),SWAP_NETWORK);
	data_stream_t dst=DScreate(arch_write(gsf,"dest-0-0"),SWAP_NETWORK);
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

