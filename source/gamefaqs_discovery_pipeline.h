#ifndef GAMEFAQS_DISCOVERY_PIPELINE_H
#define GAMEFAQS_DISCOVERY_PIPELINE_H
#ifdef __cplusplus
extern "C" {
#endif
/* Process HTML already fetched from a trusted GameFAQs PS2 saves page.
 * detail_url == NULL: discover page links (unverified).
 * detail_url != NULL: extract explicit save-file links on that detail page.
 * Returns number of merged index records, or -1 on failure.
 * Does not fetch network pages or claim file availability. */
int gfs_ingest_html(const char *index_path,const char *html,const char *detail_url,
                    const char *game,const char *region,int maximum_entries);
#ifdef __cplusplus
}
#endif
#endif
