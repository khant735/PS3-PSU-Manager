#include "gamefaqs_curl_transport.h"
#include "gamefaqs_crawl.h"
#include "gamefaqs_discovery_pipeline.h"
#include "ps2_game_titles.h"
#include "ps2_game_db_update.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

#include <ppu-types.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <tiny3d.h>
#include <libfont.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>
#include <curl/curl.h>
#include "filen.h"

#include "ps2_background_480x270.h"
#include "ps2wiki_catalogue.h"
#include "ps2homebrew_catalogue.h"
#include "catalogue_action.h"
#include "gamefaqs_saves_index.h"
#include "gamefaqs_html_native.h"
#include "ui_text_layout.h"
#include "icn_texture.h"
#include "ca_bundle_path.h"
#include "zip_extract.h"
#include "ps2_archive_file_preview.h"
#include "ps2_max_to_psu.h"
#include "ps2_cbs_to_psu.h"
#include "ps2_sharkport_header.h"

extern "C" {
#include "psu.h"
}

SYS_PROCESS_PARAM(1001, 0x100000);

#define MAX_ITEMS 512
#define PATHSZ 1024
#define HOSTBUFFER_SIZE (1024*1024)
#define HOST_ADDR_ALIGNMENT (1024*1024)

typedef struct {
 char name[256];
 char path[PATHSZ];
 int is_dir;
 int size_known;
 unsigned long long size;
 int date_known;
 time_t latest_time;
} item_t;
static item_t items[MAX_ITEMS];
static int item_count=0, selected=0, top_index=0;
#define DIR_CACHE_SLOTS 8
typedef struct { int valid; char path[PATHSZ]; int count; item_t *items; } dir_cache_t;
static dir_cache_t g_dir_cache[DIR_CACHE_SLOTS]={{0}};
static int g_dir_cache_next=0;
static char cwd[PATHSZ]="/dev_hdd0";
static char clipboard[PATHSZ]="";
static char status_line[256]="Ready";
static char success_line[320]="";
static int success_frames=0;
static int running=1, archive_mode=0;
static int g_psu_metadata_active=0,g_psu_metadata_page=0;
/* v1.49 render consolidation: PSU Details data is loaded once on modal open. */
typedef struct { int valid; psu_entry_t root; int root_ok; unsigned char *icon_sys; size_t icon_sys_size; psu_entry_t entries[256]; int entry_count; } psu_meta_cache_t;
static psu_meta_cache_t g_psu_meta_cache={0};
static int g_options_active=0,g_options_selected=0,g_fps_enabled=1,g_download_connections=0;
/* 0 = PS2Wiki, 1 = PS2 Homebrew Store. */
static int g_psu_catalogue_source=0;
/* Phase 5Q: optional live PS2Wiki catalogue with offline cache. */
static int g_catalogue_active=0,g_catalogue_category=0,g_catalogue_selected=0,g_catalogue_top_index=0;
static int g_homebrew_category=0,g_homebrew_selected=0,g_homebrew_top=0,g_homebrew_view=0,g_homebrew_asset_selected=0,g_homebrew_asset_top=0,g_homebrew_selected_entry=-1;
static int g_catalogue_verify_psu=0;
static int g_homebrew_active=0;
/* Optional, externally maintained GameFAQs save listing. No site crawling. */
static int g_saves_active=0,g_saves_level=0,g_saves_bucket=0,g_saves_selected=0,g_saves_top=0,g_saves_game_index=-1,g_saves_region_index=-1;
static gfs_save_t g_saves_entries[GFS_MAX_ENTRIES];
static int g_saves_count=0;
static const char *g_saves_index_path="/dev_hdd0/game/PSU000001/USRDIR/GAMEFAQS_SAVES.tsv";

static int g_homebrew_validate_kind=0;
static int g_downloader_active=0;
static int g_download_progress_active=0,g_download_cancel_requested=0,g_download_paused=0;
static double g_download_now=0.0,g_download_total=0.0,g_download_resume_base=0.0;
static char g_download_name[256]="";
static char g_download_url[1024]="";
static unsigned long long g_download_last_draw_us=0,g_download_start_us=0;
static double g_download_speed_bps=0.0,g_download_avg_speed_bps=0.0,g_download_high_speed_bps=0.0,g_download_low_speed_bps=0.0,g_download_elapsed=0.0,g_download_remaining=0.0;
/* Bytes already present when the current network session started.  This is
   distinct from .part's contiguous prefix because persisted .seg files may
   already contain most of the file. */
static double g_download_session_base=0.0;
static double g_download_sample_bytes=0.0; static unsigned long long g_download_sample_us=0; static int g_download_completed=0,g_download_finalizing=0;
static int g_download_active_connections=1;
static double g_conn_now[16]={0},g_conn_total[16]={0},g_conn_speed_bps[16]={0},g_conn_last_bytes[16]={0};
static unsigned long long g_conn_sample_us[16]={0};
static int g_conn_state[16]={0};
static int g_net_initialized=0;
static const char *g_settings_path="/dev_hdd0/game/PSU000001/USRDIR/settings.ini";

static void load_settings(){
 FILE *f=fopen(g_settings_path,"rb"); if(!f)return;
 char line[128];
 while(fgets(line,sizeof(line),f)){
  int v; if(sscanf(line,"fps_meter=%d",&v)==1)g_fps_enabled=v?1:0;
  else if(sscanf(line,"download_connections=%d",&v)==1){if(v<0)v=0;if(v>16)v=16;g_download_connections=v;}
 }
 fclose(f);
}
static int save_settings(){
 FILE *f=fopen(g_settings_path,"wb"); if(!f)return -1;
 fprintf(f,"# PS2 .psu Save Manager settings\n");
 fprintf(f,"fps_meter=%d\n",g_fps_enabled?1:0);
 fprintf(f,"download_connections=%d\n",g_download_connections);
 fclose(f); return 0;
}

static volatile int g_system_menu_active=0;
static volatile int g_system_menu_resume_pending=0;
static int g_startup_overlay_cleanup_done=0;
static int export_target_active=0;
static char archive_path[PATHSZ]="";
static int g_foreign_archive_view=0;
static char g_foreign_view_path[PATHSZ]="";
static char g_foreign_source_path[PATHSZ]="";

static gcmContextData *g_ctx=NULL;
static u32 *g_fb[2]={NULL,NULL};
static u32 *g_bg_cache=NULL;
static int g_bg_cache_ready=0;
static u32 *g_model_cache=NULL;
static u32 *g_model_filter_cache=NULL;
static float *g_model_zbuf=NULL;
static int g_model_cache_w=0,g_model_cache_h=0;
static int g_model_filter_w=0,g_model_filter_h=0;
static int g_model_cache_valid=0;
static int g_rename_isolated_model_raster=0;
static int g_model_cache_frame=-1000;
/* First-open preview stabilizer: hold one fully rendered model image on both
   framebuffers for a handful of presents before starting rotation. This hides
   cache/buffer warm-up on real PS3 without changing the ICN decoder. */
static int g_preview_startup_hold=0;
static int g_preview_rotation_start_frame=0;
/* Per-buffer ICN preview generation. Never write the newly rendered model into
   g_back^1: on real PS3 that buffer may still be the live scanout buffer. */
static int g_model_generation=1;
static int g_model_buffer_generation[2]={0,0};
/* Full-frame generation tracking. Changed frames are published only to the inactive
   backbuffer; the other buffer is synchronised on its next safe turn. */
static unsigned int g_ui_generation=0;
static unsigned int g_ui_buffer_generation[2]={0,0};
static u32 *g_model_base_cache=NULL;
static int g_model_base_valid=0;
static int g_model_base_x=0,g_model_base_y=0,g_model_base_w=0,g_model_base_h=0;
static int *g_model_sx=NULL,*g_model_sy=NULL;
static float *g_model_zinv=NULL,*g_model_nx=NULL,*g_model_ny=NULL;
static int g_model_vertex_cap=0;
static u32 g_fb_off[2]={0,0};
static u32 g_width=1280,g_height=720,g_pitch=0;
static int g_back=0;
static int g_title_scale=2, g_body_scale=1, g_help_scale=1;
static int g_margin=24, g_title_x=24, g_title_y=20, g_path_x=24, g_path_y=52, g_list_x=28, g_list_y=78, g_row_h=18;

typedef struct {
 int valid;
 char archive[PATHSZ];
 char entry[256];
 char info[128];
 int vertex_count;
 int shape_count;
 int face_count;
 float *positions;
 float *normals; /* one decoded ICN normal per vertex */
 float *uvs;
 u32 *colors;
 u32 *texture;
 u32 *texture_mip64;
 int texture_valid;
 int anim_track_count;
 float anim_length;
 float *anim_keys; /* [shape][64 keys][time,weight], allocated as 16*64*2 */
 unsigned short anim_counts[16];
 float center[3];
 float scale;
} icon_preview_t;
static icon_preview_t g_preview={0};
/* v1.47: decoded multi-ICN cache for the currently opened PSU.  Slots own the
   same parsed geometry/texture allocations as g_preview; switching is a move,
   not a reparse or texture copy. */
#define ICN_CACHE_SLOTS 3
typedef struct { int used; unsigned long long age; icon_preview_t preview; } icn_cache_slot_t;
static icn_cache_slot_t g_icn_cache[ICN_CACHE_SLOTS];
static unsigned long long g_icn_cache_clock=1;
static int endswith(const char *s,const char *x);
/* v1.47: ICON.SYS is authoritative for the standard PS2 save icon set.
   Offsets 260/324/388 are the 64-byte view/copy/delete filenames. */
static char g_iconsys_icn_refs[3][65];
static int g_iconsys_icn_ref_count=0;
static int g_iconsys_refs_valid=0;
/* v1.49 ICN renderer audit: icon.sys supplies the PS2 Browser lighting rig.
   Keep it beside the icon references so cached ICNs can use the parent save's
   three directional lights and ambient term without reparsing icon.sys. */
static int g_iconsys_lighting_valid=0;
static float g_iconsys_light_dir[3][3];
static float g_iconsys_light_col[3][3];
static float g_iconsys_ambient[3];
static float rd_f32le_local(const unsigned char *p){ u32 u=(u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); float f; memcpy(&f,&u,4); return f; }

static void clear_iconsys_icn_refs(){
 memset(g_iconsys_icn_refs,0,sizeof(g_iconsys_icn_refs));
 g_iconsys_icn_ref_count=0;g_iconsys_refs_valid=0;g_iconsys_lighting_valid=0;
 memset(g_iconsys_light_dir,0,sizeof(g_iconsys_light_dir)); memset(g_iconsys_light_col,0,sizeof(g_iconsys_light_col)); memset(g_iconsys_ambient,0,sizeof(g_iconsys_ambient));
}
/* PS2 icon models are named by icon.sys; the extension is not fixed.
   Some commercial saves use icon0.ico for a PS2 3D model. */
static int ps2_icon_model_name(const char *name){
 return name && (endswith(name,".icn") || endswith(name,".ico"));
}
static int iconsys_ref_index(const char *name){
 if(!name||!name[0])return -1;
 for(int i=0;i<g_iconsys_icn_ref_count;i++)if(!strcasecmp(name,g_iconsys_icn_refs[i]))return i;
 return -1;
}
static void load_iconsys_icn_refs(){
 clear_iconsys_icn_refs();
 if(!archive_mode||!archive_path[0])return;
 void *buf=NULL;size_t size=0;
 if(psu_read_file(archive_path,"icon.sys",&buf,&size)!=0||!buf||size<452){free(buf);return;}
 const unsigned char *b=(const unsigned char*)buf;
 if(memcmp(b,"PS2D",4)!=0){free(buf);return;}
 /* mcIcon layout from PS2SDK: lightDir @ 80, lightCol @ 128, ambient @ 176. */
 if(size>=192){
  int finite_ok=1;
  for(int l=0;l<3;l++)for(int c=0;c<3;c++){
   float d=rd_f32le_local(b+80+l*16+c*4), k=rd_f32le_local(b+128+l*16+c*4);
   if(!isfinite(d)||!isfinite(k)){finite_ok=0;d=0.0f;k=0.0f;}
   g_iconsys_light_dir[l][c]=d; g_iconsys_light_col[l][c]=k;
  }
  for(int c=0;c<3;c++){float a=rd_f32le_local(b+176+c*4);if(!isfinite(a)){finite_ok=0;a=1.0f;}g_iconsys_ambient[c]=a;}
  g_iconsys_lighting_valid=finite_ok;
 }
 const int off[3]={260,324,388};
 for(int k=0;k<3;k++){
  char n[65];memcpy(n,b+off[k],64);n[64]=0;
  size_t z=strnlen(n,64);n[z]=0;
  if(!n[0]||!ps2_icon_model_name(n)||iconsys_ref_index(n)>=0)continue;
  snprintf(g_iconsys_icn_refs[g_iconsys_icn_ref_count],65,"%s",n);
  if(++g_iconsys_icn_ref_count>=3)break;
 }
 g_iconsys_refs_valid=1;
 free(buf);
}
static int preview_entry_is_standard_icn(const char *name){
 if(!name||!ps2_icon_model_name(name))return 0;
 /* Compatibility fallback only when ICON.SYS is absent/malformed. */
 if(!g_iconsys_refs_valid)return endswith(name,".icn");
 return iconsys_ref_index(name)>=0;
}
static int g_frame_counter=0;
static unsigned long long g_fps_window_start_us=0;
static int g_fps_window_frames=0;
static float g_fps_value=0.0f;
/* Phase 4J real-hardware frame pacing instrumentation. Values are exponential
   moving averages in milliseconds and are shown only when the FPS meter is on. */
static float g_perf_draw_ms=0.0f, g_perf_flip_ms=0.0f, g_perf_frame_ms=0.0f;
static unsigned long long g_perf_draw_begin_us=0, g_perf_last_present_us=0;
static int g_perf_samples=0;
/* Phase 4K: split the pre-flip DRAW time into major RSX/UI stages so the real
   PS3 can identify the actual ~33 ms hotspot without changing rendering. */
static float g_perf_bg_ms=0.0f,g_perf_head_ms=0.0f,g_perf_list_ms=0.0f,g_perf_preview_ms=0.0f,g_perf_footer_ms=0.0f,g_perf_modal_ms=0.0f,g_perf_flush_ms=0.0f;
/* Phase 4L: split FOOT and count Tiny3D batch flushes. */
static float g_perf_foot_panel_ms=0.0f,g_perf_foot_legend_ms=0.0f,g_perf_foot_diag_ms=0.0f,g_perf_foot_status_ms=0.0f;
/* Phase 4N: real-hardware presentation cadence audit. */
static volatile unsigned int g_vblank_count=0;
static unsigned int g_perf_vblank_last=0,g_perf_vblank_window_count=0;
static unsigned long long g_perf_vblank_window_us=0;
static float g_perf_vblanks_per_frame=0.0f,g_perf_vblank_hz=0.0f;
static int g_perf_flip_status_before=0;
static unsigned long long fps_now_us();
/* Phase 4Y: passive instrumentation of the known-good 4N two-buffer path.
   Linker --wrap hooks timestamp the actual GCM calls made by Tiny3D without
   changing their order, arguments, return values, flip mode or framebuffer ownership. */
static volatile int g_4x_probe_active=0;
static unsigned long long g_4x_flip_enter_us=0,g_4x_first_status_us=0,g_4x_reset_us=0;
static unsigned long long g_4x_queue_begin_us=0,g_4x_queue_end_us=0,g_4x_waitcmd_begin_us=0,g_4x_waitcmd_end_us=0;
static unsigned int g_4x_status_polls=0;
/* Phase 5K: exact pre-flip waitFlip polling diagnostics. */
static unsigned int g_5k_poll_nonzero=0;
static u32 g_5k_poll_first=0,g_5k_poll_last=0;
static unsigned long long g_5k_poll_first_us=0,g_5k_poll_last_us=0;
/* Phase 5L: correlate flip submission/wait command and next waitFlip with the
   hardware VBlank counter. Diagnostic only; synchronization is unchanged. */
static unsigned long long g_5l_vb_queue=0,g_5l_vb_waitcmd=0,g_5l_vb_next_entry=0,g_5l_vb_zero=0;
static int g_5l_seen_first_poll=0;
/* Phase 5N: correlate an actually executed flip with the queue that produced it.
   The flip handler is passive: it records the VBlank and current display-buffer
   id only.  Pending queue metadata is copied into the report when that flip is
   executed, so Q/H refer to the same submitted frame instead of adjacent frames. */
static volatile u32 g_5n_flip_mode=0;
static volatile unsigned int g_5n_flip_events=0;
static volatile unsigned long long g_5n_pending_q_vb=0,g_5n_pending_w_vb=0;
static volatile u8 g_5n_pending_q_id=0xFF,g_5n_pending_disp_q=0xFF,g_5n_pending_disp_w=0xFF;
static volatile unsigned long long g_5n_rep_q_vb=0,g_5n_rep_w_vb=0,g_5n_rep_h_vb=0;
static volatile u8 g_5n_rep_q_id=0xFF,g_5n_rep_disp_q=0xFF,g_5n_rep_disp_w=0xFF;
static volatile u8 g_5n_rep_disp_h=0xFF,g_5n_rep_render_h=0xFF,g_5n_rep_same=0;
static volatile u8 g_5n_disp_n=0xFF,g_5n_disp_z=0xFF;
static float g_4x_end_ms=0.0f,g_4x_poll_ms=0.0f,g_4x_queue_ms=0.0f,g_4x_flushgap_ms=0.0f,g_4x_waitcmd_ms=0.0f,g_4x_tail_ms=0.0f;
static int g_4x_queue_rc=0;
static unsigned long long g_4y_space_begin_us=0,g_4y_space_end_us=0,g_4y_target_begin_us=0,g_4y_target_end_us=0,g_4y_cb_begin_us=0,g_4y_cb_end_us=0;
static float g_4y_space_ms=0.0f,g_4y_target_ms=0.0f,g_4y_callback_ms=0.0f,g_4y_shader_ms=0.0f;
/* Phase 5A: record every MakeCmdSpace occurrence. 4Y only retained the last
   call, which can hide an earlier blocking 16 KiB allocation behind the
   later fast shader-context 4 KiB allocation. */
static unsigned long long g_5a_space_beg[8]={0},g_5a_space_end[8]={0};
static u32 g_5a_space_bytes[8]={0};
static unsigned int g_5a_space_count=0;
static float g_5a_space1_ms=0.0f,g_5a_space2_ms=0.0f,g_5a_space3_ms=0.0f;
static u32 g_5a_space1_bytes=0,g_5a_space2_bytes=0,g_5a_space3_bytes=0;
/* Phase 5E: measure how much command data is actually emitted after the
   expensive 16 KiB reservation and before the following reservation. */
static u32 g_5e_space_off_beg[8]={0},g_5e_space_off_end[8]={0};
static u32 g_5e_between_bytes=0,g_5e_s1_inside_bytes=0,g_5e_s2_inside_bytes=0;
/* Phase 5F: distinguish command-buffer relocation performed by MakeCmdSpace
   from commands actually emitted after it. 5E's USE field compared offsets
   across the callback and therefore measured the callback's new current
   position, not 17.9 KiB of generated commands. */
static u32 g_5f_target_off0=0,g_5f_target_off1=0,g_5f_callback_off0=0,g_5f_callback_off1=0;
static u32 g_5f_post_s1_to_target=0,g_5f_target_bytes=0,g_5f_callback_bytes=0,g_5f_after_callback_bytes=0;
static inline u32 phase5e_ctx_off(void *vp){gcmContextData *c=(gcmContextData*)vp;u32 o=0;if(c&&c->current&&gcmAddressToOffset(c->current,&o)==0)return o;return 0;}
static inline u32 phase5e_forward_delta(u32 a,u32 b){return b>=a?b-a:0;}
/* Phase 5B: inspect the command FIFO callback behind the slow 16 KiB request. */
static unsigned int g_5b_cb_count=0;
static unsigned long long g_5b_cb_us=0;
static u32 g_5b_get0=0,g_5b_put0=0,g_5b_get1=0,g_5b_put1=0;
static u32 g_5b_cur_off0=0,g_5b_cur_off1=0,g_5b_linear_free0=0,g_5b_linear_free1=0;
static u32 g_5b_cb_arg=0;
/* Phase 5G: sample the RSX control register from a second PPU thread while the
   original, flicker-free GCM context callback is blocked. This does not alter
   the callback, command stream, VSYNC or framebuffer ownership. */
static volatile int g_5g_sample_active=0,g_5g_sampler_stop=0;
static volatile unsigned int g_5g_samples=0,g_5g_get_changes=0,g_5g_put_changes=0;
static volatile u32 g_5g_get_first=0,g_5g_get_last=0,g_5g_put_first=0,g_5g_put_last=0;
static volatile unsigned long long g_5g_start_us=0,g_5g_first_get_change_us=0,g_5g_last_get_change_us=0,g_5g_first_put_change_us=0;
static sys_ppu_thread_t g_5g_thread=0;
static void phase5g_sampler(void*){
 u32 pg=0,pp=0;int have=0;
 while(!g_5g_sampler_stop){
  if(g_5g_sample_active){
   gcmControlRegister *cr=gcmGetControlRegister();
   if(cr){u32 cg=cr->get,cp=cr->put;unsigned long long now=fps_now_us();
    if(!have){pg=cg;pp=cp;g_5g_get_first=cg;g_5g_put_first=cp;have=1;}
    if(cg!=pg){g_5g_get_changes++;if(!g_5g_first_get_change_us)g_5g_first_get_change_us=now;if(g_5g_start_us)g_5g_last_get_change_us=now;pg=cg;}
    if(cp!=pp){g_5g_put_changes++;if(!g_5g_first_put_change_us)g_5g_first_put_change_us=now;pp=cp;}
    g_5g_get_last=cg;g_5g_put_last=cp;g_5g_samples++;
   }
   usleep(100);
  }else{have=0;usleep(200);}
 }
 sysThreadExit(0);
}
/* Phase 5C: command-buffer consumption by major draw stage.  This is passive:
   it only samples context->current and counts any context callbacks that occur
   while constructing the frame. */
static u32 g_5c_off[8]={0};
static u32 g_5c_cb_count[7]={0};
static unsigned long long g_5c_cb_us[7]={0};
static volatile int g_5c_draw_active=0;
static volatile int g_5c_stage=0;
static inline u32 phase5c_cmd_off(){u32 o=0;if(g_ctx&&g_ctx->current&&gcmAddressToOffset(g_ctx->current,&o)==0)return o;return 0;}
static inline u32 phase5c_delta(u32 a,u32 b){return b>=a?b-a:0;}


/* Keep Tiny3D's original 64 KiB command FIFO. The 256 KiB Phase 5H increase
   was not present in the user-confirmed 4N stable build and is reverted here
   while retaining passive FIFO diagnostics. */
static u32 g_5h_cmd_requested=0,g_5h_cmd_effective=0;
extern "C" void *__real_rsxtiny_Init(u32,u32,const void*);
extern "C" void *__wrap_rsxtiny_Init(u32 cmdSize,u32 ioSize,const void *ioAddress){
 g_5h_cmd_requested=cmdSize;
 u32 effective=cmdSize;
 g_5h_cmd_effective=effective;
 return __real_rsxtiny_Init(effective,ioSize,ioAddress);
}

extern "C" u32 __real_gcmGetFlipStatus(void);
extern "C" void __real_gcmResetFlipStatus(void);
extern "C" s32 __real_gcmSetFlip(gcmContextData*,const u8);
extern "C" void __real_gcmSetWaitFlip(gcmContextData*);
extern "C" void __real_gcmSetFlipMode(const u32);
static inline u8 phase5n_display_id(){u8 id=0xFF;return gcmGetCurrentDisplayBufferId(&id)==0?id:0xFF;}
extern "C" void __wrap_gcmSetFlipMode(const u32 mode){g_5n_flip_mode=mode;__real_gcmSetFlipMode(mode);}
extern "C" u32 __wrap_gcmGetFlipStatus(void){
 unsigned long long t=fps_now_us();
 u32 st=__real_gcmGetFlipStatus();
 if(g_4x_probe_active){
  if(!g_4x_first_status_us){g_4x_first_status_us=t;g_5k_poll_first=st;g_5k_poll_first_us=t;g_5l_vb_next_entry=gcmGetVBlankCount();g_5l_seen_first_poll=1;g_5n_disp_n=phase5n_display_id();}
  if(st==0 && !g_5l_vb_zero){g_5l_vb_zero=gcmGetVBlankCount();g_5n_disp_z=phase5n_display_id();}
  g_4x_status_polls++; g_5k_poll_last=st; g_5k_poll_last_us=t;
  if(st!=0)g_5k_poll_nonzero++;
 }
 return st;
}
extern "C" void __wrap_gcmResetFlipStatus(void){
 __real_gcmResetFlipStatus(); if(g_4x_probe_active)g_4x_reset_us=fps_now_us();
}
extern "C" s32 __wrap_gcmSetFlip(gcmContextData *c,const u8 b){
 if(g_4x_probe_active)g_4x_queue_begin_us=fps_now_us();
 u8 dq=phase5n_display_id();
 s32 r=__real_gcmSetFlip(c,b);
 if(g_4x_probe_active){
  g_4x_queue_end_us=fps_now_us();g_4x_queue_rc=(int)r;g_5l_vb_queue=gcmGetVBlankCount();
  if(r==0){g_5n_pending_q_id=b;g_5n_pending_q_vb=g_5l_vb_queue;g_5n_pending_disp_q=dq;}
 } return r;
}
extern "C" void __wrap_gcmSetWaitFlip(gcmContextData *c){
 if(g_4x_probe_active)g_4x_waitcmd_begin_us=fps_now_us();
 __real_gcmSetWaitFlip(c);
 if(g_4x_probe_active){
  g_4x_waitcmd_end_us=fps_now_us();g_5l_vb_waitcmd=gcmGetVBlankCount();
  g_5n_pending_w_vb=g_5l_vb_waitcmd;g_5n_pending_disp_w=phase5n_display_id();
 }
}
extern "C" s32 __real_tiny_rsxContextCallback(gcmContextData*,u32);
extern "C" s32 __wrap_tiny_rsxContextCallback(gcmContextData *c,u32 count){
 unsigned long long b=0; gcmControlRegister *cr=NULL;
 unsigned long long draw_b=0; int draw_stage=-1;
 if(g_5c_draw_active){draw_b=fps_now_us();draw_stage=g_5c_stage;if(draw_stage>=0&&draw_stage<7)g_5c_cb_count[draw_stage]++;}
 if(g_4x_probe_active){
  b=fps_now_us(); g_5b_cb_count++; g_5b_cb_arg=count; cr=gcmGetControlRegister();
  if(g_5b_cb_count==1){
   g_5g_samples=g_5g_get_changes=g_5g_put_changes=0;g_5g_first_get_change_us=g_5g_last_get_change_us=g_5g_first_put_change_us=0;g_5g_start_us=b;g_5g_sample_active=1;
   if(cr){g_5b_put0=cr->put;g_5b_get0=cr->get;}
   u32 off=0;if(gcmAddressToOffset(c->current,&off)==0)g_5b_cur_off0=off;
   g_5b_linear_free0=(c->end>=c->current)?(u32)((c->end-c->current)*4):0;
  }
 }
 s32 r=__real_tiny_rsxContextCallback(c,count);
 if(draw_b&&draw_stage>=0&&draw_stage<7)g_5c_cb_us[draw_stage]+=fps_now_us()-draw_b;
 if(g_4x_probe_active){
  unsigned long long e=fps_now_us();g_5b_cb_us+=(e-b);cr=gcmGetControlRegister();
  if(g_5b_cb_count==1){g_5g_sample_active=0;
   if(cr){g_5b_put1=cr->put;g_5b_get1=cr->get;}
   u32 off=0;if(gcmAddressToOffset(c->current,&off)==0)g_5b_cur_off1=off;
   g_5b_linear_free1=(c->end>=c->current)?(u32)((c->end-c->current)*4):0;
  }
 }
 return r;
}
extern "C" void __real_rsxtiny_MakeCmdSpace(void*,u32);
extern "C" void __wrap_rsxtiny_MakeCmdSpace(void *c,u32 space){
 unsigned int slot=0xFFFFFFFFu;
 if(g_4x_probe_active){
  g_4y_space_begin_us=fps_now_us();
  slot=g_5a_space_count++;
  if(slot<8){g_5a_space_beg[slot]=g_4y_space_begin_us;g_5a_space_bytes[slot]=space;g_5e_space_off_beg[slot]=phase5e_ctx_off(c);}
 }
 __real_rsxtiny_MakeCmdSpace(c,space);
 if(g_4x_probe_active){
  g_4y_space_end_us=fps_now_us();
  if(slot<8){g_5a_space_end[slot]=g_4y_space_end_us;g_5e_space_off_end[slot]=phase5e_ctx_off(c);}
 }
}
extern "C" void __real_setupRenderTarget(int);
extern "C" void __wrap_setupRenderTarget(int b){
 if(g_4x_probe_active){g_4y_target_begin_us=fps_now_us();g_5f_target_off0=phase5e_ctx_off(g_ctx);}
 __real_setupRenderTarget(b);
 if(g_4x_probe_active){g_4y_target_end_us=fps_now_us();g_5f_target_off1=phase5e_ctx_off(g_ctx);}
}
extern "C" s32 __real_sysUtilCheckCallback(void);
extern "C" s32 __wrap_sysUtilCheckCallback(void){
 if(g_4x_probe_active){g_4y_cb_begin_us=fps_now_us();g_5f_callback_off0=phase5e_ctx_off(g_ctx);}
 s32 r=__real_sysUtilCheckCallback();
 if(g_4x_probe_active){g_4y_cb_end_us=fps_now_us();g_5f_callback_off1=phase5e_ctx_off(g_ctx);}
 return r;
}
static void phase4n_vblank_handler(const u32 head){(void)head;g_vblank_count++;}
static void phase5n_flip_handler(const u32 head){
 (void)head;
 g_5n_flip_events++;
 g_5n_rep_q_id=g_5n_pending_q_id;
 g_5n_rep_q_vb=g_5n_pending_q_vb;
 g_5n_rep_w_vb=g_5n_pending_w_vb;
 g_5n_rep_disp_q=g_5n_pending_disp_q;
 g_5n_rep_disp_w=g_5n_pending_disp_w;
 g_5n_rep_h_vb=gcmGetVBlankCount();
 g_5n_rep_disp_h=phase5n_display_id();
 g_5n_rep_render_h=(u8)Video_currentBuffer;
 g_5n_rep_same=(g_5n_rep_disp_h!=0xFF && g_5n_rep_disp_h==g_5n_rep_render_h)?1:0;
}

static int g_perf_text_flushes=0,g_perf_rect_flushes=0,g_perf_line_submits=0;
static float perf_ms(unsigned long long a,unsigned long long b){return b>=a?(float)(b-a)/1000.0f:0.0f;}
static void perf_avg(float *dst,float v){const float a=(g_perf_samples<8)?0.35f:0.10f; if(!g_perf_samples)*dst=v; else *dst+=(v-*dst)*a;}
static int g_ui_dirty=1;
static int g_list_dirty=0;
static u32 *g_list_base_cache=NULL;
static u32 *g_list_panel_cache=NULL;
static int g_list_panel_cache_valid=0;
/* Persistent allocated dimensions for g_list_panel_cache.  These MUST track
   the actual malloc size; using temporary variables initialized from the new
   panel size can hide a size change and make capture_region() overrun an old
   allocation during folder/device switches. */
static int g_list_panel_cache_alloc_w=0,g_list_panel_cache_alloc_h=0;
static int g_list_panel_x=0,g_list_panel_y=0,g_list_panel_w=0,g_list_panel_h=0;
static u32 *g_path_base_cache=NULL;
static int g_path_base_valid=0;
static int g_path_cache_x=0,g_path_cache_y=0,g_path_cache_w=0,g_path_cache_h=0;
static int g_folder_dirty=0;
/* Device switches must not reuse the previous device's framebuffer/list state.
   Force two independent full redraws so each backbuffer is rebuilt safely. */
static int g_device_switch_redraw_frames=0;
static int g_list_cache_valid=0;
static int g_list_cache_x=0,g_list_cache_y=0,g_list_cache_w=0,g_list_cache_h=0;
static int g_list_cache_right=0,g_list_cache_rows_y=0,g_list_cache_visible=0,g_list_cache_max_visible=0;
static int g_list_prev_selected=-1,g_list_prev_top=-1;
static int g_metadata_dirty_index=-1;
static u32 *g_preview_base_cache=NULL;
static int g_preview_base_valid=0;
static int g_preview_cache_x=0,g_preview_cache_y=0,g_preview_cache_w=0,g_preview_cache_h=0;
static int g_rename_active=0;
enum { KB_RENAME=0, KB_NEW_PSU_FILENAME=1, KB_NEW_PSU_FOLDER=2, KB_NEW_PSU_GAME_NAME=3, KB_NEW_PSU_SAVE_NAME=4, KB_NEW_PSU_TEXT=5, KB_NEW_PSU_ICON=6, KB_DOWNLOAD_URL=7 };
static int g_keyboard_purpose=KB_RENAME;
static char g_blank_psu_base[PSU_NAME_MAX+1]="";
static char g_blank_save_folder[PSU_NAME_MAX+1]="";
static char g_blank_game_name[64]="";
static char g_blank_save_name[64]="";
static char g_blank_icon_text[64]="";
static char g_rename_text[1024]="";
static char g_rename_original[256]="";
static int g_rename_key=0;
static size_t g_rename_cursor=0; /* insertion caret: byte/ASCII character position */
static int g_rename_generation=1;
static int g_rename_buffer_generation[2]={0,0};
/* Per-buffer modal state: once the full keyboard is present in a buffer,
   ordinary cursor/text edits patch only the small regions that changed. */
static int g_rename_buffer_ready[2]={0,0};
static int g_rename_buffer_key[2]={-1,-1};
static int g_rename_text_generation=1;
static int g_rename_buffer_text_generation[2]={0,0};
static int g_rename_restore_frames=0;
static u32 *g_rename_base_cache=NULL;
static int g_rename_cache_x=0,g_rename_cache_y=0,g_rename_cache_w=0,g_rename_cache_h=0;
/* Finished rename modal pixels are precomposed off-screen once per edit
   generation.  Each display buffer receives the same completed rectangle
   only while it is the inactive backbuffer.  This prevents the first-open
   frame from showing a partially drawn keyboard or mismatched buffer. */
static u32 *g_rename_composite_cache=NULL;
static int g_rename_composite_w=0,g_rename_composite_h=0;
static int g_rename_composite_generation=0;
/* v1.46.1: pristine keyboard card with no selected key.  Old key cells are
   restored from this opaque snapshot instead of alpha-painting over the
   previous highlight, which left a selection trail on real hardware. */
static u32 *g_rename_pristine_cache=NULL;
static int g_rename_pristine_w=0,g_rename_pristine_h=0;
static int g_delete_confirm_active=0;
static int g_delete_confirm_archive=0;
static int g_delete_confirm_is_dir=0;
static char g_delete_confirm_name[256]="";
static char g_delete_confirm_target[PATHSZ]="";
static char g_delete_confirm_archive_path[PATHSZ]="";
static int g_zip_confirm_active=0;
static unsigned int g_zip_confirm_conflicts=0;
static char g_zip_confirm_archive[PATHSZ]="";
static char g_zip_confirm_destination[PATHSZ]="";
static int g_apollo_warning_active=0;
static int g_apollo_warning_auto_failed=0;
static const char *g_apollo_savedata_path="/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA";
/* Persist the user's one-time opt-in outside /dev_hdd0/tmp so RPCS3 may
   wipe fakeusb without making the app ask on every launch. */
static const char g_rename_keys_upper[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-()[]";
static const char g_rename_keys_lower[]="abcdefghijklmnopqrstuvwxyz0123456789._-()[]";
static const char g_url_keys_upper[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/._-?&=%#+~@()[]";
static const char g_url_keys_lower[]="abcdefghijklmnopqrstuvwxyz0123456789:/._-?&=%#+~@()[]";
static int g_rename_lowercase=0;
#define RENAME_COLS 10
static int rename_char_key_count(){return (int)strlen(g_keyboard_purpose==KB_DOWNLOAD_URL?g_url_keys_upper:g_rename_keys_upper);}
static int rename_key_count(){return rename_char_key_count()+1;} /* final key toggles ABC/abc */
static char rename_key_char(int i){const char *set=(g_keyboard_purpose==KB_DOWNLOAD_URL)?(g_rename_lowercase?g_url_keys_lower:g_url_keys_upper):(g_rename_lowercase?g_rename_keys_lower:g_rename_keys_upper);return (i>=0&&i<rename_char_key_count())?set[i]:0;}

static int scale125(int n){ return (n * 5 + 2) / 4; }

static int endswith(const char *s,const char *x);
static void line(int x0,int y0,int x1,int y1,u32 col);
static void draw_circle_outline(int cx,int cy,int r,u32 col,int thick);
static void draw_disc(int cx,int cy,int r,u32 col);
static int model_update_interval();
static void draw_rename_keyboard();
static void rename_keyboard_geometry(int*,int*,int*,int*,int*,int*,int*);
static void draw_rename_key_cell(int key_index,int selected_state);
static void draw_rename_text_field();
static void rename_patch_key_selection(int old_key,int new_key);
static int draw_rename_modal_update();
static void draw_delete_confirmation();
static void draw_zip_overwrite_confirmation();
static void draw_options_menu();
static void draw_psu_catalogue();
static void draw_homebrew_catalogue();
static void draw_saves_catalogue();
static void saves_load_index();
static void homebrew_catalogue_fetch_category(int category);
static void homebrew_catalogue_fetch_assets(int entry_index);
static void homebrew_catalogue_load_cache();
static void draw_apollo_warning();
static int create_apollo_savedata_tree();

static void configure_ui_metrics(){
 if(g_height >= 1080 || g_width >= 1600){ g_title_scale=3; g_body_scale=2; g_help_scale=2; }
 else if(g_height >= 720 || g_width >= 1200){ g_title_scale=2; g_body_scale=1; g_help_scale=1; }
 else { g_title_scale=1; g_body_scale=1; g_help_scale=1; }
 g_margin = (g_body_scale > 1) ? 40 : 28;
 g_title_x = g_margin;
 g_title_y = (g_body_scale > 1) ? 28 : 22;
 g_path_x = g_margin;
 g_path_y = g_title_y + scale125(8 * g_title_scale) + ((g_body_scale > 1) ? 20 : 18);
 g_list_x = g_margin + 4;
 g_list_y = g_path_y + scale125(8 * g_body_scale) + ((g_body_scale > 1) ? 24 : 20);
 g_row_h = 144; /* Stage 32: fixed 128x128 output-pixel icons at every resolution */
}

static const u8 *glyph(char c){
 static const u8 blank[7]={0,0,0,0,0,0,0};
 static const u8 unk[7]={31,17,2,4,4,0,4};
 static const u8 A[7]={14,17,17,31,17,17,17},B[7]={30,17,17,30,17,17,30},C[7]={14,17,16,16,16,17,14};
 static const u8 D[7]={30,17,17,17,17,17,30},E[7]={31,16,16,30,16,16,31},F[7]={31,16,16,30,16,16,16};
 static const u8 G[7]={14,17,16,23,17,17,15},H[7]={17,17,17,31,17,17,17},I[7]={14,4,4,4,4,4,14};
 static const u8 J[7]={7,2,2,2,18,18,12},K[7]={17,18,20,24,20,18,17},L[7]={16,16,16,16,16,16,31};
 static const u8 M[7]={17,27,21,21,17,17,17},N[7]={17,25,21,19,17,17,17},O[7]={14,17,17,17,17,17,14};
 static const u8 P[7]={30,17,17,30,16,16,16},Q[7]={14,17,17,17,21,18,13},R[7]={30,17,17,30,20,18,17};
 static const u8 S[7]={15,16,16,14,1,1,30},T[7]={31,4,4,4,4,4,4},U[7]={17,17,17,17,17,17,14};
 static const u8 V[7]={17,17,17,17,17,10,4},W[7]={17,17,17,21,21,21,10},X[7]={17,17,10,4,10,17,17};
 static const u8 Y[7]={17,17,10,4,4,4,4},Z[7]={31,1,2,4,8,16,31};
 static const u8 n0[7]={14,17,19,21,25,17,14},n1[7]={4,12,4,4,4,4,14},n2[7]={14,17,1,2,4,8,31};
 static const u8 n3[7]={30,1,1,14,1,1,30},n4[7]={2,6,10,18,31,2,2},n5[7]={31,16,16,30,1,1,30};
 static const u8 n6[7]={14,16,16,30,17,17,14},n7[7]={31,1,2,4,8,8,8},n8[7]={14,17,17,14,17,17,14},n9[7]={14,17,17,15,1,1,14};
 static const u8 dot[7]={0,0,0,0,0,12,12},comma[7]={0,0,0,0,0,12,8},colon[7]={0,12,12,0,12,12,0},slash[7]={1,2,2,4,8,8,16};
 static const u8 bslash[7]={16,8,8,4,2,2,1},dash[7]={0,0,0,31,0,0,0},under[7]={0,0,0,0,0,0,31};
 static const u8 lbr[7]={6,4,4,4,4,4,6},rbr[7]={12,4,4,4,4,4,12},lp[7]={2,4,8,8,8,4,2},rp[7]={8,4,2,2,2,4,8};
 static const u8 plus[7]={0,4,4,31,4,4,0},eq[7]={0,31,0,31,0,0,0},space[7]={0,0,0,0,0,0,0};
 static const u8 ques[7]={14,17,1,2,4,0,4},amp[7]={12,18,20,8,21,18,13},pct[7]={17,2,4,8,16,17,0};
 static const u8 hash[7]={10,31,10,10,31,10,0},tilde[7]={0,0,9,22,0,0,0},at[7]={14,17,23,21,23,16,14};
 /* Lowercase forms adapted to this 5x7 renderer from wLaunchELF's built-in bitmap font. */
 static const u8 la[7]={0,0,14,1,15,17,15},lb[7]={16,16,30,17,17,17,30},lc[7]={0,0,14,16,16,16,14},ld[7]={1,1,15,17,17,17,15},le[7]={0,0,14,17,31,16,14},lf[7]={6,8,8,28,8,8,8},lg[7]={0,15,17,17,15,1,14},lh[7]={16,16,30,17,17,17,17},li[7]={4,0,12,4,4,4,14},lj[7]={2,0,6,2,2,18,12},lk[7]={16,16,18,20,24,20,18},ll[7]={12,4,4,4,4,4,14},lm[7]={0,0,26,21,21,21,21},ln[7]={0,0,30,17,17,17,17},lo[7]={0,0,14,17,17,17,14},l_p[7]={0,0,30,17,30,16,16},lq[7]={0,0,15,17,15,1,1},lr[7]={0,0,22,24,16,16,16},ls[7]={0,0,15,16,14,1,30},lt[7]={8,8,28,8,8,8,6},lu[7]={0,0,17,17,17,17,15},lv[7]={0,0,17,17,17,10,4},lw[7]={0,0,17,17,21,21,10},lx[7]={0,0,17,10,4,10,17},ly[7]={0,0,17,17,15,1,14},lz[7]={0,0,31,2,4,8,31};
 if(c>='A'&&c<='Z'){static const u8* t[]={A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z};return t[c-'A'];}
 if(c>='a'&&c<='z'){static const u8* t[]={la,lb,lc,ld,le,lf,lg,lh,li,lj,lk,ll,lm,ln,lo,l_p,lq,lr,ls,lt,lu,lv,lw,lx,ly,lz};return t[c-'a'];}
 if(c>='0'&&c<='9'){static const u8* t[]={n0,n1,n2,n3,n4,n5,n6,n7,n8,n9};return t[c-'0'];}
 switch(c){case ' ':return space;case '.':return dot;case ',':return comma;case ':':return colon;case '/':return slash;case '\\':return bslash;case '-':return dash;case '_':return under;case '[':return lbr;case ']':return rbr;case '(':return lp;case ')':return rp;case '+':return plus;case '=':return eq;case '?':return ques;case '&':return amp;case '%':return pct;case '#':return hash;case '~':return tilde;case '@':return at;default:return unk;}
}

/* v1.50 phase 2: modal/HUD GPU path.  Browser/ICN software composition is
   retained as the compatibility base while high-churn overlays are emitted as
   Tiny3D RSX primitives.  This avoids PPU pixel loops for Options, Details,
   downloader and confirmation screens without mixing the software ICN raster
   into the same draw pass. */
static int g_rsx_ui_overlay=0;
static inline u32 rsx_rgba_from_argb(u32 c){
 u32 a=(c>>24)&255u, r=(c>>16)&255u, g=(c>>8)&255u, b=c&255u;
 return (r<<24)|(g<<16)|(b<<8)|a;
}
static void rsx_flush_text_batch();
static void rsx_flush_line_batch();
#define RSX_LINE_BATCH_MAX 2048
typedef struct { int x0,y0,x1,y1; u32 col; } rsx_line_cmd_t;
static rsx_line_cmd_t g_rsx_line_batch[RSX_LINE_BATCH_MAX];
static int g_rsx_line_batch_count=0;
static void rsx_flush_line_batch(){
 if(g_rsx_line_batch_count<=0)return;
 float sx=848.0f/(float)g_width, sy=512.0f/(float)g_height;
 tiny3d_SetPolygon(TINY3D_LINES);
 for(int n=0;n<g_rsx_line_batch_count;n++){
  rsx_line_cmd_t *q=&g_rsx_line_batch[n]; u32 rgba=rsx_rgba_from_argb(q->col);
  tiny3d_VertexPos(q->x0*sx,q->y0*sy,0.0f); tiny3d_VertexColor(rgba);
  tiny3d_VertexPos(q->x1*sx,q->y1*sy,0.0f); tiny3d_VertexColor(rgba);
 }
 tiny3d_End(); g_rsx_line_batch_count=0;
}
#define RSX_RECT_BATCH_MAX 1024
typedef struct { int x,y,w,h; u32 col; } rsx_rect_cmd_t;
static rsx_rect_cmd_t g_rsx_rect_batch[RSX_RECT_BATCH_MAX];
static int g_rsx_rect_batch_count=0;
static void rsx_flush_rect_batch(){
 if(g_rsx_rect_batch_count<=0)return;
 g_perf_rect_flushes++;
 float sx=848.0f/(float)g_width, sy=512.0f/(float)g_height;
 tiny3d_SetPolygon(TINY3D_QUADS);
 for(int n=0;n<g_rsx_rect_batch_count;n++){
  rsx_rect_cmd_t *q=&g_rsx_rect_batch[n];
  float x0=q->x*sx,y0=q->y*sy,x1=(q->x+q->w)*sx,y1=(q->y+q->h)*sy;
  u32 rgba=rsx_rgba_from_argb(q->col);
  tiny3d_VertexPos(x0,y0,0.0f); tiny3d_VertexColor(rgba);
  tiny3d_VertexPos(x1,y0,0.0f); tiny3d_VertexColor(rgba);
  tiny3d_VertexPos(x1,y1,0.0f); tiny3d_VertexColor(rgba);
  tiny3d_VertexPos(x0,y1,0.0f); tiny3d_VertexColor(rgba);
 }
 tiny3d_End();
 g_rsx_rect_batch_count=0;
}
static void rsx_ui_rect(int x,int y,int w,int h,u32 col){
 /* Phase 3Q: keep consecutive solid UI primitives in one RSX submission.
    Text is flushed first so painter ordering remains identical to 3P. */
 rsx_flush_text_batch();
 rsx_flush_line_batch();
 if(w<=0||h<=0)return;
 if(g_rsx_rect_batch_count>=RSX_RECT_BATCH_MAX)rsx_flush_rect_batch();
 rsx_rect_cmd_t *q=&g_rsx_rect_batch[g_rsx_rect_batch_count++];
 q->x=x;q->y=y;q->w=w;q->h=h;q->col=col;
}
static int g_rsx_font_ready=0;
static u8 *g_rsx_font_atlas=NULL;
static u32 g_rsx_font_atlas_off=0;
#define RSX_FONT_ATLAS_W 128
#define RSX_FONT_ATLAS_H 64
#define RSX_TEXT_BATCH_MAX 256
typedef struct { int x,y,scale; u32 col; char s[512]; } rsx_text_cmd_t;
static rsx_text_cmd_t g_rsx_text_batch[RSX_TEXT_BATCH_MAX];
static int g_rsx_text_batch_count=0;

static void rsx_font_init(){
 if(g_rsx_font_ready)return;
 g_rsx_font_atlas=(u8*)tiny3d_AllocTexture(RSX_FONT_ATLAS_W*RSX_FONT_ATLAS_H*4);
 if(!g_rsx_font_atlas)return;
 u32 *pix=(u32*)g_rsx_font_atlas;
 memset(pix,0,RSX_FONT_ATLAS_W*RSX_FONT_ATLAS_H*4);
 for(int c=32;c<=126;c++){
  int gi=c-32,gx=(gi&15)*8,gy=(gi>>4)*8; const u8 *gg=glyph((char)c);
  for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(gg[yy]&(1<<(4-xx)))
   pix[(gy+yy)*RSX_FONT_ATLAS_W+(gx+xx)]=0xFFFFFFFFu;
 }
 g_rsx_font_atlas_off=tiny3d_TextureOffset(g_rsx_font_atlas);
 g_rsx_font_ready=1;
}
static void rsx_flush_text_batch(){
 if(g_rsx_text_batch_count<=0)return;
 g_perf_text_flushes++;
 rsx_font_init(); if(!g_rsx_font_ready){g_rsx_text_batch_count=0;return;}
 float sx=848.0f/(float)g_width, sy=512.0f/(float)g_height;
 tiny3d_SetTexture(0,g_rsx_font_atlas_off,RSX_FONT_ATLAS_W,RSX_FONT_ATLAS_H,RSX_FONT_ATLAS_W*4,TINY3D_TEX_FORMAT_A8R8G8B8,TEXTURE_NEAREST);
 tiny3d_SetPolygon(TINY3D_QUADS);
 for(int n=0;n<g_rsx_text_batch_count;n++){
  rsx_text_cmd_t *q=&g_rsx_text_batch[n];
  /* Phase 4C: preserve the legacy 5x7 renderer's exact 6x8 character cell.
     Phase 3P forced every RSX glyph quad to at least 8 pixels wide, while all
     UI geometry, caret positions and text_width() still advanced by 6 pixels.
     On real PS3 that made rendered strings 33% wider than their layout box and
     caused the keyboard/footer collisions.  Do not impose an 8px minimum on
     width; 6*scale is the canonical advance. */
  float fw=(float)scale125(6*q->scale)*sx, fh=(float)scale125(8*q->scale)*sy;
  if(fw<1.0f)fw=1.0f;if(fh<1.0f)fh=1.0f;
  float x=q->x*sx,y=q->y*sy; u32 rgba=rsx_rgba_from_argb(q->col);
  for(const unsigned char *sp=(const unsigned char*)q->s;*sp;sp++,x+=fw){
   int c=*sp;if(c<32||c>126)c='?';int gi=c-32,gx=(gi&15)*8,gy=(gi>>4)*8;
   float u0=(float)gx/RSX_FONT_ATLAS_W,v0=(float)gy/RSX_FONT_ATLAS_H;
   /* Only sample the legacy 6-pixel-wide character cell (5 glyph pixels +
      1 spacing column).  Sampling the full 8-pixel atlas slot into a 6-pixel
      quad would horizontally compress the glyph itself. */
   float u1=(float)(gx+6)/RSX_FONT_ATLAS_W,v1=(float)(gy+8)/RSX_FONT_ATLAS_H;
   tiny3d_VertexPos(x,y,0.0f);       tiny3d_VertexColor(rgba);tiny3d_VertexTexture(u0,v0);
   tiny3d_VertexPos(x+fw,y,0.0f);    tiny3d_VertexColor(rgba);tiny3d_VertexTexture(u1,v0);
   tiny3d_VertexPos(x+fw,y+fh,0.0f); tiny3d_VertexColor(rgba);tiny3d_VertexTexture(u1,v1);
   tiny3d_VertexPos(x,y+fh,0.0f);    tiny3d_VertexColor(rgba);tiny3d_VertexTexture(u0,v1);
  }
 }
 tiny3d_End();
 g_rsx_text_batch_count=0;
}
static void rsx_ui_text_raw(int x,int y,const char *s,u32 col,int scale){
 if(!s||!*s)return;
 rsx_flush_rect_batch();
 rsx_flush_line_batch();
 if(g_rsx_text_batch_count>=RSX_TEXT_BATCH_MAX)rsx_flush_text_batch();
 rsx_text_cmd_t *q=&g_rsx_text_batch[g_rsx_text_batch_count++];
 q->x=x;q->y=y;q->scale=scale;q->col=col;snprintf(q->s,sizeof(q->s),"%s",s);
}

static inline u32 blend_argb_over(u32 dst,u32 src){
 u32 a=(src>>24)&255u;
 if(a>=255u) return 0xFF000000u|(src&0x00FFFFFFu);
 if(a==0u) return dst;
 u32 dr=(dst>>16)&255u,dg=(dst>>8)&255u,db=dst&255u;
 u32 sr=(src>>16)&255u,sg=(src>>8)&255u,sb=src&255u;
 u32 inv=255u-a;
 u32 r=(sr*a + dr*inv)/255u;
 u32 g=(sg*a + dg*inv)/255u;
 u32 b=(sb*a + db*inv)/255u;
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static inline void px(int x,int y,u32 col){
 if((unsigned)x<g_width&&(unsigned)y<g_height){
  u32 *dst=&g_fb[g_back][y*(g_pitch/4)+x];
  *dst=blend_argb_over(*dst,col);
 }
}
static void fill(u32 col){ for(u32 y=0;y<g_height;y++){u32 *p=g_fb[g_back]+y*(g_pitch/4);for(u32 x=0;x<g_width;x++)p[x]=col;} }
static void rect(int x,int y,int w,int h,u32 col){
 if(g_rsx_ui_overlay){rsx_ui_rect(x,y,w,h,col);return;}
 if(w<=0||h<=0)return;
 int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)g_width?(int)g_width:x+w, y1=y+h>(int)g_height?(int)g_height:y+h;
 if(x0>=x1||y0>=y1)return;
 u32 a=(col>>24)&255u;
 if(a>=255u){
  u32 c=0xFF000000u|(col&0x00FFFFFFu);
  for(int yy=y0;yy<y1;yy++){u32 *d=g_fb[g_back]+yy*(g_pitch/4)+x0;for(int xx=x0;xx<x1;xx++)*d++=c;}
 }else if(a){
  for(int yy=y0;yy<y1;yy++){u32 *d=g_fb[g_back]+yy*(g_pitch/4)+x0;for(int xx=x0;xx<x1;xx++,d++)*d=blend_argb_over(*d,col);}
 }
}
static int text_width(const char *s,int scale){ return s ? scale125((int)strlen(s) * 6 * scale) : 0; }
static void text_raw(int x,int y,const char *s,u32 col,int scale){
 if(g_rsx_ui_overlay){rsx_ui_text_raw(x,y,s,col,scale);return;}
 int cell_w = 6 * scale;
 int cell_h = 7 * scale;
 for(;*s;s++,x+=scale125(cell_w)){
  const u8 *g=glyph(*s);
  for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(g[yy]&(1<<(4-xx))){
   int sx0 = xx * scale, sx1 = (xx + 1) * scale;
   int sy0 = yy * scale, sy1 = (yy + 1) * scale;
   int dx0 = x + scale125(sx0);
   int dx1 = x + scale125(sx1);
   int dy0 = y + scale125(sy0);
   int dy1 = y + scale125(sy1);
   rect(dx0, dy0, dx1 - dx0, dy1 - dy0, col);
  }
 }
}
static void text(int x,int y,const char *s,u32 col,int scale){
 /* v1.50 Phase 3O: the RSX body font is already crisp against the opaque/
    translucent cards.  Drawing every body glyph twice for a one-pixel shadow
    doubled libfont/Tiny3D glyph traffic on dense browser and downloader
    screens.  Keep the shadow only for large display text; normal RSX UI text
    is now a single glyph pass.  The legacy software/archive renderer retains
    its original shadow exactly. */
 if(g_rsx_ui_overlay && scale<3){text_raw(x,y,s,col,scale);return;}
 int shadow=(scale>=3)?2:1;
 text_raw(x+shadow,y+shadow,s,0xFF101820u,scale);
 text_raw(x,y,s,col,scale);
}
static void textf(int x,int y,u32 col,int scale,const char *fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);text(x,y,b,col,scale);}
static int wrapped_text_advance(const char *s,int width,int scale){
 ui_text_layout_result_t r=ui_text_layout_wrap(s,width,scale,32,NULL);
 return r.line_count? r.used_height+1 : ui_text_line_pitch(scale);
}
static ui_text_layout_result_t text_bounded(int x,int y,int width,int height,const char *src,u32 col,int scale){
 char lines[32][256]={{0}};
 int pitch=ui_text_line_pitch(scale),max_lines=pitch>0?height/pitch:0;
 if(max_lines>32)max_lines=32;
 ui_text_layout_result_t r=ui_text_layout_wrap(src,width,scale,max_lines,lines);
 for(int i=0;i<r.line_count;i++)text(x,y+i*pitch,lines[i],col,scale);
 return r;
}
static ui_text_layout_result_t text_centered_bounded(int x,int y,int width,int height,const char *src,u32 col,int scale){
 char lines[32][256]={{0}};
 int pitch=ui_text_line_pitch(scale),max_lines=pitch>0?height/pitch:0;
 if(max_lines>32)max_lines=32;
 ui_text_layout_result_t r=ui_text_layout_wrap(src,width,scale,max_lines,lines);
 for(int i=0;i<r.line_count;i++){
  int linew=text_width(lines[i],scale);
  int line_x=ui_text_centered_x(x,width,linew);
  text(line_x,y+i*pitch,lines[i],col,scale);
 }
 return r;
}
static int wrap_text_lines(const char *src,int maxw,int scale,char lines[][256],int max_lines){
 ui_text_layout_result_t r=ui_text_layout_wrap(src,maxw,scale,max_lines,lines);
 if(r.truncated&&r.line_count>0){size_t used=strlen(lines[r.line_count-1]);if(used<252)strcat(lines[r.line_count-1],"...");}
 return r.line_count;
}


static u32 lerp_color(u32 a,u32 b,int t,int max_t){
 if(max_t<=0)return a;
 int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255;
 int br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
 int r=ar + (br-ar)*t/max_t;
 int g=ag + (bg-ag)*t/max_t;
 int bl=ab + (bb-ab)*t/max_t;
 return 0xFF000000u|((u32)r<<16)|((u32)g<<8)|(u32)bl;
}
static void gradient_fill_v(u32 top,u32 bottom){
 for(u32 y=0;y<g_height;y++){
  u32 c=lerp_color(top,bottom,(int)y,(int)(g_height?g_height-1:1));
  u32 *p=g_fb[g_back]+y*(g_pitch/4);
  for(u32 x=0;x<g_width;x++)p[x]=c;
 }
}
static void panel_frame(int x,int y,int w,int h,u32 fillc,u32 border,u32 hi){
 rect(x,y,w,h,fillc);
 rect(x,y,w,2,hi); rect(x,y,2,h,hi);
 rect(x,y+h-2,w,2,border); rect(x+w-2,y,2,h,border);
}
static void round_rect(int x,int y,int w,int h,int r,u32 col){
 if(w<=0||h<=0)return;
 if(g_rsx_ui_overlay){ rsx_ui_rect(x,y,w,h,col); return; }
 if(r<1){rect(x,y,w,h,col);return;}
 if(r*2>w)r=w/2; if(r*2>h)r=h/2;
 rect(x+r,y,w-2*r,h,col); rect(x,y+r,r,h-2*r,col); rect(x+w-r,y+r,r,h-2*r,col);
 for(int yy=0;yy<r;yy++)for(int xx=0;xx<r;xx++){
  int dx=r-1-xx,dy=r-1-yy; if(dx*dx+dy*dy<=r*r){
   px(x+xx,y+yy,col); px(x+w-1-xx,y+yy,col); px(x+xx,y+h-1-yy,col); px(x+w-1-xx,y+h-1-yy,col);
  }
 }
}
static void gradient_rect_v(int x,int y,int w,int h,u32 top,u32 bottom){
 if(h<=0||w<=0)return;
 for(int yy=0;yy<h;yy++)rect(x,y+yy,w,1,lerp_color(top,bottom,yy,h>1?h-1:1));
}
static void modern_card(int x,int y,int w,int h,u32 fillc,u32 border,int radius){
 round_rect(x+5,y+8,w,h,radius,0x3810181Cu);
 round_rect(x+2,y+3,w,h,radius,0x20000000u);
 round_rect(x,y,w,h,radius,fillc);
 /* glass-like sheen */
 gradient_rect_v(x+2,y+2,w-4,h/3,0x30FFFFFFu,0x00000000u);
 rect(x+radius,y,w-2*radius,1,border); rect(x+radius,y+h-1,w-2*radius,1,border);
 rect(x,y+radius,1,h-2*radius,border); rect(x+w-1,y+radius,1,h-2*radius,border);
}
/* Browser list cards deliberately do not use modern_card()'s top-third
   sheen.  That sheen scaled with the full panel height and could therefore
   fade through the first several file rows.  Keep the body flat; the column
   header gets its own bounded highlight instead. */
static void browser_list_card(int x,int y,int w,int h,u32 fillc,u32 border,int radius){
 round_rect(x+5,y+8,w,h,radius,0x3810181Cu);
 round_rect(x+2,y+3,w,h,radius,0x20000000u);
 round_rect(x,y,w,h,radius,fillc);
 rect(x+radius,y,w-2*radius,1,border); rect(x+radius,y+h-1,w-2*radius,1,border);
 rect(x,y+radius,1,h-2*radius,border); rect(x+w-1,y+radius,1,h-2*radius,border);
}
static void browser_column_header(int x,int y,int w,int h){
 if(w<=0||h<=0)return;
 /* v1.35: visibly distinct Explorer header rather than another subtle sheen. */
 round_rect(x,y,w,h,5,0xF0182B40u);
 if(h>4)gradient_rect_v(x+2,y+2,w-4,h-4,0xFF315776u,0xFF172B3Fu);
 rect(x+7,y+2,w-14,1,0x806FA8CCu);
 rect(x+7,y+h-3,w-14,2,0xFF35C7FFu);
}
static void draw_wire_cube(int cx,int cy,int size,float ang,u32 col){
 float ca=cosf(ang),sa=sinf(ang),cb=cosf(ang*0.67f),sb=sinf(ang*0.67f);
 int sx[8],sy[8];
 for(int i=0;i<8;i++){
  float x=(i&1)?1.0f:-1.0f, y=(i&2)?1.0f:-1.0f, z=(i&4)?1.0f:-1.0f;
  float xr=x*ca+z*sa, zr=-x*sa+z*ca;
  float yr=y*cb-zr*sb, zz=y*sb+zr*cb+4.2f;
  sx[i]=cx+(int)(xr*(float)size/zz); sy[i]=cy+(int)(yr*(float)size/zz);
 }
 static const u8 e[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
 for(int i=0;i<12;i++)line(sx[e[i][0]],sy[e[i][0]],sx[e[i][1]],sy[e[i][1]],col);
}
static void draw_memory_card_silhouette(int x,int y,int w,int h,u32 col,u32 hi){
 /* Oversized decorative PS2 memory-card silhouette for the background. */
 rect(x,y,w,h,col);
 int notch=w/5;
 rect(x+w-notch,y,notch,h/5,0xFF020814u);
 rect(x+5,y+5,w-10,2,hi);
 rect(x+5,y+h-7,w-10,2,hi);
 for(int i=0;i<5;i++) rect(x+8+i*(w-20)/5,y+h/5,3,h/3,hi);
 for(int i=0;i<4;i++) rect(x+10,y+h*3/4+i*4,w-20,1,hi);
}
static void draw_ps2_symbol_cloud(){
 int t=g_frame_counter;
 int cx1=70+((t/3)%140), cy1=(int)g_height/3;
 int cx2=(int)g_width-150-((t/4)%110), cy2=(int)g_height*2/3;
 int s=(g_body_scale>1)?34:22;
 /* triangle */
 line(cx1,cy1-s,cx1-s,cy1+s,0xFF60E2B8u); line(cx1,cy1-s,cx1+s,cy1+s,0xFF60E2B8u); line(cx1-s,cy1+s,cx1+s,cy1+s,0xFF60E2B8u);
 /* square */
 rect(cx2-s,cy2-s,s*2,2,0xFFE7A84Fu); rect(cx2-s,cy2+s-2,s*2,2,0xFFE7A84Fu); rect(cx2-s,cy2-s,2,s*2,0xFFE7A84Fu); rect(cx2+s-2,cy2-s,2,s*2,0xFFE7A84Fu);
 /* circle and cross */
 draw_circle_outline((int)g_width/4,(int)g_height*4/5,s,0xFFCF72DCu,2);
 int xx=(int)g_width*3/4, yy=(int)g_height/5;
 line(xx-s,yy-s,xx+s,yy+s,0xFFFF7188u); line(xx-s,yy+s,xx+s,yy-s,0xFFFF7188u);
}
static void draw_disc(int cx,int cy,int r,u32 col);
static void draw_ps2_logo_bars(int x,int y,int scale){
 int bw=scale125(3*scale)+1, gap=scale125(scale)+1;
 /* abstract PS2 wordmark-like strokes: blue/cyan, kept tiny and decorative */
 rect(x,y,bw,scale125(12*scale),0xFF3C7BFFu);
 rect(x+bw+gap,y+scale125(2*scale),bw,scale125(8*scale),0xFF6FD6FFu);
 rect(x+(bw+gap)*2,y+scale125(4*scale),bw,scale125(6*scale),0xFF8A7DFFu);
}
static void draw_ps2_front_details(int x,int y,int w,int h,u32 trim){
 int face_y=y+h*2/3;
 /* memory card slots */
 int sx=x+w/13, sw=w/7, sh=h/12;
 round_rect(sx,face_y+5,sw,sh,2,0xB5060A0Fu); rect(sx+4,face_y+7,sw-8,1,0x55FFFFFFu);
 round_rect(sx+sw+8,face_y+5,sw,sh,2,0xB5060A0Fu); rect(sx+sw+12,face_y+7,sw-8,1,0x55FFFFFFu);
 /* controller ports */
 int py=face_y+sh+12;
 for(int i=0;i<2;i++){int px0=sx+i*(sw+8);round_rect(px0,py,sw,sh+3,2,0xC204070Bu);rect(px0+5,py+3,sw-10,2,0x446FA8FFu);}
 /* optical drive tray */
 int dx=x+w*55/100, dw=w*32/100, dh=h/9;
 round_rect(dx,face_y+4,dw,dh,2,0xC206090Eu); rect(dx+5,face_y+7,dw-10,1,0x50FFFFFFu);
 /* power/eject */
 int br=h/20; if(br<3)br=3;
 draw_disc(x+w*91/100,face_y+dh/2+2,br,trim);
 draw_disc(x+w*91/100,face_y+dh+br+7,br,0xFF4FAE65u);
}
static void draw_fat_ps2(int x,int y,int w,int h,u32 shell,u32 rib,u32 trim,u32 glow){
 /* glow behind the machine */
 draw_disc(x+w/2,y+h/2,(w>h?w:h)*3/5,glow);
 /* shadow */
 round_rect(x+8,y+12,w,h,5,0x4A000000u);
 /* lower chassis */
 round_rect(x,y+h/6,w,h*5/6,5,shell);
 /* characteristic stacked/ribbed top slabs */
 int top_h=h*2/3;
 for(int i=0;i<12;i++){
  int yy=y+4+i*(top_h-10)/12;
  rect(x+6,yy,w-12,2,rib);
  if(i<11) rect(x+12,yy+3,w-24,1,0x18FFFFFFu);
 }
 /* raised center top panel */
 int cx=x+w/4, cw=w/2;
 round_rect(cx,y+8,cw,h/3,3,lerp_color(shell,0xFFFFFFFFu,18,100));
 rect(cx+6,y+12,cw-12,2,0x2FFFFFFFu);
 /* front face separation */
 rect(x+4,y+h*2/3,w-8,2,trim);
 rect(x+4,y+h*2/3+4,w-8,1,0x22FFFFFFu);
 draw_ps2_front_details(x,y,w,h,trim);
 draw_ps2_logo_bars(cx+cw/2-scale125(9),y+h/8,1);
}
static void draw_ps2_slim(int x,int y,int w,int h,u32 shell,u32 trim,u32 glow){
 draw_disc(x+w/2,y+h/2,(w>h?w:h)*3/5,glow);
 round_rect(x+7,y+10,w,h,7,0x42000000u);
 round_rect(x,y,w,h,7,shell);
 /* lid seam and slim ridges */
 rect(x+w/2,y+5,1,h-10,0x30FFFFFFu);
 for(int i=0;i<6;i++) rect(x+8,y+10+i*5,w/3,1,0x2AFFFFFFu);
 round_rect(x+w*58/100,y+h/5,w*32/100,h*3/5,5,0x36000000u);
 rect(x+w*58/100,y+h/2,w*32/100,2,trim);
 draw_ps2_logo_bars(x+10,y+9,1);
}
static void draw_bios_tower(int x,int base_y,int w,int h,u32 body,u32 edge){
 if(w<4||h<8)return;
 int top_y=base_y-h;
 /* subtle perspective: narrow at top */
 int inset=w/5;
 for(int yy=0;yy<h;yy++){
  int t=yy;
  int side=(inset*(h-t))/h;
  int rw=w-side*2;
  if(rw<2)rw=2;
  u32 c=lerp_color(body,0x10000000u,yy,h);
  rect(x+side,top_y+yy,rw,1,c);
 }
 line(x+inset,top_y,x,base_y,edge);
 line(x+w-inset-1,top_y,x+w-1,base_y,edge);
 rect(x+inset,top_y,w-inset*2,1,edge);
}
static inline u32 rgb565_to_argb(u16 c){
 u32 r=(c>>11)&31u,g=(c>>5)&63u,b=c&31u;
 r=(r<<3)|(r>>2); g=(g<<2)|(g>>4); b=(b<<3)|(b>>2);
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static inline u32 rgb565_bilinear(u16 c00,u16 c10,u16 c01,u16 c11,u32 fx,u32 fy){
 /* Fixed-point bilinear filter.  The source is a compact 480x270 RGB565
    composite generated from the project-provided PS2 PNG/JPG references. */
 u32 r00=((c00>>11)&31u),g00=((c00>>5)&63u),b00=(c00&31u);
 u32 r10=((c10>>11)&31u),g10=((c10>>5)&63u),b10=(c10&31u);
 u32 r01=((c01>>11)&31u),g01=((c01>>5)&63u),b01=(c01&31u);
 u32 r11=((c11>>11)&31u),g11=((c11>>5)&63u),b11=(c11&31u);
 u32 ix=65535u-fx,iy=65535u-fy;
 u32 rt=(r00*ix+r10*fx)>>16, rb=(r01*ix+r11*fx)>>16;
 u32 gt=(g00*ix+g10*fx)>>16, gb=(g01*ix+g11*fx)>>16;
 u32 bt=(b00*ix+b10*fx)>>16, bb=(b01*ix+b11*fx)>>16;
 u32 r=(rt*iy+rb*fy)>>16,g=(gt*iy+gb*fy)>>16,b=(bt*iy+bb*fy)>>16;
 r=(r<<3)|(r>>2); g=(g<<2)|(g>>4); b=(b<<3)|(b>>2);
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static void draw_embedded_ps2_photo_background(){
 int dw=(int)g_width,dh=(int)g_height;
 /* Cover the framebuffer while preserving the source's 16:9 composition.
    On 4:3 outputs this crops the sides rather than stretching the consoles. */
 u32 sx0=0,sy0=0,sw=PS2_BG_W,sh=PS2_BG_H;
 if((long long)dw*PS2_BG_H > (long long)dh*PS2_BG_W){
  sh=(u32)(((long long)PS2_BG_W*dh)/dw); if(sh<1)sh=1;
  sy0=(PS2_BG_H-sh)/2;
 }else if((long long)dw*PS2_BG_H < (long long)dh*PS2_BG_W){
  sw=(u32)(((long long)PS2_BG_H*dw)/dh); if(sw<1)sw=1;
  sx0=(PS2_BG_W-sw)/2;
 }
 for(int y=0;y<dh;y++){
  u32 syfp=(dh>1)?((u32)((((unsigned long long)(sh-1)*y)<<16)/(unsigned)(dh-1))):0;
  u32 sy=sy0+(syfp>>16),fy=syfp&65535u,sy1=(sy+1<PS2_BG_H)?sy+1:sy;
  u32 *dst=g_fb[g_back]+(size_t)y*(g_pitch/4);
  for(int x=0;x<dw;x++){
   u32 sxfp=(dw>1)?((u32)((((unsigned long long)(sw-1)*x)<<16)/(unsigned)(dw-1))):0;
   u32 sx=sx0+(sxfp>>16),fx=sxfp&65535u,sx1=(sx+1<PS2_BG_W)?sx+1:sx;
   const u16 *r0=ps2_bg_rgb565+(size_t)sy*PS2_BG_W;
   const u16 *r1=ps2_bg_rgb565+(size_t)sy1*PS2_BG_W;
   dst[x]=rgb565_bilinear(r0[sx],r0[sx1],r1[sx],r1[sx1],fx,fy);
  }
 }
}
static void render_ps2_wallpaper_scene(){
 /* Photo-based PS2 background built from the PNG/JPG assets supplied with
    the project.  It remains CPU-rasterized and cached: no RSX texture path. */
 draw_embedded_ps2_photo_background();
 int W=(int)g_width,H=(int)g_height;
 int horizon=H*66/100;
 /* Blend the photographic collage into the PS2 BIOS aesthetic. */
 draw_disc(W*57/100,H*46/100,(g_body_scale>1)?250:150,0x0B3765D8u);
 draw_disc(W*82/100,H*31/100,(g_body_scale>1)?150:90,0x083D78FFu);
 rect(0,horizon,W,1,0x224D78B6u);
 for(int i=1;i<=4;i++)rect(0,horizon+i*i*8,W,1,0x095A8DD8u);
 /* Sparse memory-tower silhouettes in the left negative space add depth
    without competing with file names or the real console imagery. */
 const u32 tower_cols[4]={0x142A5FB8u,0x123B76D1u,0x10234D9Cu,0x122B63B3u};
 for(int i=0;i<11;i++){
  int tx=(i*83+W/24)%(W*58/100);
  int tw=(g_body_scale>1)?(16+(i%4)*6):(9+(i%4)*3);
  int th=(g_body_scale>1)?(45+(i%5)*23):(26+(i%5)*13);
  draw_bios_tower(tx,horizon-7+(i%3)*8,tw,th,tower_cols[i%4],0x164F8DE8u);
 }
 /* Gentle edge shading improves text/card contrast at every output mode. */
 int shade=H/7;
 for(int y=0;y<shade;y++){
  u32 a=(u32)((shade-y)*42/shade);
  rect(0,y,W,1,(a<<24));
  rect(0,H-1-y,W,1,(a<<24));
 }
 draw_ps2_logo_bars(W/2-scale125(13*((g_body_scale>1)?2:1)),scale125(30*((g_body_scale>1)?2:1)),2);
}


/* v1.50 Phase 3C: fully RSX-owned filesystem frame.  Do not mix PPU writes
   into Tiny3D's active render target; real RSX can retain/invalidate colour
   cache lines independently of CPU framebuffer stores. */
static void *g_rsx_bg_tex=NULL;
static u32 g_rsx_bg_off=0;
static int g_rsx_bg_ready=0;
/* v1.50 Phase 3T: archive/PSU browser is RSX-owned too.  The PS2 ICN
   triangle rasterizer remains on the PPU, but its completed colour cache is
   uploaded as a texture and composited by RSX.  No archive UI writes to the
   display framebuffer. */
static u32 *g_rsx_icn_tex=NULL,*g_rsx_model_tex=NULL;
static u32 g_rsx_icn_off=0,g_rsx_model_off=0;
static int g_rsx_preview_tex_ready=0;
#define RSX_MODEL_TEX_W 128
#define RSX_MODEL_TEX_H 128
static void rsx_preview_tex_init(){
 if(g_rsx_preview_tex_ready)return;
 g_rsx_icn_tex=(u32*)tiny3d_AllocTexture(128u*128u*4u);
 g_rsx_model_tex=(u32*)tiny3d_AllocTexture(RSX_MODEL_TEX_W*RSX_MODEL_TEX_H*4u);
 if(!g_rsx_icn_tex||!g_rsx_model_tex)return;
 memset(g_rsx_icn_tex,0,128u*128u*4u);
 memset(g_rsx_model_tex,0,RSX_MODEL_TEX_W*RSX_MODEL_TEX_H*4u);
 g_rsx_icn_off=tiny3d_TextureOffset(g_rsx_icn_tex);
 g_rsx_model_off=tiny3d_TextureOffset(g_rsx_model_tex);
 g_rsx_preview_tex_ready=1;
}
static void rsx_draw_uploaded_quad(u32 off,int tw,int th,int pitch,int x,int y,int w,int h,float u1,float v1,int linear){
 rsx_flush_text_batch();rsx_flush_rect_batch();rsx_flush_line_batch();
 float sx=848.0f/(float)g_width,sy=512.0f/(float)g_height;
 float x0=x*sx,y0=y*sy,x1=(x+w)*sx,y1=(y+h)*sy;
 tiny3d_SetTextureWrap(0,off,tw,th,pitch,TINY3D_TEX_FORMAT_A8R8G8B8,TEXTWRAP_CLAMP,TEXTWRAP_CLAMP,linear?TEXTURE_LINEAR:TEXTURE_NEAREST);
 tiny3d_SetPolygon(TINY3D_QUADS);
 tiny3d_VertexPos(x0,y0,0.0f);tiny3d_VertexColor(0xFFFFFFFFu);tiny3d_VertexTexture(0,0);
 tiny3d_VertexPos(x1,y0,0.0f);tiny3d_VertexColor(0xFFFFFFFFu);tiny3d_VertexTexture(u1,0);
 tiny3d_VertexPos(x1,y1,0.0f);tiny3d_VertexColor(0xFFFFFFFFu);tiny3d_VertexTexture(u1,v1);
 tiny3d_VertexPos(x0,y1,0.0f);tiny3d_VertexColor(0xFFFFFFFFu);tiny3d_VertexTexture(0,v1);
 tiny3d_End();
}
static void rsx_bg_init(){
 if(g_rsx_bg_ready)return;
 size_t pixels=(size_t)PS2_BG_W*(size_t)PS2_BG_H;
 u32 *tex=(u32*)tiny3d_AllocTexture((u32)(pixels*4));
 if(!tex)return;
 for(size_t i=0;i<pixels;i++){
  u16 c=ps2_bg_rgb565[i];
  u32 r=(c>>11)&31u,g=(c>>5)&63u,b=c&31u;
  r=(r<<3)|(r>>2); g=(g<<2)|(g>>4); b=(b<<3)|(b>>2);
  tex[i]=0xFF000000u|(r<<16)|(g<<8)|b;
 }
 g_rsx_bg_tex=tex; g_rsx_bg_off=tiny3d_TextureOffset(tex); g_rsx_bg_ready=1;
}
static void rsx_draw_background(){
 rsx_bg_init();
 if(!g_rsx_bg_ready)return;
 float u0=0.0f,v0=0.0f,u1=1.0f,v1=1.0f;
 float out_aspect=(float)g_width/(float)g_height;
 float src_aspect=(float)PS2_BG_W/(float)PS2_BG_H;
 if(out_aspect>src_aspect){ float keep=src_aspect/out_aspect; v0=(1.0f-keep)*0.5f; v1=1.0f-v0; }
 else if(out_aspect<src_aspect){ float keep=out_aspect/src_aspect; u0=(1.0f-keep)*0.5f; u1=1.0f-u0; }
 tiny3d_SetTextureWrap(0,g_rsx_bg_off,PS2_BG_W,PS2_BG_H,PS2_BG_W*4,TINY3D_TEX_FORMAT_A8R8G8B8,TEXTWRAP_CLAMP,TEXTWRAP_CLAMP,TEXTURE_LINEAR);
 tiny3d_SetPolygon(TINY3D_QUADS);
 tiny3d_VertexPos(0,0,1000.0f); tiny3d_VertexColor(0xFFFFFFFFu); tiny3d_VertexTexture(u0,v0);
 tiny3d_VertexPos(848,0,1000.0f); tiny3d_VertexTexture(u1,v0);
 tiny3d_VertexPos(848,512,1000.0f); tiny3d_VertexTexture(u1,v1);
 tiny3d_VertexPos(0,512,1000.0f); tiny3d_VertexTexture(u0,v1);
 tiny3d_End();
}

static void draw_background_art(){
 if(!g_bg_cache){
  size_t bytes=(size_t)g_pitch*(size_t)g_height;
  g_bg_cache=(u32*)memalign(64,bytes);
  if(g_bg_cache)memset(g_bg_cache,0,bytes);
 }
 if(g_bg_cache && !g_bg_cache_ready){
  u32 *save=g_fb[g_back];
  g_fb[g_back]=g_bg_cache;
  render_ps2_wallpaper_scene();
  g_fb[g_back]=save;
  g_bg_cache_ready=1;
 }
 if(g_bg_cache){ memcpy(g_fb[g_back],g_bg_cache,(size_t)g_pitch*(size_t)g_height); return; }
 render_ps2_wallpaper_scene();
}
static void draw_circle_outline(int cx,int cy,int r,u32 col,int thick){
 /* Phase 3U: controller Circle/O graphics must follow the active renderer.
    The legacy implementation wrote the ring with px(), so after the UI was
    migrated to Tiny3D/RSX the O token silently wrote into the CPU framebuffer
    instead of the displayed RSX frame.  Draw concentric RSX line rings while
    the RSX compositor owns the frame; retain the exact software raster path
    only for the remaining CPU/off-screen renderer. */
 if(g_rsx_ui_overlay){
  if(r<1)return; if(thick<1)thick=1;
  const int segs=32;
  for(int t=0;t<thick;t++){
   float rr=(float)(r-t); if(rr<1.0f)break;
   int px0=cx+(int)lrintf(rr), py0=cy;
   for(int i=1;i<=segs;i++){
    float a=(6.28318530718f*(float)i)/(float)segs;
    int px1=cx+(int)lrintf(cosf(a)*rr);
    int py1=cy+(int)lrintf(sinf(a)*rr);
    line(px0,py0,px1,py1,col); px0=px1; py0=py1;
   }
  }
  return;
 }
 int rr=r*r, ir=(r-thick>0)?(r-thick)*(r-thick):0;
 for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){int d=x*x+y*y; if(d<=rr && d>=ir) px(cx+x,cy+y,col);}
}
static void draw_disc(int cx,int cy,int r,u32 col){ if(g_rsx_ui_overlay){ for(int y=-r;y<=r;y++){int xx=(int)sqrtf((float)(r*r-y*y));rect(cx-xx,cy+y,xx*2+1,1,col);} return;} for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r) px(cx+x,cy+y,col); }
static int face_button_token_size(int scale){
 /* Keep face-button glyphs comfortably inside a legend line on real PS3.
    Older builds made the circle taller than the line height, which allowed
    it to spill into the next row. */
 int s=scale125(12*scale)+6;
 if(s<18)s=18;
 return s;
}
static int legend_line_height(int scale){
 int face=face_button_token_size(scale);
 int text_h=scale125(8*scale)+8;
 int h=(face>text_h?face:text_h)+8; /* explicit top/bottom safety padding */
 return h;
}
static int draw_face_button_token(const char *token,int x,int y,int scale){
 int s=face_button_token_size(scale); int r=s/2; int cx=x+r, cy=y+r;
 /* Phase 4F: controller tokens get the same small black drop shadow as the
    UI lettering so their silhouettes remain legible over blue/grey panels. */
 draw_disc(cx+2,cy+2,r,0xA0000000u);
 draw_disc(cx,cy,r,0xFF2B3540u); draw_circle_outline(cx,cy,r-1,0xFF758493u,2);
 if(!strcmp(token,"X")){ line(cx-r/2,cy-r/2,cx+r/2,cy+r/2,0xFF61C7FFu); line(cx-r/2,cy+r/2,cx+r/2,cy-r/2,0xFF61C7FFu); line(cx-r/2+1,cy-r/2,cx+r/2+1,cy+r/2,0xFF61C7FFu); line(cx-r/2+1,cy+r/2,cx+r/2+1,cy-r/2,0xFF61C7FFu); }
 else if(!strcmp(token,"O")){ draw_circle_outline(cx,cy,r/2,0xFFFF616Eu,2); }
 else if(!strcmp(token,"SQUARE")){ int q=r/2; rect(cx-q,cy-q,q*2,q*2,0x00000000); draw_circle_outline(cx,cy,r,0xFF97AFC5u,2); rect(cx-q,cy-q,q*2,2,0xFFFF69E6u); rect(cx-q,cy+q-2,q*2,2,0xFFFF69E6u); rect(cx-q,cy-q,2,q*2,0xFFFF69E6u); rect(cx+q-2,cy-q,2,q*2,0xFFFF69E6u); }
 else if(!strcmp(token,"TRIANGLE")){ int h=r/2+1; line(cx,cy-h,cx-h,cy+h/2,0xFF61FF8Du); line(cx,cy-h,cx+h,cy+h/2,0xFF61FF8Du); line(cx-h,cy+h/2,cx+h,cy+h/2,0xFF61FF8Du); }
 return s;
}
static int draw_pill_token(const char *token,int x,int y,int scale){
 int tw=text_width(token,scale); int h=scale125(10*scale)+8; int w=tw+18;
 round_rect(x+2,y+2,w,h,5,0xA0000000u);
 round_rect(x,y,w,h,5,0xFF303A46u);
 rect(x+5,y+2,w-10,1,0xFF596878u);
 text(x+9,y+4,token,0xFFF5F8FBu,scale);
 return w;
}
static int draw_lr_token(int x,int y,int scale){
 int h=scale125(10*scale)+8; int w=text_width("LEFT/RIGHT",scale)+24;
 round_rect(x+2,y+2,w,h,5,0xA0000000u);
 round_rect(x,y,w,h,5,0xFF303A46u); rect(x+5,y+2,w-10,1,0xFF596878u);
 int cy=y+h/2;
 line(x+8,cy,x+14,cy-4,0xFFB9C7D5u); line(x+8,cy,x+14,cy+4,0xFFB9C7D5u);
 line(x+w-8,cy,x+w-14,cy-4,0xFFB9C7D5u); line(x+w-8,cy,x+w-14,cy+4,0xFFB9C7D5u);
 text(x+23,y+4,"LEFT/RIGHT",0xFFF5F8FBu,scale);
 return w;
}
static int token_width_for(const char *token,int scale){
 if(!strcmp(token,"X")||!strcmp(token,"O")||!strcmp(token,"SQUARE")||!strcmp(token,"TRIANGLE")) return face_button_token_size(scale);
 if(!strcmp(token,"LEFT/RIGHT")) return text_width("LEFT/RIGHT",scale)+24;
 if(!strcmp(token,"UP/DOWN")) return scale125(28*scale);
 return text_width(token,scale)+18;
}
struct legend_entry_t{ const char *token; const char *action; };
/* Phase 3I: libfont's real-RSX advance is wider than the legacy 5x7 CPU
   estimate used by text_width().  Legends must reserve the rendered width,
   otherwise the next icon starts inside the previous label.  Keep this
   conservative only for control-layout measurement; normal text metrics and
   appearance remain unchanged. */
static int legend_text_width(const char *s,int scale){
 /* Phase 4C: RSX atlas and software renderer now share the same 6x8 cell,
    therefore control layout must use the same canonical metric as text(). */
 return text_width(s,scale);
}
static int measure_control_legend_lines(const legend_entry_t *entries,int count,int maxw,int scale){
 int x=0, lines=1;
 for(int i=0;i<count;i++){
  int ew=token_width_for(entries[i].token,scale)+8+legend_text_width(entries[i].action,scale)+24;
  if(x>0 && x+ew>maxw){ lines++; x=0; }
  x += ew;
 }
 return lines;
}
static void draw_control_legend_aligned(const legend_entry_t *entries,int count,int x,int y,int maxw,int scale,int centered){
 int line_h=legend_line_height(scale),line_width[32]={0},entry_line[32]={0},line_count=1;
 if(count>32)count=32;
 for(int i=0;i<count;i++){
  int ew=token_width_for(entries[i].token,scale)+8+legend_text_width(entries[i].action,scale)+24;
  int lineidx= line_count-1;
  if(line_width[lineidx]>0&&line_width[lineidx]+ew>maxw){lineidx++;if(lineidx>=32)break;line_count=lineidx+1;}
  entry_line[i]=lineidx;line_width[lineidx]+=ew;
 }
 int cx=x,cy=y,current_line=-1;
 for(int i=0;i<count;i++){
  int entry_row=entry_line[i];
  if(entry_row>=line_count)break;
  if(entry_row!=current_line){current_line=entry_row;cx=x+(centered?(maxw-line_width[entry_row])/2:0);cy=y+entry_row*line_h;}
  int tw=token_width_for(entries[i].token,scale);
  if(!strcmp(entries[i].token,"LEFT/RIGHT")) draw_lr_token(cx,cy,scale);
  else if(!strcmp(entries[i].token,"UP/DOWN")){
   int th=scale125(10*scale)+8, tw2=token_width_for(entries[i].token,scale);
   round_rect(cx,cy,tw2,th,5,0xFF303A46u); rect(cx+5,cy+2,tw2-10,1,0xFF596878u);
   int mx=cx+tw2/2, my=cy+th/2;
   line(mx,my-6,mx-4,my-1,0xFFF5F8FBu); line(mx,my-6,mx+4,my-1,0xFFF5F8FBu);
   line(mx,my+6,mx-4,my+1,0xFFF5F8FBu); line(mx,my+6,mx+4,my+1,0xFFF5F8FBu);
  }
  else if(!strcmp(entries[i].token,"X")||!strcmp(entries[i].token,"O")||!strcmp(entries[i].token,"SQUARE")||!strcmp(entries[i].token,"TRIANGLE")) {
   /* Raise the circular glyph slightly; its reduced size plus the enlarged
      line height guarantees it stays inside the command-bar row. */
   draw_face_button_token(entries[i].token,cx,cy-2,scale);
  }
  else draw_pill_token(entries[i].token,cx,cy,scale);
  text(cx+tw+8,cy+3,entries[i].action,0xFFF0E8D8u,scale);
  cx += tw+8+legend_text_width(entries[i].action,scale)+24;
 }
}
static void draw_control_legend(const legend_entry_t *entries,int count,int x,int y,int maxw,int scale){draw_control_legend_aligned(entries,count,x,y,maxw,scale,0);}
static void draw_control_legend_centered(const legend_entry_t *entries,int count,int x,int y,int maxw,int scale){draw_control_legend_aligned(entries,count,x,y,maxw,scale,1);}
static u32 extension_colour(const char *name){
 const char *dot=strrchr(name?name:"",'.');
 /* Distinct fixed colours for common PS3 PSU Manager file classes. */
 if(!dot||!dot[1])return 0xFF607D8Bu;
 if(!strcasecmp(dot,".psu"))return 0xFF315E9Eu;
 if(!strcasecmp(dot,".icn"))return 0xFFA45BDBu;
 if(!strcasecmp(dot,".sys"))return 0xFF00897Bu;
 if(!strcasecmp(dot,".elf"))return 0xFFDD5B64u;
 if(!strcasecmp(dot,".self"))return 0xFFE67E22u;
 if(!strcasecmp(dot,".pkg"))return 0xFFC0392Bu;
 if(!strcasecmp(dot,".cfg"))return 0xFF546E7Au;
 if(!strcasecmp(dot,".bin"))return 0xFF7E57C2u;
 if(!strcasecmp(dot,".dat"))return 0xFF00838Fu;
 if(!strcasecmp(dot,".txt"))return 0xFF558B2Fu;
 if(!strcasecmp(dot,".zip"))return 0xFFF9A825u;
 if(!strcasecmp(dot,".part"))return 0xFFAD1457u;
 if(!strcasecmp(dot,".vm1"))return 0xFF356F58u;
 if(!strcasecmp(dot,".vm2"))return 0xFF2E7D6B;
 /* Deterministic extension colour for uncommon types. */
 unsigned h=2166136261u;for(const unsigned char *q=(const unsigned char*)dot;*q;q++){h^=(unsigned)(*q|32);h*=16777619u;}
 unsigned r=70+(h&0x7f),g=70+((h>>8)&0x7f),b=70+((h>>16)&0x7f);
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static void extension_tag(const char *name,char out[8]){
 const char *dot=strrchr(name?name:"",'.'); if(!dot||!dot[1]){strcpy(out,".FILE");return;}
 int n=0;out[n++]='.';for(dot++;*dot&&n<7;dot++){char c=*dot;if(c>='a'&&c<='z')c-=32;out[n++]=c;}out[n]=0;
}
/* Stage 39: procedural 128px icon family.  No external texture allocation,
   and archive thumbnails continue to take precedence in the browser. */
static u32 icon_rgb(unsigned r,unsigned g,unsigned b){return 0xFF000000u|((r&255)<<16)|((g&255)<<8)|(b&255);}
static u32 icon_shade(u32 c,int delta){
 int r=(int)((c>>16)&255)+delta,g=(int)((c>>8)&255)+delta,b=(int)(c&255)+delta;
 if(r<0)r=0;if(r>255)r=255;if(g<0)g=0;if(g>255)g=255;if(b<0)b=0;if(b>255)b=255;
 return icon_rgb(r,g,b);
}
static u32 icon_text_colour(const char *tag){
 unsigned h=0x811c9dc5u;for(const char *p=tag;*p;p++){h^=(unsigned char)*p;h*=16777619u;}
 /* Each extension receives a deterministic, vivid but legible ink shade. */
 const u32 palette[]={0xFFFFE48Bu,0xFFBAF4FFu,0xFFFFC4D4u,0xFFCEFFB8u,0xFFE0C5FFu,0xFFFFDBADu,0xFFFFFFFFu,0xFFB8D8FFu};
 if(!strcmp(tag,".TXT"))return 0xFFB7F3FFu;
 if(!strcmp(tag,".ZIP"))return 0xFFFFE3A4u;
 return palette[(h>>9)%8];
}
static void icon_bevel_frame(int x,int y,int w,int h,u32 base,int selected){
 u32 bright=icon_shade(base,selected?95:68),dark=icon_shade(base,-68);
 round_rect(x+5,y+7,w-3,h-2,11,0x66000000u);
 round_rect(x,y,w,h,11,bright);                   /* outer raised rim */
 round_rect(x+3,y+3,w-6,h-6,9,dark);               /* dark groove */
 round_rect(x+6,y+6,w-12,h-12,8,icon_shade(base,30)); /* inner raised rim */
 gradient_rect_v(x+9,y+9,w-18,h-18,icon_shade(base,18),icon_shade(base,-38));
 rect(x+13,y+10,w-26,2,icon_shade(base,100));
 rect(x+12,y+h-14,w-24,2,icon_shade(base,-85));
}
static void icon_embossed_label(int x,int y,int w,int h,const char *label,u32 ink,int large){
 int scale=large?3:2,tw=text_width(label,scale);
 if(tw>w-16){scale=2;tw=text_width(label,scale);}
 if(tw>w-16){scale=1;tw=text_width(label,scale);}
 int tx=x+(w-tw)/2,ty=y+(h-scale125(8*scale))/2;
 text(tx+2,ty+3,label,0xCC000000u,scale);       /* deep drop shadow */
 text(tx-1,ty-1,label,icon_shade(ink,70),scale); /* bevel light */
 text(tx,ty,label,ink,scale);
}
static void draw_folder_icon(int x,int y,int size,int selected){
 int w=size,h=size;u32 base=selected?0xFFD6A642u:0xFFB98725u;
 icon_bevel_frame(x,y,w,h,base,selected);
 int fx=x+w/7,fy=y+h/3,fw=w*5/7,fh=h/2;
 round_rect(fx+3,fy-12,fw/2,18,4,0xFFFBE4A3u);
 round_rect(fx,fy,fw,fh,7,0xFF785017u);
 gradient_rect_v(fx+3,fy+4,fw-6,fh-8,0xFFFFE3A0u,0xFFD2942Au);
 rect(fx+7,fy+8,fw-14,2,0xFFFFF3C5u);
 icon_embossed_label(x,y+h*2/3,w,h/4,"DIR",0xFF183A61u,1);
}
static void draw_archive_cabinet(int x,int y,int w,int h){
 /* A compact two-drawer archive cabinet with handles. */
 round_rect(x+2,y+3,w,h,5,0x88000000u);
 round_rect(x,y,w,h,5,0xFFCEDCE6u);
 gradient_rect_v(x+4,y+4,w-8,h-8,0xFF7997ADu,0xFF283D56u);
 for(int j=0;j<2;j++){
  int yy=y+8+j*(h-14)/2;
  round_rect(x+7,yy,w-14,(h-20)/2,3,0xFF17283Du);
  rect(x+11,yy+3,w-22,2,0xFFB4CBD8u);
  round_rect(x+w/2-9,yy+7,18,5,2,0xFFE3F0F5u);
 }
}
static void draw_txt_document(int x,int y,int w,int h){
 round_rect(x+6,y+5,w*2/3,h-12,3,0xFFECF1E5u);
 for(int j=0;j<5;j++){
  int yy=y+15+j*8;
  line(x+13,yy,x+18,yy-2,0xFF64778Eu);
  line(x+18,yy-2,x+27,yy+1,0xFF64778Eu);
  line(x+27,yy+1,x+39,yy-1,0xFF64778Eu);
 }
 /* Inkwell and fountain pen, using line segments rather than a bitmap. */
 round_rect(x+w-31,y+h-29,22,19,4,0xFF071628u);
 rect(x+w-28,y+h-30,16,4,0xFFB9D6E8u);
 line(x+w-35,y+h-14,x+w-11,y+12,0xFF101927u);
 line(x+w-33,y+h-14,x+w-9,y+12,0xFFE3B96Au);
 line(x+w-11,y+12,x+w-8,y+7,0xFFF8E3B4u);
}
static void draw_file_icon(int x,int y,int size,const char *name,int selected){
 int w=size,h=size;char tag[8];extension_tag(name,tag);
 u32 base=extension_colour(name),ink=icon_text_colour(tag);
 icon_bevel_frame(x,y,w,h,base,selected);
 int pict_y=y+19,pict_h=h*3/5;
 if(!strcmp(tag,".ZIP")||!strcmp(tag,".7Z")||!strcmp(tag,".RAR"))
  draw_archive_cabinet(x+w/4,pict_y,w/2,pict_h);
 else if(!strcmp(tag,".TXT"))
  draw_txt_document(x+w/4,pict_y,w/2,pict_h);
 else if(!strcmp(tag,".ICN")||!strcmp(tag,".ICO")){
  draw_disc(x+w/2,y+h/2-12,w/5,0xFF12283Cu);
  draw_disc(x+w/2,y+h/2-12,w/9,icon_shade(base,85));
 }else if(!strcmp(tag,".PSU")||!strcmp(tag,".CBS")||!strcmp(tag,".MAX")){
  round_rect(x+w/4,y+h/4,w/2,h/3,5,0xFF17283Eu);
  draw_ps2_logo_bars(x+w/4+8,y+h/3,2);
 }else{
  round_rect(x+w/3,y+h/4,w/3,h/3,4,icon_shade(base,75));
  rect(x+w/3+5,y+h/4+8,w/3-10,2,icon_shade(base,-65));
 }
 /* Central, embossed type is always prominent; shorter names get 3x text. */
 int label_y=y+h*3/4-7,label_h=h/5+10;
 round_rect(x+11,label_y-4,w-22,label_h+6,5,0x9A071524u);
 icon_embossed_label(x,label_y,w,label_h,tag,ink,strlen(tag)<=4);
}
static void format_file_size(unsigned long long bytes,int is_dir,char *out,size_t outsz){
 if(!out||!outsz)return;
 if(is_dir){ snprintf(out,outsz,"DIR"); return; }
 if(bytes==~0ULL){ snprintf(out,outsz,"..."); return; }
 const unsigned long long KB=1024ULL, MB=1024ULL*1024ULL, GB=1024ULL*1024ULL*1024ULL;
 if(bytes<KB){ snprintf(out,outsz,"%llu B",bytes); return; }
 double v; const char *unit;
 if(bytes<MB){ v=(double)bytes/(double)KB; unit="KB"; }
 else if(bytes<GB){ v=(double)bytes/(double)MB; unit="MB"; }
 else { v=(double)bytes/(double)GB; unit="GB"; }
 if(v<10.0) snprintf(out,outsz,"%.1f %s",v,unit);
 else snprintf(out,outsz,"%.0f %s",v,unit);
}
static void format_file_date(time_t when,int known,char *out,size_t outsz){
 if(!out||!outsz)return;
 if(!known||when<=0){snprintf(out,outsz,"...");return;}
 struct tm *tmv=localtime(&when);
 if(!tmv){snprintf(out,outsz,"...");return;}
 snprintf(out,outsz,"%02d-%02d-%04d %02d:%02d",tmv->tm_mday,tmv->tm_mon+1,tmv->tm_year+1900,tmv->tm_hour,tmv->tm_min);
}
static time_t psu_time_to_time_t(const psu_time_t *pt){
 if(!pt||pt->year<1980||pt->year>2200||pt->month<1||pt->month>12||pt->day<1||pt->day>31)return (time_t)0;
 struct tm tmv;memset(&tmv,0,sizeof(tmv));
 tmv.tm_year=(int)pt->year-1900;tmv.tm_mon=(int)pt->month-1;tmv.tm_mday=(int)pt->day;
 tmv.tm_hour=(int)pt->hour;tmv.tm_min=(int)pt->min;tmv.tm_sec=(int)pt->sec;tmv.tm_isdst=-1;
 return mktime(&tmv);
}
static time_t latest_stat_time(const struct stat *st){
 if(!st)return (time_t)0;
 return st->st_mtime>st->st_ctime?st->st_mtime:st->st_ctime;
}
static void draw_browser_emblem(int x,int y,int s){
 round_rect(x,y,s,s,4,0xA83A4652u);
 int q=(s-7)/2;
 rect(x+3,y+3,q,q,0xFF4CC2FFu); rect(x+5+q,y+3,q,q,0xFF36C5A5u);
 rect(x+3,y+5+q,q,q,0xFFF2B84Bu); rect(x+5+q,y+5+q,q,q,0xFFB86FD4u);
}
static u16 rd16le_local(const u8 *p){ return (u16)(p[0] | ((u16)p[1] << 8)); }
static u32 rd32le_local(const u8 *p){ return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static s16 rs16le_local(const u8 *p){ return (s16)rd16le_local(p); }
static u32 make_rgb(u8 r,u8 g,u8 b){ return 0xFF000000u|((u32)r<<16)|((u32)g<<8)|(u32)b; }
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static float max3f(float a,float b,float c){ float m=a>b?a:b; return m>c?m:c; }

static void free_preview_payload(icon_preview_t *p){
 if(!p)return;
 free(p->anim_keys); free(p->positions); free(p->normals); free(p->uvs); free(p->colors); free(p->texture); free(p->texture_mip64);
 memset(p,0,sizeof(*p));
}
static void reset_preview_render_caches(){
 g_model_cache_valid=0;g_model_base_valid=0;g_model_buffer_generation[0]=g_model_buffer_generation[1]=0;
 g_preview_startup_hold=0;g_preview_rotation_start_frame=g_frame_counter;
 free(g_model_filter_cache);g_model_filter_cache=NULL;g_model_filter_w=g_model_filter_h=0;
}
static void free_preview(){
 free_preview_payload(&g_preview);
 g_preview.scale=1.0f;
 reset_preview_render_caches();
}
static void clear_icn_cache(){
 for(int i=0;i<ICN_CACHE_SLOTS;i++){
  if(g_icn_cache[i].used)free_preview_payload(&g_icn_cache[i].preview);
  g_icn_cache[i].used=0;g_icn_cache[i].age=0;
 }
 g_icn_cache_clock=1;
}
static void cache_active_preview(){
 if(!g_preview.archive[0]||!g_preview.entry[0])return;
 int slot=-1;
 for(int i=0;i<ICN_CACHE_SLOTS;i++)if(g_icn_cache[i].used &&
    !strcmp(g_icn_cache[i].preview.archive,g_preview.archive) &&
    !strcmp(g_icn_cache[i].preview.entry,g_preview.entry)){slot=i;break;}
 if(slot<0)for(int i=0;i<ICN_CACHE_SLOTS;i++)if(!g_icn_cache[i].used){slot=i;break;}
 if(slot<0){
  slot=0;for(int i=1;i<ICN_CACHE_SLOTS;i++)if(g_icn_cache[i].age<g_icn_cache[slot].age)slot=i;
  free_preview_payload(&g_icn_cache[slot].preview);
 }
 g_icn_cache[slot].preview=g_preview;
 memset(&g_preview,0,sizeof(g_preview));
 g_icn_cache[slot].used=1;g_icn_cache[slot].age=g_icn_cache_clock++;
 reset_preview_render_caches();
}
static int activate_cached_preview(const char *archive,const char *entry){
 for(int i=0;i<ICN_CACHE_SLOTS;i++)if(g_icn_cache[i].used &&
    !strcmp(g_icn_cache[i].preview.archive,archive) && !strcmp(g_icn_cache[i].preview.entry,entry)){
  icon_preview_t hit=g_icn_cache[i].preview;
  memset(&g_icn_cache[i].preview,0,sizeof(g_icn_cache[i].preview));
  g_icn_cache[i].used=0;g_icn_cache[i].age=0;
  cache_active_preview();
  g_preview=hit;
  reset_preview_render_caches();
  g_preview_startup_hold=0;g_preview_rotation_start_frame=g_frame_counter;
  return 1;
 }
 return 0;
}

static u32 icn_bgr555(u16 v){
 u8 r=(u8)((v & 0x1Fu)<<3);
 u8 g=(u8)(((v>>5) & 0x1Fu)<<3);
 u8 b=(u8)(((v>>10)& 0x1Fu)<<3);
 return make_rgb(r,g,b);
}

/* Preview reliability helpers.  These do not expose or retain any additional
   ICN metadata; they only make the existing texture/model preview tolerant of
   valid texture encodings and animation-record layout variants. */
static int decode_icn_raw_texture(const u8 *src,const u8 *endp,u32 *texture){
 if(!src||!endp||!texture||(size_t)(endp-src)<128u*128u*2u)return 0;
 for(int i=0;i<128*128;i++,src+=2)texture[i]=icn_bgr555(rd16le_local(src));
 return 1;
}
static int decode_icn_rle_texture(const u8 *src,const u8 *endp,u32 *texture){
 if(!src||!endp||endp<src)return 0;
 return ps2_icn_decode_rle(src,(size_t)(endp-src),texture);
}
static const u8 *icn_skip_animation_frames(const u8 *cur,const u8 *endp,u32 n_frames,size_t frame_bytes){
 for(u32 i=0;i<n_frames;i++){
  if(frame_bytes<8u || (size_t)(endp-cur)<frame_bytes)return NULL;
  u32 n_keys=rd32le_local(cur+4);
  cur+=frame_bytes;
  if(n_keys>65536u || (size_t)(endp-cur)<(size_t)n_keys*8u)return NULL;
  cur+=(size_t)n_keys*8u;
 }
 return cur;
}

static float icn_anim_weight(const icon_preview_t *p,int shape,float t){
 if(!p->anim_keys || shape<0 || shape>=p->anim_track_count)return shape==0?1.0f:0.0f;
 unsigned count=p->anim_counts[shape];
 if(!count)return shape==0?1.0f:0.0f;
 const float *k=p->anim_keys+(size_t)shape*128u;
 if(t<=k[0])return k[1];
 for(unsigned j=1;j<count;j++){
  float t1=k[j*2];
  if(t<=t1){float t0=k[(j-1)*2],a=(t1>t0)?(t-t0)/(t1-t0):0.0f;
   return k[(j-1)*2+1]*(1.0f-a)+k[j*2+1]*a;}
 }
 return k[(count-1)*2+1];
}
static int parse_icn_preview(const void *data,size_t size,const char *archive,const char *entry){
 free_preview();
 snprintf(g_preview.archive,sizeof(g_preview.archive),"%s",archive?archive:"");
 snprintf(g_preview.entry,sizeof(g_preview.entry),"%s",entry?entry:"");
 snprintf(g_preview.info,sizeof(g_preview.info),"Preview unavailable");
 if(!data || size<20){ snprintf(g_preview.info,sizeof(g_preview.info),"ICN too small"); return 0; }
 const u8 *p=(const u8*)data, *endp=p+size;
 u32 magic=rd32le_local(p+0);
 u32 shapes=rd32le_local(p+4);
 u32 tex_type=rd32le_local(p+8);
 u32 vertex_count=rd32le_local(p+16);
 if(magic!=0x00010000u){ snprintf(g_preview.info,sizeof(g_preview.info),"Unsupported ICN header"); return 0; }
 if(shapes<1||shapes>16||vertex_count<3||vertex_count>4096||(vertex_count%3)!=0){ snprintf(g_preview.info,sizeof(g_preview.info),"ICN geometry unsupported"); return 0; }
 size_t per_vertex=(size_t)shapes*8u + 8u + 8u;
 size_t vertex_bytes=(size_t)vertex_count*per_vertex;
 if(size < 20u + vertex_bytes + 20u){ snprintf(g_preview.info,sizeof(g_preview.info),"ICN truncated"); return 0; }
 float *positions=(float*)malloc((size_t)shapes*(size_t)vertex_count*3u*sizeof(float));
 float *normals=(float*)malloc((size_t)vertex_count*3u*sizeof(float));
 float *uvs=(float*)malloc((size_t)vertex_count*2u*sizeof(float));
 u32 *colors=(u32*)malloc((size_t)vertex_count*sizeof(u32));
 u32 *texture=(u32*)calloc(128u*128u,sizeof(u32));
 u32 *texture_mip64=(u32*)calloc(64u*64u,sizeof(u32));
 if(!positions||!normals||!uvs||!colors||!texture||!texture_mip64){ free(positions); free(normals); free(uvs); free(colors); free(texture); free(texture_mip64); snprintf(g_preview.info,sizeof(g_preview.info),"Out of memory"); return 0; }
 const u8 *cur=p+20;
 const u8 *tex8=NULL;
 float *anim_keys=NULL;
 unsigned short anim_counts[16]={0};
 int anim_valid=0;
 u32 n_frames=0;
 int tex_ok=0;
 float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
 for(u32 i=0;i<vertex_count;i++){
  for(u32 s=0;s<shapes;s++){
   if(cur+8>endp) goto truncated;
   float x=(float)rs16le_local(cur+0)/4096.0f;
   float y=(float)rs16le_local(cur+2)/4096.0f;
   float z=(float)rs16le_local(cur+4)/4096.0f;
   size_t idx=((size_t)s*(size_t)vertex_count+(size_t)i)*3u;
   positions[idx+0]=x; positions[idx+1]=y; positions[idx+2]=z;
   if(x<minx)minx=x; if(y<miny)miny=y; if(z<minz)minz=z;
   if(x>maxx)maxx=x; if(y>maxy)maxy=y; if(z>maxz)maxz=z;
   cur+=8;
  }
  if(cur+8>endp) goto truncated;
  normals[(size_t)i*3u+0]=(float)rs16le_local(cur+0)/4096.0f;
  normals[(size_t)i*3u+1]=(float)rs16le_local(cur+2)/4096.0f;
  normals[(size_t)i*3u+2]=(float)rs16le_local(cur+4)/4096.0f;
  { float q=sqrtf(normals[(size_t)i*3u+0]*normals[(size_t)i*3u+0]+normals[(size_t)i*3u+1]*normals[(size_t)i*3u+1]+normals[(size_t)i*3u+2]*normals[(size_t)i*3u+2]);
    if(q>0.0001f){normals[(size_t)i*3u+0]/=q;normals[(size_t)i*3u+1]/=q;normals[(size_t)i*3u+2]/=q;} }
  cur+=8; /* normal + pad */
  if(cur+8>endp) goto truncated;
  /* Texture_Data: s16 U, s16 V, RGBA */
  uvs[(size_t)i*2u+0]=(float)rs16le_local(cur+0)/4096.0f;
  uvs[(size_t)i*2u+1]=(float)rs16le_local(cur+2)/4096.0f;
  colors[i]=make_rgb(cur[4],cur[5],cur[6]);
  cur+=8;
 }
 if(cur+20>endp) goto truncated;
 /* Animation_Header is 20 bytes; n_frames is the final u32. */
 n_frames=rd32le_local(cur+16);
 if(n_frames>4096u) goto truncated;
 cur+=20;
 /* The clean renderer historically used 8-byte frame records.  Keep that as
    the primary layout, but also accept the documented 16-byte variant if the
    primary path cannot produce a valid texture.  No frame/key data is stored. */
 tex8=icn_skip_animation_frames(cur,endp,n_frames,8u);
 /* Frame records contain a shape index and float32 (time,weight) keys.
    Reject implausible tracks and fall back to the static base model. */
 if(tex8 && n_frames>0 && n_frames<=shapes){
  anim_keys=(float*)calloc(16u*128u,sizeof(float));
  if(anim_keys){
   const u8 *ak=cur;anim_valid=1;
   for(u32 f=0;f<n_frames;f++){
    u32 shape_id=rd32le_local(ak),count=rd32le_local(ak+4);ak+=8;
    if(shape_id>=shapes || count>64u || anim_counts[shape_id]){anim_valid=0;break;}
    anim_counts[shape_id]=(unsigned short)count;
    float last=-1e20f;
    for(u32 j=0;j<count;j++){
     u32 tb=rd32le_local(ak),wb=rd32le_local(ak+4);ak+=8;
     float t,w;memcpy(&t,&tb,4);memcpy(&w,&wb,4);
     if(!isfinite(t)||!isfinite(w)||t<last||t<0.0f||t>100000.0f||fabsf(w)>100.0f){anim_valid=0;break;}
     anim_keys[((size_t)shape_id*64u+j)*2u]=t;
     anim_keys[((size_t)shape_id*64u+j)*2u+1u]=w;
     last=t;
    }
    if(!anim_valid)break;
   }
   if(!anim_valid){free(anim_keys);anim_keys=NULL;memset(anim_counts,0,sizeof(anim_counts));}
  }
 }

 /* PS2 icon texture type is a bitmask: bit 2 enables texture data and
    bit 3 selects RLE compression.  Bits 0/1 are rendering flags, not
    alternate texture encodings. */
 if(tex8 && (tex_type & 4u)){
  if(tex_type & 8u) tex_ok=decode_icn_rle_texture(tex8,endp,texture);
  else tex_ok=decode_icn_raw_texture(tex8,endp,texture);
 }
 {
  float dx=maxx-minx,dy=maxy-miny,dz=maxz-minz;
  float extent=max3f(dx,dy,dz); if(extent<0.001f) extent=1.0f;
  if(tex_ok){
   for(int my=0;my<64;my++)for(int mx=0;mx<64;mx++){
    u32 c0=texture[(my*2)*128+mx*2],c1=texture[(my*2)*128+mx*2+1];
    u32 c2=texture[(my*2+1)*128+mx*2],c3=texture[(my*2+1)*128+mx*2+1];
    u32 r=(((c0>>16)&255)+((c1>>16)&255)+((c2>>16)&255)+((c3>>16)&255)+2)/4;
    u32 g=(((c0>>8)&255)+((c1>>8)&255)+((c2>>8)&255)+((c3>>8)&255)+2)/4;
    u32 b=((c0&255)+(c1&255)+(c2&255)+(c3&255)+2)/4;
    texture_mip64[my*64+mx]=0xFF000000u|(r<<16)|(g<<8)|b;
   }
  }
  g_preview.positions=positions; g_preview.normals=normals; g_preview.uvs=uvs; g_preview.colors=colors; g_preview.texture=texture; g_preview.texture_mip64=texture_mip64;
  g_preview.vertex_count=(int)vertex_count; g_preview.shape_count=(int)shapes; g_preview.face_count=(int)(vertex_count/3u);
  g_preview.valid=1; g_preview.texture_valid=tex_ok;
  g_preview.anim_keys=anim_keys;g_preview.anim_track_count=anim_valid?(int)n_frames:0;
  memcpy(g_preview.anim_counts,anim_counts,sizeof(anim_counts));
  g_preview.anim_length=0.0f;
  if(anim_valid)for(int a=0;a<(int)shapes;a++)if(anim_counts[a]){
   float last=anim_keys[((size_t)a*64u+anim_counts[a]-1u)*2u];
   if(last>g_preview.anim_length)g_preview.anim_length=last;
  }
  /* Start every newly loaded ICN from a deterministic stable frame. */
  g_preview_startup_hold=8;
  g_preview_rotation_start_frame=g_frame_counter;
  g_preview.center[0]=(minx+maxx)*0.5f; g_preview.center[1]=(miny+maxy)*0.5f; g_preview.center[2]=(minz+maxz)*0.5f;
  g_preview.scale=2.6f/extent;
  snprintf(g_preview.info,sizeof(g_preview.info),tex_ok?"128x128 textured ICN":"3D ICN model");
  return 1;
 }
truncated:
 free(anim_keys); free(positions); free(normals); free(uvs); free(colors); free(texture); free(texture_mip64);
 snprintf(g_preview.info,sizeof(g_preview.info),"ICN data truncated/unsupported");
 return 0;
}

/* v1.64 Stage 20: shared entry into the existing ICN parser.  MAX/CBS decoded
 * buffers can use ps2_archive_preview_{max,cbs} with this callback once the
 * archive browser has selected and decoded a save.  No new renderer is used.
 * The callback consumes the caller-owned data synchronously. */
static int existing_icn_preview_callback(const void *bytes,size_t length,
                                         const char *archive,const char *entry,
                                         void *context){
 (void)context;
 return parse_icn_preview(bytes,length,archive,entry);
}

static void join(char*,size_t,const char*,const char*);
static void draw_texture_scaled(int,int,int,int,const u32*);
/* Stage 33: bounded, read-only archive thumbnail cache. Decode only once per
 * archive; never re-enter the MAX/CBS decoder on a normal redraw. */
#define BROWSER_THUMB_SLOTS 64
struct browser_thumb_slot { char path[PATHSZ]; u32 *pixels; int attempted; unsigned long long age; unsigned long long file_size; time_t file_mtime; time_t file_ctime; };
static browser_thumb_slot g_browser_thumbs[BROWSER_THUMB_SLOTS];
static unsigned long long g_browser_thumb_clock=1;
/* Stage 56: one persistent RSX allocation per CPU thumbnail cache slot.
 * GPU texture storage is NOT shared between file rows. These allocations
 * intentionally survive cache eviction: Tiny3D texture memory is monotonic. */
static u32 *g_browser_thumb_rsx[BROWSER_THUMB_SLOTS]={0};
static u32 g_browser_thumb_rsx_off[BROWSER_THUMB_SLOTS]={0};
static const u32 *g_browser_thumb_uploaded_pixels[BROWSER_THUMB_SLOTS]={0};
static int g_browser_thumb_gpu_ready[BROWSER_THUMB_SLOTS]={0};
/* Stage 57: V1 branding artwork is stored in the lower-left 64x64
 * quadrant of its 128x128 atlas. Only crop when BOTH reference fingerprints
 * match; do not change original ICN data or the full 3D preview texture. */
static int icon_diag_parts(const unsigned char *,size_t,unsigned long *,unsigned long *);
static int browser_is_verified_v1_icon(const void *data,size_t size){
 unsigned long geometry=0,texture=0;
 if(!icon_diag_parts((const unsigned char*)data,size,&geometry,&texture))return 0;
 return geometry==0x625B33F0UL &&
        (texture==0xE0D22957UL || texture==0xB4B9F843UL);
}
static void browser_expand_v1_lower_left(u32 *dst,const u32 *src){
 for(int y=0;y<128;y++)for(int x=0;x<128;x++){
  /* Bilinear filtering in the selected 64x64 atlas quadrant. */
  int fx=x*63*256/127,fy=y*63*256/127;
  int x0=fx>>8,y0=fy>>8,x1=x0<63?x0+1:63,y1=y0<63?y0+1:63;
  unsigned wx=fx&255,wy=fy&255;
  u32 a=src[(64+y0)*128+x0],b=src[(64+y0)*128+x1];
  u32 c=src[(64+y1)*128+x0],d=src[(64+y1)*128+x1];
  u32 result=0;
  for(int shift=0;shift<=24;shift+=8){
   unsigned t0=(((a>>shift)&255)*(256-wx)+((b>>shift)&255)*wx+128)>>8;
   unsigned t1=(((c>>shift)&255)*(256-wx)+((d>>shift)&255)*wx+128)>>8;
   result|=((((t0*(256-wy)+t1*wy+128)>>8)&255u)<<shift);
  }
  dst[(127-y)*128+x]=result; /* Stage 60: flip V1 branding vertically only; preserve readable left-to-right lettering. */
 }
}
/* Stage 61: fingerprint decoded pixels without altering the ICN payload. */
static unsigned long browser_texture_crc32(const u32 *pixels){
 unsigned long crc=0xFFFFFFFFUL;
 if(!pixels)return 0;
 for(size_t i=0;i<128u*128u;i++){
  u32 c=pixels[i];unsigned char rgba[4]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c,255};
  for(int k=0;k<4;k++){crc^=rgba[k];for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xEDB88320UL:0);}
 }
 return (crc^0xFFFFFFFFUL)&0xFFFFFFFFUL;
}
static int browser_texture_is_v1(const u32 *pixels){
 unsigned long crc=browser_texture_crc32(pixels);
 return crc==0xE0D22957UL || crc==0xB4B9F843UL;
}
/* Preserve the full unscaled atlas; correct only its vertical presentation. */
static void browser_flip_texture_vertical(u32 *dst,const u32 *src){
 for(int y=0;y<128;y++)memcpy(dst+y*128,src+(127-y)*128,128*sizeof(u32));
}
static int browser_thumb_accept(const void *data,size_t size,const char *archive,const char *entry,void *context){
 (void)context;
 icon_preview_t saved=g_preview;
 memset(&g_preview,0,sizeof(g_preview));
 int ok=parse_icn_preview(data,size,archive,entry);
 if(ok && g_preview.texture_valid && g_preview.texture){
  /* Unknown SPS/XPS icons (possibly V2/patched) use existing browser placeholders. */
  int shark_archive=endswith(archive,".sps")||endswith(archive,".xps")||endswith(archive,".xpo");
  if(shark_archive && !browser_is_verified_v1_icon(data,size))ok=0;
  if(ok){
  u32 *copy=(u32*)malloc(128u*128u*4u);
  if(copy){
   if(browser_is_verified_v1_icon(data,size))browser_expand_v1_lower_left(copy,g_preview.texture);
   else memcpy(copy,g_preview.texture,128u*128u*4u);
   *((u32**)context)=copy;
  }
  }
 }
 free_preview_payload(&g_preview);
 g_preview=saved;
 return *((u32**)context)!=NULL;
}
static const u32 *browser_archive_thumb(const char *name){
 if(!name || (!endswith(name,".psu")&&!endswith(name,".max")&&!endswith(name,".cbs")&&!endswith(name,".sps")&&!endswith(name,".xps")&&!endswith(name,".xpo")))return NULL;
 char path[PATHSZ];join(path,sizeof(path),cwd,name);
 struct stat thumb_st;
 if(stat(path,&thumb_st)!=0 || !S_ISREG(thumb_st.st_mode))return NULL;
 int slot=-1;
 for(int i=0;i<BROWSER_THUMB_SLOTS;i++)if(g_browser_thumbs[i].attempted&&!strcmp(g_browser_thumbs[i].path,path)){
  if(g_browser_thumbs[i].file_size==(unsigned long long)thumb_st.st_size &&
     g_browser_thumbs[i].file_mtime==thumb_st.st_mtime &&
     g_browser_thumbs[i].file_ctime==thumb_st.st_ctime){
   g_browser_thumbs[i].age=g_browser_thumb_clock++;return g_browser_thumbs[i].pixels;
  }
  slot=i;break;
 }
 if(slot<0)for(int i=0;i<BROWSER_THUMB_SLOTS;i++)if(!g_browser_thumbs[i].attempted){slot=i;break;}
 if(slot<0){slot=0;for(int i=1;i<BROWSER_THUMB_SLOTS;i++)if(g_browser_thumbs[i].age<g_browser_thumbs[slot].age)slot=i;}
 browser_thumb_slot *t=&g_browser_thumbs[slot];
 /* Invalidate GPU ownership BEFORE releasing the previous decoded pixels. */
 g_browser_thumb_uploaded_pixels[slot]=NULL;
 g_browser_thumb_gpu_ready[slot]=0;
 free(t->pixels);memset(t,0,sizeof(*t));
 snprintf(t->path,sizeof(t->path),"%s",path);t->attempted=1;t->age=g_browser_thumb_clock++;
 t->file_size=(unsigned long long)thumb_st.st_size;t->file_mtime=thumb_st.st_mtime;t->file_ctime=thumb_st.st_ctime;
 if(endswith(name,".psu")){
  /* Read icon.sys references before falling back to common PS2 icon names. */
  void *sys=NULL;size_t syslen=0;char candidates[4][65]={{0}};int n=0;
  if(psu_read_file(path,"icon.sys",&sys,&syslen)==0&&sys&&syslen>=452&&!memcmp(sys,"PS2D",4)){
   const unsigned char *b=(const unsigned char*)sys;
   /* PS2 icon.sys filenames: view, copy, delete at offsets 260/324/388. */
   for(int j=0;j<3;j++){
    const char *ref=(const char*)(b+260+j*64);
    size_t k=0;while(k<64&&ref[k]&&ref[k]!='/'&&ref[k]!='\\')k++;
    if(k&&k<64){memcpy(candidates[n],ref,k);candidates[n++][k]=0;}
   }
  }
  free(sys);
  snprintf(candidates[n++],65,"icon0.ico");
  for(int j=0;j<n&&!t->pixels;j++){
   void *buf=NULL;size_t len=0;
   if(psu_read_file(path,candidates[j],&buf,&len)==0&&buf){browser_thumb_accept(buf,len,path,candidates[j],&t->pixels);}
   free(buf);
  }
 }else ps2_archive_preview_file(path,browser_thumb_accept,&t->pixels);
 return t->pixels;
}
static void ensure_preview_current(){
 /* v1.47: preview eligibility is decided from the PSU directory entry first.
    Non-ICN selections must never read their payload or enter the ICN parser.
    Keep the last decoded ICN resident so returning to it is an instant cache hit. */
 if(!archive_mode||item_count<=0||selected<0||selected>=item_count||items[selected].is_dir||!preview_entry_is_standard_icn(items[selected].name)){
  /* Keep the decoded ICN in the multi-ICN cache, but do not leave it active.
     A non-ICN selection must present an empty preview panel and the normal
     "Highlight a .ICN file" prompt. cache_active_preview() transfers
     ownership of the decoded payload out of g_preview without destroying it. */
  if(g_preview.archive[0] || g_preview.entry[0]) cache_active_preview();
  return;
 }
 if(g_preview.valid && !strcmp(g_preview.archive,archive_path) && !strcmp(g_preview.entry,items[selected].name)) return;
 if(!g_preview.valid && !strcmp(g_preview.archive,archive_path) && !strcmp(g_preview.entry,items[selected].name)) return;
 if(activate_cached_preview(archive_path,items[selected].name)) return;
 cache_active_preview();
 void *buf=NULL; size_t size=0;
 int rc=psu_read_file(archive_path,items[selected].name,&buf,&size);
 if(rc!=0){
  free_preview();
  snprintf(g_preview.archive,sizeof(g_preview.archive),"%s",archive_path);
  snprintf(g_preview.entry,sizeof(g_preview.entry),"%s",items[selected].name);
  snprintf(g_preview.info,sizeof(g_preview.info),"Preview load failed");
  return;
 }
 existing_icn_preview_callback(buf,size,archive_path,items[selected].name,NULL);
 free(buf);
}

/* Stage 34: warm the existing 3D icon cache before an icon is highlighted.
   One model per event-loop iteration, never from the draw path. Keep at most
   ICN_CACHE_SLOTS models resident; selection still takes precedence. */
static int g_icon_preload_cursor=0;
static int g_icon_preload_count=0;
static char g_icon_preload_archive[PATHSZ]={0};
static void service_icon_preload(){
 if(!archive_mode || !archive_path[0] || g_icon_preload_count>=ICN_CACHE_SLOTS)return;
 if(strcmp(g_icon_preload_archive,archive_path)){
  snprintf(g_icon_preload_archive,sizeof(g_icon_preload_archive),"%s",archive_path);
  g_icon_preload_cursor=0;g_icon_preload_count=0;
 }
 while(g_icon_preload_cursor<item_count){
  int i=g_icon_preload_cursor++;
  if(items[i].is_dir || !preview_entry_is_standard_icn(items[i].name))continue;
  int cached=(g_preview.valid&&!strcmp(g_preview.archive,archive_path)&&!strcmp(g_preview.entry,items[i].name));
  for(int j=0;j<ICN_CACHE_SLOTS&&!cached;j++)
   if(g_icn_cache[j].used&&!strcmp(g_icn_cache[j].preview.archive,archive_path)&&
      !strcmp(g_icn_cache[j].preview.entry,items[i].name))cached=1;
  if(cached)continue;
  void *buf=NULL;size_t size=0;
  if(psu_read_file(archive_path,items[i].name,&buf,&size)==0&&buf){
   /* The parser populates g_preview; save the selected model and restore it. */
   icon_preview_t active=g_preview;
   memset(&g_preview,0,sizeof(g_preview));
   int ok=existing_icn_preview_callback(buf,size,archive_path,items[i].name,NULL);
   if(ok&&g_preview.valid){cache_active_preview();g_icon_preload_count++;}
   else free_preview_payload(&g_preview);
   g_preview=active;
  }
  free(buf);
  break; /* amortize disk I/O and decoding across event-loop iterations */
 }
}
static void line(int x0,int y0,int x1,int y1,u32 col){
 if(g_rsx_ui_overlay){
  /* Phase 4M: queue controller/vector lines instead of forcing a complete
     text+rectangle flush and a Tiny3D draw submission for every segment. */
  g_perf_line_submits++;
  rsx_flush_text_batch();
  rsx_flush_rect_batch();
  if(g_rsx_line_batch_count>=RSX_LINE_BATCH_MAX)rsx_flush_line_batch();
  rsx_line_cmd_t *q=&g_rsx_line_batch[g_rsx_line_batch_count++];
  q->x0=x0;q->y0=y0;q->x1=x1;q->y1=y1;q->col=col;
  return;
 }
 int dx=abs(x1-x0), sx=x0<x1?1:-1;
 int dy=-abs(y1-y0), sy=y0<y1?1:-1;
 int err=dx+dy;
 for(;;){
  px(x0,y0,col);
  if(x0==x1 && y0==y1) break;
  int e2=err*2;
  if(e2>=dy){ err+=dy; x0+=sx; }
  if(e2<=dx){ err+=dx; y0+=sy; }
 }
}

static void draw_texture_scaled(int x,int y,int w,int h,const u32 *tex){
 /* Browser cache textures must never use the global single-preview RSX
  * upload buffer: queued quads can sample it after the next row's upload. */
 if(g_rsx_ui_overlay && tex){
  for(int i=0;i<BROWSER_THUMB_SLOTS;i++){
   if(g_browser_thumbs[i].pixels!=tex)continue;
   if(!g_browser_thumb_rsx[i]){
    g_browser_thumb_rsx[i]=(u32*)tiny3d_AllocTexture(128u*128u*4u);
    if(g_browser_thumb_rsx[i])g_browser_thumb_rsx_off[i]=tiny3d_TextureOffset(g_browser_thumb_rsx[i]);
   }
   if(!g_browser_thumb_rsx[i])return;
   if(!g_browser_thumb_gpu_ready[i] || g_browser_thumb_uploaded_pixels[i]!=tex){
    memcpy(g_browser_thumb_rsx[i],tex,128u*128u*4u);
    g_browser_thumb_uploaded_pixels[i]=tex;
    g_browser_thumb_gpu_ready[i]=1;
   }
   rsx_draw_uploaded_quad(g_browser_thumb_rsx_off[i],128,128,128*4,x,y,w,h,1.0f,1.0f,1);
   return;
  }
 }
 if(!tex||w<=0||h<=0)return;
 if(g_rsx_ui_overlay){
  rsx_preview_tex_init();if(!g_rsx_preview_tex_ready)return;
  memcpy(g_rsx_icn_tex,tex,128u*128u*4u);
  rsx_draw_uploaded_quad(g_rsx_icn_off,128,128,128*4,x,y,w,h,1.0f,1.0f,1);
  return;
 }
 for(int yy=0;yy<h;yy++){
  int sy=(yy*128)/h;
  for(int xx=0;xx<w;xx++){
   int sx=(xx*128)/w;
   px(x+xx,y+yy,tex[sy*128+sx]);
  }
 }
}

static inline float edgef(float ax,float ay,float bx,float by,float pxv,float pyv){
 return (pxv-ax)*(by-ay)-(pyv-ay)*(bx-ax);
}

static u32 modulate_rgb(u32 tex,u32 vc){
 int tr=(tex>>16)&255,tg=(tex>>8)&255,tb=tex&255;
 int vr=(vc>>16)&255,vg=(vc>>8)&255,vb=vc&255;
 /* ICN vertex colors are commonly white; modulate when games use tinting. */
 return make_rgb((u8)(tr*vr/255),(u8)(tg*vg/255),(u8)(tb*vb/255));
}

static int ensure_model_buffers(int w,int h){
 if(w<=0||h<=0)return 0;
 if(g_model_cache && g_model_zbuf && g_model_cache_w==w && g_model_cache_h==h)return 1;
 free(g_model_cache); free(g_model_zbuf);
 g_model_cache=NULL; g_model_zbuf=NULL; g_model_cache_w=g_model_cache_h=0;
 size_t n=(size_t)w*(size_t)h;
 g_model_cache=(u32*)malloc(n*sizeof(u32));
 g_model_zbuf=(float*)malloc(n*sizeof(float));
 if(!g_model_cache||!g_model_zbuf){free(g_model_cache);free(g_model_zbuf);g_model_cache=NULL;g_model_zbuf=NULL;return 0;}
 g_model_cache_w=w;g_model_cache_h=h;g_model_cache_valid=0;
 free(g_model_filter_cache);g_model_filter_cache=NULL;g_model_filter_w=g_model_filter_h=0;
 return 1;
}
static int ensure_model_filter_buffer(int sw,int sh){
 int fw=sw*2,fh=sh*2;
 if(g_model_filter_cache && g_model_filter_w==fw && g_model_filter_h==fh)return 1;
 free(g_model_filter_cache);g_model_filter_cache=NULL;g_model_filter_w=g_model_filter_h=0;
 g_model_filter_cache=(u32*)malloc((size_t)fw*(size_t)fh*sizeof(u32));
 if(!g_model_filter_cache)return 0;
 g_model_filter_w=fw;g_model_filter_h=fh;return 1;
}
static void build_model_filter_cache(){
 if(!g_model_cache||!g_model_cache_valid)return;
 int sw=g_model_cache_w,sh=g_model_cache_h;
 if(!ensure_model_filter_buffer(sw,sh))return;
 int fw=g_model_filter_w,fh=g_model_filter_h;
 /* Lightweight 2x premultiplied-alpha reconstruction.  The true geometry
    raster is now higher resolution, so this pass only smooths the remaining
    scale step instead of trying to invent silhouette detail. */
 for(int y=0;y<fh;y++){
  int gy=((y<<8)+128)/2-128;
  int y0=gy>>8, fy=gy&255;
  if(y0<0){y0=0;fy=0;} int y1=y0+1;if(y1>=sh)y1=sh-1;
  int iy=256-fy;
  u32 *dst=g_model_filter_cache+(size_t)y*(size_t)fw;
  for(int x=0;x<fw;x++){
   int gx=((x<<8)+128)/2-128;
   int x0=gx>>8, fx=gx&255;
   if(x0<0){x0=0;fx=0;} int x1=x0+1;if(x1>=sw)x1=sw-1;
   int ix=256-fx;
   u32 c00=g_model_cache[(size_t)y0*(size_t)sw+x0];
   u32 c10=g_model_cache[(size_t)y0*(size_t)sw+x1];
   u32 c01=g_model_cache[(size_t)y1*(size_t)sw+x0];
   u32 c11=g_model_cache[(size_t)y1*(size_t)sw+x1];
   int a00=c00?255:0,a10=c10?255:0,a01=c01?255:0,a11=c11?255:0;
   int w00=ix*iy,w10=fx*iy,w01=ix*fy,w11=fx*fy;
   int asum=a00*w00+a10*w10+a01*w01+a11*w11;
   int a=asum>>16;
   /* Preserve the true fractional silhouette coverage from reconstruction.
      The previous hard 28..227 remap reintroduced visible stair-steps.
      Only discard essentially invisible fragments; leave the remaining alpha
      untouched so diagonal edges blend smoothly against the preview panel. */
   if(a<4){dst[x]=0;continue;}
   unsigned long long wr=0,wg=0,wb=0,wa=0;
   if(c00){wr+=(unsigned long long)((c00>>16)&255)*w00;wg+=(unsigned long long)((c00>>8)&255)*w00;wb+=(unsigned long long)(c00&255)*w00;wa+=w00;}
   if(c10){wr+=(unsigned long long)((c10>>16)&255)*w10;wg+=(unsigned long long)((c10>>8)&255)*w10;wb+=(unsigned long long)(c10&255)*w10;wa+=w10;}
   if(c01){wr+=(unsigned long long)((c01>>16)&255)*w01;wg+=(unsigned long long)((c01>>8)&255)*w01;wb+=(unsigned long long)(c01&255)*w01;wa+=w01;}
   if(c11){wr+=(unsigned long long)((c11>>16)&255)*w11;wg+=(unsigned long long)((c11>>8)&255)*w11;wb+=(unsigned long long)(c11&255)*w11;wa+=w11;}
   if(!wa){dst[x]=0;continue;}
   u32 r=(u32)(wr/wa),g=(u32)(wg/wa),b=(u32)(wb/wa);
   dst[x]=((u32)a<<24)|(r<<16)|(g<<8)|b;
  }
 }
}
static void refine_model_filter_silhouette(const float *nx,const float *ny,int n,float cx,float cy,float scale_px){
 if(!g_model_filter_cache||!nx||!ny||n<3)return;
 int fw=g_model_filter_w,fh=g_model_filter_h;
 if(fw<=0||fh<=0)return;
 /* Keep RGB reconstructed from the shaded low-res raster, but replace alpha
    with a true high-resolution geometry coverage mask.  Project from the
    original floating-point coordinates rather than doubled integer pixels,
    avoiding the quantisation that caused the remaining staircase edges. */
 for(size_t i=0;i<(size_t)fw*(size_t)fh;i++)g_model_filter_cache[i]&=0x00FFFFFFu;
 float sxmul=2.0f, symul=2.0f;
 float pcx=cx*sxmul,pcy=cy*symul,psc=scale_px*2.0f;
 static const float ox[4]={0.25f,0.75f,0.25f,0.75f};
 static const float oy[4]={0.25f,0.25f,0.75f,0.75f};
 for(int i=0;i+2<n;i+=3){
  float x0=pcx+nx[i]*psc,y0=pcy+ny[i]*psc;
  float x1=pcx+nx[i+1]*psc,y1=pcy+ny[i+1]*psc;
  float x2=pcx+nx[i+2]*psc,y2=pcy+ny[i+2]*psc;
  float area=edgef(x0,y0,x1,y1,x2,y2);if(fabsf(area)<0.0001f)continue;
  float inv=1.0f/area;
  int minx=clampi((int)floorf(fminf(x0,fminf(x1,x2)))-1,0,fw-1);
  int maxx=clampi((int)ceilf(fmaxf(x0,fmaxf(x1,x2)))+1,0,fw-1);
  int miny=clampi((int)floorf(fminf(y0,fminf(y1,y2)))-1,0,fh-1);
  int maxy=clampi((int)ceilf(fmaxf(y0,fmaxf(y1,y2)))+1,0,fh-1);
  for(int py=miny;py<=maxy;py++)for(int pxv=minx;pxv<=maxx;pxv++){
   int covered=0;
   for(int q=0;q<4;q++){
    float fx=(float)pxv+ox[q],fy=(float)py+oy[q];
    float w0=edgef(x1,y1,x2,y2,fx,fy)*inv;
    float w1=edgef(x2,y2,x0,y0,fx,fy)*inv;
    float w2=1.0f-w0-w1;
    if(w0>=0.0f&&w1>=0.0f&&w2>=0.0f)covered++;
   }
   if(!covered)continue;
   size_t di=(size_t)py*(size_t)fw+(size_t)pxv;
   int olda=(int)(g_model_filter_cache[di]>>24);
   int a=covered*64;if(a>255)a=255;if(a<olda)a=olda;
   g_model_filter_cache[di]=(g_model_filter_cache[di]&0x00FFFFFFu)|((u32)a<<24);
  }
 }
 /* New partial pixels can lie just outside the low-res shaded silhouette and
    therefore have no reconstructed RGB.  Pull colour from the nearest opaque
    neighbour so antialiased edges use model colour rather than black. */
 for(int y=0;y<fh;y++)for(int x=0;x<fw;x++){
  size_t di=(size_t)y*(size_t)fw+(size_t)x;u32 c=g_model_filter_cache[di];
  if(!(c>>24)||(c&0x00FFFFFFu))continue;
  u32 rgb=0;
  for(int r=1;r<=2&&!rgb;r++)for(int yy=-r;yy<=r&&!rgb;yy++)for(int xx=-r;xx<=r;xx++){
   int sx=x+xx,sy=y+yy;if((unsigned)sx>=(unsigned)fw||(unsigned)sy>=(unsigned)fh)continue;
   u32 nc=g_model_filter_cache[(size_t)sy*(size_t)fw+(size_t)sx];if((nc>>24)&&(nc&0x00FFFFFFu)){rgb=nc&0x00FFFFFFu;break;}
  }
  if(rgb)g_model_filter_cache[di]=(c&0xFF000000u)|rgb;
 }
}

static int ensure_model_vertex_buffers(int n){
 if(n<=g_model_vertex_cap && g_model_sx&&g_model_sy&&g_model_zinv&&g_model_nx&&g_model_ny)return 1;
 int cap=n<256?256:n;
 int *sx=(int*)realloc(g_model_sx,(size_t)cap*sizeof(int));
 int *sy=(int*)realloc(g_model_sy,(size_t)cap*sizeof(int));
 float *zi=(float*)realloc(g_model_zinv,(size_t)cap*sizeof(float));
 float *nx=(float*)realloc(g_model_nx,(size_t)cap*sizeof(float));
 float *ny=(float*)realloc(g_model_ny,(size_t)cap*sizeof(float));
 if(!sx||!sy||!zi||!nx||!ny){
  if(sx)g_model_sx=sx;if(sy)g_model_sy=sy;if(zi)g_model_zinv=zi;if(nx)g_model_nx=nx;if(ny)g_model_ny=ny;
  return 0;
 }
 g_model_sx=sx;g_model_sy=sy;g_model_zinv=zi;g_model_nx=nx;g_model_ny=ny;g_model_vertex_cap=cap;return 1;
}
static void restore_model_base_to_buffer(u32 *dst);
static void blit_model_cache(int box_x,int box_y,int box_w,int box_h){
 if(!g_model_cache||!g_model_cache_valid||box_w<=0||box_h<=0)return;
 if(g_rsx_ui_overlay){
  rsx_preview_tex_init();if(!g_rsx_preview_tex_ready)return;
  int sw=g_model_cache_w,sh=g_model_cache_h;if(sw>RSX_MODEL_TEX_W)sw=RSX_MODEL_TEX_W;if(sh>RSX_MODEL_TEX_H)sh=RSX_MODEL_TEX_H;
  memset(g_rsx_model_tex,0,RSX_MODEL_TEX_W*RSX_MODEL_TEX_H*4u);
  for(int yy=0;yy<sh;yy++)memcpy(g_rsx_model_tex+(size_t)yy*RSX_MODEL_TEX_W,g_model_cache+(size_t)yy*g_model_cache_w,(size_t)sw*4u);
  rsx_draw_uploaded_quad(g_rsx_model_off,RSX_MODEL_TEX_W,RSX_MODEL_TEX_H,RSX_MODEL_TEX_W*4,box_x,box_y,box_w,box_h,(float)sw/RSX_MODEL_TEX_W,(float)sh/RSX_MODEL_TEX_H,1);
  return;
 }
 /* v1.46 raster fix 2: make model composition self-contained.  Before a
    raster is copied, restore the complete model viewport from the pristine
    backing captured immediately before the model was drawn.  Do this here,
    not only in the fast/rename callers, so transparent pixels can never leave
    an older orientation in either flip buffer. */
 if(g_model_base_valid && g_model_base_cache &&
    box_x==g_model_base_x && box_y==g_model_base_y &&
    box_w==g_model_base_w && box_h==g_model_base_h)
   restore_model_base_to_buffer(g_fb[g_back]);
 const u32 *cache=g_model_filter_cache?g_model_filter_cache:g_model_cache;
 int sw=g_model_filter_cache?g_model_filter_w:g_model_cache_w;
 int sh=g_model_filter_cache?g_model_filter_h:g_model_cache_h;
 /* Edge-aware final upscale.  Interior pixels remain point sampled to keep
    texture detail sharp.  Only cells that cross the model silhouette use a
    bilinear premultiplied-alpha sample, preventing nearest-neighbour stair
    steps from being reintroduced after the 2x reconstruction. */
 u32 xstep=(u32)(((u64)sw<<16)/(u32)box_w);
 u32 ystep=(u32)(((u64)sh<<16)/(u32)box_h);
 u32 yacc=0;
 for(int yy=0;yy<box_h;yy++,yacc+=ystep){
  int sy=(int)(yacc>>16);if(sy>=sh)sy=sh-1;
  int sy1=sy+1;if(sy1>=sh)sy1=sh-1;
  int fy=(int)((yacc>>8)&255u),iy=256-fy;
  u32 *dst=g_fb[g_back]+(size_t)(box_y+yy)*(g_pitch/4)+(size_t)box_x;
  u32 xacc=0;
  for(int xx=0;xx<box_w;xx++,xacc+=xstep){
   int sx=(int)(xacc>>16);if(sx>=sw)sx=sw-1;
   int sx1=sx+1;if(sx1>=sw)sx1=sw-1;
   int fx=(int)((xacc>>8)&255u),ix=256-fx;
   u32 c00=cache[(size_t)sy*(size_t)sw+(size_t)sx];
   u32 c10=cache[(size_t)sy*(size_t)sw+(size_t)sx1];
   u32 c01=cache[(size_t)sy1*(size_t)sw+(size_t)sx];
   u32 c11=cache[(size_t)sy1*(size_t)sw+(size_t)sx1];
   int a00=c00?(int)((c00>>24)&255u):0;
   int a10=c10?(int)((c10>>24)&255u):0;
   int a01=c01?(int)((c01>>24)&255u):0;
   int a11=c11?(int)((c11>>24)&255u):0;
   if(a00==0&&a10==0&&a01==0&&a11==0){
    /* v1.46 fix 3: a transparent model-cache pixel must actively restore the
       pristine model background.  Leaving dst untouched makes old rotated
       silhouettes survive when the rename modal animates the ICN. */
    if(g_model_base_valid && g_model_base_cache && box_x==g_model_base_x && box_y==g_model_base_y &&
       box_w==g_model_base_w && box_h==g_model_base_h)
      dst[xx]=g_model_base_cache[(size_t)yy*(size_t)g_model_base_w+(size_t)xx];
    continue;
   }
   if(a00==255&&a10==255&&a01==255&&a11==255){dst[xx]=c00;continue;}
   int w00=ix*iy,w10=fx*iy,w01=ix*fy,w11=fx*fy;
   unsigned long long asum=(unsigned long long)a00*w00+(unsigned long long)a10*w10+(unsigned long long)a01*w01+(unsigned long long)a11*w11;
   int a=(int)(asum>>16);
   if(a<=1){
    if(g_model_base_valid && g_model_base_cache && box_x==g_model_base_x && box_y==g_model_base_y &&
       box_w==g_model_base_w && box_h==g_model_base_h)
      dst[xx]=g_model_base_cache[(size_t)yy*(size_t)g_model_base_w+(size_t)xx];
    continue;
   }
   unsigned long long pr=0,pg=0,pb=0;
   if(a00){pr+=(unsigned long long)((c00>>16)&255u)*a00*w00;pg+=(unsigned long long)((c00>>8)&255u)*a00*w00;pb+=(unsigned long long)(c00&255u)*a00*w00;}
   if(a10){pr+=(unsigned long long)((c10>>16)&255u)*a10*w10;pg+=(unsigned long long)((c10>>8)&255u)*a10*w10;pb+=(unsigned long long)(c10&255u)*a10*w10;}
   if(a01){pr+=(unsigned long long)((c01>>16)&255u)*a01*w01;pg+=(unsigned long long)((c01>>8)&255u)*a01*w01;pb+=(unsigned long long)(c01&255u)*a01*w01;}
   if(a11){pr+=(unsigned long long)((c11>>16)&255u)*a11*w11;pg+=(unsigned long long)((c11>>8)&255u)*a11*w11;pb+=(unsigned long long)(c11&255u)*a11*w11;}
   unsigned long long denom=asum;if(!denom)continue;
   u32 r=(u32)(pr/denom),g=(u32)(pg/denom),b=(u32)(pb/denom);
   u32 c=((u32)a<<24)|(r<<16)|(g<<8)|b;
   /* v1.46 replacement compositor: the completed ICN raster is composed
      against the canonical clean model backing, never against framebuffer
      pixels left by an earlier orientation.  This makes each model frame a
      true replacement even if a flip buffer contains stale model pixels. */
   u32 base=0xFF000000u;
   if(g_model_base_valid && g_model_base_cache &&
      box_x==g_model_base_x && box_y==g_model_base_y &&
      box_w==g_model_base_w && box_h==g_model_base_h)
     base=g_model_base_cache[(size_t)yy*(size_t)g_model_base_w+(size_t)xx];
   if(a>=254)dst[xx]=0xFF000000u|(c&0x00FFFFFFu);
   else dst[xx]=blend_argb_over(base,c);
  }
 }
}
static int capture_model_base(int x,int y,int w,int h){
 if(w<=0||h<=0)return 0;
 size_t n=(size_t)w*(size_t)h;
 if(!g_model_base_cache || g_model_base_w!=w || g_model_base_h!=h){
  free(g_model_base_cache);g_model_base_cache=(u32*)malloc(n*sizeof(u32));
  if(!g_model_base_cache){g_model_base_valid=0;return 0;}
 }
 g_model_base_x=x;g_model_base_y=y;g_model_base_w=w;g_model_base_h=h;
 for(int yy=0;yy<h;yy++)memcpy(g_model_base_cache+(size_t)yy*(size_t)w,
  g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,(size_t)w*sizeof(u32));
 g_model_base_valid=1;return 1;
}
static void restore_model_base_to_buffer(u32 *dst){
 if(!g_model_base_valid||!g_model_base_cache||!dst)return;
 for(int yy=0;yy<g_model_base_h;yy++)memcpy(dst+(size_t)(g_model_base_y+yy)*(g_pitch/4)+(size_t)g_model_base_x,
  g_model_base_cache+(size_t)yy*(size_t)g_model_base_w,(size_t)g_model_base_w*sizeof(u32));
}
static int capture_preview_base(int x,int y,int w,int h){
 if(w<=0||h<=0)return 0;
 size_t n=(size_t)w*(size_t)h;
 if(!g_preview_base_cache || g_preview_cache_w!=w || g_preview_cache_h!=h){
  free(g_preview_base_cache); g_preview_base_cache=(u32*)malloc(n*sizeof(u32));
  if(!g_preview_base_cache){g_preview_base_valid=0;return 0;}
 }
 g_preview_cache_x=x;g_preview_cache_y=y;g_preview_cache_w=w;g_preview_cache_h=h;
 for(int yy=0;yy<h;yy++){
  memcpy(g_preview_base_cache+(size_t)yy*(size_t)w,
         g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,
         (size_t)w*sizeof(u32));
 }
 g_preview_base_valid=1;return 1;
}
static void restore_preview_base_to_buffer(u32 *dst){
 if(!g_preview_base_valid||!g_preview_base_cache||!dst)return;
 for(int yy=0;yy<g_preview_cache_h;yy++){
  memcpy(dst+(size_t)(g_preview_cache_y+yy)*(g_pitch/4)+(size_t)g_preview_cache_x,
         g_preview_base_cache+(size_t)yy*(size_t)g_preview_cache_w,
         (size_t)g_preview_cache_w*sizeof(u32));
 }
}
static inline u32 nearest_sample_wrap(const u32 *tex,int size,float u,float v){
 int mask=size-1;
 int x=((int)(u*(float)size+1048576.0f))&mask;
 int y=((int)(v*(float)size+1048576.0f))&mask;
 return tex[y*size+x];
}
static inline u32 bilinear_sample_wrap(const u32 *tex,int size,float u,float v){
 int mask=size-1;
 float fs=(float)(size*256);
 int uf=(int)(u*fs+268435456.0f), vf=(int)(v*fs+268435456.0f);
 int x0=(uf>>8)&mask,y0=(vf>>8)&mask,x1=(x0+1)&mask,y1=(y0+1)&mask;
 int fx=uf&255,fy=vf&255,ix=256-fx,iy=256-fy;
 u32 c00=tex[y0*size+x0],c10=tex[y0*size+x1],c01=tex[y1*size+x0],c11=tex[y1*size+x1];
 int w00=ix*iy,w10=fx*iy,w01=ix*fy,w11=fx*fy;
 u32 r=((((c00>>16)&255)*w00+((c10>>16)&255)*w10+((c01>>16)&255)*w01+((c11>>16)&255)*w11)>>16);
 u32 g=((((c00>>8)&255)*w00+((c10>>8)&255)*w10+((c01>>8)&255)*w01+((c11>>8)&255)*w11)>>16);
 u32 b=(((c00&255)*w00+(c10&255)*w10+(c01&255)*w01+(c11&255)*w11)>>16);
 return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static inline u32 blend_rgb_lod(u32 a,u32 b,int t){
 int it=256-t;
 u32 r=((((a>>16)&255)*it+((b>>16)&255)*t)>>8);
 u32 g=((((a>>8)&255)*it+((b>>8)&255)*t)>>8);
 u32 bl=(((a&255)*it+(b&255)*t)>>8);
 return 0xFF000000u|(r<<16)|(g<<8)|bl;
}
static void draw_textured_model(int box_x,int box_y,int box_w,int box_h){
 if(!g_preview.valid||!g_preview.positions||!g_preview.uvs||box_w<40||box_h<40)return;
 /* Adaptive true raster resolution.  Simple and moderate ICNs get substantially
    more real geometry pixels; only heavy meshes fall back to the compact path. */
 int max_w;
 if(g_height>=1000){
  if(g_preview.face_count<=400)max_w=128;
  else if(g_preview.face_count<=700)max_w=112;
  else if(g_preview.face_count<=1100)max_w=96;
  else if(g_preview.face_count<=1800)max_w=84;
  else max_w=72;
 }else{
  if(g_preview.face_count<=400)max_w=116;
  else if(g_preview.face_count<=700)max_w=102;
  else if(g_preview.face_count<=1100)max_w=88;
  else if(g_preview.face_count<=1800)max_w=76;
  else max_w=66;
 }
 int rw=box_w<max_w?box_w:max_w;
 int rh=(int)((long long)box_h*(long long)rw/(long long)box_w);
 if(rh<60){rh=60;if(rh>box_h)rh=box_h;}
 int max_h;
 if(g_height>=1000){
  if(g_preview.face_count<=400)max_h=116;
  else if(g_preview.face_count<=700)max_h=104;
  else if(g_preview.face_count<=1100)max_h=90;
  else if(g_preview.face_count<=1800)max_h=78;
  else max_h=66;
 }else{
  if(g_preview.face_count<=400)max_h=104;
  else if(g_preview.face_count<=700)max_h=92;
  else if(g_preview.face_count<=1100)max_h=80;
  else if(g_preview.face_count<=1800)max_h=70;
  else max_h=60;
 }
 if(rh>max_h)rh=max_h;
 if(!ensure_model_buffers(rw,rh))return;
 int rerender=!g_model_cache_valid || (g_frame_counter-g_model_cache_frame)>=model_update_interval();
 if(!rerender){blit_model_cache(box_x,box_y,box_w,box_h);return;}
 size_t pixels=(size_t)rw*(size_t)rh;
 memset(g_model_cache,0,pixels*sizeof(u32));
 for(size_t i=0;i<pixels;i++)g_model_zbuf[i]=-1e30f;
 float base_rx=-0.28f;
 /* Animated memory-card icons already contain their own motion. Do not impose
    the viewer spin on top of their authored morph animation. */
 float base_ry=(g_preview_startup_hold>0 || (g_preview.anim_keys && g_preview.anim_length>0.0f))?0.0f:(float)(g_frame_counter-g_preview_rotation_start_frame)*0.018f;
 /* Blend the morph targets according to their actual animation key tracks.
    Use static base shape if the archive has no validated tracks. */
 float weights[16]={0};
 if(g_preview.anim_keys && g_preview.anim_length>0.0f){
  float t=fmodf((float)(g_frame_counter-g_preview_rotation_start_frame)*(1.0f/60.0f)*16.0f,g_preview.anim_length);
  for(int a=0;a<g_preview.shape_count;a++)weights[a]=icn_anim_weight(&g_preview,a,t);
 }else weights[0]=1.0f;
 float crx=cosf(base_rx),srx=sinf(base_rx),cry=cosf(base_ry),sry=sinf(base_ry);
 int n=g_preview.vertex_count;
 /* Real PS3 PPU fast path: keep perspective-correct UVs for light/moderate
    meshes, but use affine UV interpolation for dense icons to avoid a costly
    reciprocal for every covered pixel. Depth testing remains perspective based. */
 int perspective_correct=(g_preview.face_count<=700);
 if(!ensure_model_vertex_buffers(n)){blit_model_cache(box_x,box_y,box_w,box_h);return;}
 int *sx=g_model_sx,*sy=g_model_sy;float *zinv=g_model_zinv,*nx=g_model_nx,*ny=g_model_ny;
 float min_nx=1e30f,max_nx=-1e30f,min_ny=1e30f,max_ny=-1e30f;
 for(int i=0;i<n;i++){
  float x=0.0f,yv=0.0f,z=0.0f;
  for(int a=0;a<g_preview.shape_count;a++)if(fabsf(weights[a])>0.00001f){
   size_t off=((size_t)a*(size_t)n+(size_t)i)*3u;
   x+=g_preview.positions[off]*weights[a];
   yv+=g_preview.positions[off+1]*weights[a];
   z+=g_preview.positions[off+2]*weights[a];
  }
  x=(x-g_preview.center[0])*g_preview.scale;
  yv=(yv-g_preview.center[1])*g_preview.scale;
  z=(z-g_preview.center[2])*g_preview.scale;
  float xr=x*cry+z*sry,zr=-x*sry+z*cry;
  float yr=yv*crx-zr*srx,zr2=yv*srx+zr*crx+3.2f;if(zr2<0.2f)zr2=0.2f;
  nx[i]=xr/zr2;ny[i]=yr/zr2;zinv[i]=1.0f/zr2;
  if(nx[i]<min_nx)min_nx=nx[i];if(nx[i]>max_nx)max_nx=nx[i];
  if(ny[i]<min_ny)min_ny=ny[i];if(ny[i]>max_ny)max_ny=ny[i];
 }
 float span_x=max_nx-min_nx,span_y=max_ny-min_ny;if(span_x<0.001f)span_x=0.001f;if(span_y<0.001f)span_y=0.001f;
 float margin=4.0f;
 float scale_x=((float)rw-margin*2.0f)/span_x,scale_y=((float)rh-margin*2.0f)/span_y;
 float scale_px=(scale_x<scale_y?scale_x:scale_y)*0.97f;
 float center_nx=(min_nx+max_nx)*0.5f,center_ny=(min_ny+max_ny)*0.5f;
 float cx=(float)rw*0.5f-center_nx*scale_px,cy=(float)rh*0.5f-center_ny*scale_px;
 for(int i=0;i<n;i++){sx[i]=(int)(cx+nx[i]*scale_px);sy[i]=(int)(cy+ny[i]*scale_px);}
 for(int i=0;i+2<n;i+=3){
  int x0=sx[i],y0=sy[i],x1=sx[i+1],y1=sy[i+1],x2=sx[i+2],y2=sy[i+2];
  float area=edgef((float)x0,(float)y0,(float)x1,(float)y1,(float)x2,(float)y2);if(fabsf(area)<0.001f)continue;
  int minx=clampi(x0<x1?(x0<x2?x0:x2):(x1<x2?x1:x2),0,rw-1),maxx=clampi(x0>x1?(x0>x2?x0:x2):(x1>x2?x1:x2),0,rw-1);
  int miny=clampi(y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2),0,rh-1),maxy=clampi(y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2),0,rh-1);
  float inv_area=1.0f/area;
  float u0=g_preview.uvs[(size_t)i*2u],v0=g_preview.uvs[(size_t)i*2u+1],u1=g_preview.uvs[(size_t)(i+1)*2u],v1=g_preview.uvs[(size_t)(i+1)*2u+1],u2=g_preview.uvs[(size_t)(i+2)*2u],v2=g_preview.uvs[(size_t)(i+2)*2u+1];
  /* Perspective-correct texture coordinates.  Affine UV interpolation is
     cheaper, but visibly bends textures as a face turns away from camera.
     Pre-multiply U/V by reciprocal Z once per vertex, then do only one
     reciprocal per covered pixel and reuse it for both coordinates. */
  float u0z=u0*zinv[i],v0z=v0*zinv[i];
  float u1z=u1*zinv[i+1],v1z=v1*zinv[i+1];
  float u2z=u2*zinv[i+2],v2z=v2*zinv[i+2];
  float du=fabsf(u1-u0);if(fabsf(u2-u0)>du)du=fabsf(u2-u0);if(fabsf(u2-u1)>du)du=fabsf(u2-u1);
  float dv=fabsf(v1-v0);if(fabsf(v2-v0)>dv)dv=fabsf(v2-v0);if(fabsf(v2-v1)>dv)dv=fabsf(v2-v1);
  int pix_span=(maxx-minx)>(maxy-miny)?(maxx-minx):(maxy-miny);if(pix_span<1)pix_span=1;
  float tex_span=(du>dv?du:dv)*128.0f,footprint=tex_span/(float)pix_span;
  int lod_blend=0;if(footprint>1.0f){lod_blend=(int)((footprint-1.0f)*192.0f);if(lod_blend>256)lod_blend=256;}
  u32 vc0=g_preview.colors[i],vc1=g_preview.colors[i+1],vc2=g_preview.colors[i+2];
  u32 tri_tint=make_rgb((u8)((((vc0>>16)&255)+((vc1>>16)&255)+((vc2>>16)&255))/3),(u8)((((vc0>>8)&255)+((vc1>>8)&255)+((vc2>>8)&255))/3),(u8)(((vc0&255)+(vc1&255)+(vc2&255))/3));
  /* Stage 63: Some untextured ICNs (ICO boy_blk.ico) contain zero RGB
     at every vertex. The old renderer multiplied by zero, producing a
     featureless black shape. Use a neutral diagnostic material only for
     zero-colour, untextured triangles so normals and icon.sys lighting can
     reveal geometry. This is NOT a recovered original texture/colour. */
  if(!g_preview.texture_valid && (tri_tint&0x00FFFFFFu)==0u)
   tri_tint=make_rgb(200,200,200);
  /* PS2 Browser-style icon.sys lighting.  Evaluate once per triangle to keep
     the real-PS3 PPU raster inner loop unchanged. Normals rotate with the ICN;
     the icon.sys lights remain in viewer space. */
  if(g_iconsys_lighting_valid && g_preview.normals){
   float ar=0.0f,ag=0.0f,ab=0.0f;
   for(int vv=0;vv<3;vv++){
    const float *nn=&g_preview.normals[(size_t)(i+vv)*3u];
    float x=nn[0],y=nn[1],z=nn[2];
    float xr=x*cry+z*sry,zr=-x*sry+z*cry;
    float yr=y*crx-zr*srx,z2=y*srx+zr*crx;
    float lr=g_iconsys_ambient[0],lg=g_iconsys_ambient[1],lb=g_iconsys_ambient[2];
    for(int l=0;l<3;l++){
     float dot=xr*g_iconsys_light_dir[l][0]+yr*g_iconsys_light_dir[l][1]+z2*g_iconsys_light_dir[l][2];
     if(dot<0.0f)dot=0.0f;
     lr+=dot*g_iconsys_light_col[l][0]; lg+=dot*g_iconsys_light_col[l][1]; lb+=dot*g_iconsys_light_col[l][2];
    }
    if(lr<0)lr=0;if(lr>1)lr=1;if(lg<0)lg=0;if(lg>1)lg=1;if(lb<0)lb=0;if(lb>1)lb=1;
    ar+=lr;ag+=lg;ab+=lb;
   }
   ar/=3.0f;ag/=3.0f;ab/=3.0f;
   int rr=(int)(((tri_tint>>16)&255)*ar+0.5f),gg=(int)(((tri_tint>>8)&255)*ag+0.5f),bb=(int)((tri_tint&255)*ab+0.5f);
   tri_tint=make_rgb((u8)clampi(rr,0,255),(u8)clampi(gg,0,255),(u8)clampi(bb,0,255));
  }
  float startx=(float)minx+0.5f,starty=(float)miny+0.5f;
  float e0_row=edgef((float)x1,(float)y1,(float)x2,(float)y2,startx,starty),e1_row=edgef((float)x2,(float)y2,(float)x0,(float)y0,startx,starty);
  float de0dx=(float)(y2-y1),de0dy=(float)-(x2-x1),de1dx=(float)(y0-y2),de1dy=(float)-(x0-x2);
  for(int pyv=miny;pyv<=maxy;pyv++){
   float e0=e0_row,e1=e1_row;int zi=pyv*rw+minx;
   for(int pxv=minx;pxv<=maxx;pxv++,zi++,e0+=de0dx,e1+=de1dx){
    float w0=e0*inv_area,w1=e1*inv_area,w2=1.0f-w0-w1;if(w0<0.0f||w1<0.0f||w2<0.0f)continue;
    float invz=w0*zinv[i]+w1*zinv[i+1]+w2*zinv[i+2];if(invz<=g_model_zbuf[zi])continue;g_model_zbuf[zi]=invz;
    float u,v;
    if(perspective_correct){
     float rz=1.0f/invz;
     u=(w0*u0z+w1*u1z+w2*u2z)*rz;
     v=(w0*v0z+w1*v1z+w2*v2z)*rz;
    }else{
     u=w0*u0+w1*u1+w2*u2;
     v=w0*v0+w1*v1+w2*v2;
    }
    /* Choose one LOD per triangle instead of doing and blending two bilinear
       texture samples per pixel. This preserves minification filtering while
       roughly halving the texture-filter cost on the PPU. */
    const u32 *sample_tex=(lod_blend>96&&g_preview.texture_mip64)?g_preview.texture_mip64:g_preview.texture;
    int sample_size=(sample_tex==g_preview.texture_mip64)?64:128;
    u32 texel=g_preview.texture_valid ? ((g_preview.face_count<=400)?bilinear_sample_wrap(sample_tex,sample_size,u,v):nearest_sample_wrap(sample_tex,sample_size,u,v)) : 0xFFFFFFFFu;
    if((tri_tint&0x00FFFFFFu)==0x00FFFFFFu)g_model_cache[zi]=texel;
    else g_model_cache[zi]=modulate_rgb(texel,tri_tint);
   }
   e0_row+=de0dy;e1_row+=de1dy;
  }
 }
 /* Do not run the previous full 2x geometry SSAA pass here.  It re-rasterized
    every triangle with four sub-samples and was the source of the ~15 FPS
    regression.  The higher true raster resolution above plus the inexpensive
    2x fractional-alpha reconstruction gives a much better quality/performance
    balance while keeping perspective-correct texture mapping intact. */
 g_model_cache_valid=1;g_model_cache_frame=g_frame_counter;
 g_model_generation++; if(g_model_generation<=0)g_model_generation=1;
 /* Do not build the old 2x premultiplied reconstruction cache on real PS3.
    It performs a large amount of 64-bit arithmetic/division on the PPU and is
    disproportionately expensive compared with its visual benefit. */
 free(g_model_filter_cache);g_model_filter_cache=NULL;g_model_filter_w=g_model_filter_h=0;
 blit_model_cache(box_x,box_y,box_w,box_h);
}


/* v1.46 isolated rename raster: render the rename-time ICN through a brand-new
   colour/Z cache which has no relationship with the persistent animation
   cache.  draw_textured_model() clears both allocations before triangle 0 and
   immediately composites the completed raster.  Afterwards the normal cache
   is restored untouched. */
static void draw_textured_model_isolated(int box_x,int box_y,int box_w,int box_h){
 u32 *saved_cache=g_model_cache; float *saved_z=g_model_zbuf;
 u32 *saved_filter=g_model_filter_cache;
 int saved_w=g_model_cache_w,saved_h=g_model_cache_h;
 int saved_valid=g_model_cache_valid,saved_frame=g_model_cache_frame;
 int saved_fw=g_model_filter_w,saved_fh=g_model_filter_h;
 int saved_generation=g_model_generation;
 g_model_cache=NULL;g_model_zbuf=NULL;g_model_filter_cache=NULL;
 g_model_cache_w=g_model_cache_h=0;g_model_filter_w=g_model_filter_h=0;
 g_model_cache_valid=0;g_model_cache_frame=-1000000;
 draw_textured_model(box_x,box_y,box_w,box_h);
 free(g_model_cache);free(g_model_zbuf);free(g_model_filter_cache);
 g_model_cache=saved_cache;g_model_zbuf=saved_z;g_model_filter_cache=saved_filter;
 g_model_cache_w=saved_w;g_model_cache_h=saved_h;
 g_model_filter_w=saved_fw;g_model_filter_h=saved_fh;
 g_model_cache_valid=saved_valid;g_model_cache_frame=saved_frame;
 g_model_generation=saved_generation;
}

static void draw_preview_panel(int x,int y,int w,int h){
 if(w<180||h<160)return;
 round_rect(x+5,y+7,w,h,9,0x42000000u);
 round_rect(x,y,w,h,9,0xE0102030u);
 gradient_rect_v(x+2,y+2,w-4,scale125(12*g_help_scale)+18,0xFF315777u,0xFF1B344Cu);
 rect(x+8,y+1,w-16,2,0xFF69D7FFu);
 rect(x+4,y+5,5,h-10,0xFF25C9FFu);
 rect(x+w-2,y+10,1,h-20,0x804C7894u);
 text(x+16,y+8,"ICN PREVIEW",0xFF7DE6FFu,g_help_scale);
 const char *name=(g_preview.entry[0]?g_preview.entry:(archive_mode&&item_count>0&&!items[selected].is_dir?items[selected].name:""));
 int line_step=scale125(8*g_help_scale)+4;
 int title_y=y+8+scale125(10*g_help_scale);
 int name_lines=0;
 if(name&&*name){
  char lines[2][256]={{0}};
  name_lines=wrap_text_lines(name,w-20,g_help_scale,lines,2);
  for(int i=0;i<name_lines;i++)text(x+14,title_y+i*line_step,lines[i],0xFFF0F5F9u,g_help_scale);
 }
 if(name_lines<1) name_lines=1;
 int content_y=title_y+line_step*name_lines+10;
 int content_h=y+h-content_y-12;
 if(content_h<70)content_h=70;

 round_rect(x+10,content_y,w-20,content_h,6,0xD009121Cu);
 rect(x+16,content_y+1,w-32,1,0x70506A80u);
 rect(x+16,content_y+content_h-2,w-32,1,0x50314558u);

 if(g_preview.valid && g_preview.texture_valid){
  /* With the statistics box removed, divide the full preview area between the
     texture and model.  Keep both comfortably sized without touching the
     working v24 renderer. */
  int tex_side=content_h/2;
  if(tex_side>w-54)tex_side=w-54;
  if(tex_side>240)tex_side=240;
  if(tex_side<64)tex_side=64;
  int tx=x+(w-tex_side)/2;
  int ty=content_y+8;
  /* Stage 61: V1 texture preview uses full 128x128 atlas, vertically flipped.
     No crop/scale rewrite and no modification of the 3D model texture. */
  const u32 *preview_pixels=g_preview.texture;
  u32 *oriented=NULL;
  if(browser_texture_is_v1(g_preview.texture)){
   oriented=(u32*)malloc(128u*128u*sizeof(u32));
   if(oriented){browser_flip_texture_vertical(oriented,g_preview.texture);preview_pixels=oriented;}
  }
  draw_texture_scaled(tx,ty,tex_side,tex_side,preview_pixels);
  free(oriented);
  rect(tx-1,ty-1,tex_side+2,1,0xFFDDE8F0u);rect(tx-1,ty+tex_side,tex_side+2,1,0xFFDDE8F0u);rect(tx-1,ty-1,1,tex_side+2,0xFFDDE8F0u);rect(tx+tex_side,ty-1,1,tex_side+2,0xFFDDE8F0u);
  round_rect(x+16,content_y+5,text_width("TEXTURE",1)+14,17,4,0xE0204D69u);
  text(x+23,content_y+8,"TEXTURE",0xFF7DE6FFu,1);
  int model_y=ty+tex_side+10;
  int model_avail=content_y+content_h-model_y-8;
  if(model_avail>=72){
   round_rect(x+14,model_y-4,w-28,model_avail+8,5,0x6A0B1118u);
   rect(x+14,model_y-4,w-28,1,0x40556779u); rect(x+14,model_y+model_avail+3,w-28,1,0x40556779u);
   rect(x+14,model_y-4,1,model_avail+8,0x40556779u); rect(x+w-15,model_y-4,1,model_avail+8,0x40556779u);
   round_rect(x+20,model_y+2,text_width("3D MODEL",1)+14,17,4,0xE0204D69u);
   text(x+27,model_y+5,"3D MODEL",0xFF7DE6FFu,1);
   if(!g_rsx_ui_overlay)capture_model_base(x+18,model_y,w-36,model_avail);
   if(g_rename_isolated_model_raster&&!g_rsx_ui_overlay)draw_textured_model_isolated(x+18,model_y,w-36,model_avail); else draw_textured_model(x+18,model_y,w-36,model_avail);
  }
 }else if(g_preview.valid){
  int model_h=content_h-24;
  if(model_h>300)model_h=300;
  int model_y=content_y+(content_h-model_h)/2;
  if(!g_rsx_ui_overlay)capture_model_base(x+16,model_y,w-32,model_h);
  if(g_rename_isolated_model_raster&&!g_rsx_ui_overlay)draw_textured_model_isolated(x+16,model_y,w-32,model_h); else draw_textured_model(x+16,model_y,w-32,model_h);
 }else{
  const char *msg=(archive_mode&&item_count>0&&selected<item_count&&!items[selected].is_dir&&preview_entry_is_standard_icn(items[selected].name))?g_preview.info:"Highlight a .ICN file";
  text(x+18,content_y+content_h/2,msg,0xFFF3F7FBu,g_help_scale);
 }
}

static int video_init(){
 /* v1.50 experimental RSX backend phase 1: Tiny3D owns video/RSX setup and
    presentation, matching the architecture used by mature PSL1GHT homebrew.
    The existing software compositor is deliberately retained for this first
    A/B build and writes into Tiny3D's inactive framebuffer. This removes our
    duplicate low-level RSX/display ownership before individual UI layers are
    migrated to GPU quads/font atlases in subsequent passes. */
 if(tiny3d_Init(16*1024*1024)!=0)return -1;
 /* Match the proven Tiny3D viewport setup used by Apollo. Tiny3D's 2D
    coordinate system is 848x512; the viewport scales that virtual surface to
    the active PS3 output mode. */
 tiny3d_UserViewport(1,0.0f,0.0f,
   (float)Video_Resolution.width/848.0f,(float)Video_Resolution.height/512.0f,
   (float)Video_Resolution.width/1920.0f,(float)Video_Resolution.height/1080.0f);
 tiny3d_Project2D();
 g_rsx_font_ready=0; rsx_font_init();
 g_ctx=(gcmContextData*)tiny3d_Get_GCM_Context();
 g_width=(u32)Video_Resolution.width;
 g_height=(u32)Video_Resolution.height;
 g_pitch=(u32)Video_pitch;
 g_fb[0]=Video_buffer[0]; g_fb[1]=Video_buffer[1];
 if(!g_ctx||!g_fb[0]||!g_fb[1]||!g_width||!g_height||!g_pitch)return -2;
 memset(g_fb[0],0,g_pitch*g_height); memset(g_fb[1],0,g_pitch*g_height);
 g_back=Video_currentBuffer;
 configure_ui_metrics();
 /* Count physical display vblanks independently of Tiny3D flips. */
 g_vblank_count=0; g_perf_vblank_last=0; g_perf_vblank_window_count=0; g_perf_vblank_window_us=0;
 gcmSetVBlankHandler(phase4n_vblank_handler);
 gcmSetFlipHandler(phase5n_flip_handler);
 return 0;
}
static unsigned long long fps_now_us(){
 struct timeval tv; gettimeofday(&tv,NULL);
 return (unsigned long long)tv.tv_sec*1000000ull+(unsigned long long)tv.tv_usec;
}
static void update_fps_counter(){
 unsigned long long now=fps_now_us();
 if(!g_fps_window_start_us)g_fps_window_start_us=now;
 g_fps_window_frames++;
 unsigned long long elapsed=now-g_fps_window_start_us;
 if(elapsed>=1000000ull){
  g_fps_value=(float)g_fps_window_frames*1000000.0f/(float)elapsed;
  g_fps_window_frames=0;
  g_fps_window_start_us=now;
 }
}
static void present(){
 rsx_flush_text_batch();
 rsx_flush_rect_batch();
 unsigned long long before_flip=fps_now_us();
 unsigned int vb_before=g_vblank_count;
 g_perf_flip_status_before=gcmGetFlipStatus();
 float draw_ms=(g_perf_draw_begin_us && before_flip>=g_perf_draw_begin_us)?
   (float)(before_flip-g_perf_draw_begin_us)/1000.0f:0.0f;
 update_fps_counter();
 /* Tiny3D performs the synchronized RSX flip and advances Video_currentBuffer.
    Time it separately: a stable ~30 FPS with a ~30 ms FLIP value proves the
    frame is missing alternate vblanks; a large DRAW value instead identifies
    CPU/RSX command construction as the bottleneck. */
 g_4x_flip_enter_us=before_flip; g_4x_first_status_us=0; g_4x_reset_us=0;
 g_4x_queue_begin_us=0; g_4x_queue_end_us=0; g_4x_waitcmd_begin_us=0; g_4x_waitcmd_end_us=0;
 g_4x_status_polls=0; g_5k_poll_nonzero=0;g_5k_poll_first=g_5k_poll_last=0;g_5k_poll_first_us=g_5k_poll_last_us=0;g_5l_vb_queue=g_5l_vb_waitcmd=g_5l_vb_next_entry=g_5l_vb_zero=0;g_5l_seen_first_poll=0;g_5n_disp_n=g_5n_disp_z=0xFF; g_4y_space_begin_us=g_4y_space_end_us=g_4y_target_begin_us=g_4y_target_end_us=g_4y_cb_begin_us=g_4y_cb_end_us=0;
 g_5a_space_count=0; for(int si=0;si<8;si++){g_5a_space_beg[si]=g_5a_space_end[si]=0;g_5a_space_bytes[si]=0;g_5e_space_off_beg[si]=g_5e_space_off_end[si]=0;} g_5e_between_bytes=g_5e_s1_inside_bytes=g_5e_s2_inside_bytes=0; g_5f_target_off0=g_5f_target_off1=g_5f_callback_off0=g_5f_callback_off1=0;g_5f_post_s1_to_target=g_5f_target_bytes=g_5f_callback_bytes=g_5f_after_callback_bytes=0; g_5b_cb_count=0;g_5b_cb_us=0;g_5b_get0=g_5b_put0=g_5b_get1=g_5b_put1=0;g_5b_cur_off0=g_5b_cur_off1=g_5b_linear_free0=g_5b_linear_free1=0;g_5b_cb_arg=0; g_4x_probe_active=1;
 tiny3d_Flip();
 g_4x_probe_active=0;
 unsigned long long after_flip=fps_now_us();
 {
  unsigned long long e=g_4x_first_status_us?g_4x_first_status_us:after_flip;
  unsigned long long r=g_4x_reset_us?g_4x_reset_us:e;
  unsigned long long qb=g_4x_queue_begin_us?g_4x_queue_begin_us:r;
  unsigned long long qe=g_4x_queue_end_us?g_4x_queue_end_us:qb;
  unsigned long long wb=g_4x_waitcmd_begin_us?g_4x_waitcmd_begin_us:qe;
  unsigned long long we=g_4x_waitcmd_end_us?g_4x_waitcmd_end_us:wb;
  perf_avg(&g_4x_end_ms,perf_ms(g_4x_flip_enter_us,e));
  perf_avg(&g_4x_poll_ms,perf_ms(e,r));
  perf_avg(&g_4x_queue_ms,perf_ms(qb,qe));
  perf_avg(&g_4x_flushgap_ms,perf_ms(qe,wb));
  perf_avg(&g_4x_waitcmd_ms,perf_ms(wb,we));
  perf_avg(&g_4x_tail_ms,perf_ms(we,after_flip));
  perf_avg(&g_4y_space_ms,(g_4y_space_begin_us&&g_4y_space_end_us)?perf_ms(g_4y_space_begin_us,g_4y_space_end_us):0.0f);
  perf_avg(&g_5a_space1_ms,(g_5a_space_count>0&&g_5a_space_end[0])?perf_ms(g_5a_space_beg[0],g_5a_space_end[0]):0.0f);
  perf_avg(&g_5a_space2_ms,(g_5a_space_count>1&&g_5a_space_end[1])?perf_ms(g_5a_space_beg[1],g_5a_space_end[1]):0.0f);
  perf_avg(&g_5a_space3_ms,(g_5a_space_count>2&&g_5a_space_end[2])?perf_ms(g_5a_space_beg[2],g_5a_space_end[2]):0.0f);
  g_5a_space1_bytes=(g_5a_space_count>0)?g_5a_space_bytes[0]:0; g_5a_space2_bytes=(g_5a_space_count>1)?g_5a_space_bytes[1]:0; g_5a_space3_bytes=(g_5a_space_count>2)?g_5a_space_bytes[2]:0;
  g_5e_s1_inside_bytes=(g_5a_space_count>0)?phase5e_forward_delta(g_5e_space_off_beg[0],g_5e_space_off_end[0]):0;
  g_5e_between_bytes=(g_5a_space_count>1)?phase5e_forward_delta(g_5e_space_off_end[0],g_5e_space_off_beg[1]):0;
  g_5e_s2_inside_bytes=(g_5a_space_count>1)?phase5e_forward_delta(g_5e_space_off_beg[1],g_5e_space_off_end[1]):0;
  /* 5F: only deltas after S1 has returned are real command emission. */
  if(g_5a_space_count>0){
   g_5f_post_s1_to_target=phase5e_forward_delta(g_5e_space_off_end[0],g_5f_target_off0);
   g_5f_target_bytes=phase5e_forward_delta(g_5f_target_off0,g_5f_target_off1);
   g_5f_callback_bytes=phase5e_forward_delta(g_5f_callback_off0,g_5f_callback_off1);
   if(g_5a_space_count>1)g_5f_after_callback_bytes=phase5e_forward_delta(g_5f_callback_off1,g_5e_space_off_beg[1]);
  }
  perf_avg(&g_4y_target_ms,(g_4y_target_begin_us&&g_4y_target_end_us)?perf_ms(g_4y_target_begin_us,g_4y_target_end_us):0.0f);
  perf_avg(&g_4y_callback_ms,(g_4y_cb_begin_us&&g_4y_cb_end_us)?perf_ms(g_4y_cb_begin_us,g_4y_cb_end_us):0.0f);
  { unsigned long long sh=g_4y_cb_end_us?g_4y_cb_end_us:(g_4y_target_end_us?g_4y_target_end_us:we); perf_avg(&g_4y_shader_ms,perf_ms(sh,after_flip)); }
 }
 unsigned int vb_after=g_vblank_count;
 unsigned int vb_delta=vb_after-vb_before;
 const float va=(g_perf_samples<8)?0.35f:0.10f;
 if(!g_perf_samples)g_perf_vblanks_per_frame=(float)vb_delta; else g_perf_vblanks_per_frame+=((float)vb_delta-g_perf_vblanks_per_frame)*va;
 if(!g_perf_vblank_window_us){g_perf_vblank_window_us=after_flip;g_perf_vblank_window_count=vb_after;}
 else if(after_flip-g_perf_vblank_window_us>=1000000ull){unsigned int dv=vb_after-g_perf_vblank_window_count;g_perf_vblank_hz=(float)dv*1000000.0f/(float)(after_flip-g_perf_vblank_window_us);g_perf_vblank_window_count=vb_after;g_perf_vblank_window_us=after_flip;}
 g_perf_vblank_last=vb_after;
 float flip_ms=(float)(after_flip-before_flip)/1000.0f;
 float frame_ms=(g_perf_last_present_us && after_flip>=g_perf_last_present_us)?
   (float)(after_flip-g_perf_last_present_us)/1000.0f:0.0f;
 g_perf_last_present_us=after_flip;
 const float a=(g_perf_samples<8)?0.35f:0.10f;
 if(!g_perf_samples){g_perf_draw_ms=draw_ms;g_perf_flip_ms=flip_ms;g_perf_frame_ms=frame_ms;}
 else {g_perf_draw_ms+=(draw_ms-g_perf_draw_ms)*a;g_perf_flip_ms+=(flip_ms-g_perf_flip_ms)*a;if(frame_ms>0.0f)g_perf_frame_ms+=(frame_ms-g_perf_frame_ms)*a;}
 if(g_perf_samples<1000000)g_perf_samples++;
 g_back=Video_currentBuffer;
}
static void shutdown_video(){if(g_ctx){gcmSetFlipHandler(NULL);gcmSetVBlankHandler(NULL);tiny3d_Exit(); g_ctx=NULL;} free(g_bg_cache); g_bg_cache=NULL; g_bg_cache_ready=0; free(g_list_base_cache); g_list_base_cache=NULL; g_list_cache_valid=0; free(g_model_cache); g_model_cache=NULL; free(g_model_filter_cache); g_model_filter_cache=NULL; free(g_model_zbuf); g_model_zbuf=NULL; g_model_cache_valid=0; free(g_model_base_cache); g_model_base_cache=NULL; g_model_base_valid=0; free(g_model_sx);g_model_sx=NULL;free(g_model_sy);g_model_sy=NULL;free(g_model_zinv);g_model_zinv=NULL;free(g_model_nx);g_model_nx=NULL;free(g_model_ny);g_model_ny=NULL;g_model_vertex_cap=0; free(g_preview_base_cache); g_preview_base_cache=NULL; g_preview_base_valid=0; free(g_rename_base_cache); g_rename_base_cache=NULL; free(g_rename_composite_cache); g_rename_composite_cache=NULL; free(g_rename_pristine_cache); g_rename_pristine_cache=NULL;}
static void sysutil_cb(u64 status,u64 param,void *userdata){
 (void)param;(void)userdata;
 /* v1.49: follow the normal PSL1GHT system-menu lifecycle.  In particular,
    do not fence our RSX presentation on DRAW_BEGIN/DRAW_END.  The previous
    fence stopped the application flip/present loop as soon as the PS/Home
    overlay began, which on real hardware left only the XMB clock visible.
    Apollo and other PSL1GHT applications leave MENU_OPEN/CLOSE informational
    and keep their normal render loop alive while sysUtilCheckCallback pumps. */
 if(status==SYSUTIL_EXIT_GAME){running=0;return;}
 if(status==SYSUTIL_MENU_OPEN){g_system_menu_active=1;return;}
 if(status==SYSUTIL_MENU_CLOSE){g_system_menu_active=0;g_system_menu_resume_pending=1;return;}
}


static int endswith(const char *s,const char *x){size_t a=strlen(s),b=strlen(x);return a>=b&&!strcasecmp(s+a-b,x);}static const char *base_name(const char *p){const char*s=strrchr(p,'/');return s?s+1:p;}
static void parent_dir(){char*s=strrchr(cwd,'/');if(!s||s==cwd){strcpy(cwd,"/");return;}*s=0;}static void join(char*out,size_t n,const char*a,const char*b){if(!strcmp(a,"/"))snprintf(out,n,"/%s",b);else snprintf(out,n,"%s/%s",a,b);}static int cmp_items(const void*A,const void*B){const item_t*a=(const item_t*)A,*b=(const item_t*)B;if(a->is_dir!=b->is_dir)return b->is_dir-a->is_dir;return strcasecmp(a->name,b->name);}
static int is_mount_root(const char *p){
 if(!strcmp(p,"/dev_hdd0")||!strcmp(p,"/dev_hdd1")||!strcmp(p,g_apollo_savedata_path))return 1;
 if(!strncmp(p,"/dev_usb",8) && strlen(p)==11 && isdigit((unsigned char)p[8]) && isdigit((unsigned char)p[9]) && isdigit((unsigned char)p[10]))return 1;
 return 0;
}
static int path_exists_dir(const char *p){struct stat st;return stat(p,&st)==0 && S_ISDIR(st.st_mode);}
static const char *preferred_hdd0_path(void){
 return path_exists_dir(g_apollo_savedata_path)?g_apollo_savedata_path:"/dev_hdd0";
}
static void reset_navigation_render_state(void){
 g_folder_dirty=0;g_list_dirty=0;g_ui_dirty=1;
 g_list_panel_cache_valid=0;g_path_base_valid=0;g_list_cache_valid=0;
 g_list_prev_selected=-1;g_list_prev_top=-1;
}
static void set_device_default_path(const char *root){
 if(!strcmp(root,"/dev_hdd0")){
  g_apollo_warning_auto_failed=0;
  if(path_exists_dir(g_apollo_savedata_path)){
   snprintf(cwd,sizeof(cwd),"%s",g_apollo_savedata_path);g_apollo_warning_active=0;
  }else{
   snprintf(cwd,sizeof(cwd),"/dev_hdd0");g_apollo_warning_active=1;
  }
 }else{snprintf(cwd,sizeof(cwd),"%s",root);g_apollo_warning_active=0;g_apollo_warning_auto_failed=0;}
}
static void refresh_devices(){
 item_count=0;
 for(int i=0;i<2 && item_count<MAX_ITEMS;i++){char p[32];snprintf(p,sizeof(p),"/dev_hdd%d",i);if(path_exists_dir(p)){item_t*it=&items[item_count++];memset(it,0,sizeof(*it));snprintf(it->name,sizeof(it->name),"HDD %d",i);strncpy(it->path,p,sizeof(it->path)-1);it->is_dir=1;}}
 for(int i=0;i<8 && item_count<MAX_ITEMS;i++){char p[32];snprintf(p,sizeof(p),"/dev_usb%03d",i);if(path_exists_dir(p)){item_t*it=&items[item_count++];memset(it,0,sizeof(*it));snprintf(it->name,sizeof(it->name),"USB %03d",i);strncpy(it->path,p,sizeof(it->path)-1);it->is_dir=1;}}
 if(selected>=item_count)selected=item_count?item_count-1:0;if(selected<0)selected=0;
 snprintf(status_line,sizeof(status_line),"%d storage device%s found",item_count,item_count==1?"":"s");
}
static int restore_dir_cache(const char *path){
 for(int c=0;c<DIR_CACHE_SLOTS;c++){
  if(g_dir_cache[c].valid && !strcmp(g_dir_cache[c].path,path) && g_dir_cache[c].items){
   item_count=g_dir_cache[c].count;
   if(item_count>MAX_ITEMS)item_count=MAX_ITEMS;
   memcpy(items,g_dir_cache[c].items,(size_t)item_count*sizeof(item_t));
   if(selected>=item_count)selected=item_count?item_count-1:0;if(selected<0)selected=0;
   return 1;
  }
 }
 return 0;
}
static void save_dir_cache(const char *path){
 int slot=g_dir_cache_next++%DIR_CACHE_SLOTS;
 if(!g_dir_cache[slot].items)g_dir_cache[slot].items=(item_t*)malloc((size_t)MAX_ITEMS*sizeof(item_t));
 if(!g_dir_cache[slot].items)return;
 g_dir_cache[slot].valid=1;snprintf(g_dir_cache[slot].path,sizeof(g_dir_cache[slot].path),"%s",path);
 g_dir_cache[slot].count=item_count;
 memcpy(g_dir_cache[slot].items,items,(size_t)item_count*sizeof(item_t));
}
static void invalidate_dir_cache(const char *path){for(int c=0;c<DIR_CACHE_SLOTS;c++)if(g_dir_cache[c].valid&&!strcmp(g_dir_cache[c].path,path))g_dir_cache[c].valid=0;}
static void invalidate_all_dir_cache(void){for(int c=0;c<DIR_CACHE_SLOTS;c++)g_dir_cache[c].valid=0;}
static void refresh_fs(){
 if(restore_dir_cache(cwd))return;
 item_count=0;DIR*d=opendir(cwd);if(!d){snprintf(status_line,sizeof(status_line),"Cannot open %s",cwd);return;}struct dirent*de;
 while((de=readdir(d))&&item_count<MAX_ITEMS){
  if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;
  size_t den=strlen(de->d_name);
  if(den>=9&&!strcasecmp(de->d_name+den-9,".part.url"))continue;
  /* Phase 3I: segmented-resume files are downloader implementation state,
     not user files.  Keep them on disk so resume survives restart, but never
     expose .part.segN or .part.segments in the normal browser. */
  const char *segmark=strstr(de->d_name,".part.seg");
  if(segmark){
   const char *tail=segmark+9;
   if(!strcmp(tail,"ments"))continue;
   if(*tail){ int ok=1; for(const char *q=tail;*q;q++)if(*q<'0'||*q>'9'){ok=0;break;} if(ok)continue; }
  }
  item_t*it=&items[item_count];memset(it,0,sizeof(*it));strncpy(it->name,de->d_name,sizeof(it->name)-1);
  /* PS3 dirent already tells us the normal file type. Do not issue a blocking
     stat() syscall for every entry just to build the browser list. Sizes are
     resolved lazily in tiny idle slices below. */
  if(de->d_type==DT_DIR){it->is_dir=1;it->size_known=1;it->size=0;it->date_known=0;it->latest_time=0;}
  else if(de->d_type==DT_REG){it->is_dir=0;it->size_known=0;it->size=~0ULL;it->date_known=0;it->latest_time=0;}
  else{
   char pth[PATHSZ];join(pth,sizeof(pth),cwd,de->d_name);struct stat st;
   if(!stat(pth,&st)){it->is_dir=S_ISDIR(st.st_mode);it->size=(unsigned long long)st.st_size;it->size_known=1;it->latest_time=latest_stat_time(&st);it->date_known=it->latest_time>0;}
   else {it->is_dir=0;it->size_known=0;it->size=~0ULL;it->date_known=0;it->latest_time=0;}
  }
  item_count++;
 }
 closedir(d);qsort(items,item_count,sizeof(items[0]),cmp_items);if(selected>=item_count)selected=item_count?item_count-1:0;if(selected<0)selected=0;save_dir_cache(cwd);
}
static int psu_collect(const psu_entry_t*e,size_t,void*){if(!strcmp(e->name,".")||!strcmp(e->name,".."))return 0;if(item_count>=MAX_ITEMS)return 1;item_t*it=&items[item_count++];memset(it,0,sizeof(*it));strncpy(it->name,e->name,sizeof(it->name)-1);it->is_dir=(e->attr==PSU_ATTR_DIR);it->size=e->size;it->size_known=1;time_t ct=psu_time_to_time_t(&e->ctime),mt=psu_time_to_time_t(&e->mtime);it->latest_time=mt>ct?mt:ct;it->date_known=it->latest_time>0;return 0;}
static void refresh_psu(){
 item_count=0;int rc=psu_list(archive_path,psu_collect,NULL);
 if(rc)snprintf(status_line,sizeof(status_line),"PSU: %s",psu_strerror(rc));
 if(!rc)load_iconsys_icn_refs(); else clear_iconsys_icn_refs();
 /* Display-only stable partition.  Non-ICNs first; unrelated ICNs next; the
    ICON.SYS View/Copy/Delete set is grouped at the very bottom. */
 if(!rc && item_count>1){
  item_t tmp[MAX_ITEMS];int n=0;
  for(int i=0;i<item_count;i++)if(!ps2_icon_model_name(items[i].name))tmp[n++]=items[i];
  for(int i=0;i<item_count;i++)if( ps2_icon_model_name(items[i].name) && iconsys_ref_index(items[i].name)<0)tmp[n++]=items[i];
  for(int i=0;i<item_count;i++)if( ps2_icon_model_name(items[i].name) && iconsys_ref_index(items[i].name)>=0)tmp[n++]=items[i];
  memcpy(items,tmp,(size_t)item_count*sizeof(items[0]));
 }
 if(selected>=item_count)selected=item_count?item_count-1:0;
}
static void refresh_fs_metadata_now(){
 if(archive_mode)return;
 for(int i=0;i<item_count;i++){
  char pth[PATHSZ];join(pth,sizeof(pth),cwd,items[i].name);struct stat st;if(stat(pth,&st)!=0)continue;
  items[i].is_dir=S_ISDIR(st.st_mode);items[i].size=items[i].is_dir?0:(unsigned long long)st.st_size;items[i].size_known=1;
  items[i].latest_time=latest_stat_time(&st);items[i].date_known=items[i].latest_time>0;
 }
 /* A resumed .part can keep the same filename while both size and mtime change.
    Force the browser row cache to be rebuilt from the fresh stat values. */
 g_list_cache_valid=0;g_metadata_dirty_index=-1;g_list_dirty=1;g_folder_dirty=1;g_ui_dirty=1;
}
static void refresh(){if(archive_mode)refresh_psu();else refresh_fs();}


static void restore_background_region_to(u32 *fb,int x,int y,int w,int h){
 if(!fb||!g_bg_cache||!g_bg_cache_ready||w<=0||h<=0)return;
 if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
 if(x+w>(int)g_width)w=(int)g_width-x; if(y+h>(int)g_height)h=(int)g_height-y;
 if(w<=0||h<=0)return;
 for(int yy=0;yy<h;yy++)
  memcpy(fb+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,
         g_bg_cache+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,
         (size_t)w*sizeof(u32));
}
static int list_visible_for_count(int count,int max_visible){
 if(max_visible<1)max_visible=1;
 if(count<1)return 0;
 return count<max_visible?count:max_visible;
}
static int list_panel_height_for_visible(int browser_header_h,int visible){
 if(visible<0)visible=0;
 return browser_header_h+8+visible*g_row_h+10;
}

static void ensure_region_cache(u32 **cache,int *cw,int *ch,int w,int h){
 if(w<=0||h<=0)return;
 if(*cache&&(*cw!=w||*ch!=h)){free(*cache);*cache=NULL;}
 if(!*cache)*cache=(u32*)malloc((size_t)w*(size_t)h*sizeof(u32));
 if(*cache){*cw=w;*ch=h;}
}
static void capture_region(u32 **cache,int *cw,int *ch,int x,int y,int w,int h){
 ensure_region_cache(cache,cw,ch,w,h);if(!*cache)return;
 for(int yy=0;yy<h;yy++)memcpy(*cache+(size_t)yy*(size_t)w,g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,(size_t)w*sizeof(u32));
}
static void restore_region_to(u32 *fb,const u32 *cache,int x,int y,int w,int h){
 if(!fb||!cache||w<=0||h<=0)return;
 for(int yy=0;yy<h;yy++)memcpy(fb+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,cache+(size_t)yy*(size_t)w,(size_t)w*sizeof(u32));
}
static void copy_region_between_buffers(int x,int y,int w,int h){
 if(w<=0||h<=0)return;
 for(int yy=0;yy<h;yy++)memcpy(g_fb[g_back^1]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,(size_t)w*sizeof(u32));
}

static unsigned long long g_last_user_activity_us=0;
static int g_lazy_metadata_cursor=0;
static void update_cached_item_metadata(int idx){
 for(int c=0;c<DIR_CACHE_SLOTS;c++)if(g_dir_cache[c].valid&&!strcmp(g_dir_cache[c].path,cwd)&&g_dir_cache[c].items&&idx<g_dir_cache[c].count){
  g_dir_cache[c].items[idx].size=items[idx].size;g_dir_cache[c].items[idx].size_known=items[idx].size_known;
  g_dir_cache[c].items[idx].latest_time=items[idx].latest_time;g_dir_cache[c].items[idx].date_known=items[idx].date_known;break;
 }
}
static void service_lazy_file_metadata(){
 if(archive_mode||item_count<=0)return;
 unsigned long long idle_now=fps_now_us();
 if(g_last_user_activity_us&&idle_now-g_last_user_activity_us>3000000ull)return; /* no background stat churn while unattended */
 if((g_frame_counter&7)!=0)return;
 int chosen=-1;
 if(selected>=0&&selected<item_count&&(!items[selected].size_known||!items[selected].date_known))chosen=selected;
 if(chosen<0&&g_list_cache_visible>0){for(int r=0;r<g_list_cache_visible;r++){int i=top_index+r;if(i>=item_count)break;if(!items[i].size_known||!items[i].date_known){chosen=i;break;}}}
 if(chosen<0){for(int tries=0;tries<item_count;tries++){int i=(g_lazy_metadata_cursor++)%item_count;if(!items[i].size_known||!items[i].date_known){chosen=i;break;}}}
 if(chosen<0)return;
 char pth[PATHSZ];join(pth,sizeof(pth),cwd,items[chosen].name);struct stat st;if(stat(pth,&st)!=0)return;
 items[chosen].is_dir=S_ISDIR(st.st_mode);items[chosen].size=items[chosen].is_dir?0:(unsigned long long)st.st_size;items[chosen].size_known=1;
 items[chosen].latest_time=latest_stat_time(&st);items[chosen].date_known=items[chosen].latest_time>0;update_cached_item_metadata(chosen);
 if(g_list_cache_valid&&chosen>=top_index&&chosen<top_index+g_list_cache_visible)g_metadata_dirty_index=chosen;
}

static void ensure_list_cache_storage(int w,int h){
 if(w<=0||h<=0)return;
 if(g_list_base_cache && (g_list_cache_w!=w||g_list_cache_h!=h)){
  free(g_list_base_cache);g_list_base_cache=NULL;g_list_cache_valid=0;
 }
 if(!g_list_base_cache)g_list_base_cache=(u32*)malloc((size_t)w*(size_t)h*sizeof(u32));
}
static void capture_list_base(int x,int y,int w,int h){
 ensure_list_cache_storage(w,h); if(!g_list_base_cache)return;
 g_list_cache_x=x;g_list_cache_y=y;g_list_cache_w=w;g_list_cache_h=h;
 for(int yy=0;yy<h;yy++){
  memcpy(g_list_base_cache+(size_t)yy*(size_t)w,
         g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,
         (size_t)w*sizeof(u32));
 }
 g_list_cache_valid=1;
}
static void restore_list_base_to_buffer(u32 *fb){
 if(!g_list_cache_valid||!g_list_base_cache||!fb)return;
 for(int yy=0;yy<g_list_cache_h;yy++){
  memcpy(fb+(size_t)(g_list_cache_y+yy)*(g_pitch/4)+(size_t)g_list_cache_x,
         g_list_base_cache+(size_t)yy*(size_t)g_list_cache_w,
         (size_t)g_list_cache_w*sizeof(u32));
 }
}
static void restore_list_row_base_to_buffer(u32 *fb,int row){
 if(!g_list_cache_valid||!g_list_base_cache||!fb||row<0||row>=g_list_cache_visible)return;
 int y0=row*g_row_h;
 int h=g_row_h;
 if(y0+h>g_list_cache_h)h=g_list_cache_h-y0;
 if(h<=0)return;
 for(int yy=0;yy<h;yy++){
  memcpy(fb+(size_t)(g_list_cache_y+y0+yy)*(g_pitch/4)+(size_t)g_list_cache_x,
         g_list_base_cache+(size_t)(y0+yy)*(size_t)g_list_cache_w,
         (size_t)g_list_cache_w*sizeof(u32));
 }
}
static void copy_list_row_between_buffers(int row){
 if(row<0||row>=g_list_cache_visible)return;
 int y0=g_list_cache_y+row*g_row_h;
 int h=g_row_h;
 if((row+1)*g_row_h>g_list_cache_h)h=g_list_cache_h-row*g_row_h;
 if(h<=0)return;
 for(int yy=0;yy<h;yy++){
  memcpy(g_fb[g_back^1]+(size_t)(y0+yy)*(g_pitch/4)+(size_t)g_list_cache_x,
         g_fb[g_back]+(size_t)(y0+yy)*(g_pitch/4)+(size_t)g_list_cache_x,
         (size_t)g_list_cache_w*sizeof(u32));
 }
}
static void draw_cached_list_row(int i,int row){
 if(i<0||i>=item_count||row<0||row>=g_list_cache_visible)return;
 int list_right=g_list_cache_right, rows_y=g_list_cache_rows_y;
 int icon_size=128;
 int icon_space=icon_size+18;
 int copied=0;
 if(!archive_mode&&clipboard[0]){char item_path[PATHSZ];join(item_path,sizeof(item_path),cwd,items[i].name);copied=!strcmp(item_path,clipboard);}
 int yy=rows_y+row*g_row_h;
 int row_x=g_list_x-8,row_w=list_right-row_x-12;
 if(i==selected){
  round_rect(row_x,yy-4,row_w,g_row_h,5,archive_mode&&export_target_active?0xFFA46C24u:0xFF215F8Cu);
  rect(row_x+1,yy-3,4,g_row_h-2,archive_mode&&export_target_active?0xFFFFCF6Cu:0xFF79D6FFu);
 }else if(row&1) round_rect(row_x,yy-4,row_w,g_row_h,4,0x4A1B2B3Au);
  rect(row_x+8,yy+g_row_h-5,row_w-16,1,0x304D718Au);
 u32 c=copied?0xFFFF7B7Bu:((i==selected)?0xFFFFFFFFu:0xFFE6EDF3u);
 char sizebuf[32],datebuf[32]; format_file_size(items[i].size,items[i].is_dir,sizebuf,sizeof(sizebuf)); format_file_date(items[i].latest_time,items[i].date_known,datebuf,sizeof(datebuf));
 int sizew=text_width(sizebuf,g_body_scale),datew=text_width(datebuf,g_body_scale);
 int size_col_w=text_width("999.9 MB",g_body_scale)+18; if(size_col_w<sizew+8)size_col_w=sizew+8;
 int date_col_w=text_width("14-09-2026 23:59",g_body_scale)+18; if(date_col_w<datew+8)date_col_w=datew+8;
 int date_right=list_right-size_col_w-10;
 int text_area=date_right-date_col_w-g_list_x-icon_space-14;
 int name_chars=text_area/scale125(6*g_body_scale)-1; if(name_chars<6)name_chars=6;
 int icon_y=yy+4;
 if(items[i].is_dir)draw_folder_icon(g_list_x+5,icon_y,icon_size,i==selected); else {const u32 *thumb=archive_mode?NULL:browser_archive_thumb(items[i].name);if(thumb)draw_texture_scaled(g_list_x+5,icon_y,128,128,thumb);else draw_file_icon(g_list_x+5,icon_y,icon_size,items[i].name,i==selected);}
 char namebuf[320]; snprintf(namebuf,sizeof(namebuf),"%-*.*s",name_chars,name_chars,items[i].name);
 text(g_list_x+icon_space+5,yy,namebuf,c,g_body_scale);
 text(date_right-datew,yy,datebuf,i==selected?0xFFFFFFFFu:0xFFB8C6D3u,g_body_scale);
 text(list_right-sizew-10,yy,sizebuf,i==selected?0xFFFFFFFFu:0xFFB8C6D3u,g_body_scale);
}
static void draw_selection_overlay(int row){
 int yy=g_list_cache_rows_y+row*g_row_h;
 int row_x=g_list_x-8,row_w=g_list_cache_right-row_x-12;
 /* v1.36: the selection is an overlay on top of an already-rendered row.
    Keep the fill translucent so the filename/icon/date/size remain visible.
    The old fully-opaque v1.35 overlay hid the selected row completely,
    which made a one-item directory such as /dev_hdd0/SAVEDATA look empty. */
 round_rect(row_x,yy-4,row_w,g_row_h,5,0x68215F8Cu);
 round_rect(row_x+2,yy-2,row_w-4,g_row_h-4,4,0x443A9ED2u);
 rect(row_x+5,yy-4,row_w-10,2,0xB8E0F8FFu);
 rect(row_x+5,yy+g_row_h-6,row_w-10,2,0xA82ACBFFu);
 rect(row_x+1,yy-3,5,g_row_h-2,0xD84AE1FFu);
}
static void draw_cached_list_rows(){
 int visible=g_list_cache_visible;
 if(selected<top_index)top_index=selected;
 if(selected>=top_index+visible)top_index=selected-visible+1;
 for(int row=0;row<visible;row++){
  int i=top_index+row; if(i>=item_count)break;
  draw_cached_list_row(i,row);
 }
}
static void restore_list_row_panel_base_to_buffer(u32 *fb,int row){
 if(!fb||!g_list_panel_cache_valid||!g_list_panel_cache||row<0||row>=g_list_cache_visible)return;
 int dst_y=g_list_cache_y+row*g_row_h,src_x=g_list_cache_x-g_list_panel_x,src_y=dst_y-g_list_panel_y;
 int h=g_row_h;if(src_x<0||src_y<0||src_x+g_list_cache_w>g_list_panel_w)return;if(src_y+h>g_list_panel_h)h=g_list_panel_h-src_y;if(h<=0)return;
 for(int yy=0;yy<h;yy++)memcpy(fb+(size_t)(dst_y+yy)*(g_pitch/4)+(size_t)g_list_cache_x,g_list_panel_cache+(size_t)(src_y+yy)*(size_t)g_list_panel_w+(size_t)src_x,(size_t)g_list_cache_w*sizeof(u32));
}
static void capture_list_row_base_from_buffer(u32 *fb,int row){
 if(!fb||!g_list_cache_valid||!g_list_base_cache||row<0||row>=g_list_cache_visible)return;int y0=row*g_row_h,h=g_row_h;if(y0+h>g_list_cache_h)h=g_list_cache_h-y0;if(h<=0)return;
 for(int yy=0;yy<h;yy++)memcpy(g_list_base_cache+(size_t)(y0+yy)*(size_t)g_list_cache_w,fb+(size_t)(g_list_cache_y+y0+yy)*(g_pitch/4)+(size_t)g_list_cache_x,(size_t)g_list_cache_w*sizeof(u32));
}
static int draw_fast_metadata_update(){
 if(g_metadata_dirty_index<0||g_ui_dirty||archive_mode||!g_list_cache_valid)return 0;int i=g_metadata_dirty_index;g_metadata_dirty_index=-1;if(i<top_index||i>=top_index+g_list_cache_visible)return 0;int row=i-top_index;
 restore_list_row_panel_base_to_buffer(g_fb[g_back],row);int saved=selected;selected=-1;draw_cached_list_row(i,row);selected=saved;capture_list_row_base_from_buffer(g_fb[g_back],row);if(i==selected)draw_selection_overlay(row);copy_list_row_between_buffers(row);present();g_frame_counter++;return 1;
}
static int draw_fast_list_update(){
 if(!g_list_dirty||g_ui_dirty||!g_list_cache_valid)return 0;
 int visible=g_list_cache_visible;
 int new_top=top_index;
 if(selected<new_top)new_top=selected;
 if(selected>=new_top+visible)new_top=selected-visible+1;
 if(new_top<0)new_top=0;

 /* Most navigation changes only the highlight. Redraw just the old and new
    rows instead of re-rendering every visible filename/icon on the Cell PPU. */
 if(g_list_prev_top==new_top && g_list_prev_selected>=new_top && g_list_prev_selected<new_top+visible && selected>=new_top && selected<new_top+visible){
  int old_row=g_list_prev_selected-new_top;
  int new_row=selected-new_top;
  if(old_row==new_row){g_list_dirty=0;present();g_frame_counter++;return 1;}
  restore_list_row_base_to_buffer(g_fb[g_back],old_row);
  /* v1.47 synchronized-row reconstruction: keep both framebuffers coherent
     immediately, but avoid the measured ~21 ms framebuffer-to-framebuffer
     memcpy.  Reconstruct the destination row from the CPU-side clean cache.
     This preserves the old synchronized semantics (unlike the rejected
     per-buffer deferred experiment) without reading pixels from the other FB. */
  restore_list_row_base_to_buffer(g_fb[g_back^1],old_row);
  /* Keep the successful opaque selected-row renderer from the OV test. */
  restore_list_row_panel_base_to_buffer(g_fb[g_back],new_row);
  draw_cached_list_row(selected,new_row);
  /* Build the same selected row locally into the other framebuffer rather
     than copying a wide scanline rectangle between RSX framebuffers. */
  {
   int saved_back=g_back;
   int other=saved_back^1;
   restore_list_row_panel_base_to_buffer(g_fb[other],new_row);
   g_back=other;
   draw_cached_list_row(selected,new_row);
   g_back=saved_back;
  }

 }else{
  /* Crossing the visible-window boundary used to force a complete 1280x720 UI
     rebuild. Restore the cached blank file panel and repaint only its header +
     visible rows instead. */
  if(!g_list_panel_cache_valid||!g_list_panel_cache){top_index=new_top;g_list_dirty=0;g_ui_dirty=1;return 0;}
  top_index=new_top;
  restore_region_to(g_fb[g_back],g_list_panel_cache,g_list_panel_x,g_list_panel_y,g_list_panel_w,g_list_panel_h);
  int emblem_s=(g_body_scale>1)?26:18;
  draw_browser_emblem(g_list_x-4,g_list_panel_y+7,emblem_s);
  char headbuf[96];snprintf(headbuf,sizeof(headbuf),"Files and folders   %d item%s",item_count,item_count==1?"":"s");
  text(g_list_x+emblem_s+10,g_list_panel_y+9,headbuf,0xFFF4F7FAu,g_body_scale);
  const char *size_head="SIZE",*date_head="DATE";int size_head_w=text_width(size_head,g_body_scale);
  int size_col_w=text_width("999.9 MB",g_body_scale)+18;int date_head_w=text_width(date_head,g_body_scale);
  text(g_list_cache_right-size_head_w-12,g_list_panel_y+9,size_head,0xFFB7C4D0u,g_body_scale);
  text(g_list_cache_right-size_col_w-date_head_w-18,g_list_panel_y+9,date_head,0xFFB7C4D0u,g_body_scale);
  int saved=selected;selected=-1;
  for(int row=0;row<visible;row++){int i=top_index+row;if(i>=item_count)break;draw_cached_list_row(i,row);}selected=saved;
  capture_list_base(g_list_x-8,g_list_cache_rows_y-4,g_list_cache_right-(g_list_x-8)-12,visible*g_row_h+8);
  if(selected>=top_index&&selected<top_index+visible)draw_selection_overlay(selected-top_index);
  copy_region_between_buffers(g_list_panel_x,g_list_panel_y,g_list_panel_w,g_list_panel_h);
 }
 g_list_prev_selected=selected;
 g_list_prev_top=top_index;
 g_list_dirty=0;
 present(); g_frame_counter++;
 return 1;
}

static int model_update_interval(){
 /* Keep display presentation at 60 Hz while budgeting the CPU-heavy software
    ICN renderer separately.  Medium/heavy icons animate at 30 Hz; only very
    dense icons fall back to 20 Hz.  Light icons can still update every frame. */
 int faces=g_preview.face_count;
 if(faces>900)return 4;   /* ~15 Hz model animation */
 if(faces>350)return 3;   /* ~20 Hz */
 return 2;                /* ~30 Hz; UI/presentation still 60 Hz */
}

static int draw_fast_folder_update(){
 if(!g_folder_dirty||archive_mode||!g_path_base_valid||!g_list_panel_cache_valid)return 0;
 /* Breadcrumb: restore the empty address bar and draw only the current path. */
 restore_region_to(g_fb[g_back],g_path_base_cache,g_path_cache_x,g_path_cache_y,g_path_cache_w,g_path_cache_h);
 text(g_path_x+scale125(16*g_body_scale),g_path_y,cwd,0xFFF1F5F8u,g_body_scale);
 /* File panel: resize to the current folder's content without recomposing the
    whole screen. The newly exposed area is restored directly from the cached
    PS2 background, then only the browser card is rebuilt. */
 int browser_header_h=scale125(8*g_body_scale)+22;
 int new_visible=list_visible_for_count(item_count,g_list_cache_max_visible);
 int new_panel_h=list_panel_height_for_visible(browser_header_h,new_visible);
 int old_panel_h=g_list_panel_h;
 int union_h=old_panel_h>new_panel_h?old_panel_h:new_panel_h;
 restore_background_region_to(g_fb[g_back],g_list_panel_x,g_list_panel_y,g_list_panel_w,union_h);
 browser_list_card(g_list_panel_x,g_list_panel_y,g_list_panel_w,new_panel_h,0xC0121D29u,0xD06A8DA8u,9);
 browser_column_header(g_list_x-9,g_list_panel_y+5,g_list_cache_right-g_list_x-2,browser_header_h-9);
 g_list_panel_h=new_panel_h;g_list_cache_visible=new_visible;
 capture_region(&g_list_panel_cache,&g_list_panel_cache_alloc_w,&g_list_panel_cache_alloc_h,
                g_list_panel_x,g_list_panel_y,g_list_panel_w,g_list_panel_h);
 g_list_panel_cache_valid=g_list_panel_cache!=NULL;
 int emblem_s=(g_body_scale>1)?26:18;
 draw_browser_emblem(g_list_x-4,g_list_panel_y+7,emblem_s);
 char headbuf[96];snprintf(headbuf,sizeof(headbuf),"Files and folders   %d item%s",item_count,item_count==1?"":"s");
 text(g_list_x+emblem_s+10,g_list_panel_y+9,headbuf,0xFFF4F7FAu,g_body_scale);
 const char *size_head="SIZE",*date_head="DATE";int size_head_w=text_width(size_head,g_body_scale);
 int size_col_w=text_width("999.9 MB",g_body_scale)+18;int date_head_w=text_width(date_head,g_body_scale);
 text(g_list_cache_right-size_head_w-12,g_list_panel_y+9,size_head,0xFFB7C4D0u,g_body_scale);
 text(g_list_cache_right-size_col_w-date_head_w-18,g_list_panel_y+9,date_head,0xFFB7C4D0u,g_body_scale);
 top_index=0;int visible=g_list_cache_visible;
 int saved=selected;selected=-1;
 for(int row=0;row<visible;row++){int i=top_index+row;if(i>=item_count)break;draw_cached_list_row(i,row);}selected=saved;
 capture_list_base(g_list_x-8,g_list_cache_rows_y-4,g_list_cache_right-(g_list_x-8)-12,visible*g_row_h+8);
 if(selected>=top_index&&selected<top_index+visible)draw_selection_overlay(selected-top_index);
 g_list_prev_selected=selected;g_list_prev_top=top_index;
 /* Folder transitions must never write into g_back^1 before the flip.
    Publish only the completed inactive backbuffer, then let the existing
    safe full-frame synchronizer update the other buffer on the next frame. */
 g_ui_generation++; if(g_ui_generation==0)g_ui_generation=1;
 g_ui_buffer_generation[g_back]=g_ui_generation;
 g_ui_buffer_generation[g_back^1]=0;
 g_folder_dirty=0;g_list_dirty=0;g_ui_dirty=0;
 present();g_frame_counter++;return 1;
}

static int draw_safe_fullframe_sync(){
 if(g_ui_dirty||g_rename_active)return 0;
 if(g_ui_generation==0||g_ui_buffer_generation[g_back]==g_ui_generation)return 0;
 int src=g_back^1;
 if(g_ui_buffer_generation[src]!=g_ui_generation)return 0;
 /* src is the framebuffer currently being displayed; read-only access is
    safe. g_back is inactive, so the destination cannot tear or blink. */
 size_t frame_bytes=(size_t)g_pitch*(size_t)g_height;
 memcpy(g_fb[g_back],g_fb[src],frame_bytes);
 g_ui_buffer_generation[g_back]=g_ui_generation;
 if(archive_mode&&g_preview.valid&&g_model_cache_valid)
  g_model_buffer_generation[g_back]=g_model_generation;
 else g_model_buffer_generation[g_back]=0;
 present();g_frame_counter++;return 1;
}

/* v79 recovery: guard the animated ICN fast path before touching model/texture
   buffers. Reconstructed from the v79 ELF (the added helper at 0x13a38). */
static void draw_textured_model_safe(int box_x,int box_y,int box_w,int box_h){
 if(!g_preview.valid || !g_preview.positions || !g_preview.uvs ||
    (!g_preview.texture && g_preview.texture_valid))return;
 if(box_w<=39 || box_h<=39)return;
 draw_textured_model(box_x,box_y,box_w,box_h);
}

static int draw_fast_preview_update(){
 if(g_ui_dirty||!archive_mode||!g_model_base_valid||!g_preview.valid)return 0;
 if(item_count<=0||selected>=item_count||items[selected].is_dir||!preview_entry_is_standard_icn(items[selected].name))return 0;

 /* First make sure the inactive backbuffer contains the same completed model
    generation as the other display buffer.  This is a cheap cache blit and
    deliberately happens before considering a new animation frame. */
 if(g_model_cache_valid && g_model_buffer_generation[g_back]!=g_model_generation){
  restore_model_base_to_buffer(g_fb[g_back]);
  blit_model_cache(g_model_base_x,g_model_base_y,g_model_base_w,g_model_base_h);
  g_model_buffer_generation[g_back]=g_model_generation;present();g_frame_counter++;
  if(g_preview_startup_hold>0)g_preview_startup_hold--;
  return 1;
 }

 /* Hold the same completed first model on both buffers for a few presents.
    This prevents the first handful of ICN frames from visibly blinking while
    caches/generations settle on real hardware. */
 if(g_preview_startup_hold>0){
  present();g_frame_counter++;
  g_preview_startup_hold--;
  if(g_preview_startup_hold==0){
   g_preview_rotation_start_frame=g_frame_counter;
   g_model_cache_frame=g_frame_counter;
  }
  return 1;
 }

 if((g_frame_counter-g_model_cache_frame)<model_update_interval())return 0;
 restore_model_base_to_buffer(g_fb[g_back]);
 int old_generation=g_model_generation;
 draw_textured_model_safe(g_model_base_x,g_model_base_y,g_model_base_w,g_model_base_h);
 if(g_model_generation!=old_generation)g_model_buffer_generation[g_back]=g_model_generation;

 /* Never mirror the preview rectangle into g_back^1 here.  On real hardware
    g_back^1 may still be scanned out.  The next frame safely synchronises that
    buffer from g_model_cache while it is inactive. */present();g_frame_counter++;return 1;
}


static uint16_t meta_rd16(const unsigned char*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t meta_rd32(const unsigned char*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static float meta_rdf32(const unsigned char*p){uint32_t v=meta_rd32(p);float f;memcpy(&f,&v,4);return f;}
static void ps2_sjis_title_ascii(const unsigned char *src,size_t n,char*out,size_t cap){
 size_t o=0;for(size_t i=0;i<n&&src[i]&&o+1<cap;){unsigned c=src[i];
  if(c<0x80){out[o++]=(char)c;i++;continue;}
  if(i+1<n&&c==0x82){unsigned d=src[i+1];char a=0;if(d>=0x4F&&d<=0x58)a=(char)('0'+d-0x4F);else if(d>=0x60&&d<=0x79)a=(char)('A'+d-0x60);else if(d>=0x81&&d<=0x9A)a=(char)('a'+d-0x81);if(a){out[o++]=a;i+=2;continue;}}
  if(i+1<n&&c==0x81&&src[i+1]==0x40){out[o++]=' ';i+=2;continue;} out[o++]='?';i+=(i+1<n)?2:1;
 }out[o]=0;
}
static void meta_time(char*out,size_t n,const psu_time_t*t){snprintf(out,n,"%02u-%02u-%04u %02u:%02u:%02u",t->day,t->month,t->year,t->hour,t->min,t->sec);}
typedef struct { psu_entry_t e[256]; int count; } meta_entries_t;
static int meta_collect_entry(const psu_entry_t*e,size_t index,void*opaque){meta_entries_t*m=(meta_entries_t*)opaque;if(m->count>=256)return 1;m->e[m->count++]=*e;return 0;}
static void psu_metadata_cache_clear(){ if(g_psu_meta_cache.icon_sys)free(g_psu_meta_cache.icon_sys); memset(&g_psu_meta_cache,0,sizeof(g_psu_meta_cache)); }
static void psu_metadata_cache_load(){
 psu_metadata_cache_clear();
 g_psu_meta_cache.root_ok=(psu_get_root(archive_path,&g_psu_meta_cache.root)==0);
 void *vd=NULL; size_t vs=0; if(psu_read_file(archive_path,"icon.sys",&vd,&vs)==0&&vd){g_psu_meta_cache.icon_sys=(unsigned char*)vd;g_psu_meta_cache.icon_sys_size=vs;}
 meta_entries_t me;memset(&me,0,sizeof(me)); if(psu_list(archive_path,meta_collect_entry,&me)==0){g_psu_meta_cache.entry_count=me.count;for(int i=0;i<me.count;i++)g_psu_meta_cache.entries[i]=me.e[i];}
 g_psu_meta_cache.valid=1;
}
/* Stage 52: fingerprints are diagnostics, never a claim of patch provenance. */
static unsigned long icon_diag_crc(const unsigned char *data,size_t n){
 unsigned long crc=0xFFFFFFFFUL;
 for(size_t i=0;i<n;i++){crc^=data[i];for(int k=0;k<8;k++)crc=(crc>>1)^((crc&1)?0xEDB88320UL:0);}
 return (crc^0xFFFFFFFFUL)&0xFFFFFFFFUL;
}
/* Stage 53: model geometry and canonical 128x128 RGBA texture fingerprints. */
static int icon_diag_parts(const unsigned char *b,size_t n,unsigned long *geometry,unsigned long *texture){
 if(!b||n<40||meta_rd32(b)!=0x10000u)return 0;
 unsigned shapes=meta_rd32(b+4),type=meta_rd32(b+8),verts=meta_rd32(b+16);
 if(shapes<1||shapes>16||verts<3||verts>4096)return 0;
 size_t end=20u+(size_t)verts*((size_t)shapes*8u+16u);
 if(end+20>n)return 0;
 *geometry=icon_diag_crc(b+20,end-20);
 unsigned frames=meta_rd32(b+end+16);if(frames>4096)return 0;
 size_t pos=end+20;
 for(unsigned i=0;i<frames;i++){
  if(pos+8>n)return 0;
  unsigned keys=meta_rd32(b+pos+4);
  if(keys>65536u || (size_t)keys>(n-pos-8)/8)return 0;
  pos+=8+(size_t)keys*8;
 }
 if(!(type&4u))return 0;
 u32 *pixels=(u32*)malloc(128u*128u*sizeof(u32));if(!pixels)return 0;
 int ok=(type&8u)?ps2_icn_decode_rle(b+pos,n-pos,pixels):decode_icn_raw_texture(b+pos,b+n,pixels);
 if(ok){
  /* Canonical RGBA byte order, independent of CPU endianness. */
  unsigned long crc=0xFFFFFFFFUL;
  for(size_t i=0;i<128u*128u;i++){
   u32 c=pixels[i];unsigned char rgba[4]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c,255};
   for(int k=0;k<4;k++){crc^=rgba[k];for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xEDB88320UL:0);}
  }
  *texture=(crc^0xFFFFFFFFUL)&0xFFFFFFFFUL;
 }
 free(pixels);return ok;
}
static const char *icon_diag_origin(unsigned long geometry,unsigned long texture){
 if(geometry==0x625B33F0UL && texture==0xE0D22957UL)return "SharkPort V1-style replacement (GT3 reference)";
 if(geometry==0x625B33F0UL && texture==0xB4B9F843UL)return "X-Port V1-style replacement (GT3 reference)";
 return "Possible V2 / patched / original icon (unverified)";
}
static void icon_diag_name(char out[65],const unsigned char *p){memcpy(out,p,64);out[64]=0;}
static int sharkport_details_active(){return g_foreign_archive_view &&
 (endswith(g_foreign_source_path,".sps")||endswith(g_foreign_source_path,".xps")||endswith(g_foreign_source_path,".xpo"));}
static int psu_metadata_page_count(){if(!g_psu_meta_cache.valid)psu_metadata_cache_load();return 4+g_psu_meta_cache.entry_count+(sharkport_details_active()?1:0);}
static void sjis_title_slice_ascii(const unsigned char*src,size_t begin,size_t end,char*out,size_t cap){if(begin>end){size_t t=begin;begin=end;end=t;}if(end>68)end=68;ps2_sjis_title_ascii(src+begin,end-begin,out,cap);}
static void draw_psu_metadata_modal(){
 int x=g_margin+20,y=g_title_y+55,w=(int)g_width-g_margin*2-40,h=(int)g_height-y-g_margin-35;round_rect(x,y,w,h,10,0xF20A1622u);rect(x+3,y+3,w-6,3,0xFF2CCBFFu);
 textf(x+18,y+14,0xFFFFFFFFu,g_title_scale,"PSU DETAILS   PAGE %d/%d",g_psu_metadata_page+1,psu_metadata_page_count());int yy=y+55,lh=scale125(10*g_body_scale)+10;
 if(!g_psu_meta_cache.valid)psu_metadata_cache_load(); psu_entry_t root=g_psu_meta_cache.root;int rr=g_psu_meta_cache.root_ok?0:-1;size_t vs=g_psu_meta_cache.icon_sys_size;unsigned char*ic=g_psu_meta_cache.icon_sys;
 if(g_psu_metadata_page==0){
  textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"PSU / MEMORY-CARD DIRECTORY METADATA");yy+=lh+4;
  textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Archive: %s",base_name(archive_path));yy+=lh;
  if(!rr){char ct[40],mt[40];meta_time(ct,sizeof(ct),&root.ctime);meta_time(mt,sizeof(mt),&root.mtime);textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Save folder: %s",root.name);yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Apollo-derived Title ID: %s",strlen(root.name)>2?root.name+2:"");yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Root mode/attributes: 0x%04X",root.attr);yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Directory entries: %u",root.size);yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Created: %s",ct);yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Modified: %s",mt);yy+=lh;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Unknown/root field @0x20: 0x%08X%08X",(unsigned)(root.unknown2>>32),(unsigned)root.unknown2);yy+=lh;}
  textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"Title ID above is derived by Apollo from folder characters 3-12; it is not a PSU field.");
 }else if(g_psu_metadata_page==1){
  textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"ICON.SYS TITLE / ICON METADATA");yy+=lh+4;
  if(ic&&vs>=452){char title[96],v[65],c[65],d[65];ps2_sjis_title_ascii(ic+192,68,title,sizeof(title));memcpy(v,ic+260,64);v[64]=0;memcpy(c,ic+324,64);c[64]=0;memcpy(d,ic+388,64);d[64]=0;
   uint16_t sl=meta_rd16(ic+6);uint32_t tr=meta_rd32(ic+12);char l1[96]={0},l2[96]={0};
   if(sl>0&&sl<68){sjis_title_slice_ascii(ic+192,0,sl,l1,sizeof(l1));sjis_title_slice_ascii(ic+192,sl,68,l2,sizeof(l2));}else snprintf(l1,sizeof(l1),"%s",title);
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Format: %.4s   icon.sys size: %u bytes",ic,(unsigned)vs);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Game Name: %s",l1);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Save Name: %s",l2[0]?l2:"(none)");yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Second-line byte offset: %u",sl);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Background transparency: %u / 128",(unsigned)tr);yy+=lh;
   textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"  0 = transparent   128 = opaque%s",tr>128?"   (non-standard value)":"");yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"View icon: %s",v);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Copy icon: %s",c);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Delete icon: %s",d);
  }else textf(x+20,yy,0xFFFFA0A0u,g_body_scale,"icon.sys missing, short, or unreadable.");
 }else if(g_psu_metadata_page==2){
  textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"ICON.SYS BACKGROUND METADATA");yy+=lh+4;
  if(ic&&vs>=452){
   static const char*cn[4]={"Upper left","Upper right","Lower left","Lower right"};
   uint32_t tr=meta_rd32(ic+12);textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Transparency: %u / 128",(unsigned)tr);yy+=lh;
   for(int i=0;i<4;i++){int o=16+i*16;uint32_t r=meta_rd32(ic+o),g=meta_rd32(ic+o+4),b=meta_rd32(ic+o+8),a=meta_rd32(ic+o+12);textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"%s: R=%u G=%u B=%u  fourth=%u",cn[i],(unsigned)r,(unsigned)g,(unsigned)b,(unsigned)a);yy+=lh;}
   yy+=4;textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"PS2 Browser background colours use four corner vectors; typical RGB range is 0-128.");
  }else textf(x+20,yy,0xFFFFA0A0u,g_body_scale,"icon.sys missing, short, or unreadable.");
 }else if(g_psu_metadata_page==3){
  textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"ICON.SYS LIGHTING METADATA");yy+=lh+4;
  if(ic&&vs>=452){
   for(int i=0;i<4;i++){textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Light %d direction: X %.3f  Y %.3f  Z %.3f  W %.3f",i+1,meta_rdf32(ic+80+i*16),meta_rdf32(ic+84+i*16),meta_rdf32(ic+88+i*16),meta_rdf32(ic+92+i*16));yy+=lh;}
   yy+=3;for(int i=0;i<4;i++){textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Light %d colour: R %.3f  G %.3f  B %.3f  A %.3f",i+1,meta_rdf32(ic+128+i*16),meta_rdf32(ic+132+i*16),meta_rdf32(ic+136+i*16),meta_rdf32(ic+140+i*16));yy+=lh;}
   yy+=3;textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Ambient light: R %.3f  G %.3f  B %.3f  A %.3f",meta_rdf32(ic+176),meta_rdf32(ic+180),meta_rdf32(ic+184),meta_rdf32(ic+188));
  }else textf(x+20,yy,0xFFFFA0A0u,g_body_scale,"icon.sys missing, short, or unreadable.");
 }else if(sharkport_details_active() && g_psu_metadata_page==4+g_psu_meta_cache.entry_count){
  textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"SHARKPORT / X-PORT ICON COMPATIBILITY");yy+=lh+4;
  textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Source archive: %s",base_name(g_foreign_source_path));yy+=lh;
  textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Embedded icon.sys: %s",ic&&vs>=452&&!memcmp(ic,"PS2D",4)?"Valid PS2D header":"Missing / invalid");yy+=lh;
  if(ic&&vs>=452&&!memcmp(ic,"PS2D",4)){
   char v[65]={0},c[65]={0},d[65]={0};
   memcpy(v,ic+260,64);memcpy(c,ic+324,64);memcpy(d,ic+388,64);
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"View model: %.64s",v);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Copy model: %.64s",c);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Delete model: %.64s",d);yy+=lh;
  }
  unsigned long view_geometry=0,view_texture=0;
  if(ic&&vs>=452&&!memcmp(ic,"PS2D",4)){ char names[3][65]; unsigned long hashes[3]={0,0,0};size_t lengths[3]={0,0,0};int ok[3]={0,0,0};
   const char *slots[3]={"View","Copy","Delete"};
   for(int i=0;i<3;i++){
    icon_diag_name(names[i],ic+260+i*64);
    if(!names[i][0])continue;
    void *model=NULL;size_t len=0;
    if(psu_read_file(archive_path,names[i],&model,&len)==0&&model){
     hashes[i]=icon_diag_crc((const unsigned char*)model,len);lengths[i]=len;ok[i]=1; if(i==0)icon_diag_parts((const unsigned char*)model,len,&view_geometry,&view_texture);
    }
    free(model);
   }
   yy+=4;
   for(int i=0;i<3;i++){
    if(ok[i])textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"%s: %u bytes  CRC32 %08lX",slots[i],(unsigned)lengths[i],hashes[i]);
    else textf(x+20,yy,0xFFFFA0A0u,g_body_scale,"%s: model unavailable",slots[i]);
    yy+=lh;
   }
   if(ok[0]&&ok[1]&&ok[2]){
    int unique=1;for(int i=1;i<3;i++){int seen=0;for(int j=0;j<i;j++)if(lengths[i]==lengths[j]&&hashes[i]==hashes[j])seen=1;if(!seen)unique++;}
    textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"Distinct icon models: %d of 3",unique);yy+=lh;
   }
  }
  yy+=4;
  textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"View geometry CRC32: %08lX   texture CRC32: %08lX",view_geometry,view_texture);yy+=lh;
  textf(x+20,yy,0xFFFFDB88u,g_body_scale,"Icon: %s",view_geometry&&view_texture?icon_diag_origin(view_geometry,view_texture):"UNKNOWN (texture unavailable)");yy+=lh+4;
  textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"Different hashes do not prove a patch or exporter version.");yy+=lh;
  textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"PSU export preserves embedded icon files unchanged.");
 }else{
  int ei=g_psu_metadata_page-4;
  if(ei>=0&&ei<g_psu_meta_cache.entry_count){psu_entry_t*e=&g_psu_meta_cache.entries[ei];
   if(!strcmp(e->name,".")) textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"SPECIAL DIRECTORY ENTRY: . (CURRENT DIRECTORY)");
   else if(!strcmp(e->name,"..")) textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"SPECIAL DIRECTORY ENTRY: .. (PARENT DIRECTORY)");
   else textf(x+20,yy,0xFF8FE7FFu,g_body_scale,"FILE ENTRY: %s",e->name);
   yy+=lh; textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"PSU DIRECTORY ENTRY %d/%d",ei+1,g_psu_meta_cache.entry_count);yy+=lh+4;
   char ct[40],mt[40];meta_time(ct,sizeof(ct),&e->ctime);meta_time(mt,sizeof(mt),&e->mtime);
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Name @0x40: %s",e->name);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Mode/attributes @0x00: 0x%04X",e->attr);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Size/count @0x04: %u (0x%08X)",(unsigned)e->size,(unsigned)e->size);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Created @0x08: %s",ct);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"  raw: unused=%02X sec=%02X min=%02X hour=%02X day=%02X month=%02X year=%04X",e->ctime.unused,e->ctime.sec,e->ctime.min,e->ctime.hour,e->ctime.day,e->ctime.month,e->ctime.year);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Modified @0x18: %s",mt);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"  raw: unused=%02X sec=%02X min=%02X hour=%02X day=%02X month=%02X year=%04X",e->mtime.unused,e->mtime.sec,e->mtime.min,e->mtime.hour,e->mtime.day,e->mtime.month,e->mtime.year);yy+=lh;
   textf(x+20,yy,0xFFFFFFFFu,g_body_scale,"Unknown @0x20: 0x%08X%08X",(unsigned)(e->unknown2>>32),(unsigned)e->unknown2);yy+=lh;
   textf(x+20,yy,0xFFB8C6D3u,g_body_scale,"Record size: 0x200 bytes; name field begins at 0x40 (32 bytes).");
  }else textf(x+20,yy,0xFFFFA0A0u,g_body_scale,"Unable to read directory entry.");
 }
 const legend_entry_t meta_controls[]={{"L1/R1","PAGE"},{"O","CLOSE"}};
 draw_control_legend(meta_controls,2,x+18,y+h-38,w-36,g_help_scale);
}
/* v1.50 Phase 3S: archive Details is a pure RSX modal scene.  The archive
   browser itself is still transitional software composition, so capture the
   already-completed browser frame once when Details opens, downsample it into
   an RSX texture, and reuse that texture behind every metadata page.  This
   prevents Details from flipping between CPU-composed archive buffers while
   Tiny3D draws the modal. */
#define DETAILS_BASE_W 848
#define DETAILS_BASE_H 512
static u32 *g_details_base_tex=NULL;
static u32 g_details_base_off=0;
static int g_details_base_ready=0;
static void details_base_capture(){
 if(!g_details_base_tex){
  g_details_base_tex=(u32*)tiny3d_AllocTexture(DETAILS_BASE_W*DETAILS_BASE_H*4);
  if(g_details_base_tex)g_details_base_off=tiny3d_TextureOffset(g_details_base_tex);
 }
 if(!g_details_base_tex){g_details_base_ready=0;return;}
 /* present() advances g_back, therefore g_back^1 is the completed frame the
    user is currently looking at when CROSS opens Details. */
 u32 *src=g_fb[g_back^1];
 int stride=(int)(g_pitch/4);
 for(int y=0;y<DETAILS_BASE_H;y++){
  int sy=(int)(((long long)y*g_height)/DETAILS_BASE_H); if(sy>=(int)g_height)sy=(int)g_height-1;
  u32 *d=g_details_base_tex+(size_t)y*DETAILS_BASE_W;
  u32 *r=src+(size_t)sy*stride;
  for(int x=0;x<DETAILS_BASE_W;x++){
   int sx=(int)(((long long)x*g_width)/DETAILS_BASE_W); if(sx>=(int)g_width)sx=(int)g_width-1;
   d[x]=r[sx];
  }
 }
 g_details_base_ready=1;
}
static void details_base_draw_rsx(){
 if(!g_details_base_ready)return;
 tiny3d_SetTextureWrap(0,g_details_base_off,DETAILS_BASE_W,DETAILS_BASE_H,DETAILS_BASE_W*4,
   TINY3D_TEX_FORMAT_A8R8G8B8,TEXTWRAP_CLAMP,TEXTWRAP_CLAMP,TEXTURE_LINEAR);
 tiny3d_SetPolygon(TINY3D_QUADS);
 tiny3d_VertexPos(0,0,1000.0f); tiny3d_VertexColor(0xFFFFFFFFu); tiny3d_VertexTexture(0,0);
 tiny3d_VertexPos(848,0,1000.0f); tiny3d_VertexTexture(1,0);
 tiny3d_VertexPos(848,512,1000.0f); tiny3d_VertexTexture(1,1);
 tiny3d_VertexPos(0,512,1000.0f); tiny3d_VertexTexture(0,1);
 tiny3d_End();
}
static int draw_psu_metadata_rsx_frame(){
 if(!g_psu_metadata_active||!g_details_base_ready)return 0;
 for(int i=0;i<8;i++)g_5c_off[i]=0; for(int i=0;i<7;i++){g_5c_cb_count[i]=0;g_5c_cb_us[i]=0;} g_5c_stage=0; g_5c_draw_active=1;
 tiny3d_Clear(0x000000FFu,TINY3D_CLEAR_ALL);
 tiny3d_AlphaTest(1,0x10,TINY3D_ALPHA_FUNC_GEQUAL);
 tiny3d_BlendFunc(1,
   (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
   (blend_dst_func)(TINY3D_BLEND_FUNC_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA),
   (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));
 tiny3d_Project2D();
 details_base_draw_rsx();
 g_rsx_ui_overlay=1;
 draw_psu_metadata_modal();
 rsx_flush_text_batch(); rsx_flush_rect_batch(); rsx_flush_line_batch();
 g_rsx_ui_overlay=0;
 present(); g_frame_counter++;
 return 1;
}
static void draw_downloader_progress();
static void draw(){
 g_perf_draw_begin_us=fps_now_us();
 /* Details owns a complete deterministic RSX frame.  Do this before every
    legacy archive fast path so no software framebuffer renderer participates
    while the metadata modal is visible. */
 /* Phase 4A: Phase 3T made the opened-PSU browser a complete RSX scene.
    Do not use the obsolete Phase 3S framebuffer snapshot behind Details; it can
    contain stale software/rename pixels.  Let the normal RSX browser/HUD frame
    render below, then composite Details in the standard overlay pass. */
 /* v1.50 Phase 3F: RSX keyboard/modal integration and lower-frequency downloader UI refresh. */
 if(g_device_switch_redraw_frames>0)g_ui_dirty=1;
 /* Phase 3T: archive mode no longer enters any software framebuffer fast
    path.  It is a deterministic RSX scene just like filesystem mode. */
 /* The browser is normally static.  Keep both framebuffers containing the
    same completed frame and only re-render when UI state changes.  ICN model
    animation has its own preview-only dirty path above, so it never forces a
    full 1080p HUD redraw. */
 /* Modal/overlay redraws must not advance or rebuild the ICN preview first.
    Doing so made the first Details pages, Options navigation and downloader
    startup alternate between a new preview generation and the modal frame. */
 if(g_ui_dirty && !g_delete_confirm_active && !g_zip_confirm_active && !g_apollo_warning_active &&
    !g_psu_metadata_active && !g_options_active && !g_catalogue_active && !g_download_progress_active)
  ensure_preview_current();
 /* Phase 3E: the RSX filesystem browser is a deterministic full-frame scene.
    Never flip an undrawn Tiny3D backbuffer. The old software compositor could
    present a cached second buffer, but the RSX scene is command-rendered and
    must be emitted into whichever buffer Tiny3D currently owns before every
    flip. Archive/ICN mode retains its established dirty/cached presentation. */
 g_ui_dirty=1; /* Phase 3T: every browser frame is RSX-owned. */
 if(!g_ui_dirty){
  present();
  if(success_frames>0){success_frames--; if(success_frames==0)g_ui_dirty=1;}
  g_frame_counter++;
  return;
 }
 /* Tiny3D requires every polygon batch to live between Clear and Flip.
    Phase 3 previously submitted browser primitives without beginning a fresh
    Tiny3D frame. RPCS3 tolerated that invalid ordering, but real RSX hardware
    retained stale command/shader state and rendered only fragments.

    For the transitional mixed renderer, begin a Tiny3D frame without clearing colour, then restore the
    cached software wallpaper into the inactive framebuffer, and only then
    submit the RSX UI. Only depth/stencil are cleared, so the queued RSX clear cannot race the PPU colour-buffer copy. */
 /* Phase 3T: both filesystem and opened-PSU browsers are complete Tiny3D
    frames.  The PPU never writes the display target while RSX owns it. */
 tiny3d_Clear(0x000000FFu,TINY3D_CLEAR_ALL);
 tiny3d_AlphaTest(1,0x10,TINY3D_ALPHA_FUNC_GEQUAL);
 tiny3d_BlendFunc(1,
   (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
   (blend_dst_func)(TINY3D_BLEND_FUNC_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA),
   (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));
 tiny3d_Project2D();
 g_5c_off[0]=phase5c_cmd_off(); g_5c_stage=0;
 unsigned long long perf_t0=fps_now_us(),perf_t1=0;
 g_perf_text_flushes=0; g_perf_rect_flushes=0; g_perf_line_submits=0;
 rsx_draw_background();
 perf_t1=fps_now_us(); perf_avg(&g_perf_bg_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[1]=phase5c_cmd_off(); g_5c_stage=1;
 g_rsx_ui_overlay=1;
 const char *title="PS2 PSU Manager v1.63";
 int title_h=scale125(12*g_title_scale)+30;
 /* modern desktop-style app bar */
 int app_x=g_margin-14, app_y=g_title_y-12, app_w=(int)g_width-g_margin*2+28;
 round_rect(app_x+4,app_y+6,app_w,title_h+4,10,0x40000000u);
 round_rect(app_x,app_y,app_w,title_h+2,10,0xFF10253Au);
 gradient_rect_v(app_x+2,app_y+2,app_w-4,title_h-1,0xFF315978u,0xFF12283Du);
 /* strong v1.35 cyan identity line makes the title bar visibly different. */
 rect(app_x+10,app_y+title_h-2,app_w-20,3,0xFF2CCBFFu);
 rect(app_x+12,app_y+3,app_w-24,1,0x806FD7F5u);
 /* small four-tile app mark */
 int mark=g_body_scale>1?16:11; int mx=g_title_x,my=g_title_y+2;
 rect(mx,my,mark,mark,0xFF4CC2FFu); rect(mx+mark+3,my,mark,mark,0xFF36C5A5u);
 rect(mx,my+mark+3,mark,mark,0xFFF2B84Bu); rect(mx+mark+3,my+mark+3,mark,mark,0xFFB86FD4u);
 int title_tx=mx+mark*2+12;
 const char *mode=archive_mode?"[PSU]":(!strncmp(cwd,"/dev_usb",8)?"[USB]":"[HDD]");
 /* v1.49: FPS and current-device indicator are plain title-bar text.  Keep
    them in the normal title-bar composition instead of independently drawn
    overlay badges, avoiding extra overlay rectangles and stale-buffer badges. */
 char title_status[96],compact_status[48];
 if(g_fps_enabled) snprintf(title_status,sizeof(title_status),"(FPS:%.1f D:%.1f F:%.1f T:%.1fms) %s",g_fps_value,g_perf_draw_ms,g_perf_flip_ms,g_perf_frame_ms,mode);
 else snprintf(title_status,sizeof(title_status),"%s",mode);
 if(g_fps_enabled)snprintf(compact_status,sizeof(compact_status),"FPS:%.1f %s",g_fps_value,mode);
 else snprintf(compact_status,sizeof(compact_status),"%s",mode);
 int status_scale=g_title_scale;
 int status_w=text_width(title_status,status_scale);
 while(status_scale>1&&status_w>(int)g_width-g_margin*2){status_scale--;status_w=text_width(title_status,status_scale);}
 int right_edge=(int)g_width-g_margin-6;
 int status_x=ui_right_aligned_x((int)g_width,g_margin+6,status_w);
 int title_draw_scale=g_title_scale;
 int titlew=text_width(title,title_draw_scale);
 int title_space=ui_inline_left_available_width(title_tx,status_x,18);
 if(g_fps_enabled&&titlew>title_space){
  snprintf(title_status,sizeof(title_status),"%s",compact_status);
  status_scale=g_title_scale;status_w=text_width(title_status,status_scale);
  while(status_scale>1&&status_w>right_edge){status_scale--;status_w=text_width(title_status,status_scale);}
  status_x=ui_right_aligned_x((int)g_width,g_margin+6,status_w);
  title_space=ui_inline_left_available_width(title_tx,status_x,18);
 }
 while(title_draw_scale>1&&titlew>title_space){title_draw_scale--;titlew=text_width(title,title_draw_scale);}
 if(titlew>title_space){title="PSU Manager";title_draw_scale=1;titlew=text_width(title,title_draw_scale);}
 text(title_tx,g_title_y,title,success_frames>0?0xFFBEE8FFu:0xFFFFFFFFu,title_draw_scale);
 text(status_x,g_title_y,title_status,0xFFEAF3F8u,status_scale);
 /* address bar / breadcrumb */
 int path_h=scale125(8*g_body_scale)+20;
 round_rect(g_path_x-8,g_path_y-8,(int)g_width-g_margin*2+16,path_h,7,0xF00B1724u);
 gradient_rect_v(g_path_x-6,g_path_y-6,(int)g_width-g_margin*2+12,path_h-4,0xFF203B55u,0xFF101E2Du);
 rect(g_path_x-6,g_path_y+path_h-11,(int)g_width-g_margin*2+12,2,0x803BC7F3u);
 round_rect(g_path_x-3,g_path_y-4,scale125(13*g_body_scale),scale125(13*g_body_scale),3,0xFF2CCBFFu);
 if(!archive_mode && !g_rsx_ui_overlay){
  g_path_cache_x=g_path_x-8;g_path_cache_y=g_path_y-8;g_path_cache_w=(int)g_width-g_margin*2+16;g_path_cache_h=path_h;
  capture_region(&g_path_base_cache,&g_path_cache_w,&g_path_cache_h,g_path_cache_x,g_path_cache_y,g_path_cache_w,g_path_cache_h);
  g_path_base_valid=g_path_base_cache!=NULL;
 }
 text(g_path_x+scale125(16*g_body_scale),g_path_y,archive_mode?archive_path:cwd,0xFFF1F5F8u,g_body_scale);
 perf_t1=fps_now_us(); perf_avg(&g_perf_head_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[2]=phase5c_cmd_off(); g_5c_stage=2;

 static const legend_entry_t legend_fs[]={
  {"X","OPEN"},{"O","BACK"},{"LEFT/RIGHT","DEVICE"},{"L1/R1","FAST SCROLLING"},{"L2","CANCEL"},{"SQUARE","COPY"},{"TRIANGLE","RENAME"},{"SELECT","DELETE"},{"L3","EXTRACT PSU / ZIP HERE"},{"R2","BLANK PSU"},{"R3","CREATE PSU FROM FOLDER"},{"START","OPTIONS"}
 };
 static const legend_entry_t legend_foreign[]={
  {"X","DETAILS"},{"O","CLOSE"},{"L1/R1","FAST SCROLLING"},{"SQUARE","EXPORT"},{"L3","CONVERT TO PSU"},{"START","OPTIONS"}
 };
 static const legend_entry_t legend_psu[]={
  {"X","DETAILS"},{"O","CLOSE PSU"},{"LEFT/RIGHT","DEVICE"},{"L1/R1","FAST SCROLLING"},{"L2","CANCEL"},{"SQUARE","EXPORT"},{"TRIANGLE","INJECT"},{"R3","RENAME"},{"SELECT","DELETE"},{"START","OPTIONS"}
 };
 const legend_entry_t *legend=archive_mode?(g_foreign_archive_view?legend_foreign:legend_psu):legend_fs;
 int legend_count=archive_mode?(g_foreign_archive_view?(int)(sizeof(legend_foreign)/sizeof(legend_foreign[0])):(int)(sizeof(legend_psu)/sizeof(legend_psu[0]))):(int)(sizeof(legend_fs)/sizeof(legend_fs[0]));
 int legend_lines=measure_control_legend_lines(legend,legend_count,(int)g_width-g_margin*2-12,g_help_scale);
 int legend_line_h=legend_line_height(g_help_scale);
 int legend_bottom_pad=14; /* breathing room below final control row */
 int legend_h=legend_lines*legend_line_h+14+legend_bottom_pad;
 int status_h=success_frames>0?(scale125(12*g_help_scale)+24):(scale125(8*g_help_scale)+18);
 int footer_h=legend_h+status_h+20;
 int footer_y=(int)g_height-footer_h-g_margin/2;

 int panel_gap=archive_mode?16:0;
 int panel_w=0;
 if(archive_mode){ panel_w=(g_body_scale>1)?460:300; if(g_width<900)panel_w=260; if(panel_w>(int)g_width/2)panel_w=(int)g_width/2; }
 int list_right=(int)g_width-g_margin-(archive_mode?panel_w+panel_gap:0);
 int list_panel_y=g_list_y-12;
 int browser_header_h=scale125(8*g_body_scale)+22;
 int rows_y=list_panel_y+browser_header_h+8;
 int available=footer_y-rows_y-14; if(available<g_row_h)available=g_row_h;
 int max_visible=available/g_row_h; if(max_visible<1)max_visible=1;
 int visible=list_visible_for_count(item_count,max_visible);
 int list_panel_h=list_panel_height_for_visible(browser_header_h,visible);
 int max_panel_h=footer_y-list_panel_y-12; if(list_panel_h>max_panel_h)list_panel_h=max_panel_h;
 browser_list_card(g_list_x-14,list_panel_y,list_right-(g_list_x-14),list_panel_h,0xC0121D29u,0xD06A8DA8u,9);
 /* Explorer-style column header */
 browser_column_header(g_list_x-9,list_panel_y+5,list_right-g_list_x-2,browser_header_h-9);
 if(!archive_mode && !g_rsx_ui_overlay){
  g_list_panel_x=g_list_x-14;g_list_panel_y=list_panel_y;g_list_panel_w=list_right-(g_list_x-14);g_list_panel_h=list_panel_h;
  capture_region(&g_list_panel_cache,&g_list_panel_cache_alloc_w,&g_list_panel_cache_alloc_h,
                 g_list_panel_x,g_list_panel_y,g_list_panel_w,g_list_panel_h);
  g_list_panel_cache_valid=g_list_panel_cache!=NULL;
 }
 int emblem_s=(g_body_scale>1)?26:18;
 draw_browser_emblem(g_list_x-4,list_panel_y+7,emblem_s);
 char headbuf[96]; snprintf(headbuf,sizeof(headbuf),"Files and folders   %d item%s",item_count,item_count==1?"":"s");
 text(g_list_x+emblem_s+10,list_panel_y+9,headbuf,0xFFFFFFFFu,g_body_scale);
 const char *size_head="SIZE",*date_head="DATE"; int size_head_w=text_width(size_head,g_body_scale);
 int size_col_w=text_width("999.9 MB",g_body_scale)+18;int date_head_w=text_width(date_head,g_body_scale);
 text(list_right-size_head_w-12,list_panel_y+9,size_head,0xFF8FE7FFu,g_body_scale);
 text(list_right-size_col_w-date_head_w-18,list_panel_y+9,date_head,0xFF8FE7FFu,g_body_scale);

 /* Cache the untouched rows area. Scrolling can restore this small region and
    redraw only the file rows instead of rebuilding the whole screen. */
 g_list_cache_right=list_right; g_list_cache_rows_y=rows_y; g_list_cache_visible=visible; g_list_cache_max_visible=max_visible;
 if(selected<top_index)top_index=selected;
 if(selected>=top_index+visible)top_index=selected-visible+1;
 int icon_size=128;
 int icon_space=icon_size+18;
 int saved_selected_for_cache=selected;
 selected=-1; /* render stable unselected rows once; selection becomes an overlay */
 for(int row=0;row<visible;row++){
  int i=top_index+row; if(i>=item_count)break;
  int copied=0;
  if(!archive_mode&&clipboard[0]){char item_path[PATHSZ];join(item_path,sizeof(item_path),cwd,items[i].name);copied=!strcmp(item_path,clipboard);}
  int yy=rows_y+row*g_row_h;
  int row_x=g_list_x-8,row_w=list_right-row_x-12;
  if(i==selected){
   round_rect(row_x,yy-4,row_w,g_row_h,5,archive_mode&&export_target_active?0xFFA46C24u:0xFF215F8Cu);
   rect(row_x+1,yy-3,4,g_row_h-2,archive_mode&&export_target_active?0xFFFFCF6Cu:0xFF79D6FFu);
  }else if(row&1) round_rect(row_x,yy-4,row_w,g_row_h,4,0x4A1B2B3Au);
  rect(row_x+8,yy+g_row_h-5,row_w-16,1,0x304D718Au);
  u32 c=copied?0xFFFF7B7Bu:((i==selected)?0xFFFFFFFFu:0xFFE6EDF3u);
  char sizebuf[32],datebuf[32]; format_file_size(items[i].size,items[i].is_dir,sizebuf,sizeof(sizebuf)); format_file_date(items[i].latest_time,items[i].date_known,datebuf,sizeof(datebuf));
  int sizew=text_width(sizebuf,g_body_scale),datew=text_width(datebuf,g_body_scale);
  int size_col_w=text_width("999.9 MB",g_body_scale)+18; if(size_col_w<sizew+8)size_col_w=sizew+8;
  int date_col_w=text_width("14-09-2026 23:59",g_body_scale)+18; if(date_col_w<datew+8)date_col_w=datew+8;
  int date_right=list_right-size_col_w-10;
  int text_area=date_right-date_col_w-g_list_x-icon_space-14;
  int name_chars=text_area/scale125(6*g_body_scale)-1; if(name_chars<6)name_chars=6;
  int icon_y=yy+4;
  if(items[i].is_dir)draw_folder_icon(g_list_x+5,icon_y,icon_size,i==selected); else {const u32 *thumb=archive_mode?NULL:browser_archive_thumb(items[i].name);if(thumb)draw_texture_scaled(g_list_x+5,icon_y,128,128,thumb);else draw_file_icon(g_list_x+5,icon_y,icon_size,items[i].name,i==selected);}
  char namebuf[320]; snprintf(namebuf,sizeof(namebuf),"%-*.*s",name_chars,name_chars,items[i].name);
  text(g_list_x+icon_space+5,yy,namebuf,c,g_body_scale);
  text(date_right-datew,yy,datebuf,i==selected?0xFFFFFFFFu:0xFFB8C6D3u,g_body_scale);
  text(list_right-sizew-10,yy,sizebuf,i==selected?0xFFFFFFFFu:0xFFB8C6D3u,g_body_scale);
 }
 selected=saved_selected_for_cache;
 if(!g_rsx_ui_overlay) capture_list_base(g_list_x-8,rows_y-4,list_right-(g_list_x-8)-12,visible*g_row_h+8);
 if(selected>=top_index&&selected<top_index+visible)draw_selection_overlay(selected-top_index);
 g_list_prev_selected=selected;
 g_list_prev_top=top_index;
 perf_t1=fps_now_us(); perf_avg(&g_perf_list_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[3]=phase5c_cmd_off(); g_5c_stage=3;
 if(archive_mode){
  /* Start the preview below the Explorer folder/header tab instead of beside it. */
  int panel_y=rows_y;
  int panel_h=footer_y-panel_y-12; if(panel_h<160)panel_h=160;
  int panel_x=list_right+panel_gap;
  int panel_w2=(int)g_width-g_margin-panel_x;
  if(!g_rsx_ui_overlay)capture_preview_base(panel_x,panel_y,panel_w2,panel_h);
  draw_preview_panel(panel_x,panel_y,panel_w2,panel_h);
 }else g_preview_base_valid=0;
 perf_t1=fps_now_us(); perf_avg(&g_perf_preview_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[4]=phase5c_cmd_off(); g_5c_stage=4;
 /* modern command bar / status bar.  Keep the silver/grey command
    background behind every wrapped legend row, including the second
    filesystem row containing R3 FOLDER->PSU.  Older builds relied on
    modern_card()'s top-third sheen, so only the first row looked enclosed. */
 unsigned long long foot_sub0=fps_now_us(),foot_sub1=0;
 browser_list_card(g_margin-8,footer_y,(int)g_width-g_margin*2+16,legend_h+status_h+12,0xE00B1723u,0xD05D86A4u,9);
 int command_x=g_margin-1, command_y=footer_y+7;
 int command_w=(int)g_width-g_margin*2+2, command_h=legend_h-3;
 round_rect(command_x,command_y,command_w,command_h,6,0xFF132B40u);
 /* Full-height command gradient: unlike the generic glass-card sheen this
    is intentionally sized to legend_h, so it cannot stop above a wrapped
    second row.  Leave a small rim for the rounded border. */
 if(command_h>8) gradient_rect_v(command_x+4,command_y+3,command_w-8,command_h-6,0xFF315B78u,0xFF142A3Eu);
 rect(command_x+7,command_y+1,command_w-14,2,0xFF75E0FFu);
 rect(command_x+7,command_y+command_h-3,command_w-14,2,0xFF1EA5D4u);
 foot_sub1=fps_now_us(); perf_avg(&g_perf_foot_panel_ms,perf_ms(foot_sub0,foot_sub1)); foot_sub0=foot_sub1;
 draw_control_legend(legend,legend_count,g_margin,footer_y+10,(int)g_width-g_margin*2-12,g_help_scale);
 foot_sub1=fps_now_us(); perf_avg(&g_perf_foot_legend_ms,perf_ms(foot_sub0,foot_sub1)); foot_sub0=foot_sub1;
 if(g_fps_enabled){
  /* Phase 4Z: put the 4Y tail split on the first diagnostics row so it is
     visible on real TVs with bottom-edge overscan.  Timing/presentation is
     otherwise identical to the stable 4Y/4N path. */
  char perf_break[224];
  snprintf(perf_break,sizeof(perf_break),"5N BUF M%u Q%u D%u>%u H%u/R%u S%u N%u Z%u",
   (unsigned)g_5n_flip_mode,(unsigned)g_5n_rep_q_id,(unsigned)g_5n_rep_disp_q,(unsigned)g_5n_rep_disp_w,
   (unsigned)g_5n_rep_disp_h,(unsigned)g_5n_rep_render_h,(unsigned)g_5n_rep_same,(unsigned)g_5n_disp_n,(unsigned)g_5n_disp_z);
  char stage_break[224];
  snprintf(stage_break,sizeof(stage_break),"5N VB Q%llu H%llu N%llu Z%llu QH%lld HZ%lld",
   g_5n_rep_q_vb,g_5n_rep_h_vb,g_5l_vb_next_entry,g_5l_vb_zero,
   (long long)(g_5n_rep_h_vb-g_5n_rep_q_vb),(long long)(g_5l_vb_zero-g_5n_rep_h_vb));
  /* Phase 5I: with the 256 KiB FIFO retained, expose the synchronized presentation split again. */
  /* previous 5F MOVE diagnostics remain available in source instrumentation but are no longer the primary on-screen row. */
  /* disabled old formatting: */
  if(0) snprintf(stage_break,sizeof(stage_break),"5F MOVE%u S1END%08X T0%08X T1%08X",
   g_5e_s1_inside_bytes,(g_5a_space_count>0?g_5e_space_off_end[0]:0),g_5f_target_off0,g_5f_target_off1);
  int py=footer_y+legend_h+6;
  text(g_margin+12,py+2,perf_break,0xFFFFD37Au,g_help_scale);
  char foot_break[224]; snprintf(foot_break,sizeof(foot_break),"%s",stage_break);
  text(g_margin+12,py+2+scale125(9*g_help_scale),foot_break,0xFFFFB45Au,g_help_scale);
  char flip_break[224];
  snprintf(flip_break,sizeof(flip_break),"5N POLL N%u NZ%u S%u>%u %.1fms S1%.1f",
   g_4x_status_polls,g_5k_poll_nonzero,(unsigned)g_5k_poll_first,(unsigned)g_5k_poll_last,
   (g_5k_poll_first_us&&g_5k_poll_last_us)?(float)(g_5k_poll_last_us-g_5k_poll_first_us)/1000.0f:0.0f,g_5a_space1_ms);
  text(g_margin+12,py+2+scale125(18*g_help_scale),flip_break,0xFFFF8E5Au,g_help_scale);

 }
 foot_sub1=fps_now_us(); perf_avg(&g_perf_foot_diag_ms,perf_ms(foot_sub0,foot_sub1)); foot_sub0=foot_sub1;
 if(success_frames>0){
  int by=footer_y+legend_h+4,bh=scale125(12*g_help_scale)+20;
  round_rect(g_margin,by,(int)g_width-g_margin*2,bh,5,0xD0208C67u);
  text(g_margin+10,by+6,success_line,0xFFFFFFFF,g_help_scale);
 }else if(!g_fps_enabled){
  int sy=footer_y+legend_h+6,bh=scale125(8*g_help_scale)+12;
  round_rect(g_margin,sy,(int)g_width-g_margin*2,bh,5,0xB0202B38u);
  rect(g_margin+2,sy+2,5,bh-4,0xFF2CCBFFu);
  text(g_margin+12,sy+2,status_line,0xFFE4ECF2u,g_help_scale);
 }
 foot_sub1=fps_now_us(); perf_avg(&g_perf_foot_status_ms,perf_ms(foot_sub0,foot_sub1)); foot_sub0=foot_sub1;
  /* Phase 3F: the filesystem/download URL keyboard is part of the same RSX
    frame as the browser.  Phase 3E dropped out of RSX mode before drawing it,
    causing PPU writes into an RSX-owned target (black surround/stray glyphs).
    Archive mode keeps the legacy software keyboard until ICN migration. */
 perf_t1=fps_now_us(); perf_avg(&g_perf_footer_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[5]=phase5c_cmd_off(); g_5c_stage=5;
 if(g_rename_active){
  g_rsx_ui_overlay=1;
  draw_rename_keyboard();
 }
 if(g_rsx_ui_overlay){ rsx_flush_text_batch(); rsx_flush_rect_batch(); rsx_flush_line_batch(); g_rsx_ui_overlay=0; }
 /* Phase 2/3 RSX overlays: these are the screens that changed frequently and
    showed the worst real-hardware blinking.  Their text/panels/buttons now go
    through Tiny3D instead of PPU framebuffer pixel loops. */
 g_rsx_ui_overlay=1;
 if(g_delete_confirm_active)draw_delete_confirmation();
 if(g_zip_confirm_active)draw_zip_overwrite_confirmation();
 if(g_apollo_warning_active)draw_apollo_warning();
 if(g_psu_metadata_active)draw_psu_metadata_modal();
 if(g_options_active)draw_options_menu();
 if(g_catalogue_active)draw_psu_catalogue();
 if(g_homebrew_active)draw_homebrew_catalogue();
 if(g_saves_active)draw_saves_catalogue();
 if(g_download_progress_active)draw_downloader_progress();
 perf_t1=fps_now_us(); perf_avg(&g_perf_modal_ms,perf_ms(perf_t0,perf_t1)); perf_t0=perf_t1; g_5c_off[6]=phase5c_cmd_off(); g_5c_stage=6;
 rsx_flush_text_batch(); rsx_flush_rect_batch(); rsx_flush_line_batch(); g_rsx_ui_overlay=0;
 perf_t1=fps_now_us(); perf_avg(&g_perf_flush_ms,perf_ms(perf_t0,perf_t1)); g_5c_off[7]=phase5c_cmd_off(); g_5c_draw_active=0;
 /* Publish this completed frame only to the inactive backbuffer. Never write
    into g_back^1 here: on real hardware that may still be the scanout buffer.
    The next draw safely synchronises the other buffer after the flip. */
 g_ui_generation++; if(g_ui_generation==0)g_ui_generation=1;
 g_ui_buffer_generation[g_back]=g_ui_generation;
 g_ui_buffer_generation[g_back^1]=0;
 if(archive_mode&&g_preview.valid&&g_model_cache_valid)
  g_model_buffer_generation[g_back]=g_model_generation;
 else g_model_buffer_generation[g_back]=0;
 g_ui_dirty=0; g_list_dirty=0;
 present();
 if(g_device_switch_redraw_frames>0){
  g_device_switch_redraw_frames--;
  if(g_device_switch_redraw_frames>0)g_ui_dirty=1;
 }
 if(success_frames>0){success_frames--; if(success_frames==0)g_ui_dirty=1;}
 g_frame_counter++;
}

static int rename_name_valid(const char *name,char *why,size_t whysz){
 if(!name||!name[0]){snprintf(why,whysz,"Filename cannot be empty");return 0;}
 size_t n=strlen(name);if(n>255){snprintf(why,whysz,"Filename is too long");return 0;}
 if(!strcmp(name,".")||!strcmp(name,"..")){snprintf(why,whysz,"Reserved filename");return 0;}
 for(size_t i=0;i<n;i++){
  unsigned char c=(unsigned char)name[i];
  if(c<' '||c==127||c=='/'||c=='\\'){snprintf(why,whysz,"Invalid character in filename");return 0;}
 }
 return 1;
}
static void rename_panel_bounds(int *ox,int *oy,int *ow,int *oh){
 int pad=18,w=(int)g_width*3/4;if(w>880)w=880;if(w<520)w=(int)g_width-40;
 int keyh=34,keycount=rename_key_count(),rows=(keycount+RENAME_COLS-1)/RENAME_COLS;
 int h=196+rows*keyh,x=((int)g_width-w)/2,y=((int)g_height-h)/2;
 /* include the small shadow/outline margin, but never make the modal cache full-screen */
 int m=10; x-=m;y-=m;w+=m*2;h+=m*2;
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>(int)g_width)w=(int)g_width-x;if(y+h>(int)g_height)h=(int)g_height-y;
 if(ox)*ox=x;if(oy)*oy=y;if(ow)*ow=w;if(oh)*oh=h;
}
static int capture_rename_base(){
 int x,y,w,h;rename_panel_bounds(&x,&y,&w,&h);
 ensure_region_cache(&g_rename_base_cache,&g_rename_cache_w,&g_rename_cache_h,w,h);
 if(!g_rename_base_cache)return 0;
 g_rename_cache_x=x;g_rename_cache_y=y;
 for(int yy=0;yy<h;yy++)memcpy(g_rename_base_cache+(size_t)yy*(size_t)w,g_fb[g_back]+(size_t)(y+yy)*(g_pitch/4)+(size_t)x,(size_t)w*sizeof(u32));
 return 1;
}
static void rename_mark_changed(){
 g_rename_generation++;if(g_rename_generation<=0)g_rename_generation=1;
}
static void rename_text_changed(){
 g_rename_text_generation++;if(g_rename_text_generation<=0)g_rename_text_generation=1;
 rename_mark_changed();
}
static void rename_full_modal_changed(){
 /* Used only when title/help/layout changes (for example blank PSU stage 1->2). */
 rename_mark_changed();
 g_rename_composite_generation=0;
 g_rename_buffer_ready[0]=g_rename_buffer_ready[1]=0;
 g_rename_buffer_key[0]=g_rename_buffer_key[1]=-1;
 g_rename_buffer_text_generation[0]=g_rename_buffer_text_generation[1]=0;
}
static int build_rename_composite(){
 if(!g_rename_active||!g_rename_base_cache)return 0;
 ensure_region_cache(&g_rename_composite_cache,&g_rename_composite_w,&g_rename_composite_h,g_rename_cache_w,g_rename_cache_h);
 if(!g_rename_composite_cache)return 0;
 /* Compose completely into the inactive backbuffer, capture the finished
    rectangle, and never expose intermediate drawing operations. */
 restore_region_to(g_fb[g_back],g_rename_base_cache,g_rename_cache_x,g_rename_cache_y,g_rename_cache_w,g_rename_cache_h);
 /* Build an exact unselected keyboard snapshot first.  This is important
    because the key fills use alpha; simply drawing an unselected key over a
    selected key blends with the old pixels and accumulates highlights. */
 int saved_key=g_rename_key; g_rename_key=-1; draw_rename_keyboard(); g_rename_key=saved_key;
 ensure_region_cache(&g_rename_pristine_cache,&g_rename_pristine_w,&g_rename_pristine_h,g_rename_cache_w,g_rename_cache_h);
 if(!g_rename_pristine_cache)return 0;
 for(int yy=0;yy<g_rename_cache_h;yy++)
  memcpy(g_rename_pristine_cache+(size_t)yy*(size_t)g_rename_cache_w,
         g_fb[g_back]+(size_t)(g_rename_cache_y+yy)*(g_pitch/4)+(size_t)g_rename_cache_x,
         (size_t)g_rename_cache_w*sizeof(u32));
 draw_rename_key_cell(saved_key,1);
 for(int yy=0;yy<g_rename_cache_h;yy++)
  memcpy(g_rename_composite_cache+(size_t)yy*(size_t)g_rename_cache_w,
         g_fb[g_back]+(size_t)(g_rename_cache_y+yy)*(g_pitch/4)+(size_t)g_rename_cache_x,
         (size_t)g_rename_cache_w*sizeof(u32));
 g_rename_composite_generation=g_rename_generation;
 return 1;
}
static void restore_rename_key_from_pristine_to(u32 *dst,int key_index){
 if(!dst||!g_rename_pristine_cache||key_index<0)return;
 int x,y,w,pad,keyw,field_y,ky;rename_keyboard_geometry(&x,&y,&w,&pad,&keyw,&field_y,&ky);
 int keyh=34,col=key_index%RENAME_COLS,row=key_index/RENAME_COLS;
 int kx=x+pad+col*keyw, yy=ky+row*keyh;
 int rx=kx,ry=yy,rw=keyw,rh=keyh;
 int sx=rx-g_rename_cache_x,sy=ry-g_rename_cache_y;
 if(sx<0||sy<0||sx+rw>g_rename_cache_w||sy+rh>g_rename_cache_h)return;
 for(int j=0;j<rh;j++) memcpy(dst+(size_t)(ry+j)*(g_pitch/4)+(size_t)rx,
   g_rename_pristine_cache+(size_t)(sy+j)*(size_t)g_rename_cache_w+(size_t)sx,(size_t)rw*sizeof(u32));
}
static void restore_rename_key_from_pristine(int key_index){restore_rename_key_from_pristine_to(g_fb[g_back],key_index);}

/* v1.47 navigation fast path: directional movement changes only two key
   cells in the inactive backbuffer.  This preserves double-buffer ownership:
   the currently displayed frontbuffer is never modified in place. */
static void rename_patch_key_selection(int old_key,int new_key){
 if(!g_rename_active||old_key==new_key||!g_rename_pristine_cache)return;
 restore_rename_key_from_pristine_to(g_fb[g_back],old_key);
 restore_rename_key_from_pristine_to(g_fb[g_back],new_key);
 /* CASE is stateful, unlike ordinary keys.  Never trust a cached pristine
    pixel copy for it after a mode toggle: explicitly repaint its unselected
    cell from the live case state when focus leaves it.  This guarantees that
    lowercase remains orange and uppercase remains green after navigation. */
 if(old_key==rename_char_key_count()) draw_rename_key_cell(old_key,0);
 if(new_key==rename_char_key_count()) draw_rename_key_cell(new_key,0);
 draw_rename_key_cell(new_key,1);
 g_rename_buffer_key[g_back]=new_key;
}


static int draw_rename_modal_update(){
 if(!g_rename_active&&g_rename_restore_frames<=0)return 0;
 if(!g_rename_base_cache)return 0;
 if(g_rename_active){
  /* v1.47: the rename overlay no longer owns an ICN renderer.  Advance the
     preview with the exact same v1.46 model-cache path used by the normal
     browser, but defer present() until after the keyboard overlay is drawn. */
  if(archive_mode&&g_model_base_valid&&g_preview.valid&&
     item_count>0&&selected>=0&&selected<item_count&&!items[selected].is_dir&&preview_entry_is_standard_icn(items[selected].name)){
   if(g_model_cache_valid && g_model_buffer_generation[g_back]!=g_model_generation){
    restore_model_base_to_buffer(g_fb[g_back]);
    blit_model_cache(g_model_base_x,g_model_base_y,g_model_base_w,g_model_base_h);
    g_model_buffer_generation[g_back]=g_model_generation;
   }else if(g_preview_startup_hold>0){
    g_preview_startup_hold--;
    if(g_preview_startup_hold==0){
     g_preview_rotation_start_frame=g_frame_counter;
     g_model_cache_frame=g_frame_counter;
    }
   }else if((g_frame_counter-g_model_cache_frame)>=model_update_interval()){
    restore_model_base_to_buffer(g_fb[g_back]);
    int old_generation=g_model_generation;
    draw_textured_model_safe(g_model_base_x,g_model_base_y,g_model_base_w,g_model_base_h);
    if(g_model_generation!=old_generation)g_model_buffer_generation[g_back]=g_model_generation;
   }
  }
  /* v1.47 cached static overlay: each framebuffer retains its completed
     keyboard pixels.  Rebuild the keyboard only when that framebuffer has
     never received it, or when the selected key/text actually changed.
     The ICN renderer remains byte-for-byte on the proven static-overlay path
     above and never enters a rename-specific rasterizer. */
  const int modal_content_current = g_rename_buffer_ready[g_back] &&
      g_rename_buffer_text_generation[g_back]==g_rename_text_generation &&
      g_rename_buffer_generation[g_back]==g_rename_generation;
  if(modal_content_current && g_rename_buffer_key[g_back]!=g_rename_key){
   /* Navigation-only change: patch exactly the old and new cells. */
   rename_patch_key_selection(g_rename_buffer_key[g_back],g_rename_key);
  }else if(!modal_content_current){
   /* Text/layout changes still use the safe complete keyboard rebuild. */
   draw_rename_keyboard();
   g_rename_buffer_ready[g_back]=1;
   g_rename_buffer_key[g_back]=g_rename_key;
   g_rename_buffer_text_generation[g_back]=g_rename_text_generation;
   g_rename_buffer_generation[g_back]=g_rename_generation;
  }
  /* v1.48 CASE indicator: this key encodes persistent state, so paint it
     from live state after every cached/full overlay operation.  This is one
     34px key cell and avoids either framebuffer ever exposing stale CASE
     pixels from the pristine/composite caches. */
  { int case_key=rename_char_key_count(); draw_rename_key_cell(case_key,g_rename_key==case_key); }
 }else if(g_rename_restore_frames>0){
  restore_region_to(g_fb[g_back],g_rename_base_cache,g_rename_cache_x,g_rename_cache_y,g_rename_cache_w,g_rename_cache_h);
  g_rename_buffer_generation[g_back]=0;
  g_rename_buffer_ready[g_back]=0;
  g_rename_restore_frames--;
 }
 present();g_frame_counter++;return 1;
}

static void cancel_rename();

static void begin_rename(){
 g_keyboard_purpose=KB_RENAME;
 if(item_count<=0||selected<0||selected>=item_count){snprintf(status_line,sizeof(status_line),"Nothing selected to rename");return;}
 snprintf(g_rename_original,sizeof(g_rename_original),"%s",items[selected].name);
 snprintf(g_rename_text,sizeof(g_rename_text),"%s",items[selected].name);
 g_rename_cursor=strlen(g_rename_text);g_rename_key=0;if(!capture_rename_base()){snprintf(status_line,sizeof(status_line),"Rename unavailable: display cache allocation failed");return;}g_rename_active=1;g_rename_restore_frames=0;g_rename_buffer_generation[0]=g_rename_buffer_generation[1]=0;g_rename_buffer_ready[0]=g_rename_buffer_ready[1]=0;g_rename_buffer_key[0]=g_rename_buffer_key[1]=-1;g_rename_buffer_text_generation[0]=g_rename_buffer_text_generation[1]=0;g_rename_text_generation++;rename_full_modal_changed();
 /* Precompose before the first visible flip so opening the keyboard cannot
    expose a partially rendered modal.  Failure falls back to the guarded
    modal renderer on the next draw call. */
 build_rename_composite();
 g_ui_dirty=0;g_list_dirty=0;
 snprintf(status_line,sizeof(status_line),"Rename: edit filename, START saves, O cancels");
}
static void blank_stage_set(int purpose,const char *initial,const char *status){
 g_keyboard_purpose=purpose;if(purpose==KB_NEW_PSU_ICON)g_rename_lowercase=1;snprintf(g_rename_text,sizeof(g_rename_text),"%s",initial?initial:"");g_rename_cursor=strlen(g_rename_text);g_rename_key=0;g_rename_text_generation++;rename_full_modal_changed();build_rename_composite();snprintf(status_line,sizeof(status_line),"%s",status);
}
static void choose_default_blank_psu_name(char *out,size_t outsz){
 char path[PATHSZ];struct stat st;snprintf(path,sizeof(path),"%s/blank.psu",cwd);
 if(stat(path,&st)!=0){snprintf(out,outsz,"blank.psu");return;}
 for(unsigned int n=0;n<10000;n++){snprintf(path,sizeof(path),"%s/blank%02u.psu",cwd,n);if(stat(path,&st)!=0){snprintf(out,outsz,"blank%02u.psu",n);return;}}
 snprintf(out,outsz,"blank.psu");
}
static void begin_blank_psu_title(){
 if(archive_mode){snprintf(status_line,sizeof(status_line),"Close PSU before creating a blank template");return;}
 g_keyboard_purpose=KB_NEW_PSU_FILENAME;g_blank_psu_base[0]=0;g_blank_save_folder[0]=0;g_blank_game_name[0]=0;g_blank_save_name[0]=0;g_blank_icon_text[0]=0;g_rename_original[0]=0;choose_default_blank_psu_name(g_rename_text,sizeof(g_rename_text));g_rename_cursor=strlen(g_rename_text);g_rename_key=0;
 if(!capture_rename_base()){snprintf(status_line,sizeof(status_line),"Blank PSU keyboard unavailable: display cache allocation failed");return;}
 g_rename_active=1;g_rename_restore_frames=0;g_rename_buffer_generation[0]=g_rename_buffer_generation[1]=0;g_rename_buffer_ready[0]=g_rename_buffer_ready[1]=0;g_rename_buffer_key[0]=g_rename_buffer_key[1]=-1;g_rename_buffer_text_generation[0]=g_rename_buffer_text_generation[1]=0;g_rename_text_generation++;rename_full_modal_changed();build_rename_composite();
 g_ui_dirty=0;g_list_dirty=0;snprintf(status_line,sizeof(status_line),"Blank PSU: enter PSU filename, START continues, O cancels");
}
static int blank_psu_title_valid(const char *typed,char *base,size_t basesz,char *why,size_t whysz){
 if(!typed||!typed[0]){snprintf(why,whysz,"Name cannot be empty");return 0;}char tmp[256];snprintf(tmp,sizeof(tmp),"%s",typed);size_t n=strlen(tmp);while(n&&tmp[n-1]==' ')tmp[--n]=0;size_t lead=0;while(tmp[lead]==' ')lead++;if(lead)memmove(tmp,tmp+lead,strlen(tmp+lead)+1);n=strlen(tmp);if(n>=4&&!strcasecmp(tmp+n-4,".psu")){tmp[n-4]=0;n-=4;while(n&&tmp[n-1]==' ')tmp[--n]=0;}if(!tmp[0]){snprintf(why,whysz,"Name cannot be empty");return 0;}if(strlen(tmp)>PSU_NAME_MAX){snprintf(why,whysz,"Name is limited to %u characters",(unsigned)PSU_NAME_MAX);return 0;}if(!rename_name_valid(tmp,why,whysz))return 0;snprintf(base,basesz,"%s",tmp);return 1;
}
static int blank_icon_base_valid(const char *typed,char *base,size_t basesz,char *why,size_t whysz){
 if(!typed||!typed[0]){snprintf(why,whysz,"Icon filename cannot be empty");return 0;}char tmp[64];snprintf(tmp,sizeof(tmp),"%s",typed);size_t n=strlen(tmp);while(n&&tmp[n-1]==' ')tmp[--n]=0;size_t lead=0;while(tmp[lead]==' ')lead++;if(lead)memmove(tmp,tmp+lead,strlen(tmp+lead)+1);n=strlen(tmp);if(n>=4&&!strcasecmp(tmp+n-4,".icn")){tmp[n-4]=0;n-=4;while(n&&tmp[n-1]==' ')tmp[--n]=0;}if(!tmp[0]){snprintf(why,whysz,"Icon filename cannot be empty");return 0;}if(strlen(tmp)>28){snprintf(why,whysz,"Icon base is limited to 28 characters");return 0;}if(!rename_name_valid(tmp,why,whysz))return 0;if(!strcasecmp(tmp,"COPY")||!strcasecmp(tmp,"DEL")){snprintf(why,whysz,"copy and del are reserved icon names");return 0;}for(size_t i=0;tmp[i];i++)if(tmp[i]>='A'&&tmp[i]<='Z')tmp[i]=(char)(tmp[i]-'A'+'a');snprintf(base,basesz,"%s",tmp);return 1;
}
static void commit_blank_psu_filename_step(){
 char base[PSU_NAME_MAX+1],why[96];if(!blank_psu_title_valid(g_rename_text,base,sizeof(base),why,sizeof(why))){snprintf(status_line,sizeof(status_line),"Blank PSU: %s",why);rename_mark_changed();return;}
 /* For the default blank.psu template name, never block or overwrite an existing save.
    Allocate blank00.psu, blank01.psu, ... using the first free filename. */
 if(!strcmp(base,"blank")){
  char out[PATHSZ];struct stat st;snprintf(out,sizeof(out),"%s/blank.psu",cwd);
  if(stat(out,&st)==0){
   int found=0;for(unsigned int n=0;n<10000;n++){char candidate[PSU_NAME_MAX+1];snprintf(candidate,sizeof(candidate),"blank%02u",n);snprintf(out,sizeof(out),"%s/%s.psu",cwd,candidate);if(stat(out,&st)!=0){snprintf(base,sizeof(base),"%s",candidate);found=1;break;}}
   if(!found){snprintf(status_line,sizeof(status_line),"Blank PSU: no free blankNN filename");rename_mark_changed();return;}
  }
 }else{
  char out[PATHSZ];struct stat st;snprintf(out,sizeof(out),"%s/%s.psu",cwd,base);if(stat(out,&st)==0){snprintf(status_line,sizeof(status_line),"Blank PSU blocked: %s.psu already exists",base);rename_mark_changed();return;}
 }
 snprintf(g_blank_psu_base,sizeof(g_blank_psu_base),"%s",base);blank_stage_set(KB_NEW_PSU_FOLDER,"EXAMPL-00000EFOLDER","Blank PSU: enter PS2 SAVE FOLDER, START continues");
}
static void blank_folder_cycle_preset(){
 static const char *presets[]={"EXAMPL-00000EFOLDER","SYS_","APP_"};
 int cur=-1;for(int i=0;i<4;i++)if(!strcmp(g_rename_text,presets[i])){cur=i;break;}
 cur=(cur+1)%3;snprintf(g_rename_text,sizeof(g_rename_text),"%s",presets[cur]);g_rename_cursor=strlen(g_rename_text);g_rename_text_generation++;rename_full_modal_changed();build_rename_composite();
 snprintf(status_line,sizeof(status_line),"PS2 SAVE FOLDER preset: %s  (SELECT cycles presets)",presets[cur]);
}
static void commit_blank_psu_folder_step(){
 char folder[PSU_NAME_MAX+1],why[96];if(!blank_psu_title_valid(g_rename_text,folder,sizeof(folder),why,sizeof(why))){snprintf(status_line,sizeof(status_line),"Blank PSU folder: %s",why);rename_mark_changed();return;}snprintf(g_blank_save_folder,sizeof(g_blank_save_folder),"%s",folder);blank_stage_set(KB_NEW_PSU_GAME_NAME,"Game Name","Blank PSU: enter GAME NAME, START continues");
}
static void commit_blank_psu_game_name_step(){
 if(!g_rename_text[0]){snprintf(status_line,sizeof(status_line),"Blank PSU: Game Name cannot be empty");rename_mark_changed();return;}snprintf(g_blank_game_name,sizeof(g_blank_game_name),"%.63s",g_rename_text);blank_stage_set(KB_NEW_PSU_SAVE_NAME,"Save Name","Blank PSU: enter SAVE NAME, START continues");
}
static void commit_blank_psu_save_name_step(){
 snprintf(g_blank_save_name,sizeof(g_blank_save_name),"%.63s",g_rename_text);blank_stage_set(KB_NEW_PSU_TEXT,"Text","Blank PSU: enter ICON TEXT shown on generated 3D icon, START continues");
}
static void commit_blank_psu_text_step(){
 if(!g_rename_text[0]){snprintf(status_line,sizeof(status_line),"Blank PSU: Icon Text cannot be empty");rename_mark_changed();return;}snprintf(g_blank_icon_text,sizeof(g_blank_icon_text),"%.63s",g_rename_text);blank_stage_set(KB_NEW_PSU_ICON,"icon.icn","Blank PSU: normal/view icon filename defaults to icon.icn; START creates");
}
static void commit_blank_psu(){
 char icon_base[29],why[96];if(!blank_icon_base_valid(g_rename_text,icon_base,sizeof(icon_base),why,sizeof(why))){snprintf(status_line,sizeof(status_line),"Blank PSU: %s",why);rename_mark_changed();return;}if(!g_blank_psu_base[0]){snprintf(status_line,sizeof(status_line),"Blank PSU: creation state was lost; restart creation");cancel_rename();return;}char out[PATHSZ];snprintf(out,sizeof(out),"%s/%s.psu",cwd,g_blank_psu_base);struct stat st;if(stat(out,&st)==0){
  if(!strncmp(g_blank_psu_base,"blank",5)){int found=0;for(unsigned int n=0;n<10000;n++){char candidate[PSU_NAME_MAX+1];snprintf(candidate,sizeof(candidate),"blank%02u",n);snprintf(out,sizeof(out),"%s/%s.psu",cwd,candidate);if(stat(out,&st)!=0){snprintf(g_blank_psu_base,sizeof(g_blank_psu_base),"%s",candidate);found=1;break;}}if(!found){snprintf(status_line,sizeof(status_line),"Blank PSU: no free blankNN filename");rename_mark_changed();return;}}
  else{snprintf(status_line,sizeof(status_line),"Blank PSU blocked: %s.psu already exists",g_blank_psu_base);rename_mark_changed();return;}
 }int rc=psu_create_blank_template_ex2(out,g_blank_save_folder,g_blank_game_name,g_blank_save_name,g_blank_icon_text,icon_base);if(rc){snprintf(status_line,sizeof(status_line),"Blank PSU failed: %s",psu_strerror(rc));rename_mark_changed();return;}char created[300],viewicon[40];snprintf(created,sizeof(created),"%s.psu",g_blank_psu_base);snprintf(viewicon,sizeof(viewicon),"%s.icn",icon_base);g_rename_active=0;g_rename_restore_frames=2;g_rename_text[0]=0;g_rename_original[0]=0;g_rename_key=0;g_keyboard_purpose=KB_RENAME;g_blank_psu_base[0]=g_blank_save_folder[0]=g_blank_game_name[0]=g_blank_save_name[0]=g_blank_icon_text[0]=0;invalidate_dir_cache(cwd);selected=0;top_index=0;refresh_fs();for(int i=0;i<item_count;i++)if(!strcmp(items[i].name,created)){selected=i;break;}snprintf(status_line,sizeof(status_line),"Created %s with %s + copy.icn + del.icn",created,viewicon);g_ui_dirty=0;g_list_dirty=1;g_folder_dirty=0;
}
static void cancel_rename(){
 int was_blank=(g_keyboard_purpose>=KB_NEW_PSU_FILENAME&&g_keyboard_purpose<=KB_NEW_PSU_ICON);g_rename_active=0;g_rename_restore_frames=2;g_rename_text[0]=0;g_rename_original[0]=0;g_rename_key=0;g_keyboard_purpose=KB_RENAME;g_blank_psu_base[0]=g_blank_save_folder[0]=g_blank_game_name[0]=g_blank_save_name[0]=g_blank_icon_text[0]=0;g_ui_dirty=0;g_list_dirty=0;snprintf(status_line,sizeof(status_line),was_blank?"Blank PSU creation cancelled":"Rename cancelled");
}
static void rename_append_char(char c){
 size_t n=strlen(g_rename_text);size_t lim=(g_keyboard_purpose==KB_DOWNLOAD_URL)?1023u:255u;if(n>=lim){snprintf(status_line,sizeof(status_line),g_keyboard_purpose==KB_DOWNLOAD_URL?"URL limit is 1023 characters":"Filename limit is 255 characters");return;}
 if(g_rename_cursor>n)g_rename_cursor=n;
 memmove(g_rename_text+g_rename_cursor+1,g_rename_text+g_rename_cursor,n-g_rename_cursor+1);
 g_rename_text[g_rename_cursor++]=c;rename_text_changed();
}
static void rename_backspace(){size_t n=strlen(g_rename_text);if(g_rename_cursor>n)g_rename_cursor=n;if(g_rename_cursor){memmove(g_rename_text+g_rename_cursor-1,g_rename_text+g_rename_cursor,n-g_rename_cursor+1);g_rename_cursor--;rename_text_changed();}}
static void rename_cursor_left(){if(g_rename_cursor){g_rename_cursor--;rename_text_changed();}}
static void rename_cursor_right(){size_t n=strlen(g_rename_text);if(g_rename_cursor<n){g_rename_cursor++;rename_text_changed();}}
static void rename_cursor_home(){if(g_rename_cursor){g_rename_cursor=0;rename_text_changed();}}
static void rename_cursor_end(){size_t n=strlen(g_rename_text);if(g_rename_cursor!=n){g_rename_cursor=n;rename_text_changed();}}
static void commit_rename(){
 if(g_foreign_archive_view){snprintf(status_line,sizeof(status_line),"Read-only archive view: rename disabled");g_rename_active=0;return;}
 if(g_keyboard_purpose==KB_NEW_PSU_FILENAME){commit_blank_psu_filename_step();return;}
 if(g_keyboard_purpose==KB_NEW_PSU_FOLDER){commit_blank_psu_folder_step();return;}
 if(g_keyboard_purpose==KB_NEW_PSU_GAME_NAME){commit_blank_psu_game_name_step();return;}
 if(g_keyboard_purpose==KB_NEW_PSU_SAVE_NAME){commit_blank_psu_save_name_step();return;}
 if(g_keyboard_purpose==KB_NEW_PSU_TEXT){commit_blank_psu_text_step();return;}
 if(g_keyboard_purpose==KB_NEW_PSU_ICON){commit_blank_psu();return;}
 char why[96];
 if(!rename_name_valid(g_rename_text,why,sizeof(why))){snprintf(status_line,sizeof(status_line),"Rename: %s",why);rename_mark_changed();return;}
 if(archive_mode&&strlen(g_rename_text)>PSU_NAME_MAX){snprintf(status_line,sizeof(status_line),"PSU filenames are limited to %u characters",(unsigned)PSU_NAME_MAX);rename_mark_changed();return;}
 if(!strcmp(g_rename_text,g_rename_original)){g_rename_active=0;g_rename_restore_frames=2;snprintf(status_line,sizeof(status_line),"Filename unchanged");g_ui_dirty=0;return;}
 if(archive_mode){
  char renamed[PSU_NAME_MAX+1];snprintf(renamed,sizeof(renamed),"%s",g_rename_text);
  int rc=psu_rename_entry(archive_path,g_rename_original,renamed);
  if(rc){snprintf(status_line,sizeof(status_line),"PSU rename failed: %s",psu_strerror(rc));rename_mark_changed();return;}
  g_rename_active=0;g_rename_restore_frames=2;g_rename_text[0]=0;g_rename_original[0]=0;g_rename_key=0;g_keyboard_purpose=KB_RENAME;
  free_preview();clear_icn_cache();refresh_psu();for(int i=0;i<item_count;i++)if(!strcmp(items[i].name,renamed)){selected=i;break;}ensure_preview_current();
  snprintf(status_line,sizeof(status_line),"Renamed PSU entry to %s",renamed);g_ui_dirty=1;g_list_dirty=1;return;
 }
 char oldp[PATHSZ],newp[PATHSZ];join(oldp,sizeof(oldp),cwd,g_rename_original);join(newp,sizeof(newp),cwd,g_rename_text);
 struct stat st;if(stat(newp,&st)==0){snprintf(status_line,sizeof(status_line),"Rename blocked: %s already exists",g_rename_text);rename_mark_changed();return;}
 if(stat(oldp,&st)!=0){snprintf(status_line,sizeof(status_line),"Rename failed: source no longer exists");rename_mark_changed();return;}
 if(rename(oldp,newp)!=0){snprintf(status_line,sizeof(status_line),"Rename failed: filesystem rejected change");rename_mark_changed();return;}
 if(clipboard[0]){
  size_t oldn=strlen(oldp);
  if(!strncmp(clipboard,oldp,oldn)&&(clipboard[oldn]==0||clipboard[oldn]=='/')){
   char tail[PATHSZ];snprintf(tail,sizeof(tail),"%s",clipboard+oldn);
   snprintf(clipboard,sizeof(clipboard),"%s%s",newp,tail);
  }
 }
 char renamed[256];snprintf(renamed,sizeof(renamed),"%s",g_rename_text);
 g_rename_active=0;g_rename_restore_frames=2;g_rename_text[0]=0;g_rename_original[0]=0;g_rename_key=0;g_keyboard_purpose=KB_RENAME;
 invalidate_dir_cache(cwd);selected=0;top_index=0;refresh_fs();
 for(int i=0;i<item_count;i++)if(!strcmp(items[i].name,renamed)){selected=i;break;}
 snprintf(status_line,sizeof(status_line),"Renamed to %s",renamed);g_ui_dirty=0;g_list_dirty=1;g_folder_dirty=0;
}
static int keyboard_footer_line_count(int width,int scale);
static void rename_keyboard_geometry(int *ox,int *oy,int *ow,int *opad,int *okeyw,int *ofieldy,int *oky){
 int pad=18,w=(int)g_width*3/4;if(w>880)w=880;if(w<520)w=(int)g_width-40;
 int keyw=(w-pad*2)/RENAME_COLS;int keyh=34;int keycount=rename_key_count(),rows=(keycount+RENAME_COLS-1)/RENAME_COLS;
 /* Phase 3H: the RSX font has wider measured glyphs than the old 5x7 CPU
    renderer. Reserve the real number of wrapped legend rows instead of the
    historical fixed two-line footer, otherwise row 2 overwrites a wrapped
    row 1 on real hardware. */
 int footer_w=w-pad*2;
 /* Phase 4E: reserve exactly the rows the measured controller legends need.
    This keeps every command visible (including R1) and lets the modal end
    immediately below the final wrapped command row. */
 int footer_lines=keyboard_footer_line_count(footer_w,g_help_scale);
 int h=154+rows*keyh+(footer_lines+1)*legend_line_height(g_help_scale)+6;
 int x=((int)g_width-w)/2,y=((int)g_height-h)/2;
 int field_y=y+48,ky=field_y+54;
 if(ox)*ox=x;if(oy)*oy=y;if(ow)*ow=w;if(opad)*opad=pad;if(okeyw)*okeyw=keyw;if(ofieldy)*ofieldy=field_y;if(oky)*oky=ky;
}
static void draw_rename_text_field(){
 int x,y,w,pad,keyw,field_y,ky;rename_keyboard_geometry(&x,&y,&w,&pad,&keyw,&field_y,&ky);
 round_rect(x+pad,field_y,w-pad*2,38,6,0xFF101820u);
 size_t sl=strlen(g_rename_text);if(g_rename_cursor>sl)g_rename_cursor=sl;
 int charw=scale125(6*g_body_scale);if(charw<1)charw=1;
 int maxchars=(w-pad*2-30)/charw;if(maxchars<8)maxchars=8;
 size_t first=0;
 if(sl>(size_t)maxchars){
  /* Keep the insertion caret visible while editing long names. */
  if(g_rename_cursor>(size_t)(maxchars-1))first=g_rename_cursor-(size_t)(maxchars-1);
  if(first+(size_t)maxchars>sl)first=sl-(size_t)maxchars;
 }
 size_t count=sl-first;if(count>(size_t)maxchars)count=(size_t)maxchars;
 char shown[256];memcpy(shown,g_rename_text+first,count);shown[count]=0;
 text(x+pad+16,field_y+10,shown[0]?shown:" ",0xFFF5F8FBu,g_body_scale);
 /* Bright vertical insertion caret. Position 0 is before the first character;
    position strlen(text) is immediately after the final character. */
 size_t vispos=(g_rename_cursor>=first)?g_rename_cursor-first:0;if(vispos>(size_t)maxchars)vispos=(size_t)maxchars;
 int cx=x+pad+16+(int)vispos*charw;
 rect(cx,field_y+7,2,24,0xFFFFE36Eu);
}

static void draw_rename_key_cell(int i,int selected_state){
 int keycount=rename_key_count();if(i<0||i>=keycount)return;
 int x,y,w,pad,keyw,field_y,ky;rename_keyboard_geometry(&x,&y,&w,&pad,&keyw,&field_y,&ky);
 int keyh=34,col=i%RENAME_COLS,row=i/RENAME_COLS,kx=x+pad+col*keyw,yy=ky+row*keyh;
 u32 bg;if(i==rename_char_key_count()) bg=g_rename_lowercase?(selected_state?0xFFFFC15Au:0xFFFF9A00u):(selected_state?0xFF62D98Bu:0xFF238B45u); else bg=selected_state?0xFF2E8AD0u:0xFF2B3C4Eu;round_rect(kx+2,yy+2,keyw-4,keyh-4,5,bg);
 if(selected_state){rect(kx+5,yy+4,keyw-10,1,0xB0C9F1FFu);rect(kx+5,yy+keyh-5,keyw-10,1,0x9052B8F2u);}
 if(i==rename_char_key_count()){
  const int ks=g_help_scale; const char *lab="CASE";int tw=text_width(lab,ks);text(kx+(keyw-tw)/2,yy+(keyh-scale125(8*ks))/2,lab,0xFFFFFFFFu,ks);
 }else{
  const int ks=g_help_scale; char c[2]={rename_key_char(i),0};int tw=text_width(c,ks);text(kx+(keyw-tw)/2,yy+(keyh-scale125(8*ks))/2,c,0xFFFFFFFFu,ks);
 }
}
static int keyboard_legend_text_scale(int scale){ return scale; }
static int keyboard_legend_item_width(const legend_entry_t *e,int scale){
 if(!e)return 0;
 int ts=keyboard_legend_text_scale(scale);
 return token_width_for(e->token,scale)+6+legend_text_width(e->action,ts);
}
static int draw_keyboard_legend_item(const char *token,const char *action,int x,int y,int scale){
 int tw=token_width_for(token,scale),ts=keyboard_legend_text_scale(scale);
 if(!strcmp(token,"LEFT/RIGHT"))draw_lr_token(x,y,scale);
 else if(!strcmp(token,"X")||!strcmp(token,"O")||!strcmp(token,"SQUARE")||!strcmp(token,"TRIANGLE"))draw_face_button_token(token,x,y-2,scale);
 else draw_pill_token(token,x,y,scale);
 text(x+tw+6,y+3,action,0xFFF0E8D8u,ts);
 return tw+6+legend_text_width(action,ts);
}
static int measure_keyboard_legend_row_lines(const legend_entry_t *entries,int count,int width,int scale){
 if(count<=0)return 0;
 int gap=scale125(5*scale),cx=0,lines=1;
 for(int i=0;i<count;i++){
  int iw=keyboard_legend_item_width(&entries[i],scale);
  int need=(cx>0?gap:0)+iw;
  if(cx>0 && cx+need>width){lines++;cx=iw;}
  else cx+=need;
 }
 return lines;
}
static int draw_keyboard_legend_row(const legend_entry_t *entries,int count,int x,int y,int width,int scale){
 /* Phase 4E: measured wrapping. Never discard a command when a row fills. */
 if(count<=0)return 0;
 int gap=scale125(5*scale),cx=x,cy=y,lines=1,lineh=legend_line_height(scale),right=x+width;
 for(int i=0;i<count;i++){
  int iw=keyboard_legend_item_width(&entries[i],scale);
  if(cx>x && cx+gap+iw>right){cy+=lineh;cx=x;lines++;}
  else if(cx>x)cx+=gap;
  cx += draw_keyboard_legend_item(entries[i].token,entries[i].action,cx,cy,scale);
 }
 return lines;
}
static int keyboard_footer_line_count(int width,int scale){
 /* Phase 4H: X/Square/Triangle live beside CASE.  The compact footer then
    packs L1/R1/L2/R2/START onto one measured row where the panel width allows,
    with Circle Cancel on the final row. */
 const char *go=(g_keyboard_purpose>=KB_NEW_PSU_FILENAME&&g_keyboard_purpose<KB_NEW_PSU_ICON)?"NEXT":(g_keyboard_purpose==KB_NEW_PSU_ICON)?"CREATE":(g_keyboard_purpose==KB_DOWNLOAD_URL)?"DOWNLOAD":"SAVE";
 if(g_keyboard_purpose==KB_NEW_PSU_FOLDER){
  const legend_entry_t mainrow[]={{"L1","CARET LEFT"},{"R1","CARET RIGHT"},{"L2","HOME"},{"R2","END"},{"SELECT","PRESET"},{"START","NEXT"}};
  return measure_keyboard_legend_row_lines(mainrow,6,width,scale);
 }
 const legend_entry_t mainrow[]={{"L1","CARET LEFT"},{"R1","CARET RIGHT"},{"L2","HOME"},{"R2","END"},{"START",go}};
 return measure_keyboard_legend_row_lines(mainrow,5,width,scale);
}
static void draw_rename_keyboard(){
 if(!g_rename_active)return;
 int x,y,w,pad,keyw,field_y,ky;rename_keyboard_geometry(&x,&y,&w,&pad,&keyw,&field_y,&ky);
 int keyh=34;int keycount=rename_key_count();int rows=(keycount+RENAME_COLS-1)/RENAME_COLS;
 int legend_scale=g_help_scale;
 int footer_w=w-pad*2;
 int footer_lines=keyboard_footer_line_count(footer_w,legend_scale);
 int h=154+rows*keyh+(footer_lines+1)*legend_line_height(legend_scale)+6;
 round_rect(x+6,y+8,w,h,12,0xFF101820u);
 round_rect(x,y,w,h,12,0xFF14202Du);
 round_rect(x+2,y+2,w-4,h-4,10,0xFF233343u);
 gradient_rect_v(x+5,y+5,w-10,42,0xFF476178u,0xFF344758u);
 rect(x+8,y+47,w-16,1,0x805D7892u);
 const char *kbtitle=(g_keyboard_purpose==KB_NEW_PSU_FILENAME)?"CREATE BLANK PSU - PSU FILENAME":(g_keyboard_purpose==KB_NEW_PSU_FOLDER)?"CREATE BLANK PSU - PS2 SAVE FOLDER":(g_keyboard_purpose==KB_NEW_PSU_GAME_NAME)?"CREATE BLANK PSU - GAME NAME":(g_keyboard_purpose==KB_NEW_PSU_SAVE_NAME)?"CREATE BLANK PSU - SAVE NAME":(g_keyboard_purpose==KB_NEW_PSU_TEXT)?"CREATE BLANK PSU - ICON TEXT":(g_keyboard_purpose==KB_NEW_PSU_ICON)?"CREATE BLANK PSU - NORMAL ICON FILENAME":(g_keyboard_purpose==KB_DOWNLOAD_URL)?"FILE DOWNLOADER - URL":(archive_mode?"RENAME PSU ENTRY":"RENAME FILE / FOLDER");
 text(x+pad,y+14,kbtitle,0xFFFFFFFFu,g_title_scale>2?2:1);
 const char *casestate=g_rename_lowercase?"LOWERCASE":"UPPERCASE";int case_scale=g_help_scale;int csw=text_width(casestate,case_scale);text(x+w-pad-csw,y+14,casestate,0xFFB8DFFFu,case_scale);
 draw_rename_text_field();
 for(int i=0;i<keycount;i++)draw_rename_key_cell(i,i==g_rename_key);
 /* Phase 4H: use all six cells after CASE for the three face-button editing
    commands.  Drop the row slightly so the token shadows clear the key cells. */
 {
  const legend_entry_t inline_cmds[]={{"X","TYPE"},{"SQUARE","BACKSPACE"},{"TRIANGLE","SPACE"},{"O","CANCEL"}};
  int case_i=rename_char_key_count();
  int case_col=case_i%RENAME_COLS, case_row=case_i/RENAME_COLS;
  int ix=x+pad+(case_col+1)*keyw+4;
  int iy=ky+case_row*keyh+7;
  int iw=w-pad-ix+x; if(iw<0)iw=0;
  draw_keyboard_legend_row(inline_cmds,4,ix,iy,iw,legend_scale);
 }
 /* Compact measured footer.  Five navigation/action controls share the next
    row when possible; Cancel gets the short final row so the panel can end
    immediately below it without a large unused lower area. */
 int helpy=ky+rows*keyh+legend_line_height(legend_scale)+3;
 const char *go=(g_keyboard_purpose>=KB_NEW_PSU_FILENAME&&g_keyboard_purpose<KB_NEW_PSU_ICON)?"NEXT":(g_keyboard_purpose==KB_NEW_PSU_ICON)?"CREATE":(g_keyboard_purpose==KB_DOWNLOAD_URL)?"DOWNLOAD":"SAVE";
 int used_lines=0;
 if(g_keyboard_purpose==KB_NEW_PSU_FOLDER){
  const legend_entry_t mainrow[]={{"L1","CARET LEFT"},{"R1","CARET RIGHT"},{"L2","HOME"},{"R2","END"},{"SELECT","PRESET"},{"START","NEXT"}};
  used_lines=draw_keyboard_legend_row(mainrow,6,x+pad,helpy,footer_w,legend_scale);
 }else{
  const legend_entry_t mainrow[]={{"L1","CARET LEFT"},{"R1","CARET RIGHT"},{"L2","HOME"},{"R2","END"},{"START",go}};
  used_lines=draw_keyboard_legend_row(mainrow,5,x+pad,helpy,footer_w,legend_scale);
 }
 (void)used_lines;
}
static void fmt_speed(char *out,size_t cap,double bps){if(bps>=1048576.0)snprintf(out,cap,"%.2f MB/s",bps/1048576.0);else if(bps>0.0)snprintf(out,cap,"%.1f KB/s",bps/1024.0);else snprintf(out,cap,"--");}
static void update_download_speed_stats(double bytes,unsigned long long now){
 if(!g_download_sample_us){g_download_sample_us=now;g_download_sample_bytes=bytes;return;}
 double dt=(double)(now-g_download_sample_us)/1000000.0;if(dt<0.20)return;
 double d=bytes-g_download_sample_bytes;if(d<0)d=0;double raw=d/dt;
 /* Phase 3J: aggregate callbacks arrive in bursts.  Smooth the displayed
    instantaneous rate so a callback catch-up cannot become a bogus
    300+ MB/s "highest" reading. */
 if(g_download_speed_bps<=0)g_download_speed_bps=raw;
 else g_download_speed_bps=g_download_speed_bps*0.72+raw*0.28;
 double cur=g_download_speed_bps;
 if(cur>0 && g_download_elapsed>=1.0){if(g_download_high_speed_bps<=0||cur>g_download_high_speed_bps)g_download_high_speed_bps=cur;if(g_download_low_speed_bps<=0||cur<g_download_low_speed_bps)g_download_low_speed_bps=cur;}
 g_download_sample_us=now;g_download_sample_bytes=bytes;
}
static void reset_download_speed_stats(double base){g_download_speed_bps=0;g_download_avg_speed_bps=0;g_download_high_speed_bps=0;g_download_low_speed_bps=0;g_download_session_base=base;g_download_sample_bytes=base;g_download_sample_us=fps_now_us();g_download_completed=0;}
typedef struct {
 unsigned long long stamp_us;
 int paused,completed,finalizing,connections;
 double total,now,elapsed,remaining,speed,avg,hi,lo;
 char sz[64],dn[64],el[32],rm[32],pc[32],cn[32],cur[32],avg_s[32],hi_s[32],lo_s[32];
 char diag[16][128];
 u32 diag_color[16];
} downloader_text_cache_t;
static downloader_text_cache_t g_dl_text_cache={0};
static void refresh_downloader_text_cache(void){
 downloader_text_cache_t *c=&g_dl_text_cache; unsigned long long now=fps_now_us();
 int state_changed=(c->paused!=g_download_paused||c->completed!=g_download_completed||c->finalizing!=g_download_finalizing||c->connections!=g_download_active_connections);
 /* Phase 3R: expensive floating-point snprintf/fmt_speed work is presentation data,
    not frame data.  Refresh at 10 Hz while transferring and only on state change
    while paused/completed.  The progress bar itself still uses live counters. */
 if(c->stamp_us && !state_changed){
  if(g_download_paused||g_download_completed||g_download_finalizing)return;
  if(now-c->stamp_us<100000ull)return;
 }
 c->stamp_us=now;c->paused=g_download_paused;c->completed=g_download_completed;c->finalizing=g_download_finalizing;c->connections=g_download_active_connections;
 c->total=g_download_total;c->now=g_download_now;c->elapsed=g_download_elapsed;c->remaining=g_download_remaining;c->speed=g_download_speed_bps;c->avg=g_download_avg_speed_bps;c->hi=g_download_high_speed_bps;c->lo=g_download_low_speed_bps;
 double pct=(c->total>0)?c->now*100.0/c->total:0;if(pct<0)pct=0;if(pct>100)pct=100;
 if(c->total>0)snprintf(c->sz,sizeof(c->sz),"%.0f bytes (%.1f MB)",c->total,c->total/1048576.0);else snprintf(c->sz,sizeof(c->sz),"Unknown");
 snprintf(c->dn,sizeof(c->dn),"%.0f bytes (%.1f MB)",c->now,c->now/1048576.0);snprintf(c->pc,sizeof(c->pc),"%.1f %%",pct);
 fmt_speed(c->cur,sizeof(c->cur),c->speed);fmt_speed(c->avg_s,sizeof(c->avg_s),c->avg);fmt_speed(c->hi_s,sizeof(c->hi_s),c->hi);fmt_speed(c->lo_s,sizeof(c->lo_s),c->lo);
 int es=(int)c->elapsed,rs=(int)c->remaining;snprintf(c->el,sizeof(c->el),"%02d:%02d:%02d",es/3600,(es/60)%60,es%60);
 if(c->total>0&&c->avg>1&&!c->completed)snprintf(c->rm,sizeof(c->rm),"%02d:%02d:%02d",rs/3600,(rs/60)%60,rs%60);else snprintf(c->rm,sizeof(c->rm),c->completed?"00:00:00":"--:--:--");
 snprintf(c->cn,sizeof(c->cn),"%d",c->connections);
 int dc=c->connections;if(dc>16)dc=16;for(int i=0;i<dc;i++){char sp[32];fmt_speed(sp,sizeof(sp),g_conn_speed_bps[i]);double cp=(g_conn_total[i]>0)?(100.0*g_conn_now[i]/g_conn_total[i]):0;if(cp>100)cp=100;snprintf(c->diag[i],sizeof(c->diag[i]),"C%d  %4.1f%%  %s  %s",i+1,cp,sp,g_conn_state[i]==2?"DONE":"ACTIVE");c->diag_color[i]=g_conn_state[i]==2?0xFF8CFF9Au:0xFFFFFFFFu;}
}
static void draw_downloader_progress(){
 int bs=g_body_scale;
 refresh_downloader_text_cache();downloader_text_cache_t *tc=&g_dl_text_cache;
 const char *labs[]={"File Name","URL","File Size","Downloaded","Progress","Connections","Current Speed","Average Speed","Highest Speed","Lowest Speed","Time Elapsed","Time Remaining"};
 const char *vals[]={g_download_name,g_download_url,tc->sz,tc->dn,tc->pc,tc->cn,tc->cur,tc->avg_s,tc->hi_s,tc->lo_s,tc->el,tc->rm};
 int title_scale=g_title_scale;
 int labelw=(g_width>=1200)?245:165;
 int titlew=text_width("FILE DOWNLOADER - PROGRESS",title_scale);
 int value_budget=text_width("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",bs);
 int max_valuew=0;
 for(int i=0;i<12;i++)if(i!=1){int vw=text_width(vals[i],bs);if(vw>value_budget)vw=value_budget;if(vw>max_valuew)max_valuew=vw;}
 int dc=g_download_active_connections;if(dc>16)dc=16;
 int max_diagw=0;for(int i=0;i<dc;i++){int dw=text_width(tc->diag[i],bs);if(dw>max_diagw)max_diagw=dw;}
 int desiredw=ui_downloader_desired_width((bs>=2)?760:600,titlew,labelw,max_valuew,max_diagw);
 int w=ui_compact_panel_width((int)g_width,desiredw,1200,24);
 int x=((int)g_width-w)/2,y=12,h=(int)g_height-24;
 int valuew=w-68-labelw-28;if(valuew<scale125(6*bs))valuew=scale125(6*bs);
 int stat_top=(g_height>=700?70:52),lh=ui_text_line_pitch(bs),stat_h=0;
 for(int i=0;i<12;i++)stat_h+=wrapped_text_advance(vals[i],valuew,bs);
 if(dc>16)dc=16;
 int diag_rows=ui_downloader_diagnostic_rows(dc);
 int diag_h=(dc>1&&!g_download_completed)?lh+diag_rows*lh+(dc>8?4:0)+8:0;
 int bh=(bs>=2)?30:20;
 int footer_legend_lines=1;
 if(!g_download_completed&&!g_download_finalizing){
  const legend_entry_t measure_lg[]={{"START",g_download_paused?"RESUME DOWNLOAD":"PAUSE DOWNLOAD"},{"O","CANCEL - KEEP PARTIAL"}};
  footer_legend_lines=measure_control_legend_lines(measure_lg,2,w-98,bs);
 }
 int measured_h=ui_downloader_panel_height(stat_top,stat_h,g_download_completed?0:diag_h,bh,lh,footer_legend_lines,legend_line_height(bs));
 /* Completed downloads do not keep the old full-height progress modal.
    Size it to the actual content: statistics + progress/status + exactly
    two footer text rows (X Exit Downloader, File saved successfully). */
 h=measured_h;
 if(h>(int)g_height-24)h=(int)g_height-24;
 /* Phase 4F: compact framed modal.  A black shadow and thin navy outside
    keyline separate the downloader from the wallpaper without wasting width. */
 round_rect(x+4,y+5,w,h,8,0x90000000u);
 round_rect(x,y,w,h,8,0xFF0B2034u);
 round_rect(x+2,y+2,w-4,h-4,7,0xFF314961u);
 rect(x+3,y+3,w-6,(bs>=2)?54:40,0xFF7EA7C8u);
 text(x+30,y+12,"FILE DOWNLOADER - PROGRESS",0xFFFFFFFFu,(g_width>=1200)?3:2);
 double pct=(g_download_total>0)?g_download_now*100.0/g_download_total:0;if(pct<0)pct=0;if(pct>100)pct=100;
 int left=x+34,top=y+stat_top;
 /* Symmetric cyan accents belong to the information body, not the title. */
 int accent_h=scale125(12*bs)+8; if(accent_h>lh+8)accent_h=lh+8;
 rect(x+10,top-3,6,accent_h,0xFF18DCE8u);
 rect(x+w-16,top-3,6,accent_h,0xFF18DCE8u);
 int row_y=top;
 for(int i=0;i<12;i++){
  int row_h=wrapped_text_advance(vals[i],valuew,bs);
  text(left,row_y,labs[i],0xFFBFD0DEu,bs);text(left+labelw,row_y,":",0xFFBFD0DEu,bs);
  text_bounded(left+labelw+28,row_y,valuew,row_h,vals[i],0xFFFFFFFFu,bs);
  row_y+=row_h;
 }
 int bx=left,bw=w-68,by=top+stat_h+10;
 if(g_download_active_connections>1 && !g_download_completed){
  text(bx,by,"CONNECTION DIAGNOSTICS",0xFF9ED9FFu,bs);
  int dy=by+lh;
  /* Preserve the original C1-C8 presentation exactly: two columns of
     four rows at the normal body text scale.  C9-C16 form a second
     two-column/four-row bank directly underneath. */
  int bank_y=dy;
  for(int bank=0;bank<2;bank++){
   int first=bank*8,remain=dc-first;if(remain<=0)break;
   int count=remain>8?8:remain;
   int rows=count<4?count:4;
   int cols=(count+3)/4;
  int col_gap=scale125(12*bs);
  int colw=(bw-col_gap)/2;
   for(int j=0;j<count;j++){
    int col=j/4,row=j%4,i=first+j;
    int col_x=bx+col*(colw+col_gap);
    text_bounded(col_x,bank_y+row*lh,colw,lh,tc->diag[i],tc->diag_color[i],bs);
   }
   bank_y+=rows*lh;
   if(bank==0&&dc>8)bank_y+=4;
  }
  by=bank_y+8;
 }
 /* Progress percentage is intentionally shown only in the labelled Progress row above. */
 rect(bx,by,bw,bh,0xFF0C1925u);rect(bx+2,by+2,bw-4,bh-4,0xFF1B3145u);int fw=(int)((bw-4)*pct/100.0);if(fw>0)rect(bx+2,by+2,fw,bh-4,0xFF22DCE8u);
 int sy=by+bh+8;
 if(g_download_completed)text(bx,sy,"Download complete - 100%",0xFF8CFF9Au,bs);else if(g_download_finalizing)text(bx,sy,"Finalizing download...",0xFF9ED9FFu,bs);else if(g_download_paused)text(bx,sy,"Paused",0xFFFFD45Au,bs);else if(g_download_resume_base>0)textf(bx,sy,0xFF9ED9FFu,bs,"Downloading - resumed from %.1f MB",g_download_resume_base/1048576.0);else text(bx,sy,"Downloading...",0xFF9ED9FFu,bs);
 int iy=sy+legend_line_height(bs)+4;
 /* Footer boxes are content-sized.  Never stretch them to the bottom of
    the screen: controls occupy one line and their explanatory message one
    line, plus small top/bottom padding. */
 int footer_h=footer_legend_lines*legend_line_height(bs)+legend_line_height(bs)+14;
 /* Phase 3M: the footer is a child of the progress modal, never a second
    free-standing panel.  The 19px statistics/diagnostic cadence above keeps
    the complete 16-connection view inside a 720p frame.  Clamp as a final
    guard for lower output modes so the child can never cross the owner. */
 int modal_bottom=y+h-10;
 /* Phase 3N: never shrink the footer below its measured content.  3M's clamp
    kept the child inside the modal but clipped the explanatory second line.
    If a low output mode is tight, move the footer upward by the exact deficit;
    the progress/status block has spare separation and the command bar remains
    fully contained. */
 if(iy+footer_h>modal_bottom){
  int deficit=iy+footer_h-modal_bottom;
  iy-=deficit;
  if(iy<sy+legend_line_height(bs))iy=sy+legend_line_height(bs);
 }
 if(iy+footer_h>modal_bottom)footer_h=modal_bottom-iy;
 if(footer_h<1)footer_h=1;
 round_rect(bx,iy,bw,footer_h,6,0xFF0D1A27u);rect(bx+2,iy+2,bw-4,2,0xFF4B718Du);
 if(g_download_completed){const legend_entry_t lg[]={{"X","EXIT DOWNLOADER"}};draw_control_legend(lg,1,bx+16,iy+6,bw-30,bs);text(bx+18,iy+6+legend_line_height(bs),"File saved successfully.",0xFFBFD0DEu,bs);}
 else if(g_download_finalizing){text(bx+18,iy+10,"Verifying and assembling downloaded ranges...",0xFFBFD0DEu,bs);text(bx+18,iy+10+legend_line_height(bs),"Please wait.",0xFFBFD0DEu,bs);}
 else{const legend_entry_t lg[]={{"START",g_download_paused?"RESUME DOWNLOAD":"PAUSE DOWNLOAD"},{"O","CANCEL - KEEP PARTIAL"}};int ll=measure_control_legend_lines(lg,2,bw-30,bs);draw_control_legend(lg,2,bx+16,iy+10,bw-30,bs);text(bx+18,iy+10+ll*legend_line_height(bs),"Partial file can be resumed later.",0xFFBFD0DEu,bs);}
}

static void downloader_wait_for_completion(){g_download_finalizing=0;g_download_completed=1;g_download_progress_active=1;g_download_now=g_download_total;g_download_remaining=0;g_ui_dirty=1;draw();int released=0;for(;;){sysUtilCheckCallback();padInfo pi;padData pd;ioPadGetInfo(&pi);int hit=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);if(pd.BTN_CROSS||pd.BTN_CIRCLE)hit=1;break;}if(!hit)released=1;else if(released){int cross=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);if(pd.BTN_CROSS)cross=1;break;}if(cross)break;}usleep(30000);}g_download_completed=0;g_download_finalizing=0;g_download_progress_active=0;invalidate_dir_cache(g_apollo_savedata_path);if(!archive_mode&&!strcmp(cwd,g_apollo_savedata_path)){refresh_fs();g_folder_dirty=1;g_list_dirty=0;}g_ui_dirty=1;}

/* Phase 3O: progress rendering is no longer intentionally capped at ~30 Hz
   (and paused rendering at ~20 Hz).  Those throttles made the on-screen FPS
   look like a renderer failure.  Target a 60 Hz presentation cadence; libcurl
   work remains independent of the UI refresh. */
static int downloader_progress_cb(void *clientp,double dltotal,double dlnow,double ultotal,double ulnow){
 (void)clientp;(void)ultotal;(void)ulnow;static int start_was_down=0;g_download_total=g_download_resume_base+dltotal;g_download_now=g_download_resume_base+dlnow;unsigned long long mn=fps_now_us();if(g_download_start_us&&mn>g_download_start_us&&!g_download_paused){g_download_elapsed=(double)(mn-g_download_start_us)/1000000.0;update_download_speed_stats(g_download_now,mn);g_download_avg_speed_bps=(g_download_elapsed>0)?(g_download_now-g_download_session_base)/g_download_elapsed:0;if(g_download_total>0&&g_download_avg_speed_bps>1)g_download_remaining=(g_download_total-g_download_now)/g_download_avg_speed_bps;}
 padInfo pi;padData pd;ioPadGetInfo(&pi);int sd=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);sd=pd.BTN_START?1:0;if(pd.BTN_CIRCLE){g_download_cancel_requested=1;return 1;}break;}if(sd&&!start_was_down)g_download_paused=!g_download_paused;start_was_down=sd;
 while(g_download_paused&&!g_download_cancel_requested){g_ui_dirty=1;draw();sysUtilCheckCallback();ioPadGetInfo(&pi);sd=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);sd=pd.BTN_START?1:0;if(pd.BTN_CIRCLE){g_download_cancel_requested=1;break;}break;}if(sd&&!start_was_down)g_download_paused=0;start_was_down=sd;}if(g_download_cancel_requested)return 1;
 unsigned long long now=fps_now_us();if(!g_download_last_draw_us||now-g_download_last_draw_us>=16666ull){g_download_last_draw_us=now;g_ui_dirty=1;draw();sysUtilCheckCallback();}return 0;
}
static void downloader_filename(const char *url,char *out,size_t cap){
 const char *end=url+strlen(url),*q=strpbrk(url,"?#");if(q)end=q;
 const char *p=end;while(p>url&&p[-1]!='/')p--;
 size_t n=(size_t)(end-p);if(!n){snprintf(out,cap,"download.bin");return;}if(n>=cap)n=cap-1;
 memcpy(out,p,n);out[n]=0;
 for(size_t i=0;i<n;i++)if(out[i]=='/'||out[i]=='\\'||out[i]==':')out[i]='_';
}
static size_t downloader_write_cb(void *ptr,size_t size,size_t nmemb,void *userdata){
 return fwrite(ptr,size,nmemb,(FILE*)userdata);
}
static int downloader_net_ready=0;
static int downloader_net_init(){
 if(downloader_net_ready)return 0;
 int rc=sysModuleLoad(SYSMODULE_NET);if(rc<0){snprintf(status_line,sizeof(status_line),"Downloader: NET module failed %08X",(unsigned)rc);return -1;}
 rc=sysModuleLoad(SYSMODULE_NETCTL);if(rc<0){snprintf(status_line,sizeof(status_line),"Downloader: NETCTL module failed %08X",(unsigned)rc);return -1;}
 rc=netInitialize();if(rc<0){snprintf(status_line,sizeof(status_line),"Downloader: netInitialize failed %08X",(unsigned)rc);return -1;}
 rc=netCtlInit();if(rc<0){snprintf(status_line,sizeof(status_line),"Downloader: netCtlInit failed %08X",(unsigned)rc);return -1;}
 rc=(int)curl_global_init(CURL_GLOBAL_ALL);if(rc!=0){snprintf(status_line,sizeof(status_line),"Downloader: curl init failed %d",rc);return -1;}
 downloader_net_ready=1;return 0;
}
typedef struct { FILE *f; curl_off_t got,start,end,file_offset; int owner,slot; char path[PATHSZ]; CURL *easy; curl_off_t *aggregate; } dl_segment_t;
static size_t segment_write_cb(void *ptr,size_t size,size_t nmemb,void *ud){
 dl_segment_t *sg=(dl_segment_t*)ud;
 size_t n=fwrite(ptr,size,nmemb,sg->f);
 curl_off_t bytes=(curl_off_t)(n*size);
 sg->got+=bytes;
 if(sg->aggregate)*sg->aggregate+=bytes;
 /* Phase 3N: keep the hot libcurl write callback deliberately tiny.  The
    previous callback queried the system timer and recalculated a worker rate
    for every received block on every one of the 16 TLS connections.  Real
    PS3 spends that time on the PPU.  Connection diagnostics are sampled in
    the multi loop at 4 Hz instead, with identical visible information. */
 return n;
}
enum { DOWNLOAD_VALIDATE_NONE=0, DOWNLOAD_VALIDATE_PSU=1, DOWNLOAD_VALIDATE_ZIP=2 };
static int download_policy_for_url(const char *url){int kind=ps2hb_asset_kind_from_url(url);return kind==PS2HB_ASSET_PSU?DOWNLOAD_VALIDATE_PSU:(kind==PS2HB_ASSET_ZIP?DOWNLOAD_VALIDATE_ZIP:DOWNLOAD_VALIDATE_NONE);}
static char g_store_ca_bundle[PATHSZ]="";
static int g_store_ca_bundle_checked=0,g_store_ca_bundle_missing=0;
static int store_ca_bundle_path(char *out,size_t cap){
 static const char *candidates[]={
  "/app_home/USRDIR/cacert.pem",
  "/dev_hdd0/game/PSU000001/USRDIR/cacert.pem",
  "/app_home/cacert.pem",
  "/dev_hdd0/game/PSU000001/cacert.pem",
  "USRDIR/cacert.pem",
  "cacert.pem"
 };
 if(!g_store_ca_bundle_checked){
  g_store_ca_bundle_checked=1;
  if(ps3_ca_bundle_find(candidates,sizeof(candidates)/sizeof(candidates[0]),g_store_ca_bundle,sizeof(g_store_ca_bundle))!=0)g_store_ca_bundle_missing=1;
 }
 if(g_store_ca_bundle_missing||!g_store_ca_bundle[0]||strlen(g_store_ca_bundle)>=cap)return 0;
 snprintf(out,cap,"%s",g_store_ca_bundle);return 1;
}
static int downloader_apply_store_tls(CURL *c){
 char ca_path[PATHSZ];
 curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
 if(!store_ca_bundle_path(ca_path,sizeof(ca_path)))return 0;
 curl_easy_setopt(c,CURLOPT_CAINFO,ca_path);return 1;
}
static size_t store_location_header(char *ptr,size_t size,size_t nmemb,void *ud){size_t n=size*nmemb;char *out=(char*)ud;if(n>10&&strncasecmp(ptr,"Location:",9)==0){const char *v=ptr+9;while(*v==' '||*v=='\t')v++;size_t z=n-(size_t)(v-ptr);while(z&&(v[z-1]=='\r'||v[z-1]=='\n'||v[z-1]==' '))z--;if(z<PS2HB_URL_SIZE){memcpy(out,v,z);out[z]=0;}}return n;}
static int store_join_redirect(const char *base,const char *location,char *out,size_t cap){if(!location||!*location)return -1;if(!strncasecmp(location,"https://",8)){snprintf(out,cap,"%s",location);return 0;}if(location[0]!='/')return -1;const char *host=strstr(base,"://");if(!host)return -1;host+=3;const char *slash=strchr(host,'/');size_t n=slash?(size_t)(slash-base):strlen(base);if(n+strlen(location)+1>cap)return -1;memcpy(out,base,n);out[n]=0;strcat(out,location);return 0;}
static int store_resolve_url(const char *input,char *resolved,size_t cap){if(!ps2hb_redirect_allowed(input)&&!ps2hb_requires_verified_tls(input))return -1;snprintf(resolved,cap,"%s",input);for(int hop=0;hop<=5;hop++){char location[PS2HB_URL_SIZE]={0};CURL *c=curl_easy_init();if(!c)return -1;curl_easy_setopt(c,CURLOPT_URL,resolved);curl_easy_setopt(c,CURLOPT_NOBODY,1L);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,store_location_header);curl_easy_setopt(c,CURLOPT_HEADERDATA,location);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_TIMEOUT,20L);curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.50");if(!downloader_apply_store_tls(c)){curl_easy_cleanup(c);return -1;}CURLcode rc=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);if(rc!=CURLE_OK)return -1;if(code<300||code>=400)return 0;if(hop==5||!location[0])return -1;char next[PS2HB_URL_SIZE];if(store_join_redirect(resolved,location,next,sizeof(next))!=0)return -1;int is_asset=download_policy_for_url(input)!=DOWNLOAD_VALIDATE_NONE;if(is_asset?!ps2hb_redirect_allowed(next):!ps2hb_requires_verified_tls(next))return -1;snprintf(resolved,cap,"%s",next);}return -1;}
static curl_off_t downloader_probe_size(const char *url){char resolved[PS2HB_URL_SIZE];const char *target=url;int store=ps2hb_requires_verified_tls(url)||download_policy_for_url(url);if(store){if(store_resolve_url(url,resolved,sizeof(resolved))!=0)return 0;target=resolved;}CURL *c=curl_easy_init();if(!c)return 0;curl_easy_setopt(c,CURLOPT_URL,target);curl_easy_setopt(c,CURLOPT_NOBODY,1L);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);if(store){if(!downloader_apply_store_tls(c)){curl_easy_cleanup(c);return 0;}}else{curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,0L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L);}curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0 (PLAYSTATION 3; 1.00)");CURLcode cr=curl_easy_perform(c);double n=0;long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_getinfo(c,CURLINFO_CONTENT_LENGTH_DOWNLOAD,&n);curl_easy_cleanup(c);return (cr==CURLE_OK&&code<400&&n>0)?(curl_off_t)n:0;}
static int setup_segment_worker(CURLM *m,dl_segment_t *w,const char *url,const char *owner_path,int owner,curl_off_t owner_start,curl_off_t a,curl_off_t b,curl_off_t *aggregate){
 memset(w,0,sizeof(*w));w->start=a;w->end=b;w->owner=owner;w->slot=-1;w->file_offset=a-owner_start;w->aggregate=aggregate;snprintf(w->path,sizeof(w->path),"%s",owner_path);
 w->f=fopen(owner_path,"r+b");if(!w->f)w->f=fopen(owner_path,"w+b");if(!w->f)return -1;if(fseek(w->f,(long)w->file_offset,SEEK_SET)!=0){fclose(w->f);w->f=NULL;return -1;}
 w->easy=curl_easy_init();if(!w->easy){fclose(w->f);w->f=NULL;return -1;}char range[96];snprintf(range,sizeof(range),"%lld-%lld",(long long)a,(long long)b);
 curl_easy_setopt(w->easy,CURLOPT_URL,url);curl_easy_setopt(w->easy,CURLOPT_RANGE,range);curl_easy_setopt(w->easy,CURLOPT_WRITEFUNCTION,segment_write_cb);curl_easy_setopt(w->easy,CURLOPT_WRITEDATA,w);curl_easy_setopt(w->easy,CURLOPT_USERAGENT,"Mozilla/5.0 (PLAYSTATION 3; 1.00)");int store=ps2hb_requires_verified_tls(url)||download_policy_for_url(url);curl_easy_setopt(w->easy,CURLOPT_FOLLOWLOCATION,store?0L:1L);curl_easy_setopt(w->easy,CURLOPT_MAXREDIRS,store?0L:20L);curl_easy_setopt(w->easy,CURLOPT_FAILONERROR,1L);curl_easy_setopt(w->easy,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(w->easy,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);if(store){if(!downloader_apply_store_tls(w->easy)){curl_easy_cleanup(w->easy);w->easy=NULL;fclose(w->f);w->f=NULL;return -1;}}else{curl_easy_setopt(w->easy,CURLOPT_SSL_VERIFYHOST,0L);curl_easy_setopt(w->easy,CURLOPT_SSL_VERIFYPEER,0L);}
 /* Phase 3N: larger receive buffers reduce write-callback/stdio overhead on
    the PPU while retaining the existing segmented range behaviour. */
 curl_easy_setopt(w->easy,CURLOPT_BUFFERSIZE,65536L);
 curl_multi_add_handle(m,w->easy);return 0;
}
static void close_segment_worker(CURLM *m,dl_segment_t *w){if(w->easy){curl_multi_remove_handle(m,w->easy);curl_easy_cleanup(w->easy);w->easy=NULL;}if(w->f){fflush(w->f);fclose(w->f);w->f=NULL;}}
static void downloader_delete_resume_url(const char *part);
static int download_segmented(const char *url,const char *dst,const char *part,int conns){
 char store_resolved[PS2HB_URL_SIZE];if(download_policy_for_url(url)){if(store_resolve_url(url,store_resolved,sizeof(store_resolved))!=0)return -1;url=store_resolved;}
 curl_off_t total=downloader_probe_size(url);if(total<=0)return 1;if(conns<2)return 1;if(conns>16)conns=16;
 /* Resume segmented downloads from the existing contiguous .part prefix. */
 struct stat rpst;curl_off_t base=0;if(stat(part,&rpst)==0&&rpst.st_size>0)base=(curl_off_t)rpst.st_size;
 if(base>=total){if((g_catalogue_verify_psu||g_homebrew_validate_kind==DOWNLOAD_VALIDATE_PSU) && psu_list(part,NULL,NULL)!=0){snprintf(status_line,sizeof(status_line),"Catalogue: invalid PSU - download rejected");unlink(part);downloader_delete_resume_url(part);return -1;}if(g_homebrew_validate_kind==DOWNLOAD_VALIDATE_ZIP&&ps2hb_validate_zip_file(part)!=0){snprintf(status_line,sizeof(status_line),"Catalogue: invalid ZIP - download rejected");unlink(part);downloader_delete_resume_url(part);return -1;}if(!g_homebrew_validate_kind)unlink(dst);if(rename(part,dst)!=0)return -1;downloader_delete_resume_url(part);invalidate_dir_cache(g_apollo_savedata_path);g_download_now=g_download_total=(double)total;g_download_active_connections=1;downloader_wait_for_completion();return 0;}
 curl_off_t remaining=total-base;if(remaining<(curl_off_t)conns*262144)conns=(int)(remaining/262144);if(conns<2)return 1;
 /* If a previous segmented transfer was cancelled, keep its original range map.
    This prevents already-downloaded C2..C16 data being discarded on resume. */
 char segmf[PATHSZ];snprintf(segmf,sizeof(segmf),"%s.segments",part);int persisted=0,pconns=0;curl_off_t ptotal=0,pbase=0;curl_off_t ps[16]={0},pe[16]={0},ph[16]={0};
 { FILE *mf=fopen(segmf,"rb");if(mf){char magic[32]={0};if(fscanf(mf,"%31s %lld %lld %d",magic,(long long*)&ptotal,(long long*)&pbase,&pconns)==4&&!strcmp(magic,"PS3PSU_SEG1")&&ptotal==total&&pconns>=2&&pconns<=16){persisted=1;for(int i=0;i<pconns;i++){int idx=-1;long long a=0,b=0,h=0;if(fscanf(mf,"%d %lld %lld %lld",&idx,&a,&b,&h)!=4||idx!=i){persisted=0;break;}ps[i]=(curl_off_t)a;pe[i]=(curl_off_t)b;ph[i]=(curl_off_t)h;}
      /* Recover partials created by the immediately preceding buggy build.
         That build appended seg0 into .part but left the manifest base at the
         old value.  If the size delta exactly equals seg0, restore .part to
         the manifest base so all 16 persisted ranges remain valid. */
      if(persisted&&pbase!=base){struct stat rs0;char rp0[PATHSZ];snprintf(rp0,sizeof(rp0),"%s.seg0",part);curl_off_t hs0=0;if(stat(rp0,&rs0)==0)hs0=(curl_off_t)rs0.st_size;if(base==pbase+hs0){if(truncate(part,(off_t)pbase)==0)base=pbase;else persisted=0;}else persisted=0;}
      if(persisted&&pbase!=base)persisted=0;}fclose(mf);} }
 if(persisted)conns=pconns;
 dl_segment_t sg[16];memset(sg,0,sizeof(sg));curl_off_t owner_start[16]={0},owner_end[16]={0};char owner_path[16][PATHSZ];CURLM *m=curl_multi_init();if(!m)return 1;int added=0,failed=0;curl_off_t chunk=(remaining+conns-1)/conns,aggregate=0;
 for(int i=0;i<conns;i++){curl_off_t a=persisted?ps[i]:base+(curl_off_t)i*chunk,b=persisted?pe[i]:a+chunk-1;if(b>=total)b=total-1;if(a>b)break;owner_start[i]=a;owner_end[i]=b;snprintf(owner_path[i],sizeof(owner_path[i]),"%s.seg%d",part,i);curl_off_t have=0;if(persisted){struct stat ss;if(stat(owner_path[i],&ss)==0)have=(curl_off_t)ss.st_size;if(have<0||have>b-a+1)have=0;}else{FILE *seed=fopen(owner_path[i],"wb");if(!seed){failed=1;break;}fclose(seed);}aggregate+=have;if(have>=b-a+1){sg[i].owner=i;sg[i].slot=i;sg[i].start=a;sg[i].end=b;sg[i].got=have;snprintf(sg[i].path,sizeof(sg[i].path),"%s",owner_path[i]);g_conn_now[i]=(double)have;g_conn_total[i]=(double)(b-a+1);g_conn_state[i]=2;added++;continue;}if(setup_segment_worker(m,&sg[i],url,owner_path[i],i,a,a+have,b,&aggregate)!=0){failed=1;break;}sg[i].got=have;sg[i].slot=i;g_conn_now[i]=(double)have;g_conn_total[i]=(double)(b-a+1);g_conn_speed_bps[i]=0;g_conn_last_bytes[i]=(double)have;g_conn_sample_us[i]=fps_now_us();g_conn_state[i]=1;added++;}
 if(failed){for(int i=0;i<added;i++){close_segment_worker(m,&sg[i]);unlink(owner_path[i]);}curl_multi_cleanup(m);return 1;}
 g_download_active_connections=added;g_download_resume_base=(double)base;g_download_now=(double)base;g_download_total=(double)total;g_download_cancel_requested=0;g_download_paused=0;g_download_start_us=fps_now_us();g_download_last_draw_us=0;reset_download_speed_stats((double)base+(double)aggregate);g_download_progress_active=1;g_ui_dirty=1;draw();g_ui_dirty=1;draw();int running_multi=0,start_was_down=0;unsigned long long pause_started_us=0;curl_multi_perform(m,&running_multi);
 while(running_multi&&!failed&&!g_download_cancel_requested){padInfo pi;padData pd;ioPadGetInfo(&pi);int sd=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);sd=pd.BTN_START?1:0;if(pd.BTN_CIRCLE)g_download_cancel_requested=1;break;}unsigned long long input_now=fps_now_us();if(sd&&!start_was_down){if(!g_download_paused){g_download_paused=1;pause_started_us=input_now;}else{unsigned long long pdur=pause_started_us&&input_now>pause_started_us?input_now-pause_started_us:0;g_download_paused=0;g_download_start_us+=pdur;g_download_sample_us+=pdur;for(int i=0;i<added;i++)g_conn_sample_us[i]+=pdur;pause_started_us=0;}}start_was_down=sd;
  if(!g_download_paused){curl_multi_perform(m,&running_multi);int msgs=0;CURLMsg *msg;while((msg=curl_multi_info_read(m,&msgs))){if(msg->msg!=CURLMSG_DONE)continue;int wi=-1;for(int i=0;i<added;i++)if(sg[i].easy==msg->easy_handle){wi=i;break;}if(wi<0)continue;long code=0;curl_easy_getinfo(sg[wi].easy,CURLINFO_RESPONSE_CODE,&code);if(msg->data.result!=CURLE_OK||code!=206){failed=1;break;}close_segment_worker(m,&sg[wi]);g_conn_state[wi]=2;
    /* Narrow work stealing: only after a worker really finishes. Split the largest active remaining range in half. */
    int donor=-1;curl_off_t best=0;for(int j=0;j<added;j++)if(sg[j].easy&&g_conn_state[j]==1&&sg[j].owner!=0){curl_off_t rem=(sg[j].end-sg[j].start+1)-sg[j].got;if(rem>best){best=rem;donor=j;}}
    /* Conservative tail assist: only split a donor when at least 8 MiB remains.
       This guarantees each half is about 4 MiB or larger and avoids late tiny-range HTTP churn. */
    if(0 && donor>=0&&best>=8388608){curl_off_t rem_start=sg[donor].start+sg[donor].got;curl_off_t old_end=sg[donor].end;int owner=sg[donor].owner;curl_off_t mid=rem_start+best/2-1;close_segment_worker(m,&sg[donor]);
      if(setup_segment_worker(m,&sg[donor],url,owner_path[owner],owner,owner_start[owner],rem_start,mid,&aggregate)!=0||setup_segment_worker(m,&sg[wi],url,owner_path[owner],owner,owner_start[owner],mid+1,old_end,&aggregate)!=0){failed=1;break;}sg[donor].slot=donor;sg[wi].slot=wi;
      unsigned long long ts=fps_now_us();g_conn_now[donor]=0;g_conn_total[donor]=(double)(mid-rem_start+1);g_conn_speed_bps[donor]=0;g_conn_last_bytes[donor]=0;g_conn_sample_us[donor]=ts;g_conn_state[donor]=1;g_conn_now[wi]=0;g_conn_total[wi]=(double)(old_end-mid);g_conn_speed_bps[wi]=0;g_conn_last_bytes[wi]=0;g_conn_sample_us[wi]=ts;g_conn_state[wi]=1;curl_multi_perform(m,&running_multi);
    }
  }}
  unsigned long long now=fps_now_us();
  for(int i=0;i<added;i++)if(sg[i].easy)g_conn_now[i]=(double)sg[i].got;
  /* Phase 3N: sample all C1-C16 rates together at 4 Hz.  This replaces timer
     calls in the receive callback and prevents diagnostics from competing
     with TLS/range I/O for PPU time. */
  if(!g_download_paused){
   for(int i=0;i<added;i++)if(g_conn_state[i]==1 && g_conn_sample_us[i] && now-g_conn_sample_us[i]>=250000ull){
    double dt=(double)(now-g_conn_sample_us[i])/1000000.0;
    double db=g_conn_now[i]-g_conn_last_bytes[i]; if(db<0)db=0;
    g_conn_speed_bps[i]=dt>0?db/dt:0;
    g_conn_last_bytes[i]=g_conn_now[i]; g_conn_sample_us[i]=now;
   }
  }
  g_download_now=(double)base+(double)aggregate;if(!g_download_paused){g_download_elapsed=(double)(now-g_download_start_us)/1000000.0;update_download_speed_stats(g_download_now,now);g_download_avg_speed_bps=(g_download_elapsed>0)?((g_download_now-g_download_session_base)/g_download_elapsed):0;if(g_download_avg_speed_bps>1)g_download_remaining=(g_download_total-g_download_now)/g_download_avg_speed_bps;}if(!g_download_last_draw_us||now-g_download_last_draw_us>=16666ull){g_download_last_draw_us=now;g_ui_dirty=1;draw();sysUtilCheckCallback();}usleep(1000);
 }
 if(!failed&&!g_download_cancel_requested){g_download_now=g_download_total;g_download_remaining=0;for(int i=0;i<added;i++)g_conn_state[i]=2;g_download_finalizing=1;g_download_progress_active=1;g_ui_dirty=1;draw();}
 for(int i=0;i<added;i++)close_segment_worker(m,&sg[i]);curl_multi_cleanup(m);
 if(g_download_cancel_requested){
  g_download_finalizing=0;g_download_progress_active=0;g_ui_dirty=1;
  /* Keep the base .part EXACTLY at the prefix size that existed when this
     segmented generation started.  Do not append seg0 here: doing so changes
     `base`, makes the persisted range map fail its pbase==base validation on
     the next resume, and causes all C1..Cn segment progress to be discarded.
     Every byte downloaded in this generation stays in its .segN owner file
     and is accounted for/reused from the .segments map. */
  /* Preserve every segment instead of throwing C2..Cn away.
     They are valid byte ranges and are reused by the next resume. */
  { char mf[PATHSZ]; snprintf(mf,sizeof(mf),"%s.segments",part); FILE *mff=fopen(mf,"wb");
    if(mff){ fprintf(mff,"PS3PSU_SEG1 %lld %lld %d\n",(long long)total,(long long)base,added);
      for(int i=0;i<added;i++){ struct stat ss; long long have=0; if(stat(owner_path[i],&ss)==0)have=(long long)ss.st_size;
        fprintf(mff,"%d %lld %lld %lld\n",i,(long long)owner_start[i],(long long)owner_end[i],have); } fclose(mff); } }
  invalidate_dir_cache(g_apollo_savedata_path);
  snprintf(status_line,sizeof(status_line),"Download cancelled - all %d connection segments kept for resume",added);return -1;
 }if(failed){g_download_finalizing=0;g_download_progress_active=0;g_ui_dirty=1;for(int i=0;i<added;i++)unlink(owner_path[i]);snprintf(status_line,sizeof(status_line),"Server rejected segmented ranges - using one connection");return 1;}
 FILE *out=fopen(part,base>0?"ab":"wb");if(!out){g_download_finalizing=0;g_download_progress_active=0;for(int i=0;i<added;i++)unlink(owner_path[i]);return -1;}char buf[65536];for(int i=0;i<added;i++){struct stat st;if(stat(owner_path[i],&st)!=0||(curl_off_t)st.st_size!=(owner_end[i]-owner_start[i]+1)){failed=1;break;}FILE *in=fopen(owner_path[i],"rb");if(!in){failed=1;break;}size_t n;while((n=fread(buf,1,sizeof(buf),in))>0)if(fwrite(buf,1,n,out)!=n){failed=1;break;}fclose(in);unlink(owner_path[i]);if(failed)break;}fclose(out);if(failed){g_download_finalizing=0;g_download_progress_active=0;unlink(part);return -1;}if((g_catalogue_verify_psu||g_homebrew_validate_kind==DOWNLOAD_VALIDATE_PSU) && psu_list(part,NULL,NULL)!=0){g_download_finalizing=0;g_download_progress_active=0;unlink(part);downloader_delete_resume_url(part);unlink(segmf);snprintf(status_line,sizeof(status_line),"Catalogue: downloaded data is not a valid PSU");return -1;}if(g_homebrew_validate_kind==DOWNLOAD_VALIDATE_ZIP&&ps2hb_validate_zip_file(part)!=0){g_download_finalizing=0;g_download_progress_active=0;unlink(part);downloader_delete_resume_url(part);unlink(segmf);snprintf(status_line,sizeof(status_line),"Catalogue: downloaded data is not a valid ZIP");return -1;}if(!g_homebrew_validate_kind)unlink(dst);if(rename(part,dst)!=0){g_download_finalizing=0;g_download_progress_active=0;return -1;}downloader_delete_resume_url(part);unlink(segmf);invalidate_dir_cache(g_apollo_savedata_path);g_download_now=g_download_total=(double)total;snprintf(status_line,sizeof(status_line),"Downloaded %s using %d connections + tail assist",g_download_name,added);g_download_active_connections=added;downloader_wait_for_completion();g_download_active_connections=1;return 0;
}
static void downloader_url_sidecar_path(const char *part,char *out,size_t outsz){snprintf(out,outsz,"%s.url",part);}
static void downloader_save_resume_url(const char *part,const char *url){char side[PATHSZ];downloader_url_sidecar_path(part,side,sizeof(side));FILE*f=fopen(side,"wb");if(f){fwrite(url,1,strlen(url),f);fclose(f);}}
static void downloader_delete_resume_url(const char *part){char side[PATHSZ];downloader_url_sidecar_path(part,side,sizeof(side));unlink(side);}
static int downloader_load_resume_url(const char *part,char *url,size_t urlsz){char side[PATHSZ];downloader_url_sidecar_path(part,side,sizeof(side));FILE*f=fopen(side,"rb");if(!f)return -1;size_t n=fread(url,1,urlsz-1,f);fclose(f);url[n]=0;while(n&&(url[n-1]=='\r'||url[n-1]=='\n'||url[n-1]==' '||url[n-1]=='\t'))url[--n]=0;return n?0:-1;}
static int catalogue_is_direct_psu(const char *url){return ps2wiki_valid_direct_psu(url);}
static int filen_ui_progress(void *,const char *name,double now,double total){
 snprintf(g_download_name,sizeof(g_download_name),"%s",name);
 return downloader_progress_cb(NULL,total,now,0,0);
}
static int filen_validate_psu(const char *path){return (g_catalogue_verify_psu||endswith(g_download_name,".psu"))?psu_list(path,NULL,NULL):0;}
static int download_filen_to_fakeusb(const char *url){
 if(create_apollo_savedata_tree()!=0){snprintf(status_line,sizeof(status_line),"Filen: cannot create save directory");return -1;}
 if(downloader_net_init()!=0)return -1;
 char ca_path[PATHSZ];
 if(!store_ca_bundle_path(ca_path,sizeof(ca_path))){snprintf(status_line,sizeof(status_line),"SSL CA Cert not found or unreadable");return -1;}
 g_download_active_connections=1;g_download_resume_base=0;g_download_now=0;g_download_total=0;
 g_download_cancel_requested=0;g_download_paused=0;g_download_completed=0;g_download_finalizing=0;
 g_download_last_draw_us=0;g_download_start_us=fps_now_us();g_download_elapsed=0;g_download_remaining=0;
 reset_download_speed_stats(0);g_download_progress_active=1;
 snprintf(g_download_name,sizeof(g_download_name),"Resolving Filen share");
 snprintf(g_download_url,sizeof(g_download_url),"https://app.filen.io/ (key hidden)");
 g_ui_dirty=1;draw();g_ui_dirty=1;draw();
 char saved[PATHSZ],error[256];
 int rc=filen_download(url,g_apollo_savedata_path,ca_path,
  filen_ui_progress,NULL,filen_validate_psu,saved,sizeof(saved),error,sizeof(error));
 g_download_progress_active=0;g_download_paused=0;g_ui_dirty=1;
 invalidate_dir_cache(g_apollo_savedata_path);
 if(rc==0){snprintf(status_line,sizeof(status_line),"Filen downloaded and decrypted: %s",g_download_name);downloader_wait_for_completion();}
 else snprintf(status_line,sizeof(status_line),"%s",error);
 return rc;
}
static int downloader_store_unique_path(const char *base,char *out,size_t cap){struct stat st;if(stat(base,&st)!=0){snprintf(out,cap,"%s",base);return 0;}const char *slash=strrchr(base,'/'),*dot=strrchr(base,'.');if(!dot|| (slash&&dot<slash))dot=base+strlen(base);size_t prefix=(size_t)(dot-base);for(int i=1;i<1000;i++){char candidate[PATHSZ];snprintf(candidate,sizeof(candidate),"%.*s (%d)%s",(int)prefix,base,i,dot);if(stat(candidate,&st)!=0){snprintf(out,cap,"%s",candidate);return 0;}}return -1;}
static int download_url_to_fakeusb(const char *url){
 char filen_uuid[37],filen_key[257];
 if(filen_parse_link(url,filen_uuid,filen_key)){memset(filen_key,0,sizeof(filen_key));return download_filen_to_fakeusb(url);}
 if(g_catalogue_verify_psu&&!catalogue_is_direct_psu(url)){snprintf(status_line,sizeof(status_line),"Catalogue requires a direct HTTP(S) .PSU file URL");return -1;}
 g_homebrew_validate_kind=download_policy_for_url(url);
 if(strstr(url,"://app.filen.io/")||strstr(url,"://filen.io/")){snprintf(status_line,sizeof(status_line),"Invalid Filen public file link; include its #key");return -1;}
 if(strncmp(url,"http://",7)&&strncmp(url,"https://",8)){snprintf(status_line,sizeof(status_line),"Downloader: URL must start http:// or https://");return -1;}
 if(create_apollo_savedata_tree()!=0){snprintf(status_line,sizeof(status_line),"Downloader: could not create fakeusb/PS2/SAVEDATA");return -1;}
 if(downloader_net_init()!=0)return -1;
 char resolved_store_url[PS2HB_URL_SIZE];if(g_homebrew_validate_kind||ps2hb_requires_verified_tls(url)){if(store_resolve_url(url,resolved_store_url,sizeof(resolved_store_url))!=0){snprintf(status_line,sizeof(status_line),"Store HTTPS/redirect validation failed");return -1;}url=resolved_store_url;}
 char name[256],dst[PATHSZ],part[PATHSZ];downloader_filename(url,name,sizeof(name));snprintf(g_download_name,sizeof(g_download_name),"%s",name);snprintf(g_download_url,sizeof(g_download_url),"%s",url);
 snprintf(dst,sizeof(dst),"%s/%s",g_apollo_savedata_path,name);if(g_homebrew_validate_kind){char unique[PATHSZ];if(downloader_store_unique_path(dst,unique,sizeof(unique))!=0){snprintf(status_line,sizeof(status_line),"Store: no unused filename is available");return -1;}snprintf(dst,sizeof(dst),"%s",unique);}snprintf(part,sizeof(part),"%s.part",dst);
 downloader_save_resume_url(part,url);
 if(g_download_connections>=2){int mr=download_segmented(url,dst,part,g_download_connections);if(mr<=0)return mr;}
 g_download_active_connections=1;for(int i=0;i<16;i++){g_conn_state[i]=0;g_conn_now[i]=g_conn_total[i]=g_conn_speed_bps[i]=0;} int retry_fresh=0;
retry_download:
 struct stat pst;curl_off_t resume_from=0;if(!retry_fresh&&stat(part,&pst)==0&&pst.st_size>0)resume_from=(curl_off_t)pst.st_size;
 g_download_resume_base=(double)resume_from;g_download_now=(double)resume_from;g_download_total=0;g_download_cancel_requested=0;g_download_paused=0;g_download_last_draw_us=0;g_download_start_us=fps_now_us();g_download_elapsed=0;g_download_remaining=0;reset_download_speed_stats((double)resume_from);g_download_progress_active=1;
 /* Seed both physical framebuffers with the same progress composition before
    curl starts.  This replaces the old keyboard->base->base transition that
    visibly flashed at download startup. */
 g_ui_dirty=1;draw();g_ui_dirty=1;draw();
 FILE *f=fopen(part,resume_from>0?"ab":"wb");if(!f){g_download_progress_active=0;snprintf(status_line,sizeof(status_line),"Downloader: cannot create %s",name);return -1;}
 CURL *c=curl_easy_init();if(!c){fclose(f);g_download_progress_active=0;snprintf(status_line,sizeof(status_line),"Downloader: curl handle failed");return -1;}
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,downloader_write_cb);curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
 int store=ps2hb_requires_verified_tls(url)||g_homebrew_validate_kind;curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0 (PLAYSTATION 3; 1.00)");curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,store?0L:1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,store?0L:20L);
 curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 if(store){if(!downloader_apply_store_tls(c)){curl_easy_cleanup(c);fclose(f);g_download_progress_active=0;snprintf(status_line,sizeof(status_line),"SSL CA Cert not found or unreadable");return -1;}}else{curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,0L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L);}curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,downloader_progress_cb);curl_easy_setopt(c,CURLOPT_PROGRESSDATA,NULL);
 if(resume_from>0)curl_easy_setopt(c,CURLOPT_RESUME_FROM_LARGE,resume_from);
 union net_ctl_info pi;memset(&pi,0,sizeof(pi));if(netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_CONFIG,&pi)>=0&&pi.http_proxy_config==NET_CTL_HTTP_PROXY_ON){
  memset(&pi,0,sizeof(pi));if(netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_SERVER,&pi)>=0)curl_easy_setopt(c,CURLOPT_PROXY,pi.http_proxy_server);
  memset(&pi,0,sizeof(pi));if(netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_PORT,&pi)>=0)curl_easy_setopt(c,CURLOPT_PROXYPORT,pi.http_proxy_port);
 }
 CURLcode cr=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);fclose(f);g_download_progress_active=0;g_ui_dirty=1;
 if(g_download_cancel_requested){invalidate_dir_cache(g_apollo_savedata_path);snprintf(status_line,sizeof(status_line),"Download cancelled - partial kept for resume");return -1;}
 if(resume_from>0 && (cr==CURLE_RANGE_ERROR || code==200)){
  /* A server that ignores Range must never be appended to the partial file. */
  unlink(part);retry_fresh=1;g_download_resume_base=0;snprintf(status_line,sizeof(status_line),"Server cannot resume - restarting download");goto retry_download;
 }
 if(cr!=CURLE_OK||code>=400||(store&&(code<200||code>=300))){struct stat es;if(stat(part,&es)==0&&es.st_size==0)unlink(part);invalidate_dir_cache(g_apollo_savedata_path);snprintf(status_line,sizeof(status_line),"Download failed: %s (HTTP %ld) - partial kept",curl_easy_strerror(cr),code);return -1;}
 if((g_catalogue_verify_psu||g_homebrew_validate_kind==DOWNLOAD_VALIDATE_PSU) && psu_list(part,NULL,NULL)!=0){unlink(part);downloader_delete_resume_url(part);snprintf(status_line,sizeof(status_line),"Catalogue: downloaded data is not a valid PSU");return -1;}if(g_homebrew_validate_kind==DOWNLOAD_VALIDATE_ZIP&&ps2hb_validate_zip_file(part)!=0){unlink(part);downloader_delete_resume_url(part);snprintf(status_line,sizeof(status_line),"Catalogue: downloaded data is not a valid ZIP");return -1;}
 if(!g_homebrew_validate_kind)unlink(dst);if(rename(part,dst)!=0){snprintf(status_line,sizeof(status_line),"Downloader: could not finalize %s",name);return -1;}
 downloader_delete_resume_url(part);g_download_resume_base=0;invalidate_dir_cache(g_apollo_savedata_path);snprintf(status_line,sizeof(status_line),"Downloaded %s to fakeusb/PS2/SAVEDATA",name);downloader_wait_for_completion();return 0;
}

/* Phase 5Q: live PS2Wiki catalogue. Category HTML supplies titles, versions
   and detail-page URLs; the selected detail page supplies the actual download
   URL via the Jekyll download_link()/bits.push() script. Nothing is guessed or
   hand-entered. Filen public shares are not direct HTTP PSU downloads. */
enum { PSU_CAT_GAMES=0, PSU_CAT_SYSTEM=1, PSU_CAT_APPS=2, PSU_CAT_RTES=3, PSU_CAT_UMCS=4, PSU_CAT_OTHER=5 };
static const char *const g_catalogue_category_names[]={"GAMES","SYSTEM APPS","APPS","RTES","UMCS","OTHER"};
static const char *const g_catalogue_category_pages[]={
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/games.html",
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/sysapps.html",
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/apps.html",
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/rtes.html",
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/umcs.html",
 "https://ps2wiki.github.io/sas-apps-archive/psu-categories/other.html"};
typedef struct {int cat;const char *title,*version;} catalogue_seed_t;
static const catalogue_seed_t g_catalogue_seed[]={
 {0,"3D Pinball Space Cadet","v0.3"},{0,"Hermes","v1.01"},
 {1,"FMCB Configurator Standalone","v1.9"},{1,"PowerOff / Shutdown","v1.966"},
 {1,"Memory Card Annihilator","v2.0a"},{1,"Original PS2 Card Test","v1.0.0.2"},
 {1,"In Game Reset (IGR)","v0.1"},
 {2,"Apollo Save Tool (PS2)","v1.0.2"},{2,"Argon","v0.7.1"},
 {2,"Graphics Synthesizer Mode Selector","v0.38"},
 {2,"Simple Media System","v2.9 Rev4"},
 {2,"wLaunchELF El Isra's Fork","v4.43x"},
 {2,"wLaunchELF krHACKen's Fork","v4.42a"},
 {2,"uLaunchELF","v4.42d"},{2,"[DBG] PS2Link","v1.9.1"},
 {2,"PlayStation2 Basic BootLoader","v1.2.0"},
 {3,"POPStarter","r13"},{3,"Neutrino","v1.3.1"},
 {3,"Kernelloader","v3.0"},{3,"Enceladus","c56803fc"},
 {3,"AthenaEnv","cdd31c9b"}};
static ps2wiki_entry_t g_catalogue_items[PS2WIKI_MAX_ITEMS];
static ps2wiki_entry_t g_catalogue_scratch[PS2WIKI_MAX_ITEMS];
static int g_catalogue_total=0;
static int g_catalogue_loaded_this_session[PS2WIKI_CATEGORY_COUNT]={0};
static char g_catalogue_notice[160]="Offline listing; press SQUARE to refresh";
static const char *const g_catalogue_cache_file="/dev_hdd0/game/PSU000001/USRDIR/PSU_CATALOGUE_CACHE.txt";

static int catalogue_entry_id(int cat,int index){
 for(int i=0;i<g_catalogue_total;i++)if(g_catalogue_items[i].category==cat){if(index--==0)return i;}return -1;
}
static int catalogue_count(int cat){int n=0;for(int i=0;i<g_catalogue_total;i++)if(g_catalogue_items[i].category==cat)n++;return n;}
static int catalogue_selected_id(){return catalogue_entry_id(g_catalogue_category,g_catalogue_selected);}
static void catalogue_apply_category(int cat,const ps2wiki_entry_t *fresh,int count){
 if(count<0||count>PS2WIKI_MAX_ITEMS)return;
 int other=0;
 for(int i=0;i<g_catalogue_total;i++)if(g_catalogue_items[i].category!=cat)other++;
 if(other+count>PS2WIKI_MAX_ITEMS)return;
 int n=0;
 for(int i=0;i<g_catalogue_total;i++)if(g_catalogue_items[i].category!=cat)g_catalogue_scratch[n++]=g_catalogue_items[i];
 for(int i=0;i<count;i++){
  ps2wiki_entry_t item=fresh[i];
  /* Only reuse a previously resolved URL when its page and version match.
     X always checks the live detail page again before starting a transfer. */
  if(!item.download_url[0])for(int j=0;j<g_catalogue_total;j++){
   if(g_catalogue_items[j].category==cat && item.detail_url[0] &&
      !strcmp(g_catalogue_items[j].detail_url,item.detail_url) &&
      !strcmp(g_catalogue_items[j].version,item.version)){
     snprintf(item.download_url,sizeof(item.download_url),"%s",g_catalogue_items[j].download_url);break;
   }
  }
  g_catalogue_scratch[n++]=item;
 }
 memcpy(g_catalogue_items,g_catalogue_scratch,(size_t)n*sizeof(g_catalogue_items[0]));
 g_catalogue_total=n;
}
static void catalogue_init_defaults(){
 g_catalogue_total=0;
 for(unsigned i=0;i<sizeof(g_catalogue_seed)/sizeof(g_catalogue_seed[0]);i++){
  ps2wiki_entry_t *e=&g_catalogue_items[g_catalogue_total++];memset(e,0,sizeof(*e));
  e->category=g_catalogue_seed[i].cat;
  snprintf(e->title,sizeof(e->title),"%s",g_catalogue_seed[i].title);
  snprintf(e->version,sizeof(e->version),"%s",g_catalogue_seed[i].version);
 }
}
static void catalogue_save_cache(){
 char tmp[PATHSZ];snprintf(tmp,sizeof(tmp),"%s.tmp",g_catalogue_cache_file);
 FILE *f=fopen(tmp,"wb");if(!f)return;
 fprintf(f,"# PS3 PSU Manager PS2Wiki catalogue cache v1\n");
 for(int i=0;i<g_catalogue_total;i++){
  const ps2wiki_entry_t *e=&g_catalogue_items[i];
  if(!ps2wiki_valid_detail_url(e->detail_url,e->category))continue;
  fprintf(f,"%d\t%s\t%s\t%s\t%s\n",e->category,e->title,e->version,e->detail_url,e->download_url);
 }
 if(fclose(f)!=0){unlink(tmp);return;}
 if(rename(tmp,g_catalogue_cache_file)!=0)unlink(tmp);
}
static void catalogue_load_cache(){
 catalogue_init_defaults();
 FILE *f=fopen(g_catalogue_cache_file,"rb");if(!f)return;
 static ps2wiki_entry_t cache[PS2WIKI_MAX_ITEMS];int n=0;
 char line[2048];
 while(fgets(line,sizeof(line),f)&&n<PS2WIKI_MAX_ITEMS){
  if(line[0]=='#')continue;
  size_t l=strlen(line);while(l&&(line[l-1]=='\r'||line[l-1]=='\n'))line[--l]=0;
  char *fields[5]={line,NULL,NULL,NULL,NULL};char *cursor=line;
  int ok=1;for(int k=1;k<5;k++){
   char *sep=strchr(cursor,'\t');if(!sep){ok=0;break;}*sep=0;cursor=sep+1;fields[k]=cursor;
  }
  if(!ok||strlen(fields[0])!=1||fields[0][0]<'0'||fields[0][0]>('0'+PS2WIKI_CATEGORY_COUNT-1))continue;
  int cat=fields[0][0]-'0';
  if(!fields[1][0]||strlen(fields[1])>=PS2WIKI_TITLE||strlen(fields[2])>=PS2WIKI_VERSION||
     strlen(fields[3])>=PS2WIKI_DETAIL_URL||!ps2wiki_valid_detail_url(fields[3],cat)||strlen(fields[4])>=PS2WIKI_DOWNLOAD_URL)continue;
  ps2wiki_entry_t *e=&cache[n++];memset(e,0,sizeof(*e));e->category=cat;
  snprintf(e->title,sizeof(e->title),"%s",fields[1]);
  snprintf(e->version,sizeof(e->version),"%s",fields[2]);
  snprintf(e->detail_url,sizeof(e->detail_url),"%s",fields[3]);
  if(ps2wiki_valid_filen_url(fields[4])||ps2wiki_valid_direct_psu(fields[4]))
   snprintf(e->download_url,sizeof(e->download_url),"%s",fields[4]);
 }
 fclose(f);
 for(int cat=0;cat<PS2WIKI_CATEGORY_COUNT;cat++){
  int count=0;for(int i=0;i<n;i++)if(cache[i].category==cat)g_catalogue_scratch[count++]=cache[i];
  /* catalogue_apply_category reuses scratch, so use the static cache subset
     as the input only after packing a separate category array. */
  if(count){static ps2wiki_entry_t subset[PS2WIKI_MAX_ITEMS];
   memcpy(subset,g_catalogue_scratch,(size_t)count*sizeof(subset[0]));catalogue_apply_category(cat,subset,count);}
 }
}
typedef struct {char *buf;size_t used,cap;int overflow;} catalogue_http_buffer_t;
static size_t catalogue_http_write(void *ptr,size_t size,size_t nmemb,void *ud){
 catalogue_http_buffer_t *b=(catalogue_http_buffer_t*)ud;
 if(size && nmemb>(size_t)-1/size)return 0;
 size_t n=size*nmemb;
 if(n>=b->cap-b->used){b->overflow=1;return 0;}
 memcpy(b->buf+b->used,ptr,n);b->used+=n;b->buf[b->used]=0;return n;
}
static int catalogue_http_get(const char *url,char **result){
 *result=NULL;
 if(strncmp(url,"https://ps2wiki.github.io/sas-apps-archive/",strlen("https://ps2wiki.github.io/sas-apps-archive/")))return -1;
 if(downloader_net_init()!=0)return -1;
 const size_t limit=384*1024;
 char *buf=(char*)malloc(limit);if(!buf)return -1;buf[0]=0;
 catalogue_http_buffer_t data={buf,0,limit,0};
 CURL *c=curl_easy_init();if(!c){free(buf);return -1;}
 curl_easy_setopt(c,CURLOPT_URL,url);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,catalogue_http_write);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,&data);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.50 (PS2Wiki catalogue)");
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,20L);
 curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 /* Existing PS3 libcurl bundle lacks a reliable CA store. Keep URLs strictly
    restricted to the PS2Wiki host, and never execute downloaded HTML. */
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,0L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L);
 CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
 curl_easy_cleanup(c);
 if(rc!=CURLE_OK||http!=200||data.overflow||data.used<64){free(buf);return -1;}
 *result=buf;return 0;
}
static void catalogue_fetch_category(int cat){
 g_catalogue_loaded_this_session[cat]=1;
 snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"Refreshing %s from PS2Wiki...",g_catalogue_category_names[cat]);g_ui_dirty=1;draw();
 char *html=NULL;
 if(catalogue_http_get(g_catalogue_category_pages[cat],&html)!=0){
  snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"PS2Wiki unreachable - showing cached listing");g_ui_dirty=1;return;
 }
 /* Use a dedicated array because catalogue_apply_category uses scratch. */
 static ps2wiki_entry_t parsed[PS2WIKI_MAX_ITEMS];
 int n=ps2wiki_parse_category(html,cat,parsed,PS2WIKI_MAX_ITEMS);free(html);
 if(n<0){snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"PS2Wiki listing could not be parsed - cached listing kept");g_ui_dirty=1;return;}
 catalogue_apply_category(cat,parsed,n);catalogue_save_cache();
 if(g_catalogue_category==cat){g_catalogue_selected=0;g_catalogue_top_index=0;}
 if(n)snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"Live PS2Wiki listing updated: %d entries",n);
 else snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"No links on this page; category hidden");
 g_ui_dirty=1;
}
static int catalogue_step_populated_category(int direction){
 int start=g_catalogue_category;
 for(int step=1;step<PS2WIKI_CATEGORY_COUNT;step++){
  int candidate=(start+direction*step)%PS2WIKI_CATEGORY_COUNT;
  if(candidate<0)candidate+=PS2WIKI_CATEGORY_COUNT;
  if(!g_catalogue_loaded_this_session[candidate])catalogue_fetch_category(candidate);
  if(catalogue_count(candidate)>0){
   g_catalogue_category=candidate;g_catalogue_selected=0;g_catalogue_top_index=0;g_ui_dirty=1;return 1;
  }
 }
 g_catalogue_category=start;g_catalogue_selected=0;g_catalogue_top_index=0;
 snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"No linked categories are available");g_ui_dirty=1;return 0;
}
static void catalogue_open_first_populated_category(){
 catalogue_fetch_category(g_catalogue_category);
 if(catalogue_count(g_catalogue_category)==0)catalogue_step_populated_category(1);
}
static int catalogue_resolve_selected(){
 int id=catalogue_selected_id();if(id<0)return 0;
 if(!ps2wiki_valid_detail_url(g_catalogue_items[id].detail_url,g_catalogue_category)){
  catalogue_fetch_category(g_catalogue_category);id=catalogue_selected_id();
  if(id<0||!ps2wiki_valid_detail_url(g_catalogue_items[id].detail_url,g_catalogue_category))return 0;
 }
 snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"Checking current PS2Wiki download link...");g_ui_dirty=1;draw();
 char *html=NULL;
 if(catalogue_http_get(g_catalogue_items[id].detail_url,&html)!=0){
  snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"Detail page unavailable - cached link retained");g_ui_dirty=1;return g_catalogue_items[id].download_url[0]!=0;
 }
 char url[PS2WIKI_DOWNLOAD_URL];int type=ps2wiki_parse_detail(html,url,sizeof(url));free(html);
 if(type==0){g_catalogue_items[id].download_url[0]=0;catalogue_save_cache();
  snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"No supported download link found on detail page");g_ui_dirty=1;return 0;}
 snprintf(g_catalogue_items[id].download_url,sizeof(g_catalogue_items[id].download_url),"%s",url);
 catalogue_save_cache();
 snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),type==2?"Current Filen share resolved (encrypted)":"Current direct PSU link resolved");g_ui_dirty=1;
 return type;
}
static void draw_psu_catalogue(){
 if(!g_catalogue_active)return;
 /* Phase 5P: calculate the footer and controller legend *before* sizing the
    list.  Phase 5O used a fixed 160px footer at 1080p even though three
    help lines plus a two-line legend need more than 200px.  Its third help
    line consequently occupied the same pixels as the legend. */
 int selected_id=catalogue_selected_id();
 int selected_filen=selected_id>=0&&ps2wiki_valid_filen_url(g_catalogue_items[selected_id].download_url);
 int selected_direct=selected_id>=0&&ps2wiki_valid_direct_psu(g_catalogue_items[selected_id].download_url);
 catalogue_action_t action=catalogue_action_for_state(selected_id>=0&&(selected_filen||selected_direct),selected_id>=0&&(selected_filen||selected_direct)?1:0,selected_filen||selected_direct);
 const legend_entry_t controls[]={{"UP/DOWN","SELECT"},{"L1/R1","CATEGORY"},{"X",catalogue_action_label(action)},{"SQUARE","REFRESH"},{"O","BACK"}};
 int w=360;
 int category_count=catalogue_count(g_catalogue_category);
 const char *head="PS2WIKI - PSU CATALOGUE";int z=text_width(head,g_title_scale)+48;if(z>w)w=z;
 char measure_title[160];
 if(!category_count)snprintf(measure_title,sizeof(measure_title),"NO LINKED CATEGORIES - L1/R1 TO SCAN");
 else snprintf(measure_title,sizeof(measure_title),"%s (%d ITEMS) - 1-%d SHOWN - L1/R1 CATEGORY",g_catalogue_category_names[g_catalogue_category],category_count,category_count);
 z=text_width(measure_title,g_body_scale)+48;if(z>w)w=z;
 for(int i=0;i<category_count;i++){int id=catalogue_entry_id(g_catalogue_category,i);if(id<0)continue;char row[220];int filen=ps2wiki_valid_filen_url(g_catalogue_items[id].download_url),direct=ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url);snprintf(row,sizeof(row),"%s  %s%s",g_catalogue_items[id].title,g_catalogue_items[id].version,filen?"  FILEN":direct?"  DIRECT":"");z=text_width(row,g_body_scale)+48;if(z>w)w=z;}
 const char *measure_lines[]={"Direct PSU link found - X downloads","Filen link found - X downloads and decrypts","X retrieves the latest download link from PS2Wiki","Source: PS2Wiki archive / System Apps","No linked categories are available"};
 for(unsigned i=0;i<sizeof(measure_lines)/sizeof(measure_lines[0]);i++){z=text_width(measure_lines[i],g_body_scale)+48;if(z>w)w=z;}
 z=text_width(g_catalogue_notice,g_help_scale)+48;if(z>w)w=z;
 for(int i=0;i<5;i++){z=token_width_for(controls[i].token,g_help_scale)+8+legend_text_width(controls[i].action,g_help_scale)+60;if(z>w)w=z;}
 w=ui_compact_panel_width((int)g_width,w,1080,16);
 int x=((int)g_width-w)/2;
 int count=category_count;
 int compact=(g_height<400);
 int rowh=compact?22:(g_height>=720?36:25);
 int header=compact?60:(g_height>=720?108:74);
 int info_lines=compact?1:3;
 int info_step=scale125(8*g_help_scale)+8;
 int legend_lines=measure_control_legend_lines(controls,5,w-36,g_help_scale);
 int legend_h=legend_lines*legend_line_height(g_help_scale);
 int info_top_pad=compact?8:12,legend_gap=compact?8:14,bottom_pad=compact?8:18;
 int footer=info_top_pad+info_lines*info_step+legend_gap+legend_h+bottom_pad;
 int avail=(int)g_height-24-header-footer,row_content_w=w-48;
 for(int i=0;i<count;i++){int id=catalogue_entry_id(g_catalogue_category,i);if(id<0)continue;char row[220];int filen=ps2wiki_valid_filen_url(g_catalogue_items[id].download_url),direct=ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url);snprintf(row,sizeof(row),"%s  %s%s",g_catalogue_items[id].title,g_catalogue_items[id].version,filen?"  FILEN":direct?"  DIRECT":"");int rh=wrapped_text_advance(row,row_content_w,g_body_scale)+4;if(rh>rowh)rowh=rh;}
 int visible=avail/rowh;if(visible<1)visible=1;if(visible>count)visible=count;
 int h=header+visible*rowh+footer;
 int y=((int)g_height-h)/2;if(y<4)y=4;
 if(g_catalogue_selected<0)g_catalogue_selected=0;
 if(g_catalogue_selected>=count)g_catalogue_selected=count?count-1:0;
 if(g_catalogue_top_index>g_catalogue_selected)g_catalogue_top_index=g_catalogue_selected;
 if(g_catalogue_selected>=g_catalogue_top_index+visible)g_catalogue_top_index=g_catalogue_selected-visible+1;
 if(g_catalogue_top_index<0)g_catalogue_top_index=0;
 if(g_catalogue_top_index>count-visible)g_catalogue_top_index=count-visible;
 round_rect(x+7,y+9,w,h,10,0x50000000u);
 round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,compact?34:48,0xD0455B70u,0x70304456u);
 rect(x+4,y+6,5,h-12,0xFF4CC2FFu);
 text_centered_bounded(x+12,y+(compact?9:13),w-24,compact?22:32,head,0xFFFFFFFFu,g_title_scale);
 char title[112];
 if(!count)snprintf(title,sizeof(title),"NO LINKED CATEGORIES - L1/R1 TO SCAN");
 else if(compact)snprintf(title,sizeof(title),"%s  %d-%d/%d  L1/R1",g_catalogue_category_names[g_catalogue_category],g_catalogue_top_index+1,g_catalogue_top_index+visible,count);
 else if(visible<count)snprintf(title,sizeof(title),"%s (%d ITEMS) - %d-%d SHOWN - L1/R1 CATEGORY",g_catalogue_category_names[g_catalogue_category],count,g_catalogue_top_index+1,g_catalogue_top_index+visible);
 else snprintf(title,sizeof(title),"%s (%d ITEMS) - L1/R1 CATEGORY",g_catalogue_category_names[g_catalogue_category],count);
 text_centered_bounded(x+16,y+(compact?36:52),w-32,ui_text_cell_height(g_body_scale)+1,title,0xFFB8E8FFu,g_body_scale);
 int listy=y+header;
 for(int row=0;row<visible;row++){
  int item_index=g_catalogue_top_index+row;
  int id=catalogue_entry_id(g_catalogue_category,item_index);if(id<0)continue;
  int yy=listy+row*rowh;
  if(item_index==g_catalogue_selected)round_rect(x+14,yy-3,w-28,rowh-1,5,0xFF315978u);
  char line[220];int filen=ps2wiki_valid_filen_url(g_catalogue_items[id].download_url),direct=ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url);snprintf(line,sizeof(line),"%s  %s%s",g_catalogue_items[id].title,g_catalogue_items[id].version,filen?"  FILEN":direct?"  DIRECT":"");
  text_centered_bounded(x+24,yy+1,row_content_w,rowh-2,line,item_index==g_catalogue_selected?0xFFFFFFFFu:0xFFE1EBF2u,g_body_scale);
 }
 int fy=listy+visible*rowh+info_top_pad;
 rect(x+18,fy-7,w-36,1,0x665F8AA7u);
 int id=catalogue_selected_id();
 if(id>=0){
  int filen=ps2wiki_valid_filen_url(g_catalogue_items[id].download_url);
  int direct=ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url);
  const char *status=direct?"Direct PSU link found - X downloads":
   filen?"Filen link found - X downloads and decrypts":
         "X retrieves the latest download link from PS2Wiki";
  text_centered_bounded(x+20,fy,w-40,info_step,status,direct?0xFF9EF2B0u:0xFFFFD07Bu,compact?g_help_scale:g_body_scale);
  if(!compact){
  const char *source=g_catalogue_category==0?"Source: PS2Wiki archive / Games":
                     g_catalogue_category==1?"Source: PS2Wiki archive / System Apps":
                     g_catalogue_category==2?"Source: PS2Wiki archive / Apps":
                     g_catalogue_category==3?"Source: PS2Wiki archive / RTEs":
                     g_catalogue_category==4?"Source: PS2Wiki archive / UMCS":
                                              "Source: PS2Wiki archive / Other";
   text_centered_bounded(x+20,fy+info_step,w-40,info_step,source,0xFFBBD1E0u,g_help_scale);
   text_centered_bounded(x+20,fy+2*info_step,w-40,info_step,g_catalogue_notice,0xFFFFD07Bu,g_help_scale);
  }
 }else if(!compact){
  text_centered_bounded(x+20,fy,w-40,info_step,"No linked categories are available",0xFFFFD07Bu,g_body_scale);
  text_centered_bounded(x+20,fy+info_step,w-40,info_step,g_catalogue_notice,0xFFBBD1E0u,g_help_scale);
 }
 int controls_y=fy+info_lines*info_step+legend_gap;
 rect(x+18,controls_y-7,w-36,1,0x665F8AA7u);
 draw_control_legend_centered(controls,5,x+18,controls_y,w-36,g_help_scale);
}

/* Independent PS2 Homebrew Store catalogue. Cache records are tab separated and
   revalidated through the parser allowlist on every read. */
static ps2hb_entry_t g_homebrew_entries[PS2HB_MAX_ITEMS];
static int g_homebrew_entry_count=0,g_homebrew_loaded[8]={0};
static unsigned char g_homebrew_cached_counts[PS2HB_MAX_ITEMS]={0};
static int g_homebrew_cache_scan_pending=1;
static ps2hb_asset_t g_homebrew_assets[PS2HB_MAX_ASSETS];
static int g_homebrew_asset_count=0;
static char g_homebrew_asset_entry_url[384]="";
static int g_homebrew_cache_drop_category=-1;
static char g_homebrew_notice[160]="Press SQUARE to refresh";
static const char *g_homebrew_cache_path="/dev_hdd0/game/PSU000001/USRDIR/PS2_HOMEBREW_STORE_CACHE.txt";
static int homebrew_entry_id(int category,int index){for(int i=0;i<g_homebrew_entry_count;i++)if(g_homebrew_entries[i].category==category){if(index--==0)return i;}return -1;}
static int homebrew_category_count(int category){int n=0;for(int i=0;i<g_homebrew_entry_count;i++)if(g_homebrew_entries[i].category==category)n++;return n;}
static int homebrew_selected_entry_id(){return homebrew_entry_id(g_homebrew_category,g_homebrew_selected);}
static int homebrew_cached_asset_count(int entry_id){
 if(entry_id<0||entry_id>=g_homebrew_entry_count)return 0;
 if(!g_homebrew_cache_scan_pending)return g_homebrew_cached_counts[entry_id];
 memset(g_homebrew_cached_counts,0,sizeof(g_homebrew_cached_counts));
 FILE *f=fopen(g_homebrew_cache_path,"rb");if(!f){g_homebrew_cache_scan_pending=0;return 0;}
 char line[1800];
 while(fgets(line,sizeof(line),f)){
  if(line[0]!='A'||line[1]!='\t')continue;
  char *ctx=NULL;char *tag=strtok_r(line,"\t\r\n",&ctx);char *parent=strtok_r(NULL,"\t\r\n",&ctx);char *title=strtok_r(NULL,"\t\r\n",&ctx);char *kind=strtok_r(NULL,"\t\r\n",&ctx);char *label=strtok_r(NULL,"\t\r\n",&ctx);char *url=strtok_r(NULL,"\t\r\n",&ctx);
  if(!tag||!parent||!title||!kind||!label||!url||atoi(kind)!=ps2hb_asset_kind_from_url(url))continue;
  for(int i=0;i<g_homebrew_entry_count;i++)if(!strcmp(parent,g_homebrew_entries[i].detail_url)&&!strcmp(title,g_homebrew_entries[i].title)&&g_homebrew_cached_counts[i]<255)g_homebrew_cached_counts[i]++;
 }
 fclose(f);g_homebrew_cache_scan_pending=0;return g_homebrew_cached_counts[entry_id];
}
static void homebrew_sanitize_field(char *s){for(;*s;s++)if(*s=='\t'||*s=='\r'||*s=='\n')*s=' ';}
static void homebrew_catalogue_save_cache(){char tmp[PATHSZ];snprintf(tmp,sizeof(tmp),"%s.tmp",g_homebrew_cache_path);FILE *f=fopen(tmp,"wb");if(!f)return;fprintf(f,"# PS2 Homebrew Store cache v1\n");for(int i=0;i<g_homebrew_entry_count;i++){ps2hb_entry_t e=g_homebrew_entries[i];homebrew_sanitize_field(e.title);if(e.category<0||e.category>=8||!ps2hb_valid_entry_url(e.detail_url))continue;fprintf(f,"E\t%d\t%s\t%s\n",e.category,e.title,e.detail_url);}for(int i=0;i<g_homebrew_asset_count;i++){ps2hb_asset_t a=g_homebrew_assets[i];homebrew_sanitize_field(a.label);if(!ps2hb_asset_kind_from_url(a.url)||a.kind!=ps2hb_asset_kind_from_url(a.url))continue;homebrew_sanitize_field(a.title);fprintf(f,"A\t%s\t%s\t%d\t%s\t%s\n",g_homebrew_asset_entry_url,a.title,a.kind,a.label,a.url);}/* Preserve other previously cached entry asset rows after validating their URLs. */FILE *old=fopen(g_homebrew_cache_path,"rb");if(old){char line[1800];while(fgets(line,sizeof(line),old)){if(line[0]!='A'||line[1]!='\t')continue;char copy[1800];snprintf(copy,sizeof(copy),"%s",line);char *ctx=NULL;char *tag=strtok_r(copy,"\t\r\n",&ctx);char *entry=strtok_r(NULL,"\t\r\n",&ctx);char *entry_title=strtok_r(NULL,"\t\r\n",&ctx);char *kind=strtok_r(NULL,"\t\r\n",&ctx);char *label=strtok_r(NULL,"\t\r\n",&ctx);char *url=strtok_r(NULL,"\t\r\n",&ctx);int drop=0;if(g_homebrew_cache_drop_category>=0&&g_homebrew_cache_drop_category<8){char section[384];snprintf(section,sizeof(section),"https://ps2homebrewstore.com%s",ps2hb_categories[g_homebrew_cache_drop_category].path);drop=entry&&!strcmp(entry,section);}if(tag&&entry&&entry_title&&kind&&label&&url&&!drop&&(strcmp(entry,g_homebrew_asset_entry_url)||g_homebrew_asset_count==0||strcmp(entry_title,g_homebrew_assets[0].title))&&atoi(kind)==ps2hb_asset_kind_from_url(url))fprintf(f,"A\t%s\t%s\t%s\t%s\t%s\n",entry,entry_title,kind,label,url);}fclose(old);}if(fclose(f)!=0){unlink(tmp);return;}if(rename(tmp,g_homebrew_cache_path)!=0)unlink(tmp);g_homebrew_cache_scan_pending=1;}
static void homebrew_catalogue_load_cache(){g_homebrew_entry_count=0;g_homebrew_cache_scan_pending=1;memset(g_homebrew_cached_counts,0,sizeof(g_homebrew_cached_counts));memset(g_homebrew_loaded,0,sizeof(g_homebrew_loaded));FILE *f=fopen(g_homebrew_cache_path,"rb");if(!f)return;char line[1800];while(fgets(line,sizeof(line),f)){if(line[0]=='#')continue;char *ctx=NULL;char *tag=strtok_r(line,"\t\r\n",&ctx);if(!tag)continue;if(!strcmp(tag,"E")){char *cat=strtok_r(NULL,"\t\r\n",&ctx),*title=strtok_r(NULL,"\t\r\n",&ctx),*url=strtok_r(NULL,"\t\r\n",&ctx);if(!cat||!title||!url||strlen(cat)>1||cat[0]<'0'||cat[0]>'7'||!ps2hb_valid_entry_url(url)||g_homebrew_entry_count>=PS2HB_MAX_ITEMS||strlen(title)>=sizeof(g_homebrew_entries[0].title)||strlen(url)>=sizeof(g_homebrew_entries[0].detail_url))continue;ps2hb_entry_t *e=&g_homebrew_entries[g_homebrew_entry_count++];memset(e,0,sizeof(*e));e->category=atoi(cat);snprintf(e->title,sizeof(e->title),"%s",title);snprintf(e->detail_url,sizeof(e->detail_url),"%s",url);}}
 fclose(f);}
static int homebrew_http_get(const char *url,char **result){*result=NULL;if(!ps2hb_requires_verified_tls(url)||downloader_net_init()!=0)return -1;char resolved[PS2HB_URL_SIZE];if(store_resolve_url(url,resolved,sizeof(resolved))!=0)return -1;const size_t limit=384*1024;char *buf=(char*)malloc(limit);if(!buf)return -1;buf[0]=0;catalogue_http_buffer_t data={buf,0,limit,0};CURL *c=curl_easy_init();if(!c){free(buf);return -1;}curl_easy_setopt(c,CURLOPT_URL,resolved);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,catalogue_http_write);curl_easy_setopt(c,CURLOPT_WRITEDATA,&data);curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.50 (PS2 Homebrew Store)");curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);curl_easy_setopt(c,CURLOPT_TIMEOUT,25L);if(!downloader_apply_store_tls(c)){curl_easy_cleanup(c);free(buf);return -1;}CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);curl_easy_cleanup(c);if(rc!=CURLE_OK||http!=200||data.overflow||data.used<32){free(buf);return -1;}*result=buf;return 0;}
static void homebrew_catalogue_fetch_category(int category){
 if(category<0||category>=8)return;
 g_homebrew_loaded[category]=1;
 snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Loading %s from PS2 Homebrew Store...",ps2hb_categories[category].name);
 g_ui_dirty=1;draw();
 char url[256];snprintf(url,sizeof(url),"https://ps2homebrewstore.com%s",ps2hb_categories[category].path);
 char *html=NULL;
 if(homebrew_http_get(url,&html)!=0){snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),g_store_ca_bundle_missing?"SSL CA Cert not found or unreadable":"Store unavailable - showing cached entries");return;}
 static ps2hb_entry_t parsed[PS2HB_MAX_ITEMS];
 int n=ps2hb_parse_category(html,category,parsed,PS2HB_MAX_ITEMS);free(html);
 /* A successfully fetched page with no supported links is not a category. */
 static ps2hb_entry_t merged[PS2HB_MAX_ITEMS];int keep=0;
 for(int i=0;i<g_homebrew_entry_count;i++)if(g_homebrew_entries[i].category!=category){if(keep<PS2HB_MAX_ITEMS)merged[keep++]=g_homebrew_entries[i];}
 if(n+keep>PS2HB_MAX_ITEMS){snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Store listing is too large - cached entries retained");return;}
 if(n>0)memcpy(merged+keep,parsed,(size_t)n*sizeof(parsed[0]));
 memcpy(g_homebrew_entries,merged,(size_t)(n+keep)*sizeof(parsed[0]));g_homebrew_entry_count=n+keep;
 g_homebrew_asset_count=0;g_homebrew_cache_drop_category=category;homebrew_catalogue_save_cache();g_homebrew_cache_drop_category=-1;
 g_homebrew_selected=0;g_homebrew_top=0;
 if(n)snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Updated: %d store entries",n);
 else snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"No supported links; category hidden");
}
static int homebrew_step_populated_category(int direction){
 int start=g_homebrew_category;
 for(int step=1;step<8;step++){
  int candidate=(start+direction*step)%8;if(candidate<0)candidate+=8;
  if(!g_homebrew_loaded[candidate])homebrew_catalogue_fetch_category(candidate);
  if(homebrew_category_count(candidate)>0){g_homebrew_category=candidate;g_homebrew_selected=0;g_homebrew_top=0;g_homebrew_view=0;g_homebrew_asset_count=0;g_ui_dirty=1;return 1;}
 }
 g_homebrew_category=start;g_homebrew_selected=0;g_homebrew_top=0;
 snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"No categories with PSU or ZIP links found");g_ui_dirty=1;return 0;
}
static void homebrew_open_first_populated_category(){
 homebrew_catalogue_fetch_category(g_homebrew_category);
 if(homebrew_category_count(g_homebrew_category)==0)homebrew_step_populated_category(1);
}
static void homebrew_catalogue_fetch_assets(int entry_index){if(entry_index<0||entry_index>=g_homebrew_entry_count)return;ps2hb_entry_t *entry=&g_homebrew_entries[entry_index];g_homebrew_asset_count=0;snprintf(g_homebrew_asset_entry_url,sizeof(g_homebrew_asset_entry_url),"%s",entry->detail_url);/* First consult any validated asset records in the local cache. */FILE *f=fopen(g_homebrew_cache_path,"rb");if(f){char line[1800];while(fgets(line,sizeof(line),f)&&g_homebrew_asset_count<PS2HB_MAX_ASSETS){if(line[0]!='A'||line[1]!='\t')continue;char *ctx=NULL;char *tag=strtok_r(line,"\t\r\n",&ctx);char *parent=strtok_r(NULL,"\t\r\n",&ctx);char *parent_title=strtok_r(NULL,"\t\r\n",&ctx);char *kind=strtok_r(NULL,"\t\r\n",&ctx);char *label=strtok_r(NULL,"\t\r\n",&ctx);char *url=strtok_r(NULL,"\t\r\n",&ctx);if(tag&&parent&&parent_title&&kind&&label&&url&&!strcmp(parent,entry->detail_url)&&!strcmp(parent_title,entry->title)&&atoi(kind)==ps2hb_asset_kind_from_url(url)){ps2hb_asset_t *a=&g_homebrew_assets[g_homebrew_asset_count++];memset(a,0,sizeof(*a));a->kind=atoi(kind);snprintf(a->title,sizeof(a->title),"%s",entry->title);snprintf(a->label,sizeof(a->label),"%s",label);snprintf(a->url,sizeof(a->url),"%s",url);}}fclose(f);}if(g_homebrew_asset_count)return;snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Checking available PSU and ZIP files...");g_ui_dirty=1;draw();char *html=NULL;if(homebrew_http_get(entry->detail_url,&html)!=0){snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Item page unavailable; no cached files");return;}g_homebrew_asset_count=ps2hb_parse_entry(html,entry->title,g_homebrew_assets,PS2HB_MAX_ASSETS);free(html);if(g_homebrew_asset_count){snprintf(g_homebrew_asset_entry_url,sizeof(g_homebrew_asset_entry_url),"%s",entry->detail_url);homebrew_catalogue_save_cache();snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"%d supported download%s found",g_homebrew_asset_count,g_homebrew_asset_count==1?"":"s");}else snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"No PSU or ZIP downloads found on this item");}
static void homebrew_catalogue_download_selected(){int ai=g_homebrew_asset_selected;if(ai<0||ai>=g_homebrew_asset_count)return;if(!ps2hb_asset_kind_from_url(g_homebrew_assets[ai].url)){snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"Cached link rejected by URL validation");return;}char url[PS2HB_URL_SIZE];snprintf(url,sizeof(url),"%s",g_homebrew_assets[ai].url);g_homebrew_active=0;g_homebrew_validate_kind=download_policy_for_url(url);g_ui_dirty=1;download_url_to_fakeusb(url);g_homebrew_validate_kind=0;snprintf(g_homebrew_notice,sizeof(g_homebrew_notice),"%s",status_line);g_homebrew_active=1;invalidate_dir_cache(cwd);refresh_fs();refresh_fs_metadata_now();}


/* User-triggered, bounded HTTPS detail-page retrieval. Never guess links. */
static int saves_refresh_game_listing(const gfs_save_t *entry){
 if(!entry)return -1;
 unsigned gid=0,sid=0;
 if(!gfs_parse_save_page_ids(entry->page_url,&gid,&sid))return -1;
 char listing[384];
 snprintf(listing,sizeof(listing),"%s",entry->page_url);
 char *suffix=strstr(listing,"/saves/");
 if(suffix)* (suffix+6)=0;
 else {
  char *end=strpbrk(listing,"?#");if(end)*end=0;
 }
 if(downloader_net_init()!=0)return -2;
 return gfs_crawl_listing_https(listing,g_saves_index_path,entry->game,entry->region,GFS_MAX_ENTRIES);
}
static int saves_fetch_detail_page(const char *page_url,const char *game,const char *region){
 unsigned gid=0,sid=0;
 if(!gfs_parse_save_page_ids(page_url,&gid,&sid)||!sid)return -1;
 if(downloader_net_init()!=0)return -2;
 char *html=NULL;size_t length=0;
 if(gfs_curl_fetch(page_url,&html,&length,NULL)!=200)return -3;
 int merged=gfs_ingest_html(g_saves_index_path,html,page_url,game,region,GFS_MAX_ENTRIES);
 gfs_curl_release(html,NULL);
 return merged<0?-4:merged;
}

static void saves_load_index(){
 g_saves_count=0;FILE *f=fopen(g_saves_index_path,"rb");if(!f)return;
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return;}long size=ftell(f);
 if(size<=0||size>1600000L){fclose(f);return;}rewind(f);
 char *buf=(char*)malloc((size_t)size+1);if(!buf){fclose(f);return;}
 size_t n=fread(buf,1,(size_t)size,f);fclose(f);buf[n]=0;
 g_saves_count=gfs_parse_index(buf,g_saves_entries,GFS_MAX_ENTRIES);if(g_saves_count<0)g_saves_count=0;free(buf);
}
static const char *saves_bucket_label(int b){static const char *const labels[]={"0-9","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};return labels[b>=0&&b<27?b:0];}
static int saves_row(int row,char *label,size_t cap){
 int count=0;
 if(g_saves_level==0){int seen[27]={0},n=0;for(int i=0;i<g_saves_count;i++)seen[gfs_bucket(g_saves_entries[i].game)]=1;for(int b=0;b<27;b++)if(seen[b]&&n++==row){snprintf(label,cap,"%s",saves_bucket_label(b));return b;}return -1;}
 for(int i=0;i<g_saves_count;i++){
  gfs_save_t *e=&g_saves_entries[i];
  if(g_saves_level==1){
   if(gfs_bucket(e->game)!=g_saves_bucket)continue;
   int seen=0;for(int j=0;j<i;j++)if(gfs_bucket(g_saves_entries[j].game)==g_saves_bucket&&!strcmp(e->game,g_saves_entries[j].game)){seen=1;break;}
   if(seen)continue;
   if(count++==row){snprintf(label,cap,"%s",e->game);return i;}
  }else if(g_saves_level==2){
   if(g_saves_game_index<0||strcmp(e->game,g_saves_entries[g_saves_game_index].game))continue;
   int seen=0;for(int j=0;j<i;j++)if(!strcmp(e->game,g_saves_entries[j].game)&&!strcmp(e->region,g_saves_entries[j].region)){seen=1;break;}
   if(seen)continue;
   if(count++==row){snprintf(label,cap,"%s",e->region);return i;}
  }else{
   if(g_saves_region_index<0||strcmp(e->game,g_saves_entries[g_saves_region_index].game)||strcmp(e->region,g_saves_entries[g_saves_region_index].region))continue;
   if(count++==row){snprintf(label,cap,"[%s] %s%s",e->format,e->description,e->download_url[0]?"  (DOWNLOAD)":"  (PAGE ONLY)");return i;}
  }
 }
 return -1;
}
static int saves_row_count(){
 if(g_saves_level==0){int n=0,seen[27]={0};for(int i=0;i<g_saves_count;i++)seen[gfs_bucket(g_saves_entries[i].game)]=1;for(int b=0;b<27;b++)n+=seen[b];return n;}
 int n=0;
 for(int i=0;i<g_saves_count;i++){
  gfs_save_t *e=&g_saves_entries[i];
  if(g_saves_level==1){
   if(gfs_bucket(e->game)!=g_saves_bucket)continue;
   int duplicate=0;for(int j=0;j<i;j++)if(gfs_bucket(g_saves_entries[j].game)==g_saves_bucket&&!strcmp(e->game,g_saves_entries[j].game)){duplicate=1;break;}
   if(!duplicate)n++;
  }else if(g_saves_level==2){
   if(g_saves_game_index<0||strcmp(e->game,g_saves_entries[g_saves_game_index].game))continue;
   int duplicate=0;for(int j=0;j<i;j++)if(!strcmp(e->game,g_saves_entries[j].game)&&!strcmp(e->region,g_saves_entries[j].region)){duplicate=1;break;}
   if(!duplicate)n++;
  }else if(g_saves_region_index>=0&&!strcmp(e->game,g_saves_entries[g_saves_region_index].game)&&!strcmp(e->region,g_saves_entries[g_saves_region_index].region))n++;
 }
 return n;
}
static void draw_saves_catalogue(){
 if(!g_saves_active)return;
 int compact=g_height<400,rowh=compact?22:(g_height>=720?36:25),count=saves_row_count();
 int w=ui_compact_panel_width((int)g_width,700,1080,16),x=((int)g_width-w)/2;
 int header=compact?64:100,footer=compact?72:105,visible=((int)g_height-30-header-footer)/rowh;
 if(visible<1)visible=1;if(visible>count)visible=count;
 int h=header+(visible?visible:1)*rowh+footer,y=((int)g_height-h)/2;if(y<4)y=4;
 round_rect(x+7,y+9,w,h,10,0x50000000u);round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,compact?34:48,0xD0455B70u,0x70304456u);
 rect(x+4,y+6,5,h-12,0xFF4CC2FFu);
 text_centered_bounded(x+12,y+10,w-24,32,"PS2 SAVES - GAMEFAQS",0xFFFFFFFFu,g_title_scale);
 char subtitle[180];snprintf(subtitle,sizeof(subtitle),"%s - %d entries",g_saves_level==0?"0-9 / A-Z":g_saves_level==1?saves_bucket_label(g_saves_bucket):g_saves_level==2?"REGIONS":"SAVE FORMATS",count);
 text_centered_bounded(x+18,y+(compact?39:56),w-36,30,subtitle,0xFFB8E8FFu,g_body_scale);
 if(g_saves_selected>=count)g_saves_selected=count>0?count-1:0;
 if(g_saves_top>g_saves_selected)g_saves_top=g_saves_selected;
 if(g_saves_selected>=g_saves_top+visible)g_saves_top=g_saves_selected-visible+1;
 for(int i=0;i<visible;i++){
  int idx=g_saves_top+i,yy=y+header+i*rowh;char label[256]="";
  if(saves_row(idx,label,sizeof(label))<0)continue;
  if(idx==g_saves_selected)round_rect(x+14,yy-3,w-28,rowh-1,5,0xFF315978u);
  text_centered_bounded(x+24,yy+1,w-48,rowh-2,label,idx==g_saves_selected?0xFFFFFFFFu:0xFFE1EBF2u,g_body_scale);
 }
 int fy=y+header+(visible?visible:1)*rowh+7;
 text_centered_bounded(x+18,fy,w-36,24,g_saves_count?"X OPEN / DOWNLOAD    O BACK":"GameFAQs index missing/empty - separate from Game DB",0xFF9EF2B0u,g_help_scale);
 text_centered_bounded(x+18,fy+25,w-36,24,g_saves_level==2?"SQUARE SCAN GAME SAVES  |  X OPEN REGION":g_saves_level==3?"SQUARE REFRESH SAVE LINKS  |  X DOWNLOAD":"Direct file links only; saves are never converted automatically",0xFFBBD1E0u,g_help_scale);
}
static void draw_homebrew_catalogue(){
 if(!g_homebrew_active)return;
 int entry_id=g_homebrew_view?-1:homebrew_selected_entry_id();
 int cached_count=entry_id>=0?homebrew_cached_asset_count(entry_id):0;
 catalogue_action_t action=g_homebrew_view?CATALOGUE_ACTION_DOWNLOAD:catalogue_action_for_state(cached_count>0,cached_count,0);
 const legend_entry_t controls[]={{"UP/DOWN","SELECT"},{"L1/R1","CATEGORY"},{"X",catalogue_action_label(action)},{"SQUARE","REFRESH"},{"O",g_homebrew_view?"BACK TO ITEMS":"BACK"}};
 int compact=g_height<400,header=compact?60:(g_height>=720?108:74),info_lines=compact?1:3,info_step=scale125(8*g_help_scale)+8;
 int count=g_homebrew_view?g_homebrew_asset_count:homebrew_category_count(g_homebrew_category);
 const char *message=count==0&&!g_homebrew_view?"No categories with downloadable PSU or ZIP links":(g_homebrew_view?"Select a PSU or ZIP file to download":(compact?(action==CATALOGUE_ACTION_OPEN_FILES?"X opens file/build list":action==CATALOGUE_ACTION_DOWNLOAD?"X downloads file":"X checks item"):(action==CATALOGUE_ACTION_OPEN_FILES?"X opens the selected item’s file/build list":action==CATALOGUE_ACTION_DOWNLOAD?"X downloads the resolved file":"X checks for PSU / ZIP links")));
 int desired=320;const char *head="PS2 HOMEBREW STORE";int z=text_width(head,g_title_scale)+48;if(z>desired)desired=z;
 char measure_title[160];if(!count&&!g_homebrew_view)snprintf(measure_title,sizeof(measure_title),"NO LINKED CATEGORIES - L1/R1 TO SCAN");else snprintf(measure_title,sizeof(measure_title),"%s (%d) - L1/R1 CATEGORY%s",ps2hb_categories[g_homebrew_category].name,count,g_homebrew_view?" - FILES":"");z=text_width(measure_title,g_body_scale)+48;if(z>desired)desired=z;
 for(int i=0;i<count;i++){char row[220];if(g_homebrew_view)snprintf(row,sizeof(row),"%s  [%s]",g_homebrew_assets[i].label,g_homebrew_assets[i].kind==PS2HB_ASSET_PSU?"PSU":"ZIP");else{int id=homebrew_entry_id(g_homebrew_category,i);snprintf(row,sizeof(row),"%s",id>=0?g_homebrew_entries[id].title:"(missing entry)");}z=text_width(row,g_body_scale)+52;if(z>desired)desired=z;}
 const char *info="HTTPS only; ZIP archives are saved without extraction";
 if(!compact){z=text_width(info,g_help_scale)+48;if(z>desired)desired=z;z=text_width(g_homebrew_notice,g_help_scale)+48;if(z>desired)desired=z;}
 z=text_width(message,compact?g_help_scale:g_body_scale)+48;if(z>desired)desired=z;
 for(int i=0;i<5;i++){z=token_width_for(controls[i].token,g_help_scale)+8+legend_text_width(controls[i].action,g_help_scale)+60;if(z>desired)desired=z;}
 int w=ui_compact_panel_width((int)g_width,desired,1080,16);int x=((int)g_width-w)/2;
 int rowh=compact?22:(g_height>=720?36:25),contentw=w-52,maxrow=0;
 for(int i=0;i<count;i++){char row[220];if(g_homebrew_view)snprintf(row,sizeof(row),"%s  [%s]",g_homebrew_assets[i].label,g_homebrew_assets[i].kind==PS2HB_ASSET_PSU?"PSU":"ZIP");else{int id=homebrew_entry_id(g_homebrew_category,i);snprintf(row,sizeof(row),"%s",id>=0?g_homebrew_entries[id].title:"(missing entry)");}int advance=wrapped_text_advance(row,contentw,g_body_scale);if(advance>maxrow)maxrow=advance;}
 if(maxrow>rowh)rowh=maxrow+4;
 int legend_lines=measure_control_legend_lines(controls,5,w-36,g_help_scale),legend_h=legend_lines*legend_line_height(g_help_scale),top=compact?8:12,gap=compact?8:14,bottom=compact?8:18,footer=top+info_lines*info_step+gap+legend_h+bottom;
 int visible=(g_height-24-header-footer)/rowh;if(visible<1)visible=1;if(visible>count)visible=count;int h=header+(visible>0?visible:1)*rowh+footer,y=((int)g_height-h)/2;if(y<4)y=4;
 round_rect(x+7,y+9,w,h,10,0x50000000u);round_rect(x,y,w,h,10,0xF0172230u);gradient_rect_v(x+3,y+3,w-6,compact?34:48,0xD0455B70u,0x70304456u);rect(x+4,y+6,5,h-12,0xFF4CC2FFu);text_centered_bounded(x+12,y+(compact?9:13),w-24,compact?22:32,head,0xFFFFFFFFu,g_title_scale);
 char title[160];if(!count&&!g_homebrew_view)snprintf(title,sizeof(title),"NO LINKED CATEGORIES - L1/R1 TO SCAN");else snprintf(title,sizeof(title),"%s (%d) - L1/R1 CATEGORY%s",ps2hb_categories[g_homebrew_category].name,count,g_homebrew_view?" - FILES":"");text_centered_bounded(x+20,y+(compact?36:52),w-40,ui_text_cell_height(g_body_scale)+1,title,0xFFB8E8FFu,g_body_scale);
 int listy=y+header,selected=g_homebrew_view?g_homebrew_asset_selected:g_homebrew_selected,topi=g_homebrew_view?g_homebrew_asset_top:g_homebrew_top;if(selected<0)selected=0;if(selected>=count&&count>0)selected=count-1;if(topi>selected)topi=selected;if(selected>=topi+visible&&visible>0)topi=selected-visible+1;if(topi<0)topi=0;
 for(int row=0;row<visible;row++){int index=topi+row,yy=listy+row*rowh;if(index==selected)round_rect(x+14,yy-3,w-28,rowh-1,5,0xFF315978u);char linebuf[220];if(g_homebrew_view)snprintf(linebuf,sizeof(linebuf),"%s  [%s]",g_homebrew_assets[index].label,g_homebrew_assets[index].kind==PS2HB_ASSET_PSU?"PSU":"ZIP");else{int id=homebrew_entry_id(g_homebrew_category,index);snprintf(linebuf,sizeof(linebuf),"%s",id>=0?g_homebrew_entries[id].title:"(missing entry)");}text_centered_bounded(x+24,yy+1,w-52,rowh-2,linebuf,index==selected?0xFFFFFFFFu:0xFFE1EBF2u,g_body_scale);}
 int fy=listy+(visible>0?visible:1)*rowh+top;rect(x+18,fy-7,w-36,1,0x665F8AA7u);text_centered_bounded(x+20,fy,w-40,info_step,message,0xFF9EF2B0u,compact?g_help_scale:g_body_scale);if(!compact){text_centered_bounded(x+20,fy+info_step,w-40,info_step,info,0xFFBBD1E0u,g_help_scale);text_centered_bounded(x+20,fy+2*info_step,w-40,info_step,g_homebrew_notice,0xFFFFD07Bu,g_help_scale);}int cy=fy+info_lines*info_step+gap;rect(x+18,cy-7,w-36,1,0x665F8AA7u);draw_control_legend_centered(controls,5,x+18,cy,w-36,g_help_scale);
}

static void start_url_keyboard(){
 /* The downloader reuses the proven rename keyboard compositor.  It must
    capture the current completed Options frame before enabling rename mode,
    exactly like begin_rename() and begin_blank_psu_title().  Without this,
    draw_rename_modal_update() has no base framebuffer cache and the URL
    keyboard never becomes visible. */
 if(!capture_rename_base()){
  snprintf(status_line,sizeof(status_line),"Downloader keyboard unavailable: display cache allocation failed");
  return;
 }
 g_options_active=0;g_downloader_active=1;g_rename_active=1;g_keyboard_purpose=KB_DOWNLOAD_URL;g_rename_lowercase=1;
 snprintf(g_rename_text,sizeof(g_rename_text),"%s","http://ipv4.download.thinkbroadband.com/200MB.zip");g_rename_cursor=strlen(g_rename_text);g_rename_key=0;
 g_rename_restore_frames=0;
 g_rename_buffer_generation[0]=g_rename_buffer_generation[1]=0;
 g_rename_buffer_ready[0]=g_rename_buffer_ready[1]=0;
 g_rename_buffer_key[0]=g_rename_buffer_key[1]=-1;
 g_rename_buffer_text_generation[0]=g_rename_buffer_text_generation[1]=0;
 g_rename_text_generation++;rename_full_modal_changed();build_rename_composite();
 g_ui_dirty=0;g_list_dirty=0;
 snprintf(status_line,sizeof(status_line),"Enter HTTP/HTTPS or full Filen file link; START downloads");
}
static void catalogue_download_selected(){
 int id=catalogue_selected_id();if(id<0)return;
 int type=ps2wiki_valid_filen_url(g_catalogue_items[id].download_url)?2:
          ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url)?1:catalogue_resolve_selected();
 id=catalogue_selected_id();if(id<0)return;
 if(type!=2 && !ps2wiki_valid_filen_url(g_catalogue_items[id].download_url) && !ps2wiki_valid_direct_psu(g_catalogue_items[id].download_url))return;
 g_catalogue_active=0;g_catalogue_verify_psu=1;g_ui_dirty=1;
 download_url_to_fakeusb(g_catalogue_items[id].download_url);
 snprintf(g_catalogue_notice,sizeof(g_catalogue_notice),"%s",status_line);
 g_catalogue_verify_psu=0;g_catalogue_active=1;g_ui_dirty=1;
 invalidate_dir_cache(cwd);refresh_fs();refresh_fs_metadata_now();
}
static void draw_options_menu(){
 if(!g_options_active)return;
 const legend_entry_t controls[]={{"UP/DOWN","SELECT"},{"LEFT/RIGHT","CHANGE CATALOGUE"},{"X","CHANGE / OPEN"},{"O","CLOSE"}};
 char rows[6][128];snprintf(rows[0],sizeof(rows[0]),"FPS METER: %s",g_fps_enabled?"ENABLED":"DISABLED");snprintf(rows[1],sizeof(rows[1]),"SIMULTANEOUS DL CONNECTIONS: %d",g_download_connections);snprintf(rows[2],sizeof(rows[2]),"FILE DOWNLOADER - DIRECT URL");snprintf(rows[3],sizeof(rows[3]),"PSU CATALOGUE: %s",g_psu_catalogue_source==0?"PS2WIKI":g_psu_catalogue_source==1?"PS2 HOMEBREW STORE":"GAMEFAQS SAVES");snprintf(rows[4],sizeof(rows[4]),"UPDATE PS2 GAME DATABASE (ONLINE)");snprintf(rows[5],sizeof(rows[5]),"EXIT TO XMB");
 int desired=300;for(int i=0;i<6;i++){int z=text_width(rows[i],g_body_scale)+64;if(z>desired)desired=z;}for(int i=0;i<4;i++){int z=token_width_for(controls[i].token,g_help_scale)+legend_text_width(controls[i].action,g_help_scale)+56;if(z>desired)desired=z;}
 int w=ui_compact_panel_width((int)g_width,desired,720,16);
 int contentw=w-64,rows_h=0,row_heights[6];
 for(int i=0;i<6;i++){row_heights[i]=wrapped_text_advance(rows[i],contentw,g_body_scale);if(row_heights[i]<ui_text_line_pitch(g_body_scale))row_heights[i]=ui_text_line_pitch(g_body_scale);rows_h+=row_heights[i]+3;}
 int control_lines=measure_control_legend_lines(controls,4,w-44,g_help_scale),controls_h=control_lines*legend_line_height(g_help_scale);
 int h=56+rows_h+8+controls_h+18;if(h>(int)g_height-8)h=(int)g_height-8;if(h<80)h=80;int x=((int)g_width-w)/2,y=((int)g_height-h)/2;if(y<4)y=4;
 round_rect(x+7,y+9,w,h,10,0x50000000u);
 round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,48,0xD0455B70u,0x70304456u);
 rect(x+8,y+51,w-16,1,0xA05D7892u); rect(x+4,y+6,5,h-12,0xFF4CC2FFu);
 text(x+22,y+18,"OPTIONS",0xFFFFFFFFu,g_title_scale);
 int ry=y+60;
 for(int i=0;i<6;i++){
  int rowh=row_heights[i]+3;
  if(i==g_options_selected)round_rect(x+18,ry-3,w-36,rowh,5,0xFF315978u);
  text_bounded(x+32,ry,contentw,row_heights[i],rows[i],0xFFFFFFFFu,g_body_scale);
  ry+=rowh;
 }
 draw_control_legend(controls,4,x+22,y+h-controls_h-18,w-44,g_help_scale);
}
static void draw_delete_confirmation(){
 if(!g_delete_confirm_active)return;
 const char *kind=g_delete_confirm_archive?"PSU entry":(g_delete_confirm_is_dir?"folder":"file");
 char linebuf[320];snprintf(linebuf,sizeof(linebuf),"Delete %s: %s",kind,g_delete_confirm_name);
 const char *warning=(g_delete_confirm_is_dir&&!g_delete_confirm_archive)?"WARNING: folder contents will also be deleted":"This action cannot be undone";
 const legend_entry_t delete_controls[]={{"X","DELETE"},{"O","CANCEL"}};
 int desired=text_width("CONFIRM DELETE",g_title_scale)+64;
 int controls_w=token_width_for("X",g_body_scale)+legend_text_width("DELETE",g_body_scale)+32+
                token_width_for("O",g_body_scale)+legend_text_width("CANCEL",g_body_scale)+32+44;
 if(controls_w>desired)desired=controls_w;
 if(desired<460)desired=460;
 int w=ui_compact_panel_width((int)g_width,desired,760,24);
 int contentw=w-44;
 int body_h=wrapped_text_advance(linebuf,contentw,g_body_scale);
 int warning_h=wrapped_text_advance(warning,contentw,g_body_scale);
 int footer_lines=measure_control_legend_lines(delete_controls,2,w-44,g_body_scale);
 int footer_h=footer_lines*legend_line_height(g_body_scale);
 int h=ui_delete_confirmation_height(body_h,warning_h,footer_lines,legend_line_height(g_body_scale));
 if(h>(int)g_height-24)h=(int)g_height-24;
 int x=((int)g_width-w)/2,y=((int)g_height-h)/2;
 round_rect(x+7,y+9,w,h,10,0x50000000u);
 round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,48,0xD0544B45u,0x70352F31u);
 rect(x+8,y+51,w-16,1,0xA06F625Au);
 rect(x+4,y+6,5,h-12,0xFFE2A84Bu);
 text(x+22,y+20,"CONFIRM DELETE",0xFFFFD27Au,g_title_scale);
 text_bounded(x+22,y+64,contentw,body_h,linebuf,0xFFFFFFFFu,g_body_scale);
 int warning_y=y+64+body_h+8;
 text_bounded(x+22,warning_y,contentw,warning_h,warning,
              (g_delete_confirm_is_dir&&!g_delete_confirm_archive)?0xFFFF9A9Au:0xFFFFB0B0u,g_body_scale);
 draw_control_legend(delete_controls,2,x+22,y+h-footer_h-18,w-44,g_body_scale);
}
static void draw_zip_overwrite_confirmation(){
 if(!g_zip_confirm_active)return;
 int w=(int)g_width*2/3;if(w>780)w=780;if(w<480)w=(int)g_width-60;
 int h=218,x=((int)g_width-w)/2,y=((int)g_height-h)/2;
 round_rect(x+7,y+9,w,h,10,0x50000000u);
 round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,48,0xD0544B45u,0x70352F31u);
 rect(x+8,y+51,w-16,1,0xA06F625Au);
 rect(x+4,y+6,5,h-12,0xFFE2A84Bu);
 text(x+22,y+20,"REPLACE EXISTING ZIP FILES?",0xFFFFD27Au,g_title_scale);
 char linebuf[128];
 snprintf(linebuf,sizeof(linebuf),"%u file%s already exist in this folder.",g_zip_confirm_conflicts,g_zip_confirm_conflicts==1?"":"s");
 text(x+22,y+68,linebuf,0xFFFFFFFFu,g_body_scale);
 text_bounded(x+22,y+98,w-44,42,g_zip_confirm_destination,0xFFCBD8E2u,g_body_scale);
 const legend_entry_t controls[]={{"X","REPLACE FILES"},{"O","CANCEL"}};
 draw_control_legend(controls,2,x+22,y+h-54,w-44,g_body_scale);
}

static void draw_apollo_warning(){
 if(!g_apollo_warning_active)return;
 int w=(int)g_width*3/4;if(w>860)w=860;if(w<520)w=(int)g_width-50;
 int h=250,x=((int)g_width-w)/2,y=((int)g_height-h)/2;
 round_rect(x+7,y+9,w,h,10,0x50000000u);
 round_rect(x,y,w,h,10,0xF0172230u);
 gradient_rect_v(x+3,y+3,w-6,48,0xD0455B70u,0x70304456u);
 rect(x+8,y+51,w-16,1,0xA05D7892u);
 rect(x+4,y+6,5,h-12,0xFF4CC2FFu);
 text(x+22,y+18,g_apollo_warning_auto_failed?"APOLLO FOLDER AUTO-RECREATE FAILED":"APOLLO SAVE TOOL FOLDER NOT FOUND",0xFFFFD27Au,g_title_scale);
 text(x+22,y+66,"Missing: /dev_hdd0/tmp/fakeusb/PS2/SAVEDATA",0xFFFFFFFFu,g_body_scale);
 if(g_apollo_warning_auto_failed)
  text(x+22,y+96,"Automatic recreation failed. Retry, or use /dev_hdd0 for now.",0xFFFFB0B0u,g_body_scale);
 else
  text(x+22,y+96,"Create it in Apollo Save Tool, or allow this app to create it.",0xFFE7EEF5u,g_body_scale);
 text(x+22,y+126,"This app creates the folder only when you explicitly choose X.",0xFFBAC8D4u,g_body_scale);
 text(x+22,y+154,"If RPCS3 clears /dev_hdd0/tmp, the warning will appear again next launch.",0xFFBAC8D4u,g_body_scale);
 const legend_entry_t apollo_controls[]={{"X","CREATE / RETRY"},{"O","USE /dev_hdd0"}};
 draw_control_legend(apollo_controls,2,x+22,y+h-58,w-44,g_body_scale);
}
static int create_apollo_savedata_tree(){
 const char *paths[]={"/dev_hdd0/tmp/fakeusb","/dev_hdd0/tmp/fakeusb/PS2","/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA"};
 for(int i=0;i<3;i++){if(path_exists_dir(paths[i]))continue;if(mkdir(paths[i],0777)!=0&&!path_exists_dir(paths[i]))return -1;}
 return path_exists_dir(g_apollo_savedata_path)?0:-1;
}
static void confirm_apollo_create(){
 if(create_apollo_savedata_tree()==0){
  g_apollo_warning_active=0;g_apollo_warning_auto_failed=0;snprintf(cwd,sizeof(cwd),"%s",g_apollo_savedata_path);selected=top_index=0;invalidate_dir_cache(cwd);reset_navigation_render_state();refresh_fs();
  snprintf(status_line,sizeof(status_line),"Created Apollo fakeusb PS2 SAVEDATA folder");
 }else{
  g_apollo_warning_active=1;g_apollo_warning_auto_failed=1;snprintf(cwd,sizeof(cwd),"/dev_hdd0");selected=top_index=0;reset_navigation_render_state();refresh_fs();snprintf(status_line,sizeof(status_line),"Could not create Apollo folder; retry or use /dev_hdd0");
 }
 g_ui_dirty=1;
}
static void cancel_apollo_warning(){g_apollo_warning_active=0;g_apollo_warning_auto_failed=0;snprintf(cwd,sizeof(cwd),"/dev_hdd0");selected=top_index=0;refresh_fs();snprintf(status_line,sizeof(status_line),"Apollo folder not created; using /dev_hdd0");g_ui_dirty=1;}

/* Decoded non-PSU inputs are read-only PSU-shaped temporary views. */
static void close_foreign_view(){
 if(g_foreign_view_path[0])unlink(g_foreign_view_path);
 g_foreign_view_path[0]=0;g_foreign_source_path[0]=0;g_foreign_archive_view=0;
}
static int open_foreign_archive(const char *input,int is_max){
 FILE *f=NULL; unsigned char *data=NULL;long sz;int rc=-1;
 char tmp[PATHSZ];
 /* Keep transient MAX/CBS views inside the requested SAVEDATA/tmp folder. */
 const char *workdir="/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA/tmp";
 if(create_apollo_savedata_tree()!=0)return -19;
 if(!path_exists_dir(workdir) && mkdir(workdir,0700)!=0 && !path_exists_dir(workdir))return -20;
 snprintf(tmp,sizeof(tmp),"%s/ps3psu_view_%08lx.psu",workdir,(unsigned long)time(NULL));
 /* A collision must not overwrite an existing temporary or user file. */
 if(access(tmp,F_OK)==0)return -8;
 f=fopen(input,"rb");if(!f)return -1;
 if(fseek(f,0,SEEK_END)|| (sz=ftell(f))<=0 || sz>20*1024*1024 || fseek(f,0,SEEK_SET)){rc=-2;goto done;}
 data=(unsigned char*)malloc((size_t)sz);if(!data){rc=-3;goto done;}
 if(fread(data,1,(size_t)sz,f)!=(size_t)sz){rc=-4;goto done;}
 fclose(f);f=NULL;
 rc=is_max?ps2_max_convert_to_psu(data,(size_t)sz,tmp):ps2_cbs_convert_to_psu(data,(size_t)sz,tmp);
 if(rc)goto done;
 if(psu_list(tmp,NULL,NULL)!=0){unlink(tmp);rc=-5;goto done;}
 close_foreign_view();
 snprintf(g_foreign_view_path,sizeof(g_foreign_view_path),"%s",tmp);
 snprintf(g_foreign_source_path,sizeof(g_foreign_source_path),"%s",input);
 snprintf(archive_path,sizeof(archive_path),"%s",tmp);
 g_foreign_archive_view=1;
 clear_icn_cache();free_preview();g_icon_preload_archive[0]=0;g_icon_preload_cursor=g_icon_preload_count=0;archive_mode=1;selected=top_index=0;
 refresh_psu();export_target_active=(item_count>0);ensure_preview_current();
 rc=0;
 done:if(f)fclose(f);free(data);return rc;
}
#include "ps2_xpo_v1.h"
/* Stage49: use the same read-only PSU-shaped viewer as MAX/CBS. */
static int open_sharkport_archive(const char *input){
 FILE *f=NULL;unsigned char *data=NULL;long sz;int rc=-1;char tmp[PATHSZ];
 const char *workdir="/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA/tmp";
 if(create_apollo_savedata_tree()!=0)return -19;
 if(!path_exists_dir(workdir)&&mkdir(workdir,0700)!=0&&!path_exists_dir(workdir))return -20;
 snprintf(tmp,sizeof(tmp),"%s/ps3psu_shark_%08lx.psu",workdir,(unsigned long)time(NULL));
 if(access(tmp,F_OK)==0)return -8;
 f=fopen(input,"rb");if(!f)return -1;
 if(fseek(f,0,SEEK_END)||(sz=ftell(f))<=0||sz>20*1024*1024||fseek(f,0,SEEK_SET)){rc=-2;goto done;}
 data=(unsigned char*)malloc((size_t)sz);if(!data){rc=-3;goto done;}
 if(fread(data,1,(size_t)sz,f)!=(size_t)sz){rc=-4;goto done;}
 fclose(f);f=NULL;
 rc=ps2_sharkport_convert_to_psu(data,(size_t)sz,tmp);if(rc)goto done;
 if(psu_list(tmp,NULL,NULL)!=0){unlink(tmp);rc=-5;goto done;}
 close_foreign_view();
 snprintf(g_foreign_view_path,sizeof(g_foreign_view_path),"%s",tmp);
 snprintf(g_foreign_source_path,sizeof(g_foreign_source_path),"%s",input);
 snprintf(archive_path,sizeof(archive_path),"%s",tmp);
 g_foreign_archive_view=1;
 clear_icn_cache();free_preview();g_icon_preload_archive[0]=0;g_icon_preload_cursor=g_icon_preload_count=0;
 archive_mode=1;selected=top_index=0;refresh_psu();export_target_active=(item_count>0);ensure_preview_current();rc=0;
 done:if(f)fclose(f);free(data);return rc;
}
/* Stage 41: safe, read-only SPS/XPS identification until file-record decoder is verified. */
static int inspect_sharkport_archive(const char *path,char *title,size_t titlecap,unsigned *candidates,int *chain,int *icon_valid,char *files,size_t filescap){
 FILE *f=fopen(path,"rb");unsigned char *raw=NULL;long sz;int rc=-1;
 ps2_sharkport_header hdr;
 if(!f)return -1;
 if(fseek(f,0,SEEK_END)||(sz=ftell(f))<33||sz>20*1024*1024||fseek(f,0,SEEK_SET))goto done;
 raw=(unsigned char*)malloc((size_t)sz);if(!raw)goto done;
 if(fread(raw,1,(size_t)sz,f)!=(size_t)sz)goto done;
 if(ps2_sharkport_read_header(raw,(size_t)sz,&hdr))goto done;
 if(candidates)*candidates=hdr.candidate_file_count;
 if(chain)*chain=hdr.record_chain_valid;
 if(icon_valid)*icon_valid=hdr.icon_sys_valid;
 if(files && filescap){
  size_t used=0;unsigned i;files[0]=0;
  for(i=0;i<hdr.candidate_file_count && i<16;i++){
   int nwrite=snprintf(files+used,filescap-used,"%s%s (%u B)",i?", ":"",hdr.candidate_files[i],hdr.candidate_sizes[i]);
   if(nwrite<0 || (size_t)nwrite>=filescap-used)break;
   used+=(size_t)nwrite;
  }
 }
 snprintf(title,titlecap,"%s",hdr.title);rc=0;
 done:free(raw);fclose(f);return rc;
}
static void do_cross(){if(item_count<=0||archive_mode)return;char p[PATHSZ];join(p,sizeof(p),cwd,items[selected].name);
 if(!items[selected].is_dir && (endswith(items[selected].name,".sps") || endswith(items[selected].name,".xps") || endswith(items[selected].name,".xpo"))){
  if(endswith(items[selected].name,".xpo")){
   FILE *xf=fopen(p,"rb");ps2_xpo_v1_info *info=(ps2_xpo_v1_info*)malloc(sizeof(ps2_xpo_v1_info));int probe=-1, exported=0, err=0;
   unsigned char *xb=NULL;long xn=0;
   if(xf && info && !fseek(xf,0,SEEK_END)){
    xn=ftell(xf);
    if(xn>0 && xn<=20*1024*1024 && !fseek(xf,0,SEEK_SET)){
     xb=(unsigned char*)malloc((size_t)xn);
     if(xb && fread(xb,1,(size_t)xn,xf)==(size_t)xn)probe=ps2_xpo_v1_probe(xb,(size_t)xn,info);
    }
   }
   if(xf)fclose(xf);
   if(!probe && xb){
    /* Raw record export only: NEVER represent these records as PS2 memory-card files. */
    char outdir[PATHSZ];
    int n=snprintf(outdir,sizeof(outdir),"%s.xpo_records",p);
    if(n<=0 || (size_t)n>=sizeof(outdir))err=-10;
    else if(mkdir(outdir,0700)!=0 && !path_exists_dir(outdir))err=-11;
    if(!err){
     for(unsigned i=0;i<info->record_count;i++){
      char output[PATHSZ];
      int m=snprintf(output,sizeof(output),"%s/record_%03u.bin",outdir,i);
      if(m<=0 || (size_t)m>=sizeof(output)){err=-12;break;}
      int result=ps2_xpo_v1_export_record(xb,(size_t)xn,info,i,output);
      if(result){err=result;break;}
      exported++;
     }
    }
    if(!err)snprintf(status_line,sizeof(status_line),"X-Port v1: exported %d raw records to .xpo_records folder (NOT PSU)",exported);
    else snprintf(status_line,sizeof(status_line),"X-Port v1: exported %d records; write error %d",exported,err);
    invalidate_dir_cache(cwd);refresh_fs();refresh_fs_metadata_now();
   }else snprintf(status_line,sizeof(status_line),"X-Port v1: invalid structure or memory error (%d)",probe);
   free(xb);free(info);g_ui_dirty=1;return;
  }
  int rc=open_sharkport_archive(p);
  if(rc)snprintf(status_line,sizeof(status_line),"X-Port/SharkPort: unsupported or invalid archive (%d)",rc);
  else snprintf(status_line,sizeof(status_line),"SharkPort/X-Port: read-only save view");
  g_ui_dirty=1;return;
 }
 if(!items[selected].is_dir && (endswith(items[selected].name,".max") || endswith(items[selected].name,".cbs"))){
  int is_max=endswith(items[selected].name,".max");
  int rc=open_foreign_archive(p,is_max);
  snprintf(status_line,sizeof(status_line),rc?"%s decode failed (%d)":"%s opened read-only",is_max?"MAX":"CBS",rc);
  g_ui_dirty=1;return;
 }
 if(!items[selected].is_dir&&endswith(items[selected].name,".part")){char url[1024];if(downloader_load_resume_url(p,url,sizeof(url))==0){snprintf(status_line,sizeof(status_line),"Resuming %s",items[selected].name);download_url_to_fakeusb(url);invalidate_dir_cache(cwd);refresh_fs();refresh_fs_metadata_now();g_folder_dirty=1;g_ui_dirty=1;}else{g_options_active=1;g_options_selected=2;start_url_keyboard();snprintf(status_line,sizeof(status_line),"Enter original URL to resume selected .part file");}return;}
 if(items[selected].is_dir){strncpy(cwd,p,sizeof(cwd)-1);cwd[sizeof(cwd)-1]=0;selected=top_index=0;refresh_fs();g_folder_dirty=1;g_ui_dirty=0;}else if(endswith(items[selected].name,".psu")){strncpy(archive_path,p,sizeof(archive_path)-1);archive_path[sizeof(archive_path)-1]=0;clear_icn_cache();free_preview();g_icon_preload_archive[0]=0;g_icon_preload_cursor=g_icon_preload_count=0;archive_mode=1;selected=top_index=0;refresh_psu();export_target_active=(item_count>0);ensure_preview_current();}}
static void do_circle(){
 if(archive_mode){archive_mode=0;close_foreign_view();archive_path[0]=0;export_target_active=0;selected=top_index=0;refresh_fs();return;}
 /* Apollo SAVEDATA is normally the protected HDD logical root. If it is truly empty,
    allow the user to back out to /dev_hdd0 so they are not trapped in an empty fakeusb tree. */
 if(!strcmp(cwd,g_apollo_savedata_path)){
  if(item_count==0){
   snprintf(cwd,sizeof(cwd),"/dev_hdd0");selected=top_index=0;refresh_fs();g_folder_dirty=1;g_ui_dirty=0;
   snprintf(status_line,sizeof(status_line),"Apollo SAVEDATA empty; returned to /dev_hdd0");
  }
  return;
 }
 if(is_mount_root(cwd))return;
 parent_dir();selected=top_index=0;refresh_fs();g_folder_dirty=1;g_ui_dirty=0;
}
static int collect_devices(char devs[][32],int max){int n=0;for(int i=0;i<2&&n<max;i++){char p[32];snprintf(p,sizeof(p),"/dev_hdd%d",i);if(path_exists_dir(p))strcpy(devs[n++],p);}for(int i=0;i<8&&n<max;i++){char p[32];snprintf(p,sizeof(p),"/dev_usb%03d",i);if(path_exists_dir(p))strcpy(devs[n++],p);}return n;}
static void switch_device(int dir){
 char devs[10][32];int n=collect_devices(devs,10);if(n<=0){strcpy(status_line,"No storage devices found");return;}
 char root[32]="";
 if(!strncmp(cwd,"/dev_hdd",8)&&strlen(cwd)>=9&&(cwd[8]=='0'||cwd[8]=='1')){memcpy(root,cwd,9);root[9]=0;}
 else if(!strncmp(cwd,"/dev_usb",8)&&strlen(cwd)>=11){memcpy(root,cwd,11);root[11]=0;}
 else strcpy(root,"/dev_hdd0");
 int cur=0;for(int i=0;i<n;i++)if(!strcmp(root,devs[i])){cur=i;break;}
 int next=(cur+dir+n)%n;
 /* Hard transition: do not carry archive/preview/list generations across devices. */
 free_preview();clear_icn_cache();close_foreign_view();archive_mode=0;export_target_active=0;archive_path[0]=0;
 g_model_buffer_generation[0]=g_model_buffer_generation[1]=0;
 g_ui_buffer_generation[0]=g_ui_buffer_generation[1]=0;g_ui_generation=0;
 g_preview_startup_hold=0;g_model_base_valid=0;g_preview_base_valid=0;
 g_list_cache_valid=0;g_list_panel_cache_valid=0;g_path_base_valid=0;
 g_list_prev_selected=-1;g_list_prev_top=-1;
 invalidate_all_dir_cache();
 set_device_default_path(devs[next]);
 selected=top_index=0;refresh_fs();
 g_device_switch_redraw_frames=2;g_ui_dirty=1;g_folder_dirty=0;g_list_dirty=0;
 snprintf(status_line,sizeof(status_line),"Device: %s",cwd);
}
static char g_xpo_candidate_pending[PATHSZ]={0};
static int g_xpo_icon_mode=0;
static const char *xpo_title_for_dir(const char *dir){
 static char resolved[512];
 static const char *paths[]={
  "/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.tsv",
  "/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA/PS2_GAME_TITLES.tsv",
  NULL
 };
 for(int i=0;paths[i];i++){
  if(ps2_game_title_lookup(paths[i],dir,resolved,sizeof(resolved))==1)return resolved;
 }
 /* Built-in emergency fallback when offline catalogue is not installed. */
 if(strstr(dir,"SCES-50294"))return "Gran Turismo 3: A-Spec";
 if(strstr(dir,"SLES-50219"))return "NBA Street";
 if(strstr(dir,"SLES-50072"))return "Street Fighter EX3";
 return NULL;
}
static void do_square(){if(item_count<=0)return;
 if(!archive_mode && !items[selected].is_dir && endswith(items[selected].name,".xpo")){
  char p[PATHSZ];join(p,sizeof(p),cwd,items[selected].name);
  if(strcmp(g_xpo_candidate_pending,p)!=0){snprintf(g_xpo_candidate_pending,sizeof(g_xpo_candidate_pending),"%s",p);g_xpo_icon_mode=0;snprintf(status_line,sizeof(status_line),"XPO->PSU: Square again for ORIGINAL; Triangle for generated title icon");g_ui_dirty=1;return;}
  g_xpo_candidate_pending[0]=0;
  int with_icons=g_xpo_icon_mode;g_xpo_icon_mode=0;
  FILE *f=fopen(p,"rb");unsigned char *b=NULL;ps2_xpo_v1_info *info=NULL;long n=0;int rc=-1;
  if(f && !fseek(f,0,SEEK_END)){n=ftell(f);if(n>0 && n<=20*1024*1024 && !fseek(f,0,SEEK_SET)){b=(unsigned char*)malloc((size_t)n);info=(ps2_xpo_v1_info*)malloc(sizeof(*info));if(b && info && fread(b,1,(size_t)n,f)==(size_t)n)rc=ps2_xpo_v1_probe(b,(size_t)n,info);}}
  if(f)fclose(f);
  if(rc==0){char out[PATHSZ];int k=snprintf(out,sizeof(out),"%s.experimental.psu",p);if(with_icons)k=snprintf(out,sizeof(out),"%s.titled.psu",p);
   if(k<=0 || (size_t)k>=sizeof(out))rc=-20;
   else if(with_icons){const char *title=xpo_title_for_dir(info->directory);rc=title?ps2_xpo_v1_write_psu_with_icons(b,(size_t)n,info,out,title,1):-21;}
   else rc=ps2_xpo_v1_write_candidate_psu(b,(size_t)n,info,out);}
  free(b);free(info);
  snprintf(status_line,sizeof(status_line),rc==0?"Experimental PSU written beside XPO (game compatibility NOT verified)":"Experimental XPO conversion failed (%d)",rc);
  invalidate_dir_cache(cwd);refresh_fs();g_ui_dirty=1;return;
 }
if(archive_mode){if(items[selected].is_dir)return;export_target_active=1;char out[PATHSZ];join(out,sizeof(out),cwd,items[selected].name);int rc=psu_extract_one(archive_path,items[selected].name,out);snprintf(status_line,sizeof(status_line),rc?"Export failed: %s":"Exported %s",rc?psu_strerror(rc):items[selected].name);return;}if(items[selected].is_dir)return;char p[PATHSZ];join(p,sizeof(p),cwd,items[selected].name);strncpy(clipboard,p,sizeof(clipboard)-1);snprintf(status_line,sizeof(status_line),"Copied for inject: %s",items[selected].name);}
static void refresh_after_zip(){invalidate_dir_cache(cwd);refresh_fs();g_folder_dirty=1;g_ui_dirty=1;}
static void run_zip_extract(int mode){
 char zip[PATHSZ],dest[PATHSZ];snprintf(zip,sizeof(zip),"%s",g_zip_confirm_archive);snprintf(dest,sizeof(dest),"%s",g_zip_confirm_destination);
 g_zip_confirm_active=0;snprintf(status_line,sizeof(status_line),"Extracting ZIP files; please wait...");draw();
 zip_extract_result_t result;int rc=zip_extract_archive(zip,dest,mode,&result);
 if(rc==ZIP_EXTRACT_NEEDS_OVERWRITE){g_zip_confirm_active=1;g_zip_confirm_conflicts=result.conflicts;snprintf(status_line,sizeof(status_line),"Confirm replacement of %u existing file%s",result.conflicts,result.conflicts==1?"":"s");g_ui_dirty=1;draw();return;}
 if(rc!=ZIP_EXTRACT_OK){snprintf(status_line,sizeof(status_line),"ZIP extraction failed: %.190s",result.error[0]?result.error:"unknown error");g_ui_dirty=1;draw();return;}
 snprintf(status_line,sizeof(status_line),"Extracted %u file%s (%llu bytes); ZIP kept",result.files,result.files==1?"":"s",result.total_bytes);
 refresh_after_zip();
}
static void begin_zip_extract(){
 if(archive_mode||item_count<=0||items[selected].is_dir||!endswith(items[selected].name,".zip")){strcpy(status_line,"Select a .zip file first");return;}
 join(g_zip_confirm_archive,sizeof(g_zip_confirm_archive),cwd,items[selected].name);
 snprintf(g_zip_confirm_destination,sizeof(g_zip_confirm_destination),"%s",cwd);
 zip_extract_result_t result;int rc=zip_extract_archive(g_zip_confirm_archive,g_zip_confirm_destination,ZIP_EXTRACT_PREFLIGHT,&result);
 if(rc==ZIP_EXTRACT_NEEDS_OVERWRITE){g_zip_confirm_conflicts=result.conflicts;g_zip_confirm_active=1;snprintf(status_line,sizeof(status_line),"Confirm replacement of %u existing file%s",result.conflicts,result.conflicts==1?"":"s");g_ui_dirty=1;return;}
 if(rc!=ZIP_EXTRACT_OK){snprintf(status_line,sizeof(status_line),"ZIP extraction failed: %.190s",result.error[0]?result.error:"unknown error");return;}
 run_zip_extract(ZIP_EXTRACT_WRITE);
}
static void cancel_zip_overwrite(){g_zip_confirm_active=0;g_zip_confirm_archive[0]=0;g_zip_confirm_destination[0]=0;snprintf(status_line,sizeof(status_line),"ZIP extraction cancelled");g_ui_dirty=1;}
static void confirm_zip_overwrite(){if(g_zip_confirm_active)run_zip_extract(ZIP_EXTRACT_OVERWRITE);}
/* Stage 31: the read-only foreign view is already a valid PSU built with
   psu_create_from_dir. Export its bytes without touching the source archive.
   O_EXCL prevents overwrites, even if another file appears during export. */
static void convert_foreign_view_to_psu(){
 if(!g_foreign_archive_view || !g_foreign_source_path[0] || !g_foreign_view_path[0])return;
 const char *filename=base_name(g_foreign_source_path);
 char stem[256],dest[PATHSZ];size_t n=strlen(filename);
 if(n>=sizeof(stem))n=sizeof(stem)-1;
 memcpy(stem,filename,n);stem[n]=0;
 char *dot=strrchr(stem,'.');if(dot && (!strcasecmp(dot,".max")||!strcasecmp(dot,".cbs"))) *dot=0;
 if(!stem[0]){strcpy(status_line,"Conversion failed: invalid filename");return;}
 FILE *src=fopen(g_foreign_view_path,"rb");if(!src){strcpy(status_line,"Conversion failed: cannot read staged PSU");return;}
 FILE *dst=NULL;int fd=-1,rc=-1;
 for(int i=0;i<10000;i++){
  int z=i?snprintf(dest,sizeof(dest),"%s/%s_%02d.psu",cwd,stem,i):snprintf(dest,sizeof(dest),"%s/%s.psu",cwd,stem);
  if(z<0||(size_t)z>=sizeof(dest))break;
  fd=open(dest,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd>=0)break;
  if(errno!=EEXIST)break;
 }
 if(fd>=0){
  dst=fdopen(fd,"wb");if(dst){
   unsigned char buf[16384];size_t r;
   rc=0;
   while((r=fread(buf,1,sizeof(buf),src))>0){if(fwrite(buf,1,r,dst)!=r){rc=-1;break;}}
   if(ferror(src)||fflush(dst)!=0)rc=-1;
   if(fclose(dst)!=0)rc=-1;dst=NULL;
  }else{close(fd);}
  if(rc==0 && psu_list(dest,NULL,NULL)!=0)rc=-1;
  if(rc!=0)unlink(dest);
 }
 fclose(src);
 if(rc==0){snprintf(status_line,sizeof(status_line),"Created %s",base_name(dest));invalidate_dir_cache(cwd);}
 else snprintf(status_line,sizeof(status_line),"MAX/CBS to PSU conversion failed");
 g_ui_dirty=1;
}
static void do_export_whole_psu(){
 if(archive_mode){if(g_foreign_archive_view)convert_foreign_view_to_psu();else strcpy(status_line,"Close PSU before whole-archive export");return;}
 if(item_count<=0||items[selected].is_dir){strcpy(status_line,"Select a .psu or .zip file first");return;}
 if(endswith(items[selected].name,".zip")){begin_zip_extract();return;}
 if(!endswith(items[selected].name,".psu")){strcpy(status_line,"Select a .psu or .zip file first");return;}
 char psu[PATHSZ],out[PATHSZ];join(psu,sizeof(psu),cwd,items[selected].name);snprintf(out,sizeof(out),"%s/%.*s_extracted",cwd,(int)(strlen(items[selected].name)-4),items[selected].name);int rc=psu_extract_all(psu,out);snprintf(status_line,sizeof(status_line),rc?"PSU export failed: %s":"Exported whole PSU to %s",rc?psu_strerror(rc):out);invalidate_dir_cache(cwd);refresh_fs();
}
static void do_triangle(){
 if(!archive_mode && item_count>0 && !items[selected].is_dir && endswith(items[selected].name,".xpo")){
  char p[PATHSZ];join(p,sizeof(p),cwd,items[selected].name);
  if(strcmp(g_xpo_candidate_pending,p)!=0){snprintf(g_xpo_candidate_pending,sizeof(g_xpo_candidate_pending),"%s",p);g_xpo_icon_mode=0;}
  g_xpo_icon_mode=1;
  snprintf(status_line,sizeof(status_line),"Generated title icon selected: press Square to confirm XPO conversion");g_ui_dirty=1;return;
 }
 if(archive_mode){if(g_foreign_archive_view){strcpy(status_line,"Read-only archive view: injection disabled");return;}if(!clipboard[0]){success_frames=0;strcpy(status_line,"Copy a file first with Square");return;}const char *name=base_name(clipboard);int rc=psu_add_or_replace(archive_path,clipboard,name);if(rc){success_frames=0;snprintf(status_line,sizeof(status_line),"Inject failed: %s",psu_strerror(rc));}else{snprintf(status_line,sizeof(status_line),"Injected %s",name);snprintf(success_line,sizeof(success_line),"SUCCESS: Imported / injected %s",name);success_frames=360;}free_preview();clear_icn_cache();refresh_psu();return;}}
static int remove_tree(const char *path){
 struct stat st;if(stat(path,&st)!=0)return -1;if(!S_ISDIR(st.st_mode))return unlink(path);
 DIR*d=opendir(path);if(!d)return -1;struct dirent*de;int rc=0;
 while((de=readdir(d))){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;char child[PATHSZ];join(child,sizeof(child),path,de->d_name);if(remove_tree(child)!=0){rc=-1;break;}}
 closedir(d);if(rc==0)rc=rmdir(path);return rc;
}
static int path_exists(const char *path){struct stat st;return stat(path,&st)==0;}
static int has_suffix_ci(const char *s,const char *suffix){size_t a=strlen(s),b=strlen(suffix);return a>=b&&!strcasecmp(s+a-b,suffix);}
static void trim_right(char *s){size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;}
static int is_hex8(const char *s){for(int i=0;i<8;i++)if(!isxdigit((unsigned char)s[i]))return 0;return 1;}
static void clean_folder_psu_name(const char *src,char *dst,size_t dstsz){
 if(!dstsz)return;snprintf(dst,dstsz,"%s",src?src:"");trim_right(dst);
 for(;;){
  size_t n=strlen(dst);int changed=0;
  if(has_suffix_ci(dst,"_EXTRACTED")){dst[n-10]=0;changed=1;}
  else if(has_suffix_ci(dst,"_EXPORTED")){dst[n-9]=0;changed=1;}
  if(changed){trim_right(dst);continue;}
  n=strlen(dst);
  if(n>=11&&dst[n-11]==' '&&dst[n-10]=='('&&dst[n-1]==')'&&is_hex8(dst+n-9)){dst[n-11]=0;trim_right(dst);continue;}
  break;
 }
 if(!dst[0])snprintf(dst,dstsz,"SAVE");
}
static void unique_psu_path(const char *base,char *out,size_t outsz){
 snprintf(out,outsz,"%s/%s.psu",cwd,base);if(!path_exists(out))return;
 for(int i=1;i<10000;i++){snprintf(out,outsz,"%s/%s_%02d.psu",cwd,base,i);if(!path_exists(out))return;}
 out[0]=0;
}
static void do_blank_psu(){begin_blank_psu_title();}
static void do_folder_psu(){
 if(archive_mode){strcpy(status_line,"Close PSU before creating from a folder");return;}if(item_count<=0||!items[selected].is_dir){strcpy(status_line,"Select a folder first");return;}
 char target[PATHSZ],out[PATHSZ],clean[256];join(target,sizeof(target),cwd,items[selected].name);clean_folder_psu_name(items[selected].name,clean,sizeof(clean));unique_psu_path(clean,out,sizeof(out));
 if(!out[0]){strcpy(status_line,"No free PSU filename available");return;}
 int rc=psu_create_from_dir(target,out,clean);snprintf(status_line,sizeof(status_line),rc?"PSU create failed: %s":"Created %s",rc?psu_strerror(rc):base_name(out));invalidate_dir_cache(cwd);refresh_fs();
}
static void begin_delete_confirmation(){
 if(g_foreign_archive_view){snprintf(status_line,sizeof(status_line),"Read-only archive view: deletion disabled");return;}
 if(item_count<=0||selected<0||selected>=item_count)return;
 if(archive_mode&&items[selected].is_dir){snprintf(status_line,sizeof(status_line),"Cannot delete PSU directory entry");return;}
 g_delete_confirm_active=1;
 g_delete_confirm_archive=archive_mode;
 g_delete_confirm_is_dir=items[selected].is_dir;
 snprintf(g_delete_confirm_name,sizeof(g_delete_confirm_name),"%s",items[selected].name);
 g_delete_confirm_target[0]=0;g_delete_confirm_archive_path[0]=0;
 if(archive_mode)snprintf(g_delete_confirm_archive_path,sizeof(g_delete_confirm_archive_path),"%s",archive_path);
 else join(g_delete_confirm_target,sizeof(g_delete_confirm_target),cwd,items[selected].name);
 snprintf(status_line,sizeof(status_line),"Confirm delete: X deletes, O cancels");
 g_ui_dirty=1;g_list_dirty=0;
}
static void cancel_delete_confirmation(){
 g_delete_confirm_active=0;g_delete_confirm_name[0]=0;g_delete_confirm_target[0]=0;g_delete_confirm_archive_path[0]=0;
 snprintf(status_line,sizeof(status_line),"Delete cancelled");g_ui_dirty=1;
}
static void confirm_delete(){
 if(!g_delete_confirm_active)return;
 char deleted[256];snprintf(deleted,sizeof(deleted),"%s",g_delete_confirm_name);
 int rc=0;
 if(g_delete_confirm_archive){
  rc=psu_delete_entry(g_delete_confirm_archive_path,g_delete_confirm_name);
  if(rc)snprintf(status_line,sizeof(status_line),"Delete failed: %s",psu_strerror(rc));
  else snprintf(status_line,sizeof(status_line),"Deleted %s",deleted);
  g_delete_confirm_active=0;g_delete_confirm_name[0]=0;
  free_preview();clear_icn_cache();refresh_psu();g_ui_dirty=1;return;
 }
 rc=g_delete_confirm_is_dir?remove_tree(g_delete_confirm_target):unlink(g_delete_confirm_target);
 if(rc){snprintf(status_line,sizeof(status_line),"Delete failed: %s",deleted);}
 else{
  if(clipboard[0]&&!strncmp(clipboard,g_delete_confirm_target,strlen(g_delete_confirm_target))&&(clipboard[strlen(g_delete_confirm_target)]==0||clipboard[strlen(g_delete_confirm_target)]=='/'))clipboard[0]=0;
  snprintf(status_line,sizeof(status_line),g_delete_confirm_is_dir?"Deleted folder %s":"Deleted %s",deleted);
 }
 g_delete_confirm_active=0;g_delete_confirm_name[0]=0;
 invalidate_dir_cache(cwd);refresh_fs();g_ui_dirty=1;
}
static void do_select(){if(g_foreign_archive_view){strcpy(status_line,"Read-only archive view: deletion disabled");return;}begin_delete_confirmation();}
static void do_cancel(){int had_clipboard=clipboard[0]!=0;int had_success=success_frames>0;int had_export=archive_mode&&export_target_active;clipboard[0]=0;export_target_active=0;success_frames=0;success_line[0]=0;if(had_clipboard&&had_export&&had_success)strcpy(status_line,"Cancelled staged copy/injection + export target; dismissed message");else if(had_clipboard&&had_export)strcpy(status_line,"Cancelled staged copy/injection and export target");else if(had_clipboard&&had_success)strcpy(status_line,"Cancelled staged copy/injection and dismissed message");else if(had_export&&had_success)strcpy(status_line,"Cancelled staged export target and dismissed message");else if(had_clipboard)strcpy(status_line,"Cancelled staged copy/injection");else if(had_export)strcpy(status_line,"Cancelled staged export target");else if(had_success)strcpy(status_line,"Dismissed current message");else strcpy(status_line,"Previous action cleared");}


static unsigned long long g_dir_watch_us=0;static time_t g_dir_watch_mtime=0;
static void service_directory_watch(){if(archive_mode||g_rename_active||g_options_active||g_catalogue_active||g_download_progress_active)return;unsigned long long now=fps_now_us();/* Do not wake/block storage continuously while the app is unattended.  Re-check immediately after controller activity. */if(g_last_user_activity_us&&now-g_last_user_activity_us>3000000ull)return;if(g_dir_watch_us&&now-g_dir_watch_us<1000000ull)return;g_dir_watch_us=now;struct stat st;if(stat(cwd,&st)!=0)return;time_t mt=latest_stat_time(&st);if(!g_dir_watch_mtime){g_dir_watch_mtime=mt;return;}if(mt!=g_dir_watch_mtime){g_dir_watch_mtime=mt;invalidate_dir_cache(cwd);int old_count=item_count;refresh_fs();if(item_count!=old_count){selected=selected<item_count?selected:(item_count?item_count-1:0);g_folder_dirty=1;g_list_dirty=0;}else g_ui_dirty=1;}}


/* Check for newer game IDs on startup without blocking the controller/UI loop. */
static volatile int g_game_db_running=0;
static void game_db_background_worker(void *unused){
 (void)unused;char message[256]={0};
 if(downloader_net_init()!=0){g_game_db_running=0;sysThreadExit(0);return;}
 int n=ps2_game_db_update("/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.tsv",message,sizeof(message));
 if(n>=1000){FILE *f=fopen("/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.check","wb");
  if(f){fputs("Last successful catalogue update\n",f);fclose(f);}}
 g_game_db_running=0;sysThreadExit(0);
}
static void game_db_auto_check(void){
 const char *stamp="/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.check";
 struct stat st;time_t now=time(NULL);
 if(g_game_db_running)return;
 if(stat(stamp,&st)==0 && now>st.st_mtime && now-st.st_mtime<7*24*3600)return;
 sys_ppu_thread_t thread;g_game_db_running=1;
 if(sysThreadCreate(&thread,game_db_background_worker,NULL,1500,0x10000,0,(char*)"ps2_game_db")!=0)g_game_db_running=0;
}

int main(){if(video_init()!=0)return 1;sysThreadCreate(&g_5g_thread,phase5g_sampler,NULL,1500,0x4000,0,(char*)"psu_5g_fifo");ioPadInit(7);atexit(shutdown_video);sysUtilRegisterCallback(0,sysutil_cb,NULL);load_settings();catalogue_load_cache();set_device_default_path("/dev_hdd0");refresh_fs();game_db_auto_check();g_last_user_activity_us=fps_now_us();enum{B_UP=1<<0,B_DOWN=1<<1,B_CROSS=1<<2,B_CIRCLE=1<<3,B_SQUARE=1<<4,B_TRIANGLE=1<<5,B_SELECT=1<<6,B_START=1<<7,B_L1=1<<8,B_R1=1<<9,B_LEFT=1<<10,B_RIGHT=1<<11,B_L2=1<<12,B_R2=1<<13,B_R3=1<<14,B_L3=1<<15};padInfo pi;padData pd;u16 prev=0;while(running){
 sysUtilCheckCallback();
 /* v1.49 clean-renderer recovery: do not touch video configuration, framebuffer
    snapshots, cache ownership, or DRAW_BEGIN/DRAW_END.  Request one ordinary
    complete application redraw after MENU_CLOSE.  g_ui_dirty blocks the
    full-frame fast synchronizer until that clean frame has been composed. */
 if(g_system_menu_resume_pending){
  g_system_menu_resume_pending=0;
  g_ui_dirty=1;
  prev=0;
 }
 /* Launch notifications are not reliably paired with a useful sysutil event on
    real hardware.  Perform one normal application redraw after the launch
    notification has had time to expire.  This is one redraw only, not a
    recurring recovery path. */
 if(!g_startup_overlay_cleanup_done && g_frame_counter>=300){
  g_startup_overlay_cleanup_done=1;
  g_ui_dirty=1;
 }
 /* While XMB owns pad focus, keep the ordinary renderer/present path alive but
    never consume its controller input in the application. */
 if(g_system_menu_active){prev=0;draw();continue;}
 ioPadGetInfo(&pi);u16 now=0;for(int p=0;p<MAX_PADS;p++)if(pi.status[p]){ioPadGetData(p,&pd);if(pd.BTN_UP)now|=B_UP;if(pd.BTN_DOWN)now|=B_DOWN;if(pd.BTN_LEFT)now|=B_LEFT;if(pd.BTN_RIGHT)now|=B_RIGHT;if(pd.BTN_CROSS)now|=B_CROSS;if(pd.BTN_CIRCLE)now|=B_CIRCLE;if(pd.BTN_SQUARE)now|=B_SQUARE;if(pd.BTN_TRIANGLE)now|=B_TRIANGLE;if(pd.BTN_SELECT)now|=B_SELECT;if(pd.BTN_START)now|=B_START;if(pd.BTN_L1)now|=B_L1;if(pd.BTN_R1)now|=B_R1;if(pd.BTN_L2)now|=B_L2;if(pd.BTN_R2)now|=B_R2;if(pd.BTN_R3)now|=B_R3;if(pd.BTN_L3)now|=B_L3;break;}u16 press=now&~prev;prev=now;if(now||press){g_last_user_activity_us=fps_now_us();g_dir_watch_us=0;}
 if(g_apollo_warning_active){
  if(press&B_CROSS)confirm_apollo_create();
  else if(press&B_CIRCLE)cancel_apollo_warning();
  draw();continue;
 }
 if(g_zip_confirm_active){
  if(press&B_CROSS)confirm_zip_overwrite();
  else if(press&B_CIRCLE)cancel_zip_overwrite();
  draw();continue;
 }
 if(g_delete_confirm_active){
  if(press&B_CROSS)confirm_delete();
  else if(press&B_CIRCLE)cancel_delete_confirmation();
  draw();continue;
 }
 if(g_options_active){
  if(press&B_UP){if(g_options_selected>0)g_options_selected--;g_ui_dirty=1;}
  if(press&B_DOWN){if(g_options_selected<5)g_options_selected++;g_ui_dirty=1;}
  if(press&B_LEFT||press&B_RIGHT){if(g_options_selected==0){g_fps_enabled=!g_fps_enabled;g_ui_dirty=1;}else if(g_options_selected==1){if(press&B_RIGHT){if(g_download_connections<16)g_download_connections++;}else{if(g_download_connections>0)g_download_connections--;}g_ui_dirty=1;}else if(g_options_selected==3){g_psu_catalogue_source=(g_psu_catalogue_source+(press&B_RIGHT?1:2))%3;g_ui_dirty=1;}}
  if(press&B_CROSS){if(g_options_selected==0){g_fps_enabled=!g_fps_enabled;g_ui_dirty=1;}else if(g_options_selected==1){g_download_connections=(g_download_connections+1)%17;g_ui_dirty=1;}else if(g_options_selected==2){start_url_keyboard();draw();continue;}else if(g_options_selected==3){if(g_psu_catalogue_source==0){g_options_active=0;g_catalogue_active=1;g_catalogue_category=0;g_catalogue_selected=0;g_catalogue_top_index=0;g_ui_dirty=1;draw();catalogue_open_first_populated_category();draw();continue;}else if(g_psu_catalogue_source==2){g_options_active=0;g_saves_active=1;g_saves_level=0;g_saves_selected=g_saves_top=0;saves_load_index();g_ui_dirty=1;draw();continue;}else{g_options_active=0;g_homebrew_active=1;g_homebrew_category=0;g_homebrew_selected=0;g_homebrew_top=0;g_homebrew_view=0;homebrew_catalogue_load_cache();g_ui_dirty=1;draw();homebrew_open_first_populated_category();draw();continue;}}else if(g_options_selected==4){char dbmsg[256]={0};int net_ok=g_game_db_running?0:downloader_net_init();int n=(g_game_db_running||net_ok!=0)?-1:ps2_game_db_update("/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.tsv",dbmsg,sizeof(dbmsg));if(net_ok!=0)snprintf(dbmsg,sizeof(dbmsg),"PS3 network initialization failed");if(g_game_db_running)snprintf(dbmsg,sizeof(dbmsg),"Automatic update already running");snprintf(status_line,sizeof(status_line),"Game DB: %s",dbmsg[0]?dbmsg:(n>=0?"Updated":"Update failed"));g_ui_dirty=1;}else{save_settings();strcpy(status_line,"Settings saved - exiting to XMB...");running=0;continue;}}
  if(press&B_CIRCLE||press&B_START){int sr=save_settings();g_options_active=0;g_ui_dirty=1;snprintf(status_line,sizeof(status_line),sr==0?"Options saved":"Could not save settings.ini");}
  draw();continue;
 }
 if(g_saves_active){
  if(press&B_SQUARE){
   if(g_saves_level==2){
    char row_label[256];int id=saves_row(g_saves_selected,row_label,sizeof(row_label));
    if(id>=0){int n=saves_refresh_game_listing(&g_saves_entries[id]);
     if(n>0){saves_load_index();snprintf(status_line,sizeof(status_line),"GameFAQs: %d pages processed",n);}
     else snprintf(status_line,sizeof(status_line),"GameFAQs: listing refresh failed (%d)",n);
    }
   }else if(g_saves_level==3){
    char row_label[256];int id=saves_row(g_saves_selected,row_label,sizeof(row_label));
    if(id>=0){
     int n=saves_fetch_detail_page(g_saves_entries[id].page_url,g_saves_entries[id].game,g_saves_entries[id].region);
     if(n>0){saves_load_index();snprintf(status_line,sizeof(status_line),"GameFAQs index updated: %d total entries",n);}
     else if(n==0)snprintf(status_line,sizeof(status_line),"GameFAQs: no explicit direct file links found");
     else snprintf(status_line,sizeof(status_line),"GameFAQs: detail retrieval failed (%d)",n);
    }
   }else{
    saves_load_index();g_saves_level=0;g_saves_selected=g_saves_top=0;
    snprintf(status_line,sizeof(status_line),"GameFAQs index reloaded: %d entries",g_saves_count);
   }
   g_ui_dirty=1;
  }
  int count=saves_row_count();
  if(press&B_UP&&g_saves_selected>0){g_saves_selected--;g_ui_dirty=1;}
  if(press&B_DOWN&&g_saves_selected+1<count){g_saves_selected++;g_ui_dirty=1;}
  if(press&B_CROSS){
   char label[256];int id=saves_row(g_saves_selected,label,sizeof(label));
   if(id>=0){
    if(g_saves_level==0)g_saves_bucket=id;
    if(g_saves_level==1)g_saves_game_index=id;
    if(g_saves_level==2)g_saves_region_index=id;
    if(g_saves_level<3){g_saves_level++;g_saves_selected=g_saves_top=0;}
    else if(gfs_download_allowed(g_saves_entries[id].download_url)){
     char url[512];snprintf(url,sizeof(url),"%s",g_saves_entries[id].download_url);
     g_saves_active=0;g_homebrew_validate_kind=download_policy_for_url(url);
     download_url_to_fakeusb(url);g_homebrew_validate_kind=0;
     g_saves_active=1;refresh_fs();refresh_fs_metadata_now();
    }else snprintf(status_line,sizeof(status_line),"Save is page-only: no verified direct download URL");
   }
   g_ui_dirty=1;
  }
  if(press&B_CIRCLE||press&B_START){if(g_saves_level>0){g_saves_level--;g_saves_selected=g_saves_top=0;}else{g_saves_active=0;g_options_active=1;g_options_selected=3;}g_ui_dirty=1;}
  draw();continue;
 }
 if(g_homebrew_active){
  int count=g_homebrew_view?g_homebrew_asset_count:homebrew_category_count(g_homebrew_category);
  if(press&B_UP){int *sel=g_homebrew_view?&g_homebrew_asset_selected:&g_homebrew_selected;if(*sel>0)(*sel)--;g_ui_dirty=1;}
  if(press&B_DOWN){int *sel=g_homebrew_view?&g_homebrew_asset_selected:&g_homebrew_selected;if(*sel+1<count)(*sel)++;g_ui_dirty=1;}
  if(press&B_L1||press&B_R1)homebrew_step_populated_category(press&B_L1?-1:1);
  if(press&B_CROSS){if(g_homebrew_view)homebrew_catalogue_download_selected();else{int id=homebrew_selected_entry_id();if(id>=0){g_homebrew_selected_entry=id;homebrew_catalogue_fetch_assets(id);if(g_homebrew_asset_count==1){g_homebrew_asset_selected=0;homebrew_catalogue_download_selected();}else if(g_homebrew_asset_count>1){g_homebrew_view=1;g_homebrew_asset_selected=0;g_homebrew_asset_top=0;}}}g_ui_dirty=1;}
  if(press&B_SQUARE){g_homebrew_view=0;homebrew_catalogue_fetch_category(g_homebrew_category);if(homebrew_category_count(g_homebrew_category)==0)homebrew_step_populated_category(1);g_ui_dirty=1;}
  if(press&B_CIRCLE||press&B_START){if(g_homebrew_view){g_homebrew_view=0;g_ui_dirty=1;}else{g_homebrew_active=0;g_options_active=1;g_options_selected=3;g_ui_dirty=1;}}
  draw();continue;
 }
 if(g_catalogue_active){
  int count=catalogue_count(g_catalogue_category);
  if(press&B_UP){if(g_catalogue_selected>0)g_catalogue_selected--;g_ui_dirty=1;}
  if(press&B_DOWN){if(g_catalogue_selected+1<count)g_catalogue_selected++;g_ui_dirty=1;}
  if(press&B_L1)catalogue_step_populated_category(-1);
  if(press&B_R1)catalogue_step_populated_category(1);
  if(press&B_CROSS){catalogue_download_selected();draw();continue;}
  if(press&B_SQUARE){catalogue_fetch_category(g_catalogue_category);if(catalogue_count(g_catalogue_category)==0)catalogue_step_populated_category(1);draw();continue;}
  if(press&B_CIRCLE||press&B_START){g_catalogue_active=0;g_options_active=1;g_options_selected=3;g_ui_dirty=1;}
  draw();continue;
 }
 if(g_psu_metadata_active){
  int pages=psu_metadata_page_count();
  if(press&B_L1){g_psu_metadata_page--;if(g_psu_metadata_page<0)g_psu_metadata_page=pages-1;g_ui_dirty=1;}
  if(press&B_R1){g_psu_metadata_page++;if(g_psu_metadata_page>=pages)g_psu_metadata_page=0;g_ui_dirty=1;}
  if(press&B_CIRCLE){g_psu_metadata_active=0;psu_metadata_cache_clear();g_ui_dirty=1;}
  draw();continue;
 }
 if(g_rename_active){
  int keycount=rename_key_count(),rows=(keycount+RENAME_COLS-1)/RENAME_COLS;
  if(press&B_LEFT){int col=g_rename_key%RENAME_COLS;if(col>0)g_rename_key--;else{int row=g_rename_key/RENAME_COLS;int last=row*RENAME_COLS+RENAME_COLS-1;if(last>=keycount)last=keycount-1;g_rename_key=last;}}
  if(press&B_RIGHT){int col=g_rename_key%RENAME_COLS;if(col<RENAME_COLS-1&&g_rename_key+1<keycount)g_rename_key++;else g_rename_key=(g_rename_key/RENAME_COLS)*RENAME_COLS;}
  if(press&B_UP){int k=g_rename_key-RENAME_COLS;if(k<0){int col=g_rename_key%RENAME_COLS;k=(rows-1)*RENAME_COLS+col;if(k>=keycount)k=keycount-1;}g_rename_key=k;}
  if(press&B_DOWN){int k=g_rename_key+RENAME_COLS;if(k>=keycount)k=g_rename_key%RENAME_COLS;g_rename_key=k;}
  if(press&B_CROSS){
   if(g_rename_key==rename_char_key_count()){
    if(g_keyboard_purpose==KB_NEW_PSU_ICON){g_rename_lowercase=1;rename_full_modal_changed();build_rename_composite();}
    else {g_rename_lowercase=!g_rename_lowercase;
    rename_full_modal_changed();
    /* CASE changes every alphabet key label and the persistent CASE mode colour.
       Rebuild the pristine navigation snapshot immediately; otherwise the
       cell-patch fast path can restore stale pre-toggle pixels when focus moves. */
    build_rename_composite();}
   }else rename_append_char((g_keyboard_purpose==KB_NEW_PSU_ICON && rename_key_char(g_rename_key)>='A' && rename_key_char(g_rename_key)<='Z')?(char)(rename_key_char(g_rename_key)-'A'+'a'):rename_key_char(g_rename_key));
  }
  if(press&B_SQUARE)rename_backspace();
  if(press&B_TRIANGLE)rename_append_char(' ');
  if(press&B_L1)rename_cursor_left();
  if(press&B_R1)rename_cursor_right();
  if(press&B_L2)rename_cursor_home();
  if(press&B_R2)rename_cursor_end();
  if((press&B_SELECT)&&g_keyboard_purpose==KB_NEW_PSU_FOLDER)blank_folder_cycle_preset();
  if(press&B_START){if(g_keyboard_purpose==KB_DOWNLOAD_URL){
   char url[1024];snprintf(url,sizeof(url),"%s",g_rename_text);
   g_rename_active=0;g_downloader_active=0;g_keyboard_purpose=KB_RENAME;g_ui_dirty=1;
   g_catalogue_verify_psu=0;
   download_url_to_fakeusb(url);g_catalogue_verify_psu=0;
   invalidate_dir_cache(cwd);refresh_fs();refresh_fs_metadata_now();
   g_options_active=1;g_options_selected=2;g_ui_dirty=1;
  }else commit_rename();}
  if(press&B_CIRCLE){if(g_keyboard_purpose==KB_DOWNLOAD_URL){g_rename_active=0;g_downloader_active=0;g_keyboard_purpose=KB_RENAME;{g_options_active=1;g_options_selected=2;}g_ui_dirty=1;snprintf(status_line,sizeof(status_line),"Downloader cancelled");}else cancel_rename();}
  draw();continue;
 }
 u16 navpress=press&(B_UP|B_DOWN|B_L1|B_R1);int nav_old_selected=selected;int nav_old_icn=(archive_mode&&item_count>0&&selected>=0&&selected<item_count&&!items[selected].is_dir&&preview_entry_is_standard_icn(items[selected].name));if(navpress&&!archive_mode){g_list_dirty=0;g_ui_dirty=1;}if(press&~(B_UP|B_DOWN|B_L1|B_R1))g_ui_dirty=1;if(press&B_UP){if(selected>0)selected--;}if(press&B_DOWN){if(selected+1<item_count)selected++;}if(press&B_L1){selected=(selected>10)?selected-10:0;}if(press&B_R1){if(item_count>0){selected+=10;if(selected>=item_count)selected=item_count-1;}}if(navpress&&archive_mode&&selected!=nav_old_selected){int nav_new_icn=(item_count>0&&selected>=0&&selected<item_count&&!items[selected].is_dir&&preview_entry_is_standard_icn(items[selected].name));export_target_active=1;/* Strict PSU fast path: non-ICN -> non-ICN must not enter preview/cache code at all. */if(nav_old_icn||nav_new_icn){ensure_preview_current();g_ui_dirty=1;}else{g_list_dirty=1;}}if(press&B_L2)do_cancel();if(press&B_R2)do_blank_psu();if(press&B_R3){if(archive_mode){if(g_foreign_archive_view)strcpy(status_line,"Read-only archive view: rename disabled");else begin_rename();}else do_folder_psu();}if(press&B_L3)do_export_whole_psu();if(press&B_LEFT)switch_device(-1);if(press&B_RIGHT)switch_device(1);if(press&B_CROSS){if(archive_mode){psu_metadata_cache_load();g_details_base_ready=0;g_psu_metadata_active=1;g_psu_metadata_page=0;g_ui_dirty=1;}else do_cross();}if(press&B_CIRCLE)do_circle();if(press&B_SQUARE)do_square();if(press&B_TRIANGLE){if(archive_mode)do_triangle();else begin_rename();}if(press&B_SELECT)do_select();if(press&B_START){g_options_active=1;g_options_selected=0;g_ui_dirty=1;}service_directory_watch();service_lazy_file_metadata();service_icon_preload();draw();}ioPadEnd();return 0;}

