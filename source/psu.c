#include "psu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#else
#include <direct.h>
#endif

#define PSU_EINVAL -1
#define PSU_EIO     -2
#define PSU_ENOMEM  -3
#define PSU_EFORMAT -4
#define PSU_ENOENT  -5
#define PSU_EEXIST  -6

static uint16_t rd16le(const uint8_t *p){ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32le(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t rd64le(const uint8_t *p){ uint64_t v=0; for(int i=7;i>=0;--i) v=(v<<8)|p[i]; return v; }
static void wr16le(uint8_t *p,uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wr32le(uint8_t *p,uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void wr64le(uint8_t *p,uint64_t v){ for(int i=0;i<8;i++){ p[i]=(uint8_t)(v&0xff); v>>=8; } }

static int is_file_attr(uint16_t a){ return a == PSU_ATTR_FILE || ((a & 0x8000u) && (a & 0x0010u)); }
static int is_dir_attr(uint16_t a){ return a == PSU_ATTR_DIR || ((a & 0x8000u) && (a & 0x0020u)); }
static uint32_t pad_for(uint32_t n){ uint32_t r=n & (PSU_DATA_ALIGN-1); return r ? PSU_DATA_ALIGN-r : 0; }

static void unpack_time(psu_time_t *t,const uint8_t *p){
    t->unused=p[0]; t->sec=p[1]; t->min=p[2]; t->hour=p[3]; t->day=p[4]; t->month=p[5]; t->year=rd16le(p+6);
}
static void pack_time(uint8_t *p,const psu_time_t *t){
    p[0]=t->unused; p[1]=t->sec; p[2]=t->min; p[3]=t->hour; p[4]=t->day; p[5]=t->month; wr16le(p+6,t->year);
}
static void time_from_time_t(psu_time_t *out,time_t when){
    struct tm *tmv=localtime(&when); memset(out,0,sizeof(*out)); if(!tmv) return;
    out->sec=(uint8_t)tmv->tm_sec; out->min=(uint8_t)tmv->tm_min; out->hour=(uint8_t)tmv->tm_hour;
    out->day=(uint8_t)tmv->tm_mday; out->month=(uint8_t)(tmv->tm_mon+1); out->year=(uint16_t)(tmv->tm_year+1900);
}

static void unpack_header(psu_entry_t *e,const uint8_t h[PSU_HEADER_SIZE]){
    memset(e,0,sizeof(*e)); e->attr=rd16le(h+0x00); e->size=rd32le(h+0x04);
    unpack_time(&e->ctime,h+0x08); unpack_time(&e->mtime,h+0x18); e->unknown2=rd64le(h+0x20);
    memcpy(e->name,h+0x40,PSU_NAME_MAX); e->name[PSU_NAME_MAX]=0;
}
static void pack_header(uint8_t h[PSU_HEADER_SIZE],const psu_entry_t *e){
    memset(h,0,PSU_HEADER_SIZE); wr16le(h+0x00,e->attr); wr32le(h+0x04,e->size);
    pack_time(h+0x08,&e->ctime); pack_time(h+0x18,&e->mtime); wr64le(h+0x20,e->unknown2);
    size_t n=strlen(e->name); if(n>PSU_NAME_MAX) n=PSU_NAME_MAX; memcpy(h+0x40,e->name,n);
}
static int read_header(FILE *f,psu_entry_t *e){ uint8_t h[PSU_HEADER_SIZE]; if(fread(h,1,sizeof(h),f)!=sizeof(h)) return PSU_EIO; unpack_header(e,h); return 0; }
static int write_header(FILE *f,const psu_entry_t *e){ uint8_t h[PSU_HEADER_SIZE]; pack_header(h,e); return fwrite(h,1,sizeof(h),f)==sizeof(h)?0:PSU_EIO; }
static int skip_data(FILE *f,uint32_t size){ long long n=(long long)size+pad_for(size); return fseek(f,(long)n,SEEK_CUR)==0?0:PSU_EIO; }

const char *psu_strerror(int c){
    switch(c){case 0:return "ok";case PSU_EINVAL:return "invalid argument";case PSU_EIO:return "I/O error";case PSU_ENOMEM:return "out of memory";case PSU_EFORMAT:return "invalid/unsupported PSU";case PSU_ENOENT:return "entry not found";case PSU_EEXIST:return "entry already exists";default:return "unknown PSU error";}
}

int psu_get_root(const char *path,psu_entry_t *root_out){
    if(!path||!root_out)return PSU_EINVAL;FILE *f=fopen(path,"rb");if(!f)return PSU_EIO;
    int rc=read_header(f,root_out);fclose(f);if(rc)return rc;if(!is_dir_attr(root_out->attr))return PSU_EFORMAT;return 0;
}

int psu_list(const char *path,psu_list_cb cb,void *opaque){
    if(!path) return PSU_EINVAL; FILE *f=fopen(path,"rb"); if(!f) return PSU_EIO;
    psu_entry_t root; int rc=read_header(f,&root); if(rc){fclose(f);return rc;}
    if(!is_dir_attr(root.attr) || root.size<2 || root.size>10000){ fclose(f); return PSU_EFORMAT; }
    for(uint32_t i=0;i<root.size;i++){
        psu_entry_t e; if(read_header(f,&e)){fclose(f);return PSU_EFORMAT;}
        if(cb && cb(&e,i,opaque)){ fclose(f); return 0; }
        if(is_file_attr(e.attr) && e.size){ rc=skip_data(f,e.size); if(rc){fclose(f);return rc;} }
    }
    fclose(f); return 0;
}

static int safe_name(const char *s){
    if(!s || !*s || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for(;*s;s++) if(*s=='/'||*s=='\\'||*s==':'||((unsigned char)*s<32)) return 0;
    return 1;
}
static int mkdir_one(const char *p){
#ifdef _WIN32
    if(_mkdir(p)==0 || errno==EEXIST) return 0;
#else
    if(mkdir(p,0777)==0 || errno==EEXIST) return 0;
#endif
    return PSU_EIO;
}
static int copy_n(FILE *in,FILE *out,uint32_t n){
    uint8_t b[64*1024]; while(n){ size_t chunk=n<sizeof(b)?n:sizeof(b); if(fread(b,1,chunk,in)!=chunk) return PSU_EIO; if(fwrite(b,1,chunk,out)!=chunk) return PSU_EIO; n-=(uint32_t)chunk; } return 0;
}

int psu_extract_all(const char *path,const char *dest){
    if(!path||!dest) return PSU_EINVAL; if(mkdir_one(dest)) return PSU_EIO;
    FILE *f=fopen(path,"rb"); if(!f) return PSU_EIO; psu_entry_t root; if(read_header(f,&root)||!is_dir_attr(root.attr)){fclose(f);return PSU_EFORMAT;}
    for(uint32_t i=0;i<root.size;i++){
        psu_entry_t e; if(read_header(f,&e)){fclose(f);return PSU_EFORMAT;}
        if(is_file_attr(e.attr) && safe_name(e.name)){
            char outp[1024]; snprintf(outp,sizeof(outp),"%s/%s",dest,e.name); FILE *o=fopen(outp,"wb"); if(!o){fclose(f);return PSU_EIO;}
            int rc=copy_n(f,o,e.size); fclose(o); if(rc){fclose(f);return rc;} if(fseek(f,(long)pad_for(e.size),SEEK_CUR)){fclose(f);return PSU_EIO;}
        } else if(is_file_attr(e.attr)) { if(skip_data(f,e.size)){fclose(f);return PSU_EIO;} }
    }
    fclose(f); return 0;
}

int psu_extract_one(const char *path,const char *name,const char *dest){
    if(!path||!name||!dest) return PSU_EINVAL; FILE *f=fopen(path,"rb"); if(!f) return PSU_EIO; psu_entry_t root; if(read_header(f,&root)||!is_dir_attr(root.attr)){fclose(f);return PSU_EFORMAT;}
    for(uint32_t i=0;i<root.size;i++){
        psu_entry_t e; if(read_header(f,&e)){fclose(f);return PSU_EFORMAT;}
        if(is_file_attr(e.attr) && !strncmp(e.name,name,PSU_NAME_MAX)){
            FILE *o=fopen(dest,"wb"); if(!o){fclose(f);return PSU_EIO;} int rc=copy_n(f,o,e.size); fclose(o); fclose(f); return rc;
        }
        if(is_file_attr(e.attr) && skip_data(f,e.size)){fclose(f);return PSU_EIO;}
    }
    fclose(f); return PSU_ENOENT;
}

int psu_read_file(const char *path,const char *name,void **data_out,size_t *size_out){
    if(data_out) *data_out=NULL; if(size_out) *size_out=0;
    if(!path||!name||!data_out||!size_out) return PSU_EINVAL;
    FILE *f=fopen(path,"rb"); if(!f) return PSU_EIO; psu_entry_t root; if(read_header(f,&root)||!is_dir_attr(root.attr)){fclose(f);return PSU_EFORMAT;}
    for(uint32_t i=0;i<root.size;i++){
        psu_entry_t e; if(read_header(f,&e)){fclose(f);return PSU_EFORMAT;}
        if(is_file_attr(e.attr) && !strcasecmp(e.name,name)){
            uint8_t *buf=(uint8_t*)malloc(e.size?e.size:1); if(!buf){fclose(f);return PSU_ENOMEM;}
            if(e.size && fread(buf,1,e.size,f)!=e.size){free(buf);fclose(f);return PSU_EIO;}
            *data_out=buf; *size_out=e.size; fclose(f); return 0;
        }
        if(is_file_attr(e.attr) && skip_data(f,e.size)){fclose(f);return PSU_EIO;}
    }
    fclose(f); return PSU_ENOENT;
}


typedef struct { char name[PSU_NAME_MAX+1]; char path[1024]; uint32_t size; psu_time_t ct,mt; } host_file_t;
static int cmp_hf(const void *a,const void *b){return strcmp(((const host_file_t*)a)->name,((const host_file_t*)b)->name);}

static int scan_dir(const char *dir,host_file_t **out,size_t *count){
#ifndef _WIN32
    DIR *d=opendir(dir); if(!d) return PSU_EIO; size_t cap=16,n=0; host_file_t *v=(host_file_t*)calloc(cap,sizeof(*v)); if(!v){closedir(d);return PSU_ENOMEM;}
    struct dirent *de; while((de=readdir(d))){ if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue; if(strlen(de->d_name)>PSU_NAME_MAX) continue;
        char p[1024]; snprintf(p,sizeof(p),"%s/%s",dir,de->d_name); struct stat st; if(stat(p,&st)||!S_ISREG(st.st_mode))continue;
        if(n==cap){cap*=2; host_file_t *nv=(host_file_t*)realloc(v,cap*sizeof(*v)); if(!nv){free(v);closedir(d);return PSU_ENOMEM;}v=nv;}
        strncpy(v[n].name,de->d_name,PSU_NAME_MAX); strncpy(v[n].path,p,sizeof(v[n].path)-1); v[n].size=(uint32_t)st.st_size; time_from_time_t(&v[n].ct,st.st_ctime); time_from_time_t(&v[n].mt,st.st_mtime); n++; }
    closedir(d); qsort(v,n,sizeof(*v),cmp_hf); *out=v;*count=n;return 0;
#else
    (void)dir;(void)out;(void)count; return PSU_EINVAL;
#endif
}

static int write_zeros(FILE *f,uint32_t n){ uint8_t z[1024]={0}; while(n){uint32_t c=n>sizeof(z)?sizeof(z):n;if(fwrite(z,1,c,f)!=c)return PSU_EIO;n-=c;}return 0; }
static int copy_file_to_psu(FILE *out,const host_file_t *hf){
    psu_entry_t e; memset(&e,0,sizeof(e));e.attr=PSU_ATTR_FILE;e.size=hf->size;e.ctime=hf->ct;e.mtime=hf->mt;strncpy(e.name,hf->name,PSU_NAME_MAX);
    int rc=write_header(out,&e); if(rc)return rc; FILE *in=fopen(hf->path,"rb"); if(!in)return PSU_EIO; rc=copy_n(in,out,e.size); fclose(in); if(rc)return rc; return write_zeros(out,pad_for(e.size));
}

int psu_create_from_dir(const char *src,const char *outp,const char *save_name){
    if(!src||!outp) return PSU_EINVAL; host_file_t *files=NULL; size_t n=0; int rc=scan_dir(src,&files,&n); if(rc)return rc;
    const char *nm=(save_name&&*save_name)?save_name:"SAVE";
    FILE *o=fopen(outp,"wb"); if(!o){free(files);return PSU_EIO;} time_t now=time(NULL); psu_entry_t e; memset(&e,0,sizeof(e));
    e.attr=PSU_ATTR_DIR;e.size=(uint32_t)(n+2);time_from_time_t(&e.ctime,now);e.mtime=e.ctime;strncpy(e.name,nm,PSU_NAME_MAX);e.name[PSU_NAME_MAX]=0; if((rc=write_header(o,&e)))goto done;
    e.attr=PSU_ATTR_DIR;e.size=0;strcpy(e.name,"."); if((rc=write_header(o,&e)))goto done; strcpy(e.name,".."); if((rc=write_header(o,&e)))goto done;
    for(size_t i=0;i<n;i++) if((rc=copy_file_to_psu(o,&files[i])))goto done;
done: fclose(o);free(files); if(rc) remove(outp);return rc;
}

int psu_create_blank(const char *outp,const char *save_name){
    if(!outp) return PSU_EINVAL;
    const char *nm=(save_name&&*save_name)?save_name:"BLANK";
    if(strlen(nm)>PSU_NAME_MAX) return PSU_EINVAL;
    FILE *o=fopen(outp,"wb"); if(!o) return PSU_EIO;
    time_t now=time(NULL); psu_entry_t e; int rc=0; memset(&e,0,sizeof(e));
    e.attr=PSU_ATTR_DIR; e.size=2; time_from_time_t(&e.ctime,now); e.mtime=e.ctime; strncpy(e.name,nm,PSU_NAME_MAX);
    if((rc=write_header(o,&e))) goto done_blank;
    memset(&e,0,sizeof(e)); e.attr=PSU_ATTR_DIR; strcpy(e.name,".");
    if((rc=write_header(o,&e))) goto done_blank;
    strcpy(e.name,".."); rc=write_header(o,&e);
done_blank:
    if(fclose(o)!=0 && !rc) rc=PSU_EIO;
    if(rc) remove(outp);
    return rc;
}


/* -------------------------------------------------------------------------
   uLE-style blank save template support.

   ICON.SYS is the fixed 964-byte PS2D descriptor.  The generated ICON.ICN is
   deliberately simple and static: a shallow textured plaque whose 128x128
   BGR555 texture contains the title supplied by the user.  The entire PSU is
   written to a temporary file and renamed into place only after every entry
   has been generated successfully, so a failed template build cannot leave a
   half-written .psu behind. */
static void wrf32le(uint8_t *p,float f){ uint32_t v=0; memcpy(&v,&f,sizeof(v)); wr32le(p,v); }
static uint16_t rgb_to_bgr555(unsigned r,unsigned g,unsigned b){
    return (uint16_t)(((r>>3)&31u)|(((g>>3)&31u)<<5)|(((b>>3)&31u)<<10));
}
static const uint8_t *font5x7(char c){
    /* 5-bit rows, MSB on the left.  Input from the on-screen keyboard is
       uppercase ASCII, with a small punctuation set. */
    static const uint8_t blank[7]={0,0,0,0,0,0,0};
    static const uint8_t A[7]={14,17,17,31,17,17,17},B[7]={30,17,17,30,17,17,30},C[7]={14,17,16,16,16,17,14},D[7]={30,17,17,17,17,17,30};
    static const uint8_t E[7]={31,16,16,30,16,16,31},F[7]={31,16,16,30,16,16,16},G[7]={14,17,16,23,17,17,15},H[7]={17,17,17,31,17,17,17};
    static const uint8_t I[7]={14,4,4,4,4,4,14},J[7]={7,2,2,2,18,18,12},K[7]={17,18,20,24,20,18,17},L[7]={16,16,16,16,16,16,31};
    static const uint8_t M[7]={17,27,21,21,17,17,17},N[7]={17,25,21,19,17,17,17},O[7]={14,17,17,17,17,17,14},P[7]={30,17,17,30,16,16,16};
    static const uint8_t Q[7]={14,17,17,17,21,18,13},R[7]={30,17,17,30,20,18,17},S[7]={15,16,16,14,1,1,30},T[7]={31,4,4,4,4,4,4};
    static const uint8_t U[7]={17,17,17,17,17,17,14},V[7]={17,17,17,17,17,10,4},W[7]={17,17,17,21,21,21,10},X[7]={17,17,10,4,10,17,17};
    static const uint8_t Y[7]={17,17,10,4,4,4,4},Z[7]={31,1,2,4,8,16,31};
    static const uint8_t D0[7]={14,17,19,21,25,17,14},D1[7]={4,12,4,4,4,4,14},D2[7]={14,17,1,2,4,8,31},D3[7]={30,1,1,14,1,1,30};
    static const uint8_t D4[7]={2,6,10,18,31,2,2},D5[7]={31,16,16,30,1,1,30},D6[7]={14,16,16,30,17,17,14},D7[7]={31,1,2,4,8,8,8};
    static const uint8_t D8[7]={14,17,17,14,17,17,14},D9[7]={14,17,17,15,1,1,14};
    static const uint8_t DOT[7]={0,0,0,0,0,6,6},DASH[7]={0,0,0,31,0,0,0},UNDER[7]={0,0,0,0,0,0,31};
    static const uint8_t LP[7]={2,4,8,8,8,4,2},RP[7]={8,4,2,2,2,4,8},LB[7]={14,8,8,8,8,8,14},RB[7]={14,2,2,2,2,2,14};
    static const uint8_t *letters[26]={A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z};
    static const uint8_t *digits[10]={D0,D1,D2,D3,D4,D5,D6,D7,D8,D9};
    if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
    if(c>='A'&&c<='Z')return letters[c-'A']; if(c>='0'&&c<='9')return digits[c-'0'];
    switch(c){case '.':return DOT;case '-':return DASH;case '_':return UNDER;case '(':return LP;case ')':return RP;case '[':return LB;case ']':return RB;default:return blank;}
}
static void tex_put(uint16_t *tex,int x,int y,uint16_t c){ if((unsigned)x<128u&&(unsigned)y<128u)tex[y*128+x]=c; }
static void draw_title_texture(uint16_t *tex,const char *title){
    uint16_t c0=rgb_to_bgr555(12,30,70),c1=rgb_to_bgr555(28,74,138),grid=rgb_to_bgr555(38,92,158),fg=rgb_to_bgr555(245,248,255),shadow=rgb_to_bgr555(0,8,28);
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        unsigned t=(unsigned)y; unsigned r=(12u*(127u-t)+28u*t)/127u,g=(30u*(127u-t)+74u*t)/127u,b=(70u*(127u-t)+138u*t)/127u;
        tex[y*128+x]=rgb_to_bgr555(r,g,b);
    }
    (void)c0;(void)c1;
    for(int x=4;x<124;x++){tex_put(tex,x,4,grid);tex_put(tex,x,123,grid);} for(int y=4;y<124;y++){tex_put(tex,4,y,grid);tex_put(tex,123,y,grid);}
    char clean[33]; size_t n=0; for(const char *p=title;*p&&n<32;p++)clean[n++]=*p; clean[n]=0;
    const int max_per_line=10,scale=2,cw=12,ch=16; int lines=(int)((n+max_per_line-1)/max_per_line); if(lines<1)lines=1;if(lines>4)lines=4;
    int y0=64-(lines*ch)/2;
    for(int li=0;li<lines;li++){
        int start=li*max_per_line,len=(int)n-start;if(len>max_per_line)len=max_per_line;if(len<0)len=0;
        int x0=64-(len*cw)/2;
        for(int ci=0;ci<len;ci++){
            const uint8_t *rows=font5x7(clean[start+ci]); int bx=x0+ci*cw,by=y0+li*ch;
            for(int ry=0;ry<7;ry++)for(int rx=0;rx<5;rx++)if(rows[ry]&(1u<<(4-rx))){
                for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++){tex_put(tex,bx+rx*scale+sx+1,by+ry*scale+sy+1,shadow);tex_put(tex,bx+rx*scale+sx,by+ry*scale+sy,fg);}
            }
        }
    }
}
static size_t sjis_fullwidth_ascii(uint8_t *out,size_t cap,const char *text){
    size_t n=0;if(!out||!cap||!text)return 0;
    for(const unsigned char *p=(const unsigned char*)text;*p;p++){
        uint16_t sj=0;
        if(*p>='0'&&*p<='9')sj=(uint16_t)(0x824Fu+(*p-'0'));
        else if(*p>='A'&&*p<='Z')sj=(uint16_t)(0x8260u+(*p-'A'));
        else if(*p>='a'&&*p<='z')sj=(uint16_t)(0x8281u+(*p-'a'));
        else if(*p==' ')sj=0x8140u;
        else if(*p=='-')sj=0x817Cu;
        else if(*p=='.')sj=0x8144u;
        else if(*p=='_')sj=0x8151u;
        else if(*p=='/')sj=0x815Eu;
        else if(*p==':')sj=0x8146u;
        if(sj){if(n+2>cap)break;out[n++]=(uint8_t)(sj>>8);out[n++]=(uint8_t)sj;}
        else {if(n+1>cap)break;out[n++]=*p;}
    }
    return n;
}
static void make_icon_sys_lines(uint8_t out[964],const char *game_name,const char *save_title){
    memset(out,0,964); memcpy(out,"PS2D",4); wr32le(out+12,0x60);
    static const uint32_t bg[4][4]={{36,24,92,0},{76,84,128,0},{18,28,70,0},{48,60,108,0}};
    for(int i=0;i<4;i++)for(int j=0;j<4;j++)wr32le(out+16+i*16+j*4,bg[i][j]);
    static const float ld[3][4]={{0.5f,0.5f,0.5f,0},{0,-0.4f,-0.1f,0},{-0.5f,-0.5f,0.5f,0}};
    static const float lc[3][4]={{0.3f,0.3f,0.3f,0},{0.4f,0.4f,0.4f,0},{0.5f,0.5f,0.5f,0}};
    static const float amb[4]={0.5f,0.5f,0.5f,0};
    for(int i=0;i<3;i++)for(int j=0;j<4;j++)wrf32le(out+80+i*16+j*4,ld[i][j]);
    for(int i=0;i<3;i++)for(int j=0;j<4;j++)wrf32le(out+128+i*16+j*4,lc[i][j]);
    for(int j=0;j<4;j++)wrf32le(out+176+j*4,amb[j]);
    size_t first=sjis_fullwidth_ascii(out+192,68,game_name?game_name:"");
    size_t second=0;if(save_title&&*save_title&&first<68)second=sjis_fullwidth_ascii(out+192+first,68-first,save_title);
    /* icon.sys secondLineOffset is a byte offset into the 68-byte Shift-JIS title. */
    wr16le(out+6,(uint16_t)(second?first:0));
    memcpy(out+260,"icon.icn",8); memcpy(out+324,"icon.icn",8); memcpy(out+388,"icon.icn",8);
}
static void make_icon_sys(uint8_t out[964],const char *title){make_icon_sys_lines(out,title,"");}

static void icn_vertex(uint8_t **pp,int16_t x,int16_t y,int16_t z,int16_t nx,int16_t ny,int16_t nz,uint16_t u,uint16_t v){
    /* Generated template meshes use the opposite vertical axis and front/back
       depth convention from the working game ICNs as interpreted by the
       preview path.  Correct generated model coordinates only: flip Y so
       texture top/bottom stays upright, and flip Z so the intended textured
       front face is the near face seen by the preview camera.  Keep texture
       bytes and UVs untouched. */
    y=(int16_t)-y; ny=(int16_t)-ny;
    z=(int16_t)-z; nz=(int16_t)-nz;
    uint8_t *p=*pp;wr16le(p+0,(uint16_t)x);wr16le(p+2,(uint16_t)y);wr16le(p+4,(uint16_t)z);wr16le(p+6,0);p+=8;
    wr16le(p+0,(uint16_t)nx);wr16le(p+2,(uint16_t)ny);wr16le(p+4,(uint16_t)nz);wr16le(p+6,0);p+=8;
    wr16le(p+0,u);wr16le(p+2,v);p[4]=255;p[5]=255;p[6]=255;p[7]=255;p+=8;*pp=p;
}
static int make_icon_icn(uint8_t **data_out,uint32_t *size_out,const char *title){
    enum{VC=36}; const uint32_t total=20u+VC*24u+20u+128u*128u*2u; uint8_t *buf=(uint8_t*)calloc(1,total);if(!buf)return PSU_ENOMEM;
    wr32le(buf+0,0x00010000u);wr32le(buf+4,1);wr32le(buf+8,4);wr32le(buf+12,0x3F800000u);wr32le(buf+16,VC);
    uint8_t *p=buf+20; const int16_t X=3600,Y=2500,Z=420,N=4096; const uint16_t U0=0,V0=0,U1=4096,V1=4096;
#define VTX(x,y,z,nx,ny,nz,u,v) icn_vertex(&p,(x),(y),(z),(nx),(ny),(nz),(u),(v))
    /* front */ VTX(-X,-Y,Z,0,0,N,U0,V1);VTX(X,-Y,Z,0,0,N,U1,V1);VTX(X,Y,Z,0,0,N,U1,V0);VTX(-X,-Y,Z,0,0,N,U0,V1);VTX(X,Y,Z,0,0,N,U1,V0);VTX(-X,Y,Z,0,0,N,U0,V0);
    /* back */ VTX(X,-Y,-Z,0,0,-N,U0,V1);VTX(-X,-Y,-Z,0,0,-N,U1,V1);VTX(-X,Y,-Z,0,0,-N,U1,V0);VTX(X,-Y,-Z,0,0,-N,U0,V1);VTX(-X,Y,-Z,0,0,-N,U1,V0);VTX(X,Y,-Z,0,0,-N,U0,V0);
    /* left */ VTX(-X,-Y,-Z,-N,0,0,U0,V1);VTX(-X,-Y,Z,-N,0,0,U1,V1);VTX(-X,Y,Z,-N,0,0,U1,V0);VTX(-X,-Y,-Z,-N,0,0,U0,V1);VTX(-X,Y,Z,-N,0,0,U1,V0);VTX(-X,Y,-Z,-N,0,0,U0,V0);
    /* right */ VTX(X,-Y,Z,N,0,0,U0,V1);VTX(X,-Y,-Z,N,0,0,U1,V1);VTX(X,Y,-Z,N,0,0,U1,V0);VTX(X,-Y,Z,N,0,0,U0,V1);VTX(X,Y,-Z,N,0,0,U1,V0);VTX(X,Y,Z,N,0,0,U0,V0);
    /* top */ VTX(-X,Y,Z,0,N,0,U0,V1);VTX(X,Y,Z,0,N,0,U1,V1);VTX(X,Y,-Z,0,N,0,U1,V0);VTX(-X,Y,Z,0,N,0,U0,V1);VTX(X,Y,-Z,0,N,0,U1,V0);VTX(-X,Y,-Z,0,N,0,U0,V0);
    /* bottom */ VTX(-X,-Y,-Z,0,-N,0,U0,V1);VTX(X,-Y,-Z,0,-N,0,U1,V1);VTX(X,-Y,Z,0,-N,0,U1,V0);VTX(-X,-Y,-Z,0,-N,0,U0,V1);VTX(X,-Y,Z,0,-N,0,U1,V0);VTX(-X,-Y,Z,0,-N,0,U0,V0);
#undef VTX
    wr32le(p+0,1);wr32le(p+4,1);wrf32le(p+8,1.0f);wr32le(p+12,0);wr32le(p+16,0);p+=20;
    uint16_t tex[128*128];draw_title_texture(tex,title);for(int i=0;i<128*128;i++){wr16le(p,tex[i]);p+=2;}
    *data_out=buf;*size_out=total;return 0;
}

/* Three-icon blank-save template support.  The normal/view icon can be
   renamed by the user (ICON.ICN by default), while copy/delete use distinct
   fixed filenames and visibly different meshes/textures. */
static void draw_text_line(uint16_t *tex,const char *text,int y,int scale,uint16_t fg,uint16_t shadow){
    int len=(int)strlen(text); if(len>16)len=16; int cw=6*scale; int x0=64-(len*cw)/2;
    for(int ci=0;ci<len;ci++){
        const uint8_t *rows=font5x7(text[ci]); int bx=x0+ci*cw;
        for(int ry=0;ry<7;ry++)for(int rx=0;rx<5;rx++)if(rows[ry]&(1u<<(4-rx))){
            for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++){
                tex_put(tex,bx+rx*scale+sx+1,y+ry*scale+sy+1,shadow);
                tex_put(tex,bx+rx*scale+sx,y+ry*scale+sy,fg);
            }
        }
    }
}
static void draw_icon_texture_variant(uint16_t *tex,const char *title,int variant){
    unsigned r0,g0,b0,r1,g1,b1,gr,gg,gb;
    if(variant==1){r0=8;g0=58;b0=54;r1=20;g1=142;b1=126;gr=48;gg=190;gb=170;}
    else if(variant==2){r0=74;g0=10;b0=18;r1=172;g1=28;b1=38;gr=235;gg=72;gb=80;}
    else {r0=12;g0=30;b0=70;r1=28;g1=74;b1=138;gr=38;gg=92;gb=158;}
    uint16_t grid=rgb_to_bgr555(gr,gg,gb),fg=rgb_to_bgr555(245,248,255),shadow=rgb_to_bgr555(0,8,28);
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        unsigned t=(unsigned)y;unsigned r=(r0*(127u-t)+r1*t)/127u,g=(g0*(127u-t)+g1*t)/127u,b=(b0*(127u-t)+b1*t)/127u;
        tex[y*128+x]=rgb_to_bgr555(r,g,b);
    }
    for(int x=4;x<124;x++){tex_put(tex,x,4,grid);tex_put(tex,x,123,grid);}for(int y=4;y<124;y++){tex_put(tex,4,y,grid);tex_put(tex,123,y,grid);}
    if(variant==1)draw_text_line(tex,"COPY",12,2,fg,shadow);else if(variant==2)draw_text_line(tex,"DELETE",12,2,fg,shadow);else draw_text_line(tex,"VIEW",12,2,fg,shadow);
    char clean[33];size_t n=0;for(const char *q=title;*q&&n<32;q++)clean[n++]=*q;clean[n]=0;
    const int max_per_line=10,scale=2,cw=12,ch=16;int lines=(int)((n+max_per_line-1)/max_per_line);if(lines<1)lines=1;if(lines>3)lines=3;int y0=68-(lines*ch)/2;
    for(int li=0;li<lines;li++){
        int start=li*max_per_line,len=(int)n-start;if(len>max_per_line)len=max_per_line;if(len<0)len=0;char part[11];memcpy(part,clean+start,(size_t)len);part[len]=0;
        draw_text_line(tex,part,y0+li*ch,scale,fg,shadow);
    }
}
static void emit_box(uint8_t **pp,int16_t cx,int16_t cy,int16_t cz,int16_t hx,int16_t hy,int16_t hz){
    const int16_t N=4096;const uint16_t U0=0,V0=0,U1=4096,V1=4096;
#define BX(x,y,z,nx,ny,nz,u,v) icn_vertex(pp,(int16_t)(cx+(x)),(int16_t)(cy+(y)),(int16_t)(cz+(z)),(nx),(ny),(nz),(u),(v))
    BX(-hx,-hy,hz,0,0,N,U0,V1);BX(hx,-hy,hz,0,0,N,U1,V1);BX(hx,hy,hz,0,0,N,U1,V0);BX(-hx,-hy,hz,0,0,N,U0,V1);BX(hx,hy,hz,0,0,N,U1,V0);BX(-hx,hy,hz,0,0,N,U0,V0);
    BX(hx,-hy,-hz,0,0,-N,U0,V1);BX(-hx,-hy,-hz,0,0,-N,U1,V1);BX(-hx,hy,-hz,0,0,-N,U1,V0);BX(hx,-hy,-hz,0,0,-N,U0,V1);BX(-hx,hy,-hz,0,0,-N,U1,V0);BX(hx,hy,-hz,0,0,-N,U0,V0);
    BX(-hx,-hy,-hz,-N,0,0,U0,V1);BX(-hx,-hy,hz,-N,0,0,U1,V1);BX(-hx,hy,hz,-N,0,0,U1,V0);BX(-hx,-hy,-hz,-N,0,0,U0,V1);BX(-hx,hy,hz,-N,0,0,U1,V0);BX(-hx,hy,-hz,-N,0,0,U0,V0);
    BX(hx,-hy,hz,N,0,0,U0,V1);BX(hx,-hy,-hz,N,0,0,U1,V1);BX(hx,hy,-hz,N,0,0,U1,V0);BX(hx,-hy,hz,N,0,0,U0,V1);BX(hx,hy,-hz,N,0,0,U1,V0);BX(hx,hy,hz,N,0,0,U0,V0);
    BX(-hx,hy,hz,0,N,0,U0,V1);BX(hx,hy,hz,0,N,0,U1,V1);BX(hx,hy,-hz,0,N,0,U1,V0);BX(-hx,hy,hz,0,N,0,U0,V1);BX(hx,hy,-hz,0,N,0,U1,V0);BX(-hx,hy,-hz,0,N,0,U0,V0);
    BX(-hx,-hy,-hz,0,-N,0,U0,V1);BX(hx,-hy,-hz,0,-N,0,U1,V1);BX(hx,-hy,hz,0,-N,0,U1,V0);BX(-hx,-hy,-hz,0,-N,0,U0,V1);BX(hx,-hy,hz,0,-N,0,U1,V0);BX(-hx,-hy,hz,0,-N,0,U0,V0);
#undef BX
}
/* Emit a rectangular prism rotated +/-45 degrees in the icon X/Y plane.
   Used by the generated delete icon so its silhouette is an unambiguous X,
   rather than an upright +/cross. */
static void emit_box_diag45(uint8_t **pp,int sign,int16_t hx,int16_t hy,int16_t hz){
    const int32_t C=2896; /* round(4096/sqrt(2)) */
    const int16_t N=4096;const uint16_t U0=0,V0=0,U1=4096,V1=4096;
#define RX(x,y) ((int16_t)(((int32_t)(x)*C-(int32_t)(sign)*(y)*C)/4096))
#define RY(x,y) ((int16_t)(((int32_t)(sign)*(x)*C+(int32_t)(y)*C)/4096))
#define RNX(nx,ny) ((int16_t)(((int32_t)(nx)*C-(int32_t)(sign)*(ny)*C)/4096))
#define RNY(nx,ny) ((int16_t)(((int32_t)(sign)*(nx)*C+(int32_t)(ny)*C)/4096))
#define DV(x,y,z,nx,ny,nz,u,v) icn_vertex(pp,RX((x),(y)),RY((x),(y)),(z),RNX((nx),(ny)),RNY((nx),(ny)),(nz),(u),(v))
    DV(-hx,-hy,hz,0,0,N,U0,V1);DV(hx,-hy,hz,0,0,N,U1,V1);DV(hx,hy,hz,0,0,N,U1,V0);DV(-hx,-hy,hz,0,0,N,U0,V1);DV(hx,hy,hz,0,0,N,U1,V0);DV(-hx,hy,hz,0,0,N,U0,V0);
    DV(hx,-hy,-hz,0,0,-N,U0,V1);DV(-hx,-hy,-hz,0,0,-N,U1,V1);DV(-hx,hy,-hz,0,0,-N,U1,V0);DV(hx,-hy,-hz,0,0,-N,U0,V1);DV(-hx,hy,-hz,0,0,-N,U1,V0);DV(hx,hy,-hz,0,0,-N,U0,V0);
    DV(-hx,-hy,-hz,-N,0,0,U0,V1);DV(-hx,-hy,hz,-N,0,0,U1,V1);DV(-hx,hy,hz,-N,0,0,U1,V0);DV(-hx,-hy,-hz,-N,0,0,U0,V1);DV(-hx,hy,hz,-N,0,0,U1,V0);DV(-hx,hy,-hz,-N,0,0,U0,V0);
    DV(hx,-hy,hz,N,0,0,U0,V1);DV(hx,-hy,-hz,N,0,0,U1,V1);DV(hx,hy,-hz,N,0,0,U1,V0);DV(hx,-hy,hz,N,0,0,U0,V1);DV(hx,hy,-hz,N,0,0,U1,V0);DV(hx,hy,hz,N,0,0,U0,V0);
    DV(-hx,hy,hz,0,N,0,U0,V1);DV(hx,hy,hz,0,N,0,U1,V1);DV(hx,hy,-hz,0,N,0,U1,V0);DV(-hx,hy,hz,0,N,0,U0,V1);DV(hx,hy,-hz,0,N,0,U1,V0);DV(-hx,hy,-hz,0,N,0,U0,V0);
    DV(-hx,-hy,-hz,0,-N,0,U0,V1);DV(hx,-hy,-hz,0,-N,0,U1,V1);DV(hx,-hy,hz,0,-N,0,U1,V0);DV(-hx,-hy,-hz,0,-N,0,U0,V1);DV(hx,-hy,hz,0,-N,0,U1,V0);DV(-hx,-hy,hz,0,-N,0,U0,V0);
#undef DV
#undef RNY
#undef RNX
#undef RY
#undef RX
}
static int make_icon_icn_variant(uint8_t **data_out,uint32_t *size_out,const char *title,int variant){
    uint32_t vc=(variant==0)?36u:72u;uint32_t total=20u+vc*24u+20u+128u*128u*2u;uint8_t *buf=(uint8_t*)calloc(1,total);if(!buf)return PSU_ENOMEM;
    wr32le(buf+0,0x00010000u);wr32le(buf+4,1);wr32le(buf+8,4);wr32le(buf+12,0x3F800000u);wr32le(buf+16,vc);uint8_t *q=buf+20;
    if(variant==1){emit_box(&q,-520,260,-180,2900,1800,240);emit_box(&q,520,-260,180,2900,1800,240);}
    else if(variant==2){
        /* Two equal diagonal bars crossing at their exact centres.  Keep a
           tiny Z separation to avoid coplanar z-fighting while retaining a
           symmetric X silhouette from the normal preview angle. */
        emit_box_diag45(&q, 1,620,3000,300);
        emit_box_diag45(&q,-1,620,3000,300);
    }
    else emit_box(&q,0,0,0,3600,2500,420);
    wr32le(q+0,1);wr32le(q+4,1);wrf32le(q+8,1.0f);wr32le(q+12,0);wr32le(q+16,0);q+=20;
    uint16_t tex[128*128];draw_icon_texture_variant(tex,title,variant);
    /* Keep generated texture rows in normal top-to-bottom drawing order.
       Generated UVs are also kept exactly as authored in icn_vertex(), so
       the raw 2D preview and the mapped 3D face share the same orientation. */
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){wr16le(q,tex[y*128+x]);q+=2;}
    *data_out=buf;*size_out=total;return 0;
}
static void make_icon_sys3(uint8_t out[964],const char *game_name,const char *save_title,const char *view_name,const char *copy_name,const char *delete_name){
    make_icon_sys_lines(out,game_name,save_title);memset(out+260,0,64);memset(out+324,0,64);memset(out+388,0,64);
    strncpy((char*)out+260,view_name,63);strncpy((char*)out+324,copy_name,63);strncpy((char*)out+388,delete_name,63);
}

static int write_mem_psu_file(FILE *o,const char *name,const uint8_t *data,uint32_t size,const psu_time_t *when){
    psu_entry_t e;memset(&e,0,sizeof(e));e.attr=PSU_ATTR_FILE;e.size=size;e.ctime=*when;e.mtime=*when;strncpy(e.name,name,PSU_NAME_MAX);
    int rc=write_header(o,&e);if(!rc&&size&&fwrite(data,1,size,o)!=size)rc=PSU_EIO;if(!rc)rc=write_zeros(o,pad_for(size));return rc;
}
int psu_create_blank_template_ex2(const char *outp,const char *save_name,const char *game_name,const char *save_title,const char *icon_text,const char *view_icon_base){
    if(!outp||!save_name||!*save_name||strlen(save_name)>PSU_NAME_MAX||!safe_name(save_name))return PSU_EINVAL;
    if(!game_name||!*game_name)game_name=save_name;if(!save_title)save_title="";if(!icon_text||!*icon_text)icon_text=game_name;if(!view_icon_base||!*view_icon_base)view_icon_base="icon";
    if(strlen(view_icon_base)>28||!safe_name(view_icon_base)||!strcasecmp(view_icon_base,"COPY")||!strcasecmp(view_icon_base,"DELETE"))return PSU_EINVAL;
    struct stat st;if(stat(outp,&st)==0)return PSU_EEXIST;
    char view_name[PSU_NAME_MAX+1];snprintf(view_name,sizeof(view_name),"%s.icn",view_icon_base);const char *copy_name="copy.icn",*delete_name="del.icn";
    uint8_t iconsys[964];uint8_t *view=NULL,*copy=NULL,*del=NULL;uint32_t view_size=0,copy_size=0,del_size=0;int rc=0;
    make_icon_sys3(iconsys,game_name,save_title,view_name,copy_name,delete_name);
    if((rc=make_icon_icn_variant(&view,&view_size,icon_text,0)))goto build_fail;
    if((rc=make_icon_icn_variant(&copy,&copy_size,icon_text,1)))goto build_fail;
    if((rc=make_icon_icn_variant(&del,&del_size,icon_text,2)))goto build_fail;
    {
        char tmp[1200];snprintf(tmp,sizeof(tmp),"%s.tmp",outp);remove(tmp);FILE *o=fopen(tmp,"wb");if(!o){rc=PSU_EIO;goto build_fail;}
        time_t now=time(NULL);psu_time_t when;time_from_time_t(&when,now);psu_entry_t e;memset(&e,0,sizeof(e));e.attr=PSU_ATTR_DIR;e.size=6;e.ctime=when;e.mtime=when;strncpy(e.name,save_name,PSU_NAME_MAX);
        if((rc=write_header(o,&e)))goto done_template3;memset(&e,0,sizeof(e));e.attr=PSU_ATTR_DIR;e.ctime=when;e.mtime=when;strcpy(e.name,".");if((rc=write_header(o,&e)))goto done_template3;strcpy(e.name,"..");if((rc=write_header(o,&e)))goto done_template3;
        if((rc=write_mem_psu_file(o,"icon.sys",iconsys,sizeof(iconsys),&when)))goto done_template3;
        if((rc=write_mem_psu_file(o,view_name,view,view_size,&when)))goto done_template3;
        if((rc=write_mem_psu_file(o,copy_name,copy,copy_size,&when)))goto done_template3;
        if((rc=write_mem_psu_file(o,delete_name,del,del_size,&when)))goto done_template3;
    done_template3:
        if(fclose(o)!=0&&!rc)rc=PSU_EIO;if(rc){remove(tmp);goto build_fail;}if(stat(outp,&st)==0){remove(tmp);rc=PSU_EEXIST;goto build_fail;}if(rename(tmp,outp)!=0){remove(tmp);rc=PSU_EIO;goto build_fail;}
    }
 build_fail:
    free(view);free(copy);free(del);return rc;
}
int psu_create_blank_template_ex(const char *outp,const char *save_name,const char *display_title,const char *icon_text,const char *view_icon_base){
    return psu_create_blank_template_ex2(outp,save_name,display_title,"",icon_text,view_icon_base);
}
int psu_create_blank_template(const char *outp,const char *save_name,const char *display_title){
    return psu_create_blank_template_ex2(outp,save_name,display_title,"",display_title,"icon");
}
typedef struct { psu_entry_t e; uint8_t *data; } mem_entry_t;
static void free_entries(mem_entry_t *v,size_t n){if(!v)return;for(size_t i=0;i<n;i++)free(v[i].data);free(v);}
static int load_psu(const char *path,psu_entry_t *root,mem_entry_t **out,size_t *n){
    FILE *f=fopen(path,"rb");if(!f)return PSU_EIO;if(read_header(f,root)||!is_dir_attr(root->attr)||root->size>10000){fclose(f);return PSU_EFORMAT;}
    mem_entry_t *v=(mem_entry_t*)calloc(root->size,sizeof(*v));if(!v){fclose(f);return PSU_ENOMEM;}
    for(uint32_t i=0;i<root->size;i++){if(read_header(f,&v[i].e)){free_entries(v,i);fclose(f);return PSU_EFORMAT;} if(is_file_attr(v[i].e.attr)&&v[i].e.size){v[i].data=(uint8_t*)malloc(v[i].e.size);if(!v[i].data){free_entries(v,i);fclose(f);return PSU_ENOMEM;} if(fread(v[i].data,1,v[i].e.size,f)!=v[i].e.size){free_entries(v,i+1);fclose(f);return PSU_EFORMAT;} if(fseek(f,(long)pad_for(v[i].e.size),SEEK_CUR)){free_entries(v,i+1);fclose(f);return PSU_EFORMAT;}}}
    fclose(f);*out=v;*n=root->size;return 0;
}
static int save_psu(const char *path,const psu_entry_t *root,mem_entry_t *v,size_t n){
    char tmp[1200];snprintf(tmp,sizeof(tmp),"%s.tmp",path);FILE *o=fopen(tmp,"wb");if(!o)return PSU_EIO;psu_entry_t r=*root;r.size=(uint32_t)n;int rc=write_header(o,&r);
    for(size_t i=0;!rc&&i<n;i++){rc=write_header(o,&v[i].e);if(!rc&&is_file_attr(v[i].e.attr)){if(v[i].e.size && fwrite(v[i].data,1,v[i].e.size,o)!=v[i].e.size)rc=PSU_EIO;if(!rc)rc=write_zeros(o,pad_for(v[i].e.size));}}
    if(fclose(o)!=0&&!rc)rc=PSU_EIO;if(rc){remove(tmp);return rc;}remove(path);if(rename(tmp,path)!=0){remove(tmp);return PSU_EIO;}return 0;
}

int psu_add_or_replace(const char *path,const char *src,const char *entry_name){
    if(!safe_name(entry_name)||strlen(entry_name)>PSU_NAME_MAX)return PSU_EINVAL; struct stat st;if(stat(src,&st)||!S_ISREG(st.st_mode)||st.st_size>0xffffffffLL)return PSU_EIO;
    FILE *in=fopen(src,"rb");if(!in)return PSU_EIO;uint8_t *data=(uint8_t*)malloc((size_t)st.st_size?((size_t)st.st_size):1);if(!data){fclose(in);return PSU_ENOMEM;}if(st.st_size&&fread(data,1,(size_t)st.st_size,in)!=(size_t)st.st_size){free(data);fclose(in);return PSU_EIO;}fclose(in);
    psu_entry_t root;mem_entry_t *v=NULL;size_t n=0;int rc=load_psu(path,&root,&v,&n);if(rc){free(data);return rc;}size_t idx=n;
    for(size_t i=0;i<n;i++)if(is_file_attr(v[i].e.attr)&&!strncmp(v[i].e.name,entry_name,PSU_NAME_MAX)){idx=i;break;}
    if(idx==n){mem_entry_t *nv=(mem_entry_t*)realloc(v,(n+1)*sizeof(*v));if(!nv){free_entries(v,n);free(data);return PSU_ENOMEM;}v=nv;memset(&v[n],0,sizeof(v[n]));n++;}
    else free(v[idx].data);
    memset(&v[idx].e,0,sizeof(v[idx].e));v[idx].e.attr=PSU_ATTR_FILE;v[idx].e.size=(uint32_t)st.st_size;time_from_time_t(&v[idx].e.ctime,st.st_ctime);time_from_time_t(&v[idx].e.mtime,st.st_mtime);strncpy(v[idx].e.name,entry_name,PSU_NAME_MAX);v[idx].data=data;
    rc=save_psu(path,&root,v,n);free_entries(v,n);return rc;
}

int psu_delete_entry(const char *path,const char *name){
    if(!safe_name(name))return PSU_EINVAL;psu_entry_t root;mem_entry_t *v=NULL;size_t n=0;int rc=load_psu(path,&root,&v,&n);if(rc)return rc;size_t idx=n;
    for(size_t i=0;i<n;i++)if(is_file_attr(v[i].e.attr)&&!strncmp(v[i].e.name,name,PSU_NAME_MAX)){idx=i;break;}if(idx==n){free_entries(v,n);return PSU_ENOENT;}
    free(v[idx].data);for(size_t i=idx+1;i<n;i++)v[i-1]=v[i];n--;rc=save_psu(path,&root,v,n);free_entries(v,n);return rc;
}


int psu_rename_entry(const char *path,const char *old_name,const char *new_name){
    if(!path||!safe_name(old_name)||!safe_name(new_name)||strlen(new_name)>PSU_NAME_MAX)return PSU_EINVAL;
    if(!strncmp(old_name,new_name,PSU_NAME_MAX))return 0;
    psu_entry_t root;mem_entry_t *v=NULL;size_t n=0;int rc=load_psu(path,&root,&v,&n);if(rc)return rc;
    size_t src=n;
    for(size_t i=0;i<n;i++){
        if(is_file_attr(v[i].e.attr)&&!strncmp(v[i].e.name,new_name,PSU_NAME_MAX)){free_entries(v,n);return PSU_EEXIST;}
        if(is_file_attr(v[i].e.attr)&&!strncmp(v[i].e.name,old_name,PSU_NAME_MAX))src=i;
    }
    if(src==n){free_entries(v,n);return PSU_ENOENT;}
    memset(v[src].e.name,0,sizeof(v[src].e.name));strncpy(v[src].e.name,new_name,PSU_NAME_MAX);
    /* Preserve PS2 browser icon references when one of the three ICN files is renamed. */
    for(size_t i=0;i<n;i++)if(is_file_attr(v[i].e.attr)&&!strcasecmp(v[i].e.name,"ICON.SYS")&&v[i].e.size>=452&&v[i].data){
        static const size_t offs[3]={260,324,388};
        for(int k=0;k<3;k++){
            char cur[65];memcpy(cur,v[i].data+offs[k],64);cur[64]=0;
            if(!strncmp(cur,old_name,64)){
                memset(v[i].data+offs[k],0,64);strncpy((char*)v[i].data+offs[k],new_name,63);
            }
        }
        break;
    }
    rc=save_psu(path,&root,v,n);free_entries(v,n);return rc;
}

