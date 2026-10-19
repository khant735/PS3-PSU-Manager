#include "ps2_max_decode.h"
#include "ps2_save_archive_header.h"
#include <string.h>
#define RING 4096u
#define MATCH 60u
#define THRESHOLD 2u
#define CHARS (256u-THRESHOLD+MATCH)
#define Q1 32768u
#define Q2 65536u
#define Q3 98304u
#define Q4 131072u
#define MAX_OUT (64u*1024u*1024u)
typedef struct {
 const uint8_t *src; size_t src_len, bitpos;
 uint32_t low, high, value;
 uint8_t ring[RING]; size_t r;
 unsigned short freq[CHARS+1],cum[CHARS+1];
 unsigned short sym_to_char[CHARS+1],char_to_sym[CHARS];
 unsigned int pos_cum[RING+1];
} dec;
static int bit(dec *d, unsigned *v) {
 /* The reference LZARI reader supplies zero bits after the physical stream end.
    * Allow a bounded 64-bit tail for arithmetic-code finalisation. */
 if(d->bitpos>=d->src_len*8u) {
  if(d->bitpos-d->src_len*8u>=64u)return -1;
  *v=0; d->bitpos++; return 0;
 }
 *v=(d->src[d->bitpos>>3]>>(7u-(d->bitpos&7u)))&1u;
 d->bitpos++; return 0;
}
static int init(dec *d) {
 size_t i; unsigned b;
 d->low=0;d->high=Q4;d->value=0;
 d->cum[CHARS]=0;
 for(i=CHARS;i>0;i--){d->sym_to_char[i]=(unsigned short)(i-1);d->char_to_sym[i-1]=(unsigned short)i;d->freq[i]=1;d->cum[i-1]=(unsigned short)(d->cum[i]+1);}
 d->freq[0]=0;d->pos_cum[RING]=0;
 for(i=RING;i>0;i--)d->pos_cum[i-1]=d->pos_cum[i]+10000u/(unsigned)(i+200u);
 memset(d->ring,' ',RING);d->r=RING-MATCH;
 for(i=0;i<17;i++){if(bit(d,&b))return -1;d->value=d->value*2+b;}
 return 0;
}
static void update(dec *d,unsigned sym){
 unsigned i,c;unsigned short ch1,ch2;
 if(d->cum[0]>=Q1-1){c=0;for(i=CHARS;i>0;i--){d->cum[i]=(unsigned short)c;c+=(d->freq[i]=(unsigned short)((d->freq[i]+1)>>1));}d->cum[0]=(unsigned short)c;}
 for(i=sym;i>1 && d->freq[i]==d->freq[i-1];i--){}
 if(i<sym){ch1=d->sym_to_char[i];ch2=d->sym_to_char[sym];d->sym_to_char[i]=ch2;d->sym_to_char[sym]=ch1;d->char_to_sym[ch1]=(unsigned short)sym;d->char_to_sym[ch2]=(unsigned short)i;}
 d->freq[i]++;while(i>0){i--;d->cum[i]++;}
}
static int renorm(dec *d){unsigned b;
 for(;;){
  if(d->low>=Q2){d->value-=Q2;d->low-=Q2;d->high-=Q2;}
  else if(d->low>=Q1 && d->high<=Q3){d->value-=Q1;d->low-=Q1;d->high-=Q1;}
  else if(d->high>Q2)break;
  d->low*=2;d->high*=2;
  if(bit(d,&b))return -1;
  d->value=d->value*2+b;
 }
 return 0;
}
static int symbol(dec *d,unsigned *ch){
 unsigned lo=1,hi=CHARS,sym;uint64_t range,x;
 range=d->high-d->low;if(!range||d->value<d->low)return -1;
 x=((uint64_t)(d->value-d->low+1)*d->cum[0]-1)/range;
 if(x>=d->cum[0])return -1;
 while(lo<hi){unsigned mid=(lo+hi)/2;if(d->cum[mid]>x)lo=mid+1;else hi=mid;}
 sym=lo;
 d->high=d->low+(uint32_t)(range*d->cum[sym-1]/d->cum[0]);
 d->low+=(uint32_t)(range*d->cum[sym]/d->cum[0]);
 if(renorm(d))return -1;
 *ch=d->sym_to_char[sym];update(d,sym);return 0;
}
static int position(dec *d,unsigned *p){
 unsigned lo=1,hi=RING;uint64_t range,x;
 range=d->high-d->low;if(!range||d->value<d->low)return -1;
 x=((uint64_t)(d->value-d->low+1)*d->pos_cum[0]-1)/range;
 if(x>=d->pos_cum[0])return -1;
 while(lo<hi){unsigned mid=(lo+hi)/2;if(d->pos_cum[mid]>x)lo=mid+1;else hi=mid;}
 *p=lo-1;
 d->high=d->low+(uint32_t)(range*d->pos_cum[*p]/d->pos_cum[0]);
 d->low+=(uint32_t)(range*d->pos_cum[*p+1]/d->pos_cum[0]);
 return renorm(d);
}
int ps2_max_lzari_decode(const uint8_t *src,size_t src_len,uint8_t *dst,size_t cap,size_t *written){
 dec d;size_t out=0,target;unsigned ch,p;int rc=-1;
 if(written)*written=0;
 if(!src||!dst||!written||src_len<7||src_len>(size_t)-1/8)return -1;
 target=(size_t)src[0]|((size_t)src[1]<<8)|((size_t)src[2]<<16)|((size_t)src[3]<<24);
 if(!target||target>MAX_OUT||target>cap)return -2;
 memset(&d,0,sizeof(d));d.src=src+4;d.src_len=src_len-4;
 if(init(&d))return -3;
 while(out<target){
  if(symbol(&d,&ch))goto done;
  if(ch<256){dst[out++]=(uint8_t)ch;d.ring[d.r]=(uint8_t)ch;d.r=(d.r+1)&(RING-1);}
  else {unsigned count=ch-255+THRESHOLD;size_t start,k;
   if(position(&d,&p)||count>target-out)goto done;
   start=(d.r-p-1)&(RING-1);
   for(k=0;k<count;k++){uint8_t v=d.ring[(start+k)&(RING-1)];dst[out++]=v;d.ring[d.r]=v;d.r=(d.r+1)&(RING-1);}
  }
 }
 *written=out;rc=0;
 done:return rc;
}
int ps2_max_archive_decode(const uint8_t *a,size_t n,uint8_t *dst,size_t cap,size_t *written){
 ps2_archive_header h;int rc;
 if(!a||!dst||!written)return -1;
 if(ps2_archive_read_header(a,n,&h)||h.format!=PS2_SAVE_MAX)return -2;
 rc=ps2_max_lzari_decode(a+h.payload_offset,h.compressed_bytes,dst,cap,written);
 return rc;
}
