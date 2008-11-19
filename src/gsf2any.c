#include "config.h"
#include <string.h>
#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#ifdef OUTPUT_DIR
#include "dirops.h"
#endif
#ifdef OUTPUT_BCG
#include "bcg_user.h"
#endif
#include "ltsman.h"

#ifdef OUTPUT_DIR
#define WHAT "dir"
#endif
#ifdef OUTPUT_BCG
#define WHAT "bcg"
#endif

static lts_t lts;
static stream_t stream[1024];
#ifdef OUTPUT_DIR
static stream_t act;
static archive_t dir=NULL;
#endif
#ifdef OUTPUT_BCG
static string_index_t act;
static int act_count=0;
static int src_id=-1;
static int lbl_id=-1;
static int dst_id=-1;
#define BUF_SIZE 65536
static uint8_t src_buf[BUF_SIZE];
static uint8_t lbl_buf[BUF_SIZE];
static uint8_t dst_buf[BUF_SIZE];
static int src_len=0;
static int lbl_len=0;
static int dst_len=0;
static int src_head=0;
static int lbl_head=0;
static int dst_head=0;
#endif

#ifdef OUTPUT_DIR
static void pkt_write_line(stream_t s,uint16_t len,char*data){
	data[len]='\n';
	DSwrite(s,data,len+1);
}
#endif

#ifdef OUTPUT_BCG
int bcg_opened=0;
char*bcg_name=NULL;
static void bcg_follow_info(void*context,uint16_t len,void*data){
	lts_info_add(lts,len,data);
	if (bcg_opened) return;
	if (lts_has_root(lts)){
		BCG_IO_WRITE_BCG_BEGIN (bcg_name,lts_get_root_ofs(lts),1,"written by gsf2dir",0);
		bcg_opened=1;
	}
}
static void pkt_write_index(string_index_t si,uint16_t len,char*data){
	data[len]=0;
	//Warning(info,"label %d is %s",act_count,data);
	SIputAt(si,data,act_count);
	act_count++;
}
#endif

int new_item(void*arg,int id,char*name){
	(void)arg;
	//printf("item %d is %s\n",id,name);
	if (id>=1024) {
		Fatal(1,error,"sorry, this version is limited to 1024 sub-streams");
	}
	if (!strcmp(name,"info")) {
#ifdef OUTPUT_DIR
		stream[id]=packet_stream(lts_info_add,lts);
#endif
#ifdef OUTPUT_BCG
		stream[id]=packet_stream(bcg_follow_info,NULL);
#endif
		return 0;
	}
	if (!strcmp(name,"actions")) {
#ifdef OUTPUT_DIR
		stream[id]=packet_stream(pkt_write_line,act);
#endif
#ifdef OUTPUT_BCG
		stream[id]=packet_stream(pkt_write_index,act);
#endif
		return 0;
	}
#ifdef OUTPUT_DIR
	stream[id]=arch_write(dir,name,NULL,0);
#endif
#ifdef OUTPUT_BCG
	stream[id]=NULL;
	if (!strcmp(name,"src-0-0")) src_id=id;
	if (!strcmp(name,"label-0-0")) lbl_id=id;
	if (!strcmp(name,"dest-0-0")) dst_id=id;
#endif
	return 0;
}

int end_item(void*arg,int id){
	(void)arg;
	//printf("end of item %d\n",id);
	if (stream[id]) stream_close(&stream[id]);
	return 0;
}

int cp_data(void*arg,int id,uint8_t*data,size_t len){
	(void)arg;
	//printf("data for %d\n",id);
#ifdef OUTPUT_BCG
	if (id==src_id) {
		if ((len+src_len)>BUF_SIZE) Fatal(1,error,"buffer overflow");
		for(int i=0;i<len;i++){
			src_buf[(src_head+src_len)%BUF_SIZE]=data[i];
			src_len++;
		}
	}
	if (id==lbl_id) {
		if ((len+lbl_len)>BUF_SIZE) Fatal(1,error,"buffer overflow");
		for(int i=0;i<len;i++){
			lbl_buf[(lbl_head+lbl_len)%BUF_SIZE]=data[i];
			lbl_len++;
		}
	}
	if (id==dst_id) {
		if ((len+dst_len)>BUF_SIZE) Fatal(1,error,"buffer overflow");
		for(int i=0;i<len;i++){
			dst_buf[(dst_head+dst_len)%BUF_SIZE]=data[i];
			dst_len++;
		}
	}
	while(src_len>3 && lbl_len>3 && dst_len>3){
		uint32_t s=PKT_U32(src_buf,src_head);
		uint32_t l=PKT_U32(lbl_buf,lbl_head);
		uint32_t d=PKT_U32(dst_buf,dst_head);
		//Warning(info,"%d --%d-> %d",s,l,d);
		src_head=(src_head+4)%BUF_SIZE;
		lbl_head=(lbl_head+4)%BUF_SIZE;
		dst_head=(dst_head+4)%BUF_SIZE;
		src_len-=4;
		lbl_len-=4;
		dst_len-=4;
		char*ls;
		if ((ls=SIget(act,l))==NULL) Fatal(1,error,"transition arrived before label");
		BCG_IO_WRITE_BCG_EDGE (s,(strcmp(ls,"tau"))?ls:"i",d);
		//Warning(info,"%d --%s-> %d",s,ls,d);
	}
#endif
	if (stream[id]) stream_write(stream[id],data,len);
	return 0;
}

int main(int argc, char *argv[]){
	struct arch_enum_callbacks cb;
	cb.new_item=new_item;
	cb.end_item=end_item;
	cb.data=cp_data;
	runtime_init_args(&argc,&argv);
	archive_t gsf;

	int blocksize=prop_get_U32("bs",4096);
	char*in=prop_get_S("in",NULL);
	if(in){
		gsf=arch_gsf_read(file_input(in));
	} else {
		gsf=arch_gsf_read(stream_input(stdin));
	}
	if (argc!=2) Fatal(1,error,"usage gsf2" WHAT " [options] output");
	lts=lts_new();
#ifdef OUTPUT_DIR
	dir=arch_dir_create(argv[1],blocksize,DELETE_ALL);
	act=arch_write(dir,"TermDB",NULL,0);
#endif
#ifdef OUTPUT_BCG
	Warning(info,"init bcg");
	BCG_INIT();
	act=lts_get_string_index(lts);
	bcg_name=argv[1];
#endif
	arch_enum_t e=arch_enum(gsf,NULL);
	if (arch_enumerate(e,&cb,NULL)){
		Warning(info,"unexpected non-zero return");
	}
	arch_enum_free(&e);
#ifdef OUTPUT_DIR
	DSclose(&act);
	stream_t ds=arch_write(dir,"info",NULL,0);
	lts_write_info(lts,ds,LTS_INFO_DIR);
	DSclose(&ds);
	arch_close(&dir);
#endif
#ifdef OUTPUT_BCG
	BCG_IO_WRITE_BCG_END ();
#endif
	arch_close(&gsf);
	return 0;
}


