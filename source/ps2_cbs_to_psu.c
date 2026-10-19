#define _POSIX_C_SOURCE 200809L
#include "ps2_cbs_to_psu.h"
#include "ps2_cbs_archive.h"
#include "psu.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Patch only metadata fields in the temporary PSU created by the existing writer.
   Never change file contents, entry lengths or the production PSU implementation. */
static int restore_cbs_metadata(const char *psu, const ps2_cbs_archive *a,
                                const unsigned char *cbs, size_t cbs_len) {
    FILE *f=fopen(psu,"r+b");
    unsigned char h[PSU_HEADER_SIZE];
    size_t seen=0;
    if(!f) return -1;
    /* CBS header contains root directory created/modified timestamps and mode.
       Offsets are from the 296-byte CodeBreaker header, not its decoded payload. */
    if(!cbs || cbs_len<296 || memcmp(cbs,"CFU\0",4)!=0) goto fail;
    if(fread(h,1,sizeof(h),f)!=sizeof(h)) goto fail;
    memcpy(h+0x08,cbs+52,8);
    memcpy(h+0x18,cbs+60,8);
    {
        unsigned mode=(unsigned)cbs[72] | ((unsigned)cbs[73]<<8);
        if((mode & 0xF000u)==0x4000u) {
            h[0]=(unsigned char)mode;
            h[1]=(unsigned char)(mode>>8);
        }
    }
    if(fseek(f,0,SEEK_SET)!=0 || fwrite(h,1,sizeof(h),f)!=sizeof(h)) goto fail;
    /* Root, dot and dot-dot headers precede file entries. */
    if(fseek(f,3L*PSU_HEADER_SIZE,SEEK_SET)!=0) goto fail;
    while(seen<a->count) {
        long start=ftell(f);
        uint32_t size;
        size_t j;
        if(start<0 || fread(h,1,sizeof(h),f)!=sizeof(h)) goto fail;
        size=(uint32_t)h[4]|((uint32_t)h[5]<<8)|((uint32_t)h[6]<<16)|((uint32_t)h[7]<<24);
        for(j=0;j<a->count;j++) {
            const ps2_cbs_entry *e=&a->entries[j];
            if(size==e->size && strncmp((const char*)h+0x40,e->name,32)==0) break;
        }
        if(j==a->count) goto fail;
        /* Only regular file attributes are meaningful here; preserve unknown bits. */
        if((a->entries[j].mode & 0xF000u)==0x8000u) {
            h[0]=(unsigned char)a->entries[j].mode;
            h[1]=(unsigned char)(a->entries[j].mode>>8);
        }
        memcpy(h+0x08,a->entries[j].created,8);
        memcpy(h+0x18,a->entries[j].modified,8);
        if(fseek(f,start,SEEK_SET)!=0 || fwrite(h,1,sizeof(h),f)!=sizeof(h)) goto fail;
        /* Writer aligns data to 1024-byte boundaries. */
        if(size>0x7fffffffU) goto fail;
        if(fseek(f,(long)((size+1023u)&~1023u),SEEK_CUR)!=0) goto fail;
        seen++;
    }
    if(fclose(f)!=0) return -1;
    return 0;
fail:
    fclose(f);
    return -1;
}

/* Only invoked explicitly. Existing PSU routines are linked unchanged.
   Temp staging is private (0700), and files are opened O_EXCL in a private staging directory. */
/* PS3-safe private staging: avoid mkdtemp/mkstemp/link (not portable to cellFs). */
static int make_private_stage(char *dest, size_t cap, const char *output, const char *tag) {
    static unsigned serial=0;
    unsigned k;
    for(k=0;k<256;k++) {
        int z=snprintf(dest,cap,"%s.%sstage_%08lx_%04x",output,tag,
                       (unsigned long)getpid(),(unsigned)((++serial+k)&65535u));
        if(z<0 || (size_t)z>=cap)return -1;
        if(mkdir(dest,0700)==0)return 0;
        if(errno!=EEXIST)return -1;
    }
    return -1;
}
static int make_private_temp(char *dest,size_t cap,const char *output,const char *tag) {
    static unsigned serial=0;
    unsigned k;
    for(k=0;k<256;k++) {
        int z=snprintf(dest,cap,"%s.%stmp_%08lx_%04x",output,tag,
                       (unsigned long)getpid(),(unsigned)((++serial+k)&65535u));
        if(z<0 || (size_t)z>=cap)return -1;
        if(access(dest,F_OK)!=0 && errno==ENOENT)return 0;
    }
    return -1;
}
static int place_without_overwrite(const char *temp,const char *output) {
    struct stat st;
    if(stat(output,&st)==0 || errno!=ENOENT)return -1;
    /* PS3 single-process browser checks destination again before rename. */
    return rename(temp,output);
}

int ps2_cbs_convert_to_psu(const unsigned char *bytes, size_t len, const char *output) {
    ps2_cbs_archive a;
    char *dir=NULL, *temp=NULL, *path=NULL;
    size_t i, n;
    int rc=-1, fd=-1, dir_created=0, temp_created=0;
    struct stat st;
    if (!bytes || !output || !*output || strlen(output)>3000) return -1;
    if (stat(output,&st)==0 || errno!=ENOENT) return -2;
    memset(&a,0,sizeof(a));
    if (ps2_cbs_open(bytes,len,&a)!=0) return -3;
    n=strlen(output);
    dir=(char*)malloc(n+32); temp=(char*)malloc(n+32); path=(char*)malloc(n+80);
    if (!dir || !temp || !path) {rc=-10;goto done;}
    /* Staging directory is adjacent to destination to avoid cross-volume rename. */
    if (make_private_stage(dir,n+32,output,"cbs")!=0) {rc=-11;goto done;}
    dir_created=1;
    for (i=0;i<a.count;i++) {
        const ps2_cbs_entry *e=&a.entries[i];
        size_t j;
        /* Enforce unique filenames, including case-insensitive collisions. */
        for(j=0;j<i;j++) if (strcasecmp(e->name,a.entries[j].name)==0) goto done;
        if (snprintf(path,n+80,"%s/%s",dir,e->name)<0) goto done;
        fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) {rc=-12;goto done;}
        {
            size_t off=0;
            const unsigned char *p=a.decoded+e->data_offset;
            while(off<e->size) {
                ssize_t w=write(fd,p+off,e->size-off);
                if(w<=0) goto done;
                off+=(size_t)w;
            }
        }
        if(close(fd)!=0) {fd=-1;goto done;}
        fd=-1;
    }
    if(make_private_temp(temp,n+32,output,"cbs")!=0) {rc=-14;goto done;}
    temp_created=1;
    if(psu_create_from_dir(dir,temp,a.directory)!=0) {rc=-16;goto done;}
    if(restore_cbs_metadata(temp,&a,bytes,len)!=0) {rc=-17;goto done;}
    /* Fail closed on destination collision. link() is atomic and refuses overwrite. */
    if(place_without_overwrite(temp,output)!=0) {rc=-18;goto done;}
    rc=0;
done:
    if(fd>=0) close(fd);
    if(temp_created) unlink(temp);
    if(dir_created) {
        for(i=0;i<a.count;i++) {
            if(path) {snprintf(path,n+80,"%s/%s",dir,a.entries[i].name);unlink(path);}
        }
        rmdir(dir);
    }
    free(path);free(temp);free(dir);
    ps2_cbs_close(&a);
    return rc;
}
