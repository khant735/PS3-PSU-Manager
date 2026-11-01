/* Native 7z/LZMA2 GameFAQs cache reader. Requires 7-Zip SDK C decoder. */
#include "ps3_gfs_7z.h"
#include "7z.h"
#include "7zFile.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include <stdlib.h>
#include <string.h>
#define GFS_7Z_INPUT_BUFFER (1u << 15)
#define GFS_7Z_MAX_FILES 100000u
static const ISzAlloc gfs_alloc = { SzAlloc, SzFree };
static int gfs_is_tsv(const CSzArEx *db, UInt32 i) {
 size_t n=SzArEx_GetFileNameUtf16(db,i,NULL);
 UInt16 *s; int ok;
 if(n<5||n>4096)return 0;
 s=(UInt16*)malloc(n*sizeof(UInt16)); if(!s)return 0;
 SzArEx_GetFileNameUtf16(db,i,s);
 ok=s[n-5]=='.'&&(s[n-4]=='t'||s[n-4]=='T')&&(s[n-3]=='s'||s[n-3]=='S')&&(s[n-2]=='v'||s[n-2]=='V');
 free(s);return ok;
}
int ps3_gfs_read_7z(const char *archive,size_t max_entry_bytes,ps3_gfs_line_fn callback,void *context) {
 CFileInStream file;CLookToRead2 look;CSzArEx db;
 UInt32 i,blockIndex=(UInt32)-1;Byte *decoded=NULL;
 size_t decoded_size=0;int status=-1;SRes res;
 if(!archive||!callback||!max_entry_bytes||max_entry_bytes>(8u<<20))return -1;
 memset(&look,0,sizeof(look));File_Construct(&file.file);
 if(InFile_Open(&file.file,archive)!=0)return -2;
 FileInStream_CreateVTable(&file);file.wres=0;
 LookToRead2_CreateVTable(&look,False);
 look.buf=(Byte*)ISzAlloc_Alloc(&gfs_alloc,GFS_7Z_INPUT_BUFFER);
 if(!look.buf){File_Close(&file.file);return -1;}
 look.bufSize=GFS_7Z_INPUT_BUFFER;look.realStream=&file.vt;LookToRead2_INIT(&look);
 CrcGenerateTable();SzArEx_Init(&db);
 res=SzArEx_Open(&db,&look.vt,&gfs_alloc,&gfs_alloc);
 if(res!=SZ_OK){status=-3;goto finish;}
 status=0;
 if(db.NumFiles>GFS_7Z_MAX_FILES){status=-4;goto finish;}
 for(i=0;i<db.NumFiles;i++){
  UInt64 size;UInt32 folder;size_t offset=0,produced=0,pos;
  if(SzArEx_IsDir(&db,i)||!gfs_is_tsv(&db,i))continue;
  size=SzArEx_GetFileSize(&db,i);
  if(size>max_entry_bytes){status=-5;break;}
  folder=db.FileToFolder[i];
  if(folder==(UInt32)-1||folder>=db.db.NumFolders){status=-6;break;}
  if(db.FolderToFile[folder+1]-db.FolderToFile[folder]!=1){status=-7;break;}
  res=SzArEx_Extract(&db,&look.vt,i,&blockIndex,&decoded,&decoded_size,&offset,&produced,&gfs_alloc,&gfs_alloc);
  if(res!=SZ_OK||produced!=(size_t)size||offset>decoded_size||produced>decoded_size-offset){status=-8;break;}
  for(pos=0;pos<produced;){
   size_t end=pos;
   while(end<produced&&decoded[offset+end]!='\n')end++;
   if(end>pos&&decoded[offset+end-1]=='\r')end--;
   if(end>pos&&callback((const char*)(decoded+offset+pos),end-pos,context)){status=-9;break;}
   while(pos<produced&&decoded[offset+pos]!='\n')pos++;
   if(pos<produced)pos++;
  }
  if(status)break;
  ISzAlloc_Free(&gfs_alloc,decoded);decoded=NULL;decoded_size=0;blockIndex=(UInt32)-1;
 }
finish:
 SzArEx_Free(&db,&gfs_alloc);
 ISzAlloc_Free(&gfs_alloc,decoded);ISzAlloc_Free(&gfs_alloc,look.buf);
 File_Close(&file.file);return status;
}
