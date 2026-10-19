#ifndef PSU_FILEN_H
#define PSU_FILEN_H
#include <string>
#include <vector>
#include <stddef.h>
typedef std::vector<unsigned char> FilenBytes;
bool filen_parse_link(const char *url,char uuid[37],char key[257]);
bool filen_decrypt_metadata(const std::string &data,const std::string &key,FilenBytes &out,std::string &error);
bool filen_decrypt_chunk(const FilenBytes &data,const std::string &key,int version,FilenBytes &out,std::string &error);
/* Return nonzero to cancel. Callback also runs during metadata/chunk requests. */
typedef int (*FilenProgress)(void *,const char *,double,double);
typedef int (*FilenValidate)(const char *);
int filen_download(const char *url,const char *directory,const char *ca_bundle,
 FilenProgress progress,void *context,FilenValidate validate,char *saved,size_t saved_cap,char *error,size_t error_cap);
#endif
