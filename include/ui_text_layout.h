#ifndef UI_TEXT_LAYOUT_H
#define UI_TEXT_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
 int line_count;
 int used_height;
 int truncated;
} ui_text_layout_result_t;

int ui_text_cell_height(int scale);
int ui_text_line_pitch(int scale);
int ui_compact_panel_width(int screen_width,int desired_width,int max_width,int margin);
int ui_text_centered_x(int panel_x,int panel_width,int line_width);
int ui_right_aligned_x(int screen_width,int right_margin,int text_width);
int ui_inline_left_available_width(int left_x,int right_x,int gap);
int ui_downloader_panel_height(int stat_top,int stat_height,int diagnostics_height,
                               int progress_height,int status_height,
                               int footer_legend_lines,int legend_line_height);
int ui_downloader_diagnostic_rows(int connection_count);
int ui_downloader_desired_width(int base_width,int title_width,int label_width,
                                int max_value_width,int max_diagnostic_width);
int ui_delete_confirmation_height(int body_height,int warning_height,
                                  int footer_lines,int line_height);
ui_text_layout_result_t ui_text_layout_wrap(const char *text,int max_width,int scale,
                                             int max_lines,char lines[][256]);

#ifdef __cplusplus
}
#endif
#endif
