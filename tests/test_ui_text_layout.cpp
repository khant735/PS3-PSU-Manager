#include "ui_text_layout.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main(){
 char lines[8][256]={{0}};
 assert(ui_compact_panel_width(1920,480,1080,16)==480);
 assert(ui_compact_panel_width(720,900,1080,16)==688);
 assert(ui_compact_panel_width(426,500,1080,16)==394);
 assert(ui_text_centered_x(100,400,200)==200);
 assert(ui_text_centered_x(100,401,200)==200);
 assert(ui_text_centered_x(100,400,400)==100);
 assert(ui_right_aligned_x(720,28,120)==572);
 assert(ui_inline_left_available_width(60,300,18)==222);
 assert(ui_inline_left_available_width(60,70,18)==0);
 assert(ui_downloader_panel_height(52,150,100,20,11,1,22)==425);
 assert(ui_downloader_panel_height(52,150,100,20,11,2,22)==447);
 assert(ui_downloader_desired_width(760,585,245,300,450)==988);
 assert(ui_downloader_desired_width(600,450,165,215,225)==600);
 assert(ui_downloader_diagnostic_rows(0)==0);
 assert(ui_downloader_diagnostic_rows(1)==1);
 assert(ui_downloader_diagnostic_rows(4)==4);
 assert(ui_downloader_diagnostic_rows(5)==4);
 assert(ui_downloader_diagnostic_rows(8)==4);
 assert(ui_downloader_diagnostic_rows(9)==5);
 assert(ui_downloader_diagnostic_rows(12)==8);
 assert(ui_downloader_diagnostic_rows(16)==8);
 assert(ui_delete_confirmation_height(44,22,2,22)>ui_delete_confirmation_height(22,22,2,22));
 assert(ui_compact_panel_width(720,600,1200,24)==600);
 assert(ui_compact_panel_width(426,600,1200,24)==378);
 assert(ui_compact_panel_width(1920,988,1200,24)==988);
 ui_text_layout_result_t r=ui_text_layout_wrap("one two three",72,1,8,lines);
 assert(r.line_count==2&&!r.truncated);
 assert(std::strcmp(lines[0],"one two")==0);
 assert(std::strcmp(lines[1],"three")==0);
 r=ui_text_layout_wrap("abcd",32,1,8,lines);
 assert(r.line_count==1&&std::strcmp(lines[0],"abcd")==0);
 r=ui_text_layout_wrap("https://a/b?x=1&y=2",56,1,8,lines);
 assert(r.line_count>1&&!r.truncated);
 assert(std::strcmp(lines[0],"https:")==0 || std::strcmp(lines[0],"https:/")==0);
 r=ui_text_layout_wrap("a\nb",100,1,8,lines);
 assert(r.line_count==2&&std::strcmp(lines[0],"a")==0&&std::strcmp(lines[1],"b")==0);
 r=ui_text_layout_wrap("a\n\nb",100,1,8,lines);
 assert(r.line_count==3&&lines[1][0]==0);
 r=ui_text_layout_wrap("one two three",24,1,2,lines);
 assert(r.line_count==2&&r.truncated);
 r=ui_text_layout_wrap("Delete folder: .filen-eba81917-a680-42cc-975a-1040eb841592",240,1,8,lines);
 assert(r.line_count>1&&!r.truncated);
 r=ui_text_layout_wrap("https://example.com/a/long/path",32,1,32,nullptr);
 assert(r.line_count>1&&!r.truncated);
 for(int scale=1;scale<=3;scale++){
  int cell=ui_text_cell_height(scale);
  int pitch=ui_text_line_pitch(scale);
  assert(pitch==cell+1);
  r=ui_text_layout_wrap("A\nB",100,scale,8,lines);
  assert(r.line_count==2&&r.used_height==cell+pitch);
 }
 r=ui_text_layout_wrap("",100,1,8,lines);
 assert(r.line_count==0&&r.used_height==0&&!r.truncated);
 puts("PASS: bounded text wrapping, URL breaks, explicit newlines, and scale-aware line pitch");
}
