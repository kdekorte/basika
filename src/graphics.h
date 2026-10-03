#ifndef GRAPHICS_H
#define GRAPHICS_H

int init_graphics();
void set_graphics_headless(int headless);
void update_graphics();
void close_graphics();
void set_screen_mode(int mode);
void set_screen_newimage(int width, int height, int colors);
int graphics_get_text_rows(void);
int graphics_get_text_cols(void);
void set_pixel(double x, double y, int color);
void draw_line(double x1, double y1, double x2, double y2, int color, int fill);
void draw_circle(double cx, double cy, double radius, int color, int fill);
int graphics_save_screenshot(const char *filename);
int graphics_loadimage(const char *filename, int mode);
int graphics_freeimage(int handle);
int graphics_putimage(int x1, int y1, int x2, int y2, int handle,
					  int sx1, int sy1, int sx2, int sy2, int has_source);
int get_pixel(double x, double y);
void draw_paint(double x, double y, int color, int border_color);

void graphics_set_window(int use_screen, double x1, double y1, double x2, double y2);
void graphics_reset_window();
void graphics_set_view(int use_screen, int x1, int y1, int x2, int y2, int color, int boundary);
void graphics_reset_view();
void set_text_cursor(int row, int col);
void set_text_color(int color);
void graphics_cls();
int graphics_is_active();
void handle_events();
void wait_for_keypress();
void set_window_title(const char *title);

void get_graphics_cursor(double *x, double *y);
void set_graphics_cursor(double x, double y);

void graphics_sleep(int ms);
void graphics_print(const char *text);
void graphics_printstring(int px, int py, const char *text);
int graphics_printwidth(const char *text);
int graphics_loadfont(const char *filename, int size);
int graphics_setfont(int handle);
int graphics_freefont(int handle);
int get_graphics_key(void);
int get_graphics_char(void);
void graphics_readline(char *buffer, int size);
void graphics_present_now();
void graphics_present_if_autodisplay();
void graphics_set_autodisplay(int enabled);
void set_present_interval(int ms);

#endif