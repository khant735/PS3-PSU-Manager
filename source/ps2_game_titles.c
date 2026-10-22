#include "ps2_game_titles.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int ps2_game_extract_serial(const char *dir, char out[11]) {
    size_t n;
    if (!dir || !out) return 0;
    n = strlen(dir);
    for (size_t i=0;i+10<=n;i++) {
        /* Memory-card folders commonly prefix B and suffix GAMEDATA:
           BESCES-50294GAMEDATA contains SCES-50294 at offset 2. */
        if (isalpha((unsigned char)dir[i]) && isalpha((unsigned char)dir[i+1]) &&
            isalpha((unsigned char)dir[i+2]) && isalpha((unsigned char)dir[i+3]) &&
            dir[i+4]=='-' &&
            isdigit((unsigned char)dir[i+5]) && isdigit((unsigned char)dir[i+6]) &&
            isdigit((unsigned char)dir[i+7]) && isdigit((unsigned char)dir[i+8]) &&
            isdigit((unsigned char)dir[i+9])) {
            for (int j=0;j<10;j++) out[j]=(char)toupper((unsigned char)dir[i+j]);
            out[10]=0;
            return 1;
        }
    }
    return 0;
}
int ps2_game_title_lookup(const char *database_path, const char *save_directory,
                          char *title_out, size_t cap) {
    FILE *f; char serial[11],line[1024];
    if (!database_path || !title_out || !cap) return -2;
    title_out[0]=0;
    if (!ps2_game_extract_serial(save_directory,serial)) return 0;
    f=fopen(database_path,"rb"); if(!f)return -1;
    while(fgets(line,sizeof(line),f)) {
        char *sep=strchr(line,'\t'); size_t len;
        if(!sep || (size_t)(sep-line)!=10 || strncmp(line,serial,10))continue;
        sep++;len=strcspn(sep,"\r\n");
        if(!len || len>=cap)continue;
        memcpy(title_out,sep,len);title_out[len]=0;fclose(f);return 1;
    }
    fclose(f);return 0;
}

/* Only report dates explicitly supplied with provenance; never infer them from
   the game title, serial prefix, or a release year of another region. */
int ps2_game_release_lookup(const char *database_path,const char *serial,
                            char *date_out,size_t date_cap,
                            char *precision_out,size_t precision_cap,
                            char *source_out,size_t source_cap){
    char line[2048];
    if(!database_path||!serial||!date_out||!precision_out||!source_out||
       !date_cap||!precision_cap||!source_cap)return -2;
    date_out[0]=precision_out[0]=source_out[0]=0;
    FILE *f=fopen(database_path,"rb");
    if(!f)return -1;
    while(fgets(line,sizeof(line),f)){
        char *field[8]={0};int count=0;
        if(line[0]=='#')continue;
        field[count++]=line;
        for(char *p=line;*p&&count<8;p++)if(*p=='\\t'){*p=0;field[count++]=p+1;}
        if(count!=8||strcmp(field[0],serial))continue;
        char *date=field[5],*precision=field[6],*source=field[7];
        source[strcspn(source,"\\r\\n")]=0;
        if(!date[0]||!source[0]||!strcmp(precision,"unknown"))continue;
        size_t n=strlen(date);
        if(n!=4&&n!=7&&n!=10)continue;
        int valid=1;
        for(size_t i=0;i<n;i++)if((i==4||(i==7&&n==10))?
            date[i]!='-':!isdigit((unsigned char)date[i]))valid=0;
        if(!valid||((n==4)&&strcmp(precision,"year"))||
           ((n==7)&&strcmp(precision,"month"))||
           ((n==10)&&strcmp(precision,"day")))continue;
        if(strlen(date)>=date_cap||strlen(precision)>=precision_cap||
           strlen(source)>=source_cap)continue;
        strcpy(date_out,date);strcpy(precision_out,precision);strcpy(source_out,source);
        fclose(f);return 1;
    }
    fclose(f);return 0;
}
