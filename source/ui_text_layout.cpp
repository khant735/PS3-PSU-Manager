#include "ui_text_layout.h"
#include <ctype.h>
#include <string.h>

static int scale125_local(int n){return (n*5+2)/4;}

int ui_text_cell_height(int scale){
 if(scale<1)scale=1;
 return scale125_local(8*scale);
}

int ui_text_line_pitch(int scale){return ui_text_cell_height(scale)+1;}

int ui_compact_panel_width(int screen_width,int desired_width,int max_width,int margin){
 if(screen_width<=0)return 0;
 if(margin<0)margin=0;
 int available=screen_width-2*margin;
 if(available<1)available=screen_width;
 int width=desired_width>0?desired_width:available;
 if(max_width>0&&width>max_width)width=max_width;
 if(width>available)width=available;
 return width;
}

int ui_text_centered_x(int panel_x,int panel_width,int line_width){
 if(panel_width<0)panel_width=0;
 if(line_width<0)line_width=0;
 return panel_x+(panel_width-line_width)/2;
}

int ui_right_aligned_x(int screen_width,int right_margin,int text_width){
 if(screen_width<0)screen_width=0;
 if(right_margin<0)right_margin=0;
 if(text_width<0)text_width=0;
 int x=screen_width-right_margin-text_width;
 return x>0?x:0;
}

int ui_inline_left_available_width(int left_x,int right_x,int gap){
 if(gap<0)gap=0;
 int width=right_x-left_x-gap;
 return width>0?width:0;
}

int ui_downloader_panel_height(int stat_top,int stat_height,int diagnostics_height,
                               int progress_height,int status_height,
                               int footer_legend_lines,int legend_line_height){
 if(stat_top<0)stat_top=0;
 if(stat_height<0)stat_height=0;
 if(diagnostics_height<0)diagnostics_height=0;
 if(progress_height<0)progress_height=0;
 if(status_height<0)status_height=0;
 if(footer_legend_lines<1)footer_legend_lines=1;
 if(legend_line_height<0)legend_line_height=0;
 /* Matches the vertical gaps and margins drawn by draw_downloader_progress. */
 return stat_top+stat_height+10+diagnostics_height+progress_height+8+
        status_height+4+(footer_legend_lines+1)*legend_line_height+14+12;
}

int ui_downloader_diagnostic_rows(int connection_count){
 if(connection_count<=0)return 0;
 if(connection_count>16)connection_count=16;
 int rows=connection_count<4?connection_count:4;
 if(connection_count>8){
  int second_bank=connection_count-8;
  rows+=second_bank<4?second_bank:4;
 }
 return rows;
}

int ui_downloader_desired_width(int base_width,int title_width,int label_width,
                                int max_value_width,int max_diagnostic_width){
 int width=base_width>0?base_width:0;
 int need=title_width+60;if(need>width)width=need;
 need=label_width+max_value_width+96;if(need>width)width=need;
 need=max_diagnostic_width*2+88;if(need>width)width=need;
 return width;
}

int ui_delete_confirmation_height(int body_height,int warning_height,
                                  int footer_lines,int line_height){
 if(body_height<0)body_height=0;
 if(warning_height<0)warning_height=0;
 if(footer_lines<1)footer_lines=1;
 if(line_height<0)line_height=0;
 return 64+body_height+8+warning_height+10+footer_lines*line_height+18;
}

static int url_break_char(char c){return c=='/'||c=='?'||c=='&'||c=='='||c=='-';}

static void emit_line(ui_text_layout_result_t *r,int max_lines,char lines[][256],const char *line,int len){
 if(r->line_count>=max_lines){r->truncated=1;return;}
 if(len>255)len=255;
 if(lines){if(len>0)memcpy(lines[r->line_count],line,(size_t)len);lines[r->line_count][len]=0;}
 r->line_count++;
}

ui_text_layout_result_t ui_text_layout_wrap(const char *src,int max_width,int scale,int max_lines,char lines[][256]){
 ui_text_layout_result_t r={0,0,0};
 if(!src||max_lines<=0||!*src)return r;
 if(scale<1)scale=1;
 int advance=scale125_local(6*scale);
 int chars=advance>0?max_width/advance:0;
 if(chars>255)chars=255;
 if(chars<1){r.truncated=1;return r;}
 const char *p=src;
 char line[256];int llen=0;
 while(*p){
  if(*p=='\n'){
   emit_line(&r,max_lines,lines,line,llen);llen=0;p++;
   if(!*p)emit_line(&r,max_lines,lines,line,0);
   continue;
  }
  while(*p==' '||*p=='\t'||*p=='\r')p++;
  if(!*p)break;
  if(*p=='\n')continue;
  const char *word=p;
  while(*p&&*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n')p++;
  int word_len=(int)(p-word);
  if(llen&&llen+1+word_len<=chars){line[llen++]=' ';memcpy(line+llen,word,(size_t)word_len);llen+=word_len;line[llen]=0;continue;}
  if(llen){emit_line(&r,max_lines,lines,line,llen);llen=0;if(r.truncated)break;}
  if(word_len<=chars){memcpy(line,word,(size_t)word_len);llen=word_len;line[llen]=0;continue;}
  int off=0;
  while(off<word_len){
   int take=word_len-off;if(take>chars)take=chars;
   if(off+take<word_len){int preferred=0;for(int i=1;i<=take;i++)if(url_break_char(word[off+i-1]))preferred=i;if(preferred)take=preferred;}
   memcpy(line,word+off,(size_t)take);line[take]=0;emit_line(&r,max_lines,lines,line,take);if(r.truncated)break;off+=take;
  }
  if(r.truncated)break;
 }
 if(!r.truncated&&llen)emit_line(&r,max_lines,lines,line,llen);
 if(r.line_count>0)r.used_height=ui_text_cell_height(scale)+(r.line_count-1)*ui_text_line_pitch(scale);
 return r;
}
