#define _POSIX_C_SOURCE 200809L
#include "ps2_max_to_psu.h"
#include "ps2_max_archive.h"
#include "ps2_save_archive_header.h"
#include "psu.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

int ps2_max_convert_to_psu(const unsigned char *bytes, size_t len, const char *output) {
    ps2_archive_header header;
    unsigned char *decoded=NULL;
    ps2_max_entry *entries=NULL;
    char *dir=NULL, *temp=NULL, *path=NULL;
    size_t i,j,n,decoded_len=0,count=0;
    int rc=-1,fd=-1,staging=0,temporary=0;
    struct stat st;
    if(!bytes || !output || !*output || strlen(output)>3000) return -1;
    if(stat(output,&st)==0 || errno!=ENOENT) return -2;
    if(ps2_archive_read_header(bytes,len,&header)!=0 || header.format!=PS2_SAVE_MAX) return -3;
    if(header.unpacked_bytes>64u*1024u*1024u || header.entry_count>65535u) return -3;
    decoded=(unsigned char*)malloc(header.unpacked_bytes);
    entries=(ps2_max_entry*)calloc(header.entry_count,sizeof(*entries));
    n=strlen(output);
    dir=(char*)malloc(n+32);temp=(char*)malloc(n+32);path=(char*)malloc(n+80);
    if(!decoded || !entries || !dir || !temp || !path) {rc=-10;goto done;}
    if(ps2_max_archive_list(bytes,len,decoded,header.unpacked_bytes,entries,
                             header.entry_count,&decoded_len,&count)!=0 || count!=header.entry_count) {rc=-9;goto done;}
    for(i=0;i<count;i++) for(j=0;j<i;j++)
        if(strcasecmp(entries[i].name,entries[j].name)==0) goto done;
    if(make_private_stage(dir,n+32,output,"max")!=0) {rc=-11;goto done;}
    staging=1;
    for(i=0;i<count;i++) {
        const ps2_max_entry *e=&entries[i];
        size_t off=0;
        if(e->data_offset>decoded_len || e->size>decoded_len-e->data_offset) goto done;
        if(snprintf(path,n+80,"%s/%s",dir,e->name)<0) goto done;
        fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) {rc=-12;goto done;}
        while(off<e->size) {
            ssize_t w=write(fd,decoded+e->data_offset+off,e->size-off);
            if(w<=0) goto done;
            off+=(size_t)w;
        }
        if(close(fd)!=0) {fd=-1;goto done;}
        fd=-1;
    }
    if(make_private_temp(temp,n+32,output,"max")!=0) {rc=-14;goto done;}
    temporary=1;
    if(psu_create_from_dir(dir,temp,header.directory)!=0) {rc=-16;goto done;}
    /* link() refuses to overwrite an existing output, including a race. */
    if(place_without_overwrite(temp,output)!=0) {rc=-18;goto done;}
    rc=0;
done:
    if(fd>=0) close(fd);
    if(temporary) unlink(temp);
    if(staging) {
        for(i=0;i<count;i++) {
            if(path) {snprintf(path,n+80,"%s/%s",dir,entries[i].name);unlink(path);}
        }
        rmdir(dir);
    }
    free(path);free(temp);free(dir);free(entries);free(decoded);
    return rc;
}
