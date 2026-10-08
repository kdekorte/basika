#ifndef GRAPHICS_H
#define GRAPHICS_H

int init_graphics();
void set_graphics_headless(int headless);
void update_graphics();
void close_graphics();
void set_screen_mode(int mode);
void set_screen_newimage(int width, int height, int colors);
int graphics_page_count(void);
int graphics_set_pages(int active, int visual);
int graphics_copy_page(int source, int destination);
int graphics_get_text_rows(void);
void graphics_set_text_size(int columns, int rows);
int graphics_get_text_cols(void);
void set_pixel(double x, double y, unsigned int color);
void draw_line(double x1, double y1, double x2, double y2, unsigned int color, int box, unsigned int style);
void draw_circle(double cx, double cy, double radius, unsigned int color,
                 int has_start, double start, int has_end, double end,
                 int has_aspect, double aspect);
int graphics_save_screenshot(const char *filename);
int graphics_loadimage(const char *filename, int mode);
int graphics_newimage(int width, int height, int mode);
int graphics_copyimage(int handle);
int graphics_freeimage(int handle);
int graphics_putimage(int x1, int y1, int x2, int y2, int handle,
                      int sx1, int sy1, int sx2, int sy2, int has_source);
int graphics_putimage_ex(int has_dest, int dx1, int dy1, int has_dest2, int dx2, int dy2,
                         int source, int destination,
                         int has_src, int sx1, int sy1, int has_src2, int sx2, int sy2);
int graphics_valid_handle(int handle);
int graphics_set_dest(int handle);
int graphics_get_dest(void);
int graphics_set_source(int handle);
int graphics_get_source(void);
double graphics_point(double x, double y);
int graphics_image_size(int handle, int want_height);
int graphics_screen_from_image(int handle);
double get_pixel(double x, double y);
void draw_paint(double x, double y, unsigned int color, unsigned int border_color);
void draw_paint_tile(double x, double y, const unsigned char *tile, int tile_length, unsigned int border_color);
int graphics_is_32bit(void);
int graphics_set_palette(int count, const int *attributes, const long *values);
void graphics_reset_palette(void);
int graphics_palette_size(void);
void graphics_set_draw_colors(int has_fg, unsigned int fg, int has_bg, unsigned int bg);
unsigned int graphics_get_foreground(void);
unsigned int graphics_get_background(void);
unsigned int graphics_match_color(int r, int g, int b, int a);
void graphics_color_components(unsigned int color, int *r, int *g, int *b, int *a);
int graphics_width(void);
int graphics_height(void);

void graphics_set_window(int use_screen, double x1, double y1, double x2, double y2);
void graphics_reset_window();
void graphics_set_view(int use_screen, int x1, int y1, int x2, int y2,
                       int has_color, unsigned int color, int has_boundary, unsigned int boundary);
void graphics_reset_view();
void set_text_cursor(int row, int col);
void get_text_cursor(int *row, int *col);
void set_text_color(unsigned int color);
void graphics_cls();
int graphics_is_active();
void handle_events();
int graphics_pen(int n);
int graphics_keyhit(void);
int graphics_keydown(int code);
int graphics_mouse_input(void);
int graphics_mouse_x(void);
int graphics_mouse_y(void);
int graphics_mouse_button(int n);
int graphics_mouse_wheel(void);
int graphics_stick(int n);
void wait_for_keypress();
void set_window_title(const char *title);
/* $RESIZE / _RESIZE: allow lets the user resize the window; scaling is
 * RESIZE_SCALE_NONE, _STRETCH or _SMOOTH (-1 keeps the current method). */
enum { RESIZE_SCALE_NONE, RESIZE_SCALE_STRETCH, RESIZE_SCALE_SMOOTH };
void graphics_set_resize(int allow, int scaling);
/* _FULLSCREEN modes, numbered as the _FULLSCREEN function returns them. */
enum { FULLSCREEN_OFF, FULLSCREEN_STRETCH, FULLSCREEN_SQUAREPIXELS };
void graphics_set_fullscreen(int mode, int smooth);
int graphics_fullscreen_mode(void);
int graphics_resize_event(void);
int graphics_resize_width(void);
int graphics_resize_height(void);

void get_graphics_cursor(double *x, double *y);
void set_graphics_cursor(double x, double y);
void get_graphics_cursor_physical(int *x, int *y);

void graphics_sleep(int ms);
void graphics_sleep_seconds(double seconds);
void graphics_delay(double seconds);
void graphics_limit(double fps);
void graphics_print(const char *text);
void graphics_printstring(int px, int py, const char *text);
int graphics_printwidth(const char *text);
/* _LOADFONT styles, combined: */
enum { FONT_STYLE_BOLD = 1, FONT_STYLE_ITALIC = 2, FONT_STYLE_UNDERLINE = 4 };
/* _PRINTMODE values, as QB64 numbers them. */
enum { PRINTMODE_KEEP = 1, PRINTMODE_ONLY = 2, PRINTMODE_FILL = 3 };
int graphics_printmode_for(int handle, int mode);
int graphics_get_dest_handle(void);
int graphics_loadfont(const char *filename, int size, int style);
int graphics_setfont(int handle);
int graphics_current_font(void);
int graphics_freefont(int handle);
void graphics_release_program_resources(void);
int get_graphics_key(void);
int get_graphics_char(void);
void graphics_readline(char *buffer, int size);
void graphics_readline_initial(char *buffer, int size, const char *initial);
void graphics_present_now();
void graphics_present_if_autodisplay();
void graphics_set_autodisplay(int enabled);
void set_present_interval(int ms);

#endif