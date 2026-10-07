#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "graphics.h"
#include "interpreter.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int autodisplay_enabled = 1;
void update_graphics(); // Forward declaration
static void render_canvas_to_window(void); // Forward declaration
static SDL_Texture **get_active_glyph_cache(void); // Forward declaration
static SDL_Texture *glyph_cache[128] = {NULL};
static SDL_Texture *canvas = NULL;
/* Screen pages: drawing goes to the active page (canvas), and the window
 * shows the visual page; SCREEN ..., apage, vpage and PCOPY manage them. */
#define MAX_SCREEN_PAGES 8
static SDL_Texture *pages[MAX_SCREEN_PAGES] = {NULL};
static int page_count = 1;
static int visual_page = 0;
/* Where the canvas was last drawn in the window, to map mouse positions. */
static SDL_FRect presented_area = {0, 0, 0, 0};
static SDL_Color current_text_color = {255, 255, 255, 255};
static int canvas_width = 1280;
static int canvas_height = 400;
static int cursor_x = 0;
static int cursor_y = 0;

static double gfx_cursor_x = 0;
static double gfx_cursor_y = 0;

#define MAX_IMAGE_SLOTS 64


static TTF_Font *font = NULL;
static char font_path[512] = "";
static const int FONT_SIZE = 16;         // Initial DOS-style pixel font size
static int current_row_height = 16;      // Use pixel-based rows for doubled text modes
static int current_col_width = 16;       // Defaults to 80 columns at doubled resolution
static int last_key_code = 0;
static int last_key_char = 0;

static int mode_res_w = 640;
static int mode_res_h = 200;
static int text_columns = 80;
static int text_rows = 25;

static SDL_Color get_graphics_color(unsigned int color_value);

/* 32-bit images (_NEWIMAGE(w, h, 32)) take &HAARRGGBB colors; every other
 * mode uses palette indexes. */
static int color_mode_32 = 0;
static int palette_mode_256 = 0;
static int current_screen_mode = 2;
static unsigned int fg_color = 15;
static unsigned int bg_color = 0;
static void reset_draw_colors(int mode);
static void restore_default_palette(void);
static void reset_dest_to_screen(void);

static int view_active = 0;
static int view_screen = 0;
static int view_x1 = 0, view_y1 = 0, view_x2 = -1, view_y2 = -1;

static int window_active = 0;
static int window_screen = 0;
static double win_x1 = 0, win_y1 = 0, win_x2 = 0, win_y2 = 0;

/* Coordinates round to the nearest pixel (half to even, like CINT), as in
 * QBasic; values far off screen are clamped so they stay valid ints. */
static int round_coordinate(double value) {
    double r = nearbyint(value);
    if (!(r > -1e9)) return -1000000000;
    if (r > 1e9) return 1000000000;
    return (int)r;
}

static void transform_coords(double x, double y, int *px, int *py) {
    if (window_active) {
        double px_d = view_x1 + (x - win_x1) * (view_x2 - view_x1) / (win_x2 - win_x1);
        double py_d;
        if (window_screen) {
            py_d = view_y1 + (y - win_y1) * (view_y2 - view_y1) / (win_y2 - win_y1);
        } else {
            py_d = view_y2 + (y - win_y1) * (view_y1 - view_y2) / (win_y2 - win_y1);
        }
        *px = (int)(px_d + (px_d < 0 ? -0.5 : 0.5));
        *py = (int)(py_d + (py_d < 0 ? -0.5 : 0.5));
    } else if (view_active && !view_screen) {
        *px = round_coordinate(x) + view_x1;
        *py = round_coordinate(y) + view_y1;
    } else {
        *px = round_coordinate(x);
        *py = round_coordinate(y);
    }
}


static void remove_clipping() {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderClipRect(renderer, NULL);
}

static int clip_point(int px, int py) {
    if (view_active) {
        if (px < view_x1 || px > view_x2 || py < view_y1 || py > view_y2) return 0;
    } else {
        if (px < 0 || px >= mode_res_w || py < 0 || py >= mode_res_h) return 0;
    }
    return 1;
}

void graphics_set_window(int use_screen, double x1, double y1, double x2, double y2) {
    window_active = 1;
    window_screen = use_screen;
    win_x1 = x1; win_y1 = y1; win_x2 = x2; win_y2 = y2;
}
void graphics_reset_window() {
    window_active = 0;
}
void graphics_set_view(int use_screen, int x1, int y1, int x2, int y2,
                       int has_color, unsigned int color, int has_boundary, unsigned int boundary) {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    view_active = 1;
    view_screen = use_screen;
    view_x1 = x1; view_y1 = y1; view_x2 = x2; view_y2 = y2;
    // We should draw background color and boundary if needed
    // But for now, just set viewport
    // If color >= 0, fill viewport
    // If boundary >= 0, draw border
    if (has_color) {
        if (!renderer || !canvas) return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_Color draw_color = get_graphics_color(color);
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
        double xs = (double)canvas_width / mode_res_w;
        double ys = (double)canvas_height / mode_res_h;
        SDL_FRect r = { (float)(x1*xs), (float)(y1*ys), (float)((x2-x1+1)*xs), (float)((y2-y1+1)*ys) };
        SDL_RenderFillRect(renderer, &r);
        update_graphics();
    }
    if (has_boundary) {
        if (!renderer || !canvas) return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_Color draw_color = get_graphics_color(boundary);
        SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
        double xs = (double)canvas_width / mode_res_w;
        double ys = (double)canvas_height / mode_res_h;
        SDL_FRect top = { (float)(x1*xs), (float)(y1*ys), (float)((x2-x1+1)*xs), (float)ys };
        SDL_FRect bottom = { (float)(x1*xs), (float)(y2*ys), (float)((x2-x1+1)*xs), (float)ys };
        SDL_FRect left = { (float)(x1*xs), (float)(y1*ys), (float)xs, (float)((y2-y1+1)*ys) };
        SDL_FRect right = { (float)(x2*xs), (float)(y1*ys), (float)xs, (float)((y2-y1+1)*ys) };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
        update_graphics();
    }
}
void graphics_reset_view() {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    view_active = 0;
    view_x1 = 0; view_y1 = 0; view_x2 = mode_res_w - 1; view_y2 = mode_res_h - 1;
    remove_clipping();
}

void get_graphics_cursor(double *x, double *y) {
    if (x) *x = gfx_cursor_x;
    if (y) *y = gfx_cursor_y;
}

void set_graphics_cursor(double x, double y) {
    gfx_cursor_x = x;
    gfx_cursor_y = y;
}

static void reload_font(int target_height) {
    if (font_path[0] == '\0') return;
    
    if (font) {
        TTF_CloseFont(font);
        font = NULL;
    }

    // Clear glyph cache
    for (int i = 0; i < 128; i++) {
        if (glyph_cache[i]) {
            SDL_DestroyTexture(glyph_cache[i]);
            glyph_cache[i] = NULL;
        }
    }

    font = TTF_OpenFont(font_path, (float)target_height);
    if (!font) {
        fprintf(stderr, "Failed to reload font at size %d: %s\n", target_height, SDL_GetError());
        return;
    }

    // Update dimensions based on text layout
    current_col_width = canvas_width / text_columns;
    current_row_height = canvas_height / text_rows;

    // Re-populate Glyph Cache for ASCII
    SDL_Color white = {255, 255, 255, 255};
    int cell_w = current_col_width;
    int cell_h = current_row_height;
    for (int i = 32; i < 127; i++) {
        char s[2] = {(char)i, 0};
        SDL_Surface* surf = TTF_RenderText_Blended(font, s, 0, white);
        if (surf) {
            SDL_Surface* cell_surf = SDL_CreateSurface(cell_w, cell_h, SDL_PIXELFORMAT_RGBA8888);
            if (cell_surf) {
                SDL_ClearSurface(cell_surf, 0, 0, 0, 0);
                int oy = (cell_h - surf->h) / 2;
                if (oy < 0) oy = 0;
                SDL_Rect dst_rect = {0, oy, cell_w, surf->h};
                SDL_BlitSurfaceScaled(surf, NULL, cell_surf, &dst_rect, SDL_SCALEMODE_NEAREST);
                glyph_cache[i] = SDL_CreateTextureFromSurface(renderer, cell_surf);
                SDL_DestroySurface(cell_surf);
            } else {
                glyph_cache[i] = SDL_CreateTextureFromSurface(renderer, surf);
            }
            if (glyph_cache[i]) {
                SDL_SetTextureBlendMode(glyph_cache[i], SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(glyph_cache[i], SDL_SCALEMODE_NEAREST);
            }
            SDL_DestroySurface(surf);
        }
    }
}

/* Replaces the screen pages with count blank pages at the canvas size. */
static void allocate_pages(int count) {
    for (int i = 0; i < MAX_SCREEN_PAGES; i++) {
        if (pages[i]) SDL_DestroyTexture(pages[i]);
        pages[i] = NULL;
    }
    canvas = NULL;
    page_count = count < 1 ? 1 : (count > MAX_SCREEN_PAGES ? MAX_SCREEN_PAGES : count);
    visual_page = 0;
    if (!renderer) return;
    for (int i = 0; i < page_count; i++) {
        pages[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                     canvas_width, canvas_height);
        if (!pages[i]) continue;
        SDL_SetTextureBlendMode(pages[i], SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(pages[i], SDL_SCALEMODE_NEAREST); // Pixel-perfect retro graphics
        SDL_SetRenderTarget(renderer, pages[i]);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
    }
    canvas = pages[0];
}

/* Pages per mode, as on a VGA card. */
static int pages_for_mode(int mode) {
    switch (mode) {
        case 0: case 7: return 8;
        case 8: return 4;
        case 9: case 10: return 2;
        default: return 1;
    }
}

int graphics_page_count(void) {
    return page_count;
}

/* SCREEN ..., apage, vpage: returns 0 if either page does not exist. */
int graphics_set_pages(int active, int visual) {
    if (active < 0 || active >= page_count || visual < 0 || visual >= page_count) return 0;
    canvas = pages[active];
    visual_page = visual;
    graphics_present_if_autodisplay();
    return 1;
}

/* PCOPY source, destination. */
int graphics_copy_page(int source, int destination) {
    if (source < 0 || source >= page_count || destination < 0 || destination >= page_count) return 0;
    if (!renderer || source == destination) return 1;
    SDL_SetRenderTarget(renderer, pages[destination]);
    SDL_SetRenderClipRect(renderer, NULL);
    SDL_RenderTexture(renderer, pages[source], NULL, NULL);
    SDL_SetRenderTarget(renderer, canvas);
    if (destination == visual_page) graphics_present_if_autodisplay();
    return 1;
}

void set_screen_mode(int mode) {
    reset_dest_to_screen();
    current_screen_mode = mode;
    switch(mode) {
        case 1:  mode_res_w = 320; mode_res_h = 200; break;
        case 2:  mode_res_w = 640; mode_res_h = 200; break;
        case 7:  mode_res_w = 320; mode_res_h = 200; break;
        case 8:  mode_res_w = 640; mode_res_h = 200; break;
        case 9:  mode_res_w = 640; mode_res_h = 350; break;
        case 12: mode_res_w = 640; mode_res_h = 480; break;
        case 13: mode_res_w = 320; mode_res_h = 200; break;
        case 0:
        default: mode_res_w = 640; mode_res_h = 200; break;
    }

    canvas_width = mode_res_w * 2;
    canvas_height = mode_res_h * 2;
    color_mode_32 = 0;
    palette_mode_256 = (mode == 13);
    restore_default_palette();
    reset_draw_colors(mode);
    view_active = 0;
    window_active = 0;
    view_x1 = 0; view_y1 = 0; view_x2 = mode_res_w - 1; view_y2 = mode_res_h - 1;
    gfx_cursor_x = mode_res_w / 2;
    gfx_cursor_y = mode_res_h / 2;

    // Determine text layout based on classic BASIC mode documentation
    if (mode_res_w <= 320) text_columns = 40; else text_columns = 80;
    if (mode_res_h == 200) text_rows = 25;
    else if (mode_res_h == 350) text_rows = 35;
    else if (mode_res_h == 480) text_rows = 30; // standard 80x30 layout for 640x480
    else text_rows = 25;

    current_col_width = canvas_width / text_columns;
    current_row_height = canvas_height / text_rows;

    // Re-scale font and glyph cache for the new resolution
    if (font_path[0] != '\0') {
        reload_font(current_row_height);
    }
    
    if (current_row_height < 1) current_row_height = 1;

    // Recreate the pages at the new doubled resolution
    allocate_pages(pages_for_mode(mode));

    if (renderer && canvas) {
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (window) render_canvas_to_window();
    }
}

void set_screen_newimage(int width, int height, int colors) {
    if (width <= 0 || height <= 0) return;
    reset_dest_to_screen();
    color_mode_32 = (colors == 32);
    palette_mode_256 = !color_mode_32;
    current_screen_mode = -1;
    reset_draw_colors(-1);

    mode_res_w = width;
    mode_res_h = height;

    canvas_width = width;
    canvas_height = height;
    if (window) SDL_SetWindowSize(window, width, height);

    text_columns = width / 8;
    text_rows = height / 16;
    if (text_columns < 1) text_columns = 1;
    if (text_rows < 1) text_rows = 1;

    current_col_width = canvas_width / text_columns;
    current_row_height = canvas_height / text_rows;

    if (font_path[0] != '\0') {
        reload_font(current_row_height);
    }

    if (current_row_height < 1) current_row_height = 1;

    cursor_x = 0;
    cursor_y = 0;
    gfx_cursor_x = 0;
    gfx_cursor_y = 0;
    view_active = 0;
    window_active = 0;
    view_x1 = 0; view_y1 = 0; view_x2 = mode_res_w - 1; view_y2 = mode_res_h - 1;

    allocate_pages(1);

    if (renderer && canvas) {
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (window) render_canvas_to_window();
    }
}

/* WIDTH columns, rows: re-lays the text grid over the current screen. */
void graphics_set_text_size(int columns, int rows) {
    if (columns < 1 || rows < 1) return;
    text_columns = columns;
    text_rows = rows;
    current_col_width = canvas_width / text_columns;
    current_row_height = canvas_height / text_rows;
    if (current_col_width < 1) current_col_width = 1;
    if (current_row_height < 1) current_row_height = 1;
    if (font_path[0] != '\0') reload_font(current_row_height);
    cursor_x = 0;
    cursor_y = 0;
}

int graphics_get_text_rows(void) {
    return text_rows;
}

int graphics_get_text_cols(void) {
    return text_columns;
}

static void check_scroll() {
    if (!font) return;
    if (cursor_y + current_row_height > canvas_height) {
        SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, canvas_width, canvas_height);
        SDL_SetRenderTarget(renderer, temp);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_FRect src = {0.0f, (float)current_row_height, (float)canvas_width, (float)(canvas_height - current_row_height)};
        SDL_FRect dst = {0.0f, 0.0f, (float)canvas_width, (float)(canvas_height - current_row_height)};
        SDL_RenderTexture(renderer, canvas, &src, &dst);

        SDL_SetRenderTarget(renderer, canvas);
        SDL_RenderTexture(renderer, temp, NULL, NULL);

        // Clear the new line at the bottom
        SDL_FRect bottom_rect = {0.0f, (float)(canvas_height - current_row_height), (float)canvas_width, (float)current_row_height};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, &bottom_rect);

        SDL_DestroyTexture(temp);
        cursor_y -= current_row_height;
    }
}

void graphics_cls() {
    if (renderer && canvas) {
        SDL_Color background = get_graphics_color(bg_color);
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderClipRect(renderer, NULL);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, NULL);
        cursor_x = 0;
        cursor_y = 0;
        update_graphics();
    }
}

void graphics_sleep(int ms) {
    if (ms > 0) {
        graphics_present_if_autodisplay();
        handle_events();
        SDL_Delay(ms);
    }
}

/* Waits up to ms milliseconds while keeping the window responsive. When
 * wake_on_key is set, a key press ends the wait early and stays buffered for
 * INKEY$. A negative ms waits until a key is pressed. */
static void wait_responsive(double ms, int wake_on_key) {
    Uint64 start = SDL_GetTicksNS();
    Uint64 limit = ms < 0 ? 0 : (Uint64)(ms * 1000000.0);
    while (!stop_running) {
        handle_events();
        if (wake_on_key && (last_key_char || last_key_code)) break;
        Uint64 elapsed = SDL_GetTicksNS() - start;
        if (ms >= 0 && elapsed >= limit) break;
        Uint64 remaining = ms >= 0 ? limit - elapsed : 10000000;
        Uint32 step = (Uint32)(remaining / 1000000);
        if (step > 10) step = 10;
        if (step == 0) SDL_DelayPrecise(remaining);
        else SDL_Delay(step);
    }
}

/* SLEEP [seconds]: QBasic waits the given seconds or until a key is pressed;
 * no argument (or 0) waits for a key. */
void graphics_sleep_seconds(double seconds) {
    graphics_present_if_autodisplay();
    wait_responsive(seconds > 0 ? seconds * 1000.0 : -1, 1);
}

/* _DELAY seconds: pause without waking on keys. */
void graphics_delay(double seconds) {
    graphics_present_if_autodisplay();
    if (seconds > 0) wait_responsive(seconds * 1000.0, 0);
    else handle_events();
}

/* _LIMIT fps: hold a loop to at most fps iterations per second. */
void graphics_limit(double fps) {
    static Uint64 next_frame = 0;
    if (fps <= 0) return;
    Uint64 period = (Uint64)(1000000000.0 / fps);
    Uint64 now = SDL_GetTicksNS();
    if (next_frame == 0 || now > next_frame + period) next_frame = now;
    if (next_frame > now) wait_responsive((double)(next_frame - now) / 1000000.0, 0);
    else handle_events();
    next_frame += period;
}

static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int map_key_to_trap_index(SDL_Keycode key) {
    switch (key) {
        case SDLK_F1: return 1;
        case SDLK_F2: return 2;
        case SDLK_F3: return 3;
        case SDLK_F4: return 4;
        case SDLK_F5: return 5;
        case SDLK_F6: return 6;
        case SDLK_F7: return 7;
        case SDLK_F8: return 8;
        case SDLK_F9: return 9;
        case SDLK_F10: return 10;
        case SDLK_UP: return 11;
        case SDLK_LEFT: return 12;
        case SDLK_RIGHT: return 13;
        case SDLK_DOWN: return 14;
        default:
            break;
    }

    return 0;
}

static void clear_text_cell(int x, int y) {
    if (!renderer || !canvas) return;
    SDL_Color background = get_graphics_color(bg_color);
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, 255);
    SDL_FRect cell = {(float)x, (float)y, (float)current_col_width, (float)current_row_height};
    SDL_RenderFillRect(renderer, &cell);
}

void graphics_print(const char *text) {
    if (!font || !canvas || !text) return;
    SDL_SetRenderTarget(renderer, canvas);
    
    while (*text) {
        if (*text == '\n') {
            cursor_x = 0;
            cursor_y += current_row_height;
            check_scroll();
            SDL_SetRenderTarget(renderer, canvas);
            text++;
            continue;
        }
        if (*text == '\r') {
            cursor_x = 0;
            text++;
            continue;
        }
        if (*text == ' ') {
            clear_text_cell(cursor_x, cursor_y);
            cursor_x += current_col_width;
            text++;
        } else {
            clear_text_cell(cursor_x, cursor_y);
            unsigned char c = (unsigned char)*text;
            SDL_Texture **cache = get_active_glyph_cache();
            if (c < 128 && cache[c]) {
                SDL_SetTextureColorMod(cache[c], current_text_color.r, current_text_color.g, current_text_color.b);
                SDL_FRect dest = {(float)cursor_x, (float)cursor_y, (float)current_col_width, (float)current_row_height};
                SDL_RenderTexture(renderer, cache[c], NULL, &dest);
                text++;
            } else {
                int len = utf8_char_len(c);
                char s[5] = {0};
                for (int i = 0; i < len && text[i]; i++) s[i] = text[i];
                SDL_Surface* surf = TTF_RenderText_Blended(font, s, 0, current_text_color);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                    SDL_FRect dest = {(float)cursor_x, (float)cursor_y, (float)current_col_width, (float)current_row_height};
                    SDL_RenderTexture(renderer, tex, NULL, &dest);
                    SDL_DestroySurface(surf);
                    SDL_DestroyTexture(tex);
                }
                text += len;
            }
            cursor_x += current_col_width;
        }

        if (cursor_x >= canvas_width) {
            cursor_x = 0;
            cursor_y += current_row_height;
            check_scroll();
            SDL_SetRenderTarget(renderer, canvas);
        }
    }
    update_graphics();
}

void graphics_printstring(int px, int py, const char *text) {
    if (!font || !canvas || !text) return;
    SDL_SetRenderTarget(renderer, canvas);

    SDL_Surface* surf = TTF_RenderText_Blended(font, text, 0, current_text_color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            SDL_FRect dest = {(float)px, (float)py, (float)surf->w, (float)surf->h};
            SDL_RenderTexture(renderer, tex, NULL, &dest);
            SDL_DestroyTexture(tex);
        }
        SDL_DestroySurface(surf);
    }
    update_graphics();
}

int graphics_printwidth(const char *text) {
    if (!text) return 0;
    if (font) {
        int w = 0, h = 0;
        if (TTF_GetStringSize(font, text, 0, &w, &h)) {
            return w;
        }
    }
    return (int)strlen(text) * current_col_width;
}

/* ---- Images (_NEWIMAGE, _LOADIMAGE, _DEST, _SOURCE, _PUTIMAGE) ----
 * Every drawing command uses the "current canvas" globals. An image keeps
 * its own copy of that state, and _DEST swaps it in, so all primitives,
 * PRINT and PAINT draw into images unchanged. Handles follow QB64: images
 * are below -1, -1 means failure and 0 is the screen. */

typedef struct {
    SDL_Texture *canvas;
    int canvas_width, canvas_height, mode_res_w, mode_res_h;
    int color_mode_32, palette_mode_256, screen_mode;
    unsigned int fg_color, bg_color;
    SDL_Color text_color;
    double gfx_cursor_x, gfx_cursor_y;
    int cursor_x, cursor_y;
    int text_columns, text_rows, col_width, row_height;
    int view_active, view_screen, view_x1, view_y1, view_x2, view_y2;
    int window_active, window_screen;
    double win_x1, win_y1, win_x2, win_y2;
} DrawState;

typedef struct {
    int in_use;
    DrawState state; /* state.canvas is the image's texture */
} ImageSlot;

static ImageSlot image_slots[MAX_IMAGE_SLOTS];
static DrawState screen_state;
static int dest_handle = 0;   /* 0 = screen */
static int source_handle = 0;

static void save_draw_state(DrawState *s) {
    *s = (DrawState){canvas, canvas_width, canvas_height, mode_res_w, mode_res_h,
                     color_mode_32, palette_mode_256, current_screen_mode,
                     fg_color, bg_color, current_text_color, gfx_cursor_x, gfx_cursor_y,
                     cursor_x, cursor_y, text_columns, text_rows, current_col_width, current_row_height,
                     view_active, view_screen, view_x1, view_y1, view_x2, view_y2,
                     window_active, window_screen, win_x1, win_y1, win_x2, win_y2};
}

static void load_draw_state(const DrawState *s) {
    canvas = s->canvas; canvas_width = s->canvas_width; canvas_height = s->canvas_height;
    mode_res_w = s->mode_res_w; mode_res_h = s->mode_res_h;
    color_mode_32 = s->color_mode_32; palette_mode_256 = s->palette_mode_256;
    current_screen_mode = s->screen_mode;
    fg_color = s->fg_color; bg_color = s->bg_color; current_text_color = s->text_color;
    gfx_cursor_x = s->gfx_cursor_x; gfx_cursor_y = s->gfx_cursor_y;
    cursor_x = s->cursor_x; cursor_y = s->cursor_y;
    text_columns = s->text_columns; text_rows = s->text_rows;
    current_col_width = s->col_width; current_row_height = s->row_height;
    view_active = s->view_active; view_screen = s->view_screen;
    view_x1 = s->view_x1; view_y1 = s->view_y1; view_x2 = s->view_x2; view_y2 = s->view_y2;
    window_active = s->window_active; window_screen = s->window_screen;
    win_x1 = s->win_x1; win_y1 = s->win_y1; win_x2 = s->win_x2; win_y2 = s->win_y2;
}

static ImageSlot *image_for_handle(int handle) {
    int slot = -handle - 2;
    if (handle >= -1 || slot >= MAX_IMAGE_SLOTS || !image_slots[slot].in_use) return NULL;
    return &image_slots[slot];
}

/* The saved state of a handle (0 = screen); NULL for an invalid handle. */
static DrawState *state_for_handle(int handle) {
    if (handle == 0) return &screen_state;
    ImageSlot *image = image_for_handle(handle);
    return image ? &image->state : NULL;
}

/* Makes handle the current canvas, saving the previous one's state. */
static int switch_canvas(int handle) {
    if (handle == dest_handle) return 1;
    DrawState *target = state_for_handle(handle);
    DrawState *current = state_for_handle(dest_handle);
    if (!target) return 0;
    if (current) save_draw_state(current);
    load_draw_state(target);
    dest_handle = handle;
    if (renderer && canvas) SDL_SetRenderTarget(renderer, canvas);
    return 1;
}

/* SCREEN always draws on the screen again, as in QB64. */
static void reset_dest_to_screen(void) {
    if (dest_handle != 0) switch_canvas(0);
    source_handle = 0;
}

int graphics_valid_handle(int handle) {
    return state_for_handle(handle) != NULL;
}

/* _DEST handle: later drawing goes to that image (0 is the screen). */
int graphics_set_dest(int handle) {
    return switch_canvas(handle);
}

int graphics_get_dest(void) { return dest_handle; }

/* _SOURCE handle: POINT, GET and _PUTIMAGE read from that image. */
int graphics_set_source(int handle) {
    if (!state_for_handle(handle)) return 0;
    source_handle = handle;
    return 1;
}

int graphics_get_source(void) { return source_handle; }

/* POINT/GET read the _SOURCE image. */
double graphics_point(double x, double y) {
    if (source_handle == dest_handle || !state_for_handle(source_handle)) return get_pixel(x, y);
    int previous = dest_handle;
    switch_canvas(source_handle);
    double value = get_pixel(x, y);
    switch_canvas(previous);
    return value;
}

static int free_image_slot(void) {
    for (int i = 0; i < MAX_IMAGE_SLOTS; i++) {
        if (!image_slots[i].in_use) return i;
    }
    return -1;
}

/* Creates a blank width x height image; mode 32 makes it 32-bit (cleared to
 * transparent black), 256 or 13 a 256-color image, and any other screen
 * mode number a 16-color image. Returns the handle, or -1. */
static int create_image(int width, int height, int mode, SDL_Texture *contents, int contents_scaled) {
    if (!renderer || width < 1 || height < 1 || width > 16384 || height > 16384) return -1;
    int slot = free_image_slot();
    if (slot < 0) return -1;
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                             width, height);
    if (!texture) return -1;
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, mode == 32 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderClipRect(renderer, NULL);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, mode == 32 ? 0 : 255);
    SDL_RenderClear(renderer);
    if (contents) {
        SDL_BlendMode original;
        SDL_GetTextureBlendMode(contents, &original);
        SDL_SetTextureBlendMode(contents, SDL_BLENDMODE_NONE);
        SDL_FRect dest = {0, 0, (float)width, (float)height};
        SDL_RenderTexture(renderer, contents, NULL, contents_scaled ? &dest : NULL);
        SDL_SetTextureBlendMode(contents, original);
    }
    SDL_SetRenderTarget(renderer, canvas);

    ImageSlot *image = &image_slots[slot];
    image->in_use = 1;
    /* Start from the screen's font metrics, then describe the image. */
    save_draw_state(&image->state);
    DrawState *st = &image->state;
    st->canvas = texture;
    st->canvas_width = st->mode_res_w = width;
    st->canvas_height = st->mode_res_h = height;
    st->color_mode_32 = mode == 32;
    st->palette_mode_256 = mode == 256 || mode == 13;
    st->screen_mode = (mode == 32 || mode == 256) ? -1 : mode;
    st->fg_color = mode == 32 ? 0xFFFFFFFFu : 15;
    st->bg_color = mode == 32 ? 0xFF000000u : 0;
    st->gfx_cursor_x = width / 2;
    st->gfx_cursor_y = height / 2;
    st->cursor_x = st->cursor_y = 0;
    st->text_columns = width / 8 > 0 ? width / 8 : 1;
    st->text_rows = height / 16 > 0 ? height / 16 : 1;
    st->col_width = width / st->text_columns;
    st->row_height = height / st->text_rows;
    st->view_active = 0; st->view_screen = 0;
    st->view_x1 = 0; st->view_y1 = 0; st->view_x2 = width - 1; st->view_y2 = height - 1;
    st->window_active = 0;
    st->text_color = st->color_mode_32 ? (SDL_Color){255, 255, 255, 255} : current_text_color;
    return -(slot + 2);
}

int graphics_newimage(int width, int height, int mode) {
    if (mode != 32 && mode != 256 && (mode < 0 || mode > 13)) return -1;
    return create_image(width, height, mode, NULL, 0);
}

int graphics_loadimage(const char *filename, int mode) {
    if (!filename || !renderer) return -1;
    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) return -1;
    SDL_Texture *loaded = SDL_CreateTextureFromSurface(renderer, surface);
    int width = surface->w, height = surface->h;
    SDL_DestroySurface(surface);
    if (!loaded) return -1;
    int handle = create_image(width, height, mode == 256 ? 256 : 32, loaded, 0);
    SDL_DestroyTexture(loaded);
    return handle;
}

/* _COPYIMAGE(handle): a new image with the same size, mode and pixels. */
int graphics_copyimage(int handle) {
    DrawState *source = state_for_handle(handle);
    if (!source) return -1;
    if (handle == dest_handle) save_draw_state(source);
    int mode = source->color_mode_32 ? 32 : source->palette_mode_256 ? 256
             : source->screen_mode >= 0 ? source->screen_mode : 256;
    return create_image(source->mode_res_w, source->mode_res_h, mode, source->canvas, 1);
}

int graphics_freeimage(int handle) {
    ImageSlot *image = image_for_handle(handle);
    if (!renderer || !image) return 0;
    if (dest_handle == handle) switch_canvas(0);
    if (source_handle == handle) source_handle = 0;
    SDL_DestroyTexture(image->state.canvas);
    memset(image, 0, sizeof(*image));
    return 1;
}

/* _WIDTH(handle) / _HEIGHT(handle); handle 1 means the current destination. */
int graphics_image_size(int handle, int want_height) {
    if (handle == 1) return want_height ? mode_res_h : mode_res_w;
    DrawState *state = state_for_handle(handle);
    if (!state) return -1;
    if (handle == dest_handle) return want_height ? mode_res_h : mode_res_w;
    return want_height ? state->mode_res_h : state->mode_res_w;
}

/* _PUTIMAGE: copies the source rectangle of one image onto the destination
 * rectangle of another (both in their own pixel coordinates). A single
 * destination point keeps the source size; no destination stretches over the
 * whole image; a reversed rectangle mirrors the picture. */
int graphics_putimage_ex(int has_dest, int dx1, int dy1, int has_dest2, int dx2, int dy2,
                         int source, int destination,
                         int has_src, int sx1, int sy1, int has_src2, int sx2, int sy2) {
    if (!renderer) return 0;
    DrawState *current = state_for_handle(dest_handle);
    if (current) save_draw_state(current); // the live state is the destination's
    DrawState *src = state_for_handle(source);
    DrawState *dst = state_for_handle(destination);
    if (!src || !dst || !src->canvas || !dst->canvas) return 0;
    if (!has_src) { sx1 = 0; sy1 = 0; sx2 = src->mode_res_w - 1; sy2 = src->mode_res_h - 1; }
    else if (!has_src2) { sx2 = src->mode_res_w - 1; sy2 = src->mode_res_h - 1; }
    int flip_x = 0, flip_y = 0;
    if (sx2 < sx1) { int t = sx1; sx1 = sx2; sx2 = t; flip_x = !flip_x; }
    if (sy2 < sy1) { int t = sy1; sy1 = sy2; sy2 = t; flip_y = !flip_y; }
    if (!has_dest) { dx1 = 0; dy1 = 0; dx2 = dst->mode_res_w - 1; dy2 = dst->mode_res_h - 1; }
    else if (!has_dest2) { dx2 = dx1 + (sx2 - sx1); dy2 = dy1 + (sy2 - sy1); }
    if (dx2 < dx1) { int t = dx1; dx1 = dx2; dx2 = t; flip_x = !flip_x; }
    if (dy2 < dy1) { int t = dy1; dy1 = dy2; dy2 = t; flip_y = !flip_y; }

    double sxs = (double)src->canvas_width / src->mode_res_w, sys = (double)src->canvas_height / src->mode_res_h;
    double dxs = (double)dst->canvas_width / dst->mode_res_w, dys = (double)dst->canvas_height / dst->mode_res_h;
    SDL_FRect source_rect = {(float)(sx1 * sxs), (float)(sy1 * sys),
                             (float)((sx2 - sx1 + 1) * sxs), (float)((sy2 - sy1 + 1) * sys)};
    SDL_FRect dest_rect = {(float)(dx1 * dxs), (float)(dy1 * dys),
                           (float)((dx2 - dx1 + 1) * dxs), (float)((dy2 - dy1 + 1) * dys)};
    SDL_Texture *source_texture = src->canvas;
    SDL_Texture *copy = NULL;
    if (src->canvas == dst->canvas) {
        // A texture cannot be drawn onto itself; copy the source first.
        int handle = graphics_copyimage(source);
        ImageSlot *image = image_for_handle(handle);
        if (!image) return 0;
        copy = image->state.canvas;
        source_texture = copy;
        image->state.canvas = NULL;
        memset(image, 0, sizeof(*image));
    }
    SDL_BlendMode original;
    SDL_GetTextureBlendMode(source_texture, &original);
    // 32-bit images blend by alpha; the screen and palette images copy.
    SDL_SetTextureBlendMode(source_texture, src->color_mode_32 && source != 0 ? SDL_BLENDMODE_BLEND
                                                                              : SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, dst->canvas);
    SDL_SetRenderClipRect(renderer, NULL);
    SDL_FlipMode flip = (SDL_FlipMode)((flip_x ? SDL_FLIP_HORIZONTAL : 0) | (flip_y ? SDL_FLIP_VERTICAL : 0));
    SDL_RenderTextureRotated(renderer, source_texture, &source_rect, &dest_rect, 0, NULL, flip);
    SDL_SetTextureBlendMode(source_texture, original);
    if (copy) SDL_DestroyTexture(copy);
    SDL_SetRenderTarget(renderer, canvas);
    if (destination == 0) update_graphics();
    return 1;
}

/* The older BASIKA form: _PUTIMAGE (x1,y1)[-(x2,y2)], handle[, (sx1,sy1)-(sx2,sy2)]. */
int graphics_putimage(int x1, int y1, int x2, int y2, int handle,
                      int sx1, int sy1, int sx2, int sy2, int has_source) {
    int has_dest2 = !(x2 < x1 || y2 < y1);
    return graphics_putimage_ex(1, x1, y1, has_dest2, x2, y2, handle, dest_handle,
                                has_source, sx1, sy1, has_source, sx2, sy2);
}

/* SCREEN handle: shows an image by making the screen a copy of it. */
int graphics_screen_from_image(int handle) {
    DrawState *image = state_for_handle(handle);
    if (!image || handle == 0) return 0;
    int mode = image->color_mode_32 ? 32 : 256;
    int previous_dest = dest_handle;
    switch_canvas(0);
    set_screen_newimage(image->mode_res_w, image->mode_res_h, mode);
    save_draw_state(&screen_state);
    graphics_putimage_ex(0, 0, 0, 0, 0, 0, handle, 0, 0, 0, 0, 0, 0, 0);
    if (previous_dest != 0 && previous_dest != handle) switch_canvas(previous_dest);
    return 1;
}

/* ---- Font slot management for _LOADFONT / _FONT / _FREEFONT ---- */

#define MAX_FONT_SLOTS 32

typedef struct {
    TTF_Font *font;
    SDL_Texture *glyph_cache[128];
    int col_width;
    int row_height;
    int in_use;
} FontSlot;

static FontSlot font_slots[MAX_FONT_SLOTS];
static int active_font_slot = -1;  /* -1 = default built-in font */

/* Return the glyph cache for the currently active font */
static SDL_Texture **get_active_glyph_cache(void) {
    if (active_font_slot >= 0 && font_slots[active_font_slot].in_use) {
        return font_slots[active_font_slot].glyph_cache;
    }
    return glyph_cache;
}

/* Helper: build glyph cache for a font slot */
static void build_slot_glyph_cache(FontSlot *slot) {
    if (!slot->font || !renderer) return;
    SDL_Color white = {255, 255, 255, 255};
    int cell_w = slot->col_width > 0 ? slot->col_width : 16;
    int cell_h = slot->row_height > 0 ? slot->row_height : 32;
    for (int i = 32; i < 127; i++) {
        if (slot->glyph_cache[i]) {
            SDL_DestroyTexture(slot->glyph_cache[i]);
            slot->glyph_cache[i] = NULL;
        }
        char s[2] = {(char)i, 0};
        SDL_Surface* surf = TTF_RenderText_Blended(slot->font, s, 0, white);
        if (surf) {
            SDL_Surface* cell_surf = SDL_CreateSurface(cell_w, cell_h, SDL_PIXELFORMAT_RGBA8888);
            if (cell_surf) {
                SDL_ClearSurface(cell_surf, 0, 0, 0, 0);
                int oy = (cell_h - surf->h) / 2;
                if (oy < 0) oy = 0;
                SDL_Rect dst_rect = {0, oy, cell_w, surf->h};
                SDL_BlitSurfaceScaled(surf, NULL, cell_surf, &dst_rect, SDL_SCALEMODE_NEAREST);
                slot->glyph_cache[i] = SDL_CreateTextureFromSurface(renderer, cell_surf);
                SDL_DestroySurface(cell_surf);
            } else {
                slot->glyph_cache[i] = SDL_CreateTextureFromSurface(renderer, surf);
            }
            if (slot->glyph_cache[i]) {
                SDL_SetTextureBlendMode(slot->glyph_cache[i], SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(slot->glyph_cache[i], SDL_SCALEMODE_NEAREST);
            }
            SDL_DestroySurface(surf);
        }
    }
}

int graphics_loadfont(const char *filename, int size) {
    if (!filename || size <= 0) return 0;
    TTF_Init();

    /* Find a free slot (slots 0..MAX_FONT_SLOTS-1) */
    int slot_idx = -1;
    for (int i = 0; i < MAX_FONT_SLOTS; i++) {
        if (!font_slots[i].in_use) { slot_idx = i; break; }
    }
    if (slot_idx < 0) return 0;  /* no free slots */

    TTF_Font *loaded = TTF_OpenFont(filename, (float)size);
    const char *search_prefixes[] = {
        "./",
        "../",
        "/usr/local/share/basika/",
        "/System/Library/Fonts/",
        "/System/Library/Fonts/Supplemental/",
        "/Library/Fonts/",
        NULL
    };
    if (!loaded) {
        for (int i = 0; search_prefixes[i] != NULL; i++) {
            char alt_path[512];
            snprintf(alt_path, sizeof(alt_path), "%s%s", search_prefixes[i], filename);
            loaded = TTF_OpenFont(alt_path, (float)size);
            if (loaded) break;
        }
    }
    if (!loaded) {
        const char *base = strrchr(filename, '/');
        base = base ? base + 1 : filename;
        for (int i = 0; search_prefixes[i] != NULL; i++) {
            char alt_path[512];
            snprintf(alt_path, sizeof(alt_path), "%s%s", search_prefixes[i], base);
            loaded = TTF_OpenFont(alt_path, (float)size);
            if (loaded) break;
        }
    }
    if (!loaded) {
        fprintf(stderr, "_LOADFONT error: Could not open font '%s': %s\n", filename, SDL_GetError());
        return 0;
    }

    FontSlot *slot = &font_slots[slot_idx];
    slot->font = loaded;
    slot->in_use = 1;
    memset(slot->glyph_cache, 0, sizeof(slot->glyph_cache));

    /* Compute glyph metrics */
    int glyph_w = 0, glyph_h = 0;
    TTF_GetStringSize(loaded, "W", 0, &glyph_w, &glyph_h);
    if (glyph_w > 0 && glyph_h > 0) {
        slot->col_width = glyph_w;
        slot->row_height = glyph_h;
        int line_skip = TTF_GetFontLineSkip(loaded);
        if (line_skip > slot->row_height) slot->row_height = line_skip;
    } else {
        slot->col_width = size / 2;
        slot->row_height = size;
    }

    if (renderer) build_slot_glyph_cache(slot);

    /* Handle is slot_idx + 1 (handles are 1-based; 0 means error) */
    return slot_idx + 1;
}

static TTF_Font *default_font = NULL;

int graphics_setfont(int handle) {
    if (handle <= 0 || handle > MAX_FONT_SLOTS) {
        /* Handle 0 or negative: revert to the default built-in font */
        if (handle == 0) {
            active_font_slot = -1;
            if (default_font) font = default_font;
            /* Restore default font metrics */
            current_col_width = canvas_width / text_columns;
            current_row_height = canvas_height / text_rows;
            if (default_font) {
                int glyph_w = 0, glyph_h = 0;
                if (TTF_GetStringSize(default_font, "W", 0, &glyph_w, &glyph_h)) {
                    int line_skip = TTF_GetFontLineSkip(default_font);
                    if (line_skip > current_row_height) current_row_height = line_skip;
                    if (glyph_h + 2 > current_row_height) current_row_height = glyph_h + 2;
                }
            }
            return 1;
        }
        return 0;
    }

    int slot_idx = handle - 1;
    if (!font_slots[slot_idx].in_use) return 0;

    FontSlot *slot = &font_slots[slot_idx];
    /* Save the default font pointer before first switch */
    if (!default_font && font) default_font = font;
    active_font_slot = slot_idx;
    font = slot->font;

    /* Build glyph cache for the slot if it was loaded before renderer was ready */
    if (renderer && slot->font && !slot->glyph_cache[32]) {
        build_slot_glyph_cache(slot);
    }

    /* Update active metrics to the slot's font (no glyph cache pointer copying) */
    current_col_width = slot->col_width;
    current_row_height = slot->row_height;

    return 1;
}

int graphics_freefont(int handle) {
    if (handle <= 0 || handle > MAX_FONT_SLOTS) return 0;
    int slot_idx = handle - 1;
    if (!font_slots[slot_idx].in_use) return 0;

    /* Cannot free the currently active font */
    if (active_font_slot == slot_idx) return 0;

    FontSlot *slot = &font_slots[slot_idx];
    for (int i = 0; i < 128; i++) {
        if (slot->glyph_cache[i]) { SDL_DestroyTexture(slot->glyph_cache[i]); slot->glyph_cache[i] = NULL; }
    }
    TTF_CloseFont(slot->font);
    slot->font = NULL;
    slot->in_use = 0;
    return 1;
}

void set_text_cursor(int row, int col) {
    if (!font) return;
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    cursor_x = (col - 1) * current_col_width;
    cursor_y = (row - 1) * current_row_height;
}

void get_text_cursor(int *row, int *col) {
    if (!font) {
        if (row) *row = 1;
        if (col) *col = 1;
        return;
    }
    if (row) *row = cursor_y / current_row_height + 1;
    if (col) *col = cursor_x / current_col_width + 1;
}

static int headless_mode = 0;

void set_graphics_headless(int headless) {
    headless_mode = headless;
}

int graphics_is_active() {
    return window != NULL;
}

int init_graphics() {
    if (window) return 1;
    if (headless_mode) {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return 0;
    SDL_InitSubSystem(SDL_INIT_JOYSTICK); // optional: STICK/STRIG work without a joystick
    if (!TTF_Init()) return 0;
    
    // Create a 4:3 Window (1024x768)
    SDL_WindowFlags flags = 0;
    if (headless_mode) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    window = SDL_CreateWindow("BASIKA Virtual Framebuffer", 1024, 768, flags);
        
    if (!window) return 0;
    renderer = SDL_CreateRenderer(window, NULL); // Use default renderer flags

    set_screen_mode(2); // Default graphics mode when no SCREEN command is issued
    
    // Search for Modern DOS fonts in common directories and use the first one found.
    const char *font_candidates[] = {
        "./fonts/ModernDOS8x16.ttf",
        "/usr/local/share/basika/fonts/ModernDOS8x16.ttf",
        "./ModernDOS8x16.ttf",
        "./ModernDOS9x18.ttf",
        "./ModernDOS10x20.ttf",
        "./ModernDOS11x22.ttf",
        "/Library/Fonts/ModernDOS8x16.ttf",
        "/Library/Fonts/ModernDOS9x18.ttf",
        "/Library/Fonts/ModernDOS10x20.ttf",
        "/Library/Fonts/ModernDOS11x22.ttf",
        "/System/Library/Fonts/ModernDOS8x16.ttf",
        "/System/Library/Fonts/ModernDOS9x18.ttf",
        "/usr/share/fonts/truetype/modern-dos/ModernDOS8x16.ttf",
        "/usr/share/fonts/truetype/modern-dos/ModernDOS9x18.ttf",
        NULL
    };
    for (int i = 0; font_candidates[i] != NULL; i++) {
        font = TTF_OpenFont(font_candidates[i], FONT_SIZE);
        if (font) {
            strncpy(font_path, font_candidates[i], sizeof(font_path) - 1);
            break;
        }
    }
    if (!font) {
        const char *fallback_fonts[] = {
            "/System/Library/Fonts/Monaco.ttf",
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "/Library/Fonts/Andale Mono.ttf",
            "/Library/Fonts/Arial.ttf",
            NULL
        };
        for (int i = 0; fallback_fonts[i] != NULL; i++) {
            font = TTF_OpenFont(fallback_fonts[i], FONT_SIZE);
            if (font) {
                strncpy(font_path, fallback_fonts[i], sizeof(font_path) - 1);
                break;
            }
        }
    }
    if (!font) {
        fprintf(stderr, "Failed to load any font: %s\n", SDL_GetError());
        return 0; // Indicate graphics initialization failure
    }

    // Now reload the font at the size matching the current mode
    reload_font(current_row_height);

    current_text_color = (SDL_Color){255, 255, 255, 255};
    if (window) render_canvas_to_window();
    return 1;
}

// Presentation throttling: present to the window at most once per interval (ms)
static Uint64 last_present = 0;
static int present_interval_ms = 16; // default ~60 FPS

static double get_target_aspect_ratio(void) {
    if (mode_res_h == 200 || mode_res_h == 350) {
        return 4.0 / 3.0; // CRT aspect ratio for legacy 200/350 line modes
    }
    if (canvas_width > 0 && canvas_height > 0) {
        return (double)canvas_width / (double)canvas_height;
    }
    return 4.0 / 3.0;
}

static void render_canvas_to_window(void) {
    if (!renderer || !canvas || !window) return;
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);
    double target_aspect = get_target_aspect_ratio();
    int dest_w = win_w;
    int dest_h = (int)(dest_w / target_aspect + 0.5);
    if (dest_h > win_h) {
        dest_h = win_h;
        dest_w = (int)(dest_h * target_aspect + 0.5);
    }
    SDL_FRect dst = { (float)((win_w - dest_w) / 2), (float)((win_h - dest_h) / 2), (float)dest_w, (float)dest_h };
    presented_area = dst;
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, pages[visual_page] ? pages[visual_page] : canvas, NULL, &dst);
    SDL_RenderPresent(renderer);
    last_present = SDL_GetTicks();
    SDL_SetRenderTarget(renderer, canvas);
}

void set_present_interval(int ms) {
    if (ms < 0) ms = 0;
    present_interval_ms = ms;
}

void graphics_present_now() {
    if (!renderer || !canvas) return;
    if (window) {
        render_canvas_to_window();
    }
}

void graphics_present_if_autodisplay() {
    if (autodisplay_enabled) graphics_present_now();
}

void graphics_set_autodisplay(int enabled) {
    autodisplay_enabled = enabled != 0;
    if (autodisplay_enabled) graphics_present_now();
}

void set_text_color(unsigned int color_value) {
    if (!color_mode_32 && !palette_mode_256) {
        /* 16-color modes: attributes 16-31 are the blinking variants. */
        if ((int)color_value < 0) color_value = 0;
        color_value &= 15;
    }
    current_text_color = get_graphics_color(color_value);
    if (current_text_color.a == 0 && !color_mode_32) current_text_color.a = 255;
}

static SDL_Color palette256[256];
static int palette_initialized = 0;

static SDL_Color default_palette256[256];

static void init_palette256(void) {
    if (palette_initialized) return;
    palette256[0]  = (SDL_Color){0, 0, 0, 255};
    palette256[1]  = (SDL_Color){0, 0, 170, 255};
    palette256[2]  = (SDL_Color){0, 170, 0, 255};
    palette256[3]  = (SDL_Color){0, 170, 170, 255};
    palette256[4]  = (SDL_Color){170, 0, 0, 255};
    palette256[5]  = (SDL_Color){170, 0, 170, 255};
    palette256[6]  = (SDL_Color){170, 85, 0, 255};
    palette256[7]  = (SDL_Color){170, 170, 170, 255};
    palette256[8]  = (SDL_Color){85, 85, 85, 255};
    palette256[9]  = (SDL_Color){85, 85, 255, 255};
    palette256[10] = (SDL_Color){85, 255, 85, 255};
    palette256[11] = (SDL_Color){85, 255, 255, 255};
    palette256[12] = (SDL_Color){255, 85, 85, 255};
    palette256[13] = (SDL_Color){255, 85, 255, 255};
    palette256[14] = (SDL_Color){255, 255, 85, 255};
    palette256[15] = (SDL_Color){255, 255, 255, 255};

    for (int i = 16; i < 256; i++) {
        double t = (double)(i - 16) / 240.0;
        double r = 0, g = 0, b = 0;
        if (t < 0.2) {
            double f = t / 0.2;
            r = 0; g = 255 * f; b = 255;
        } else if (t < 0.4) {
            double f = (t - 0.2) / 0.2;
            r = 0; g = 255; b = 255 * (1.0 - f);
        } else if (t < 0.6) {
            double f = (t - 0.4) / 0.2;
            r = 255 * f; g = 255; b = 0;
        } else if (t < 0.8) {
            double f = (t - 0.6) / 0.2;
            r = 255; g = 255 * (1.0 - f); b = 0;
        } else {
            double f = (t - 0.8) / 0.2;
            r = 255; g = 0; b = 255 * f;
        }
        palette256[i] = (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, 255};
    }
    memcpy(default_palette256, palette256, sizeof(palette256));
    palette_initialized = 1;
}

static SDL_Color get_graphics_color(unsigned int color_value) {
    if (color_mode_32) {
        return (SDL_Color){(Uint8)((color_value >> 16) & 0xFF), (Uint8)((color_value >> 8) & 0xFF),
                           (Uint8)(color_value & 0xFF), (Uint8)((color_value >> 24) & 0xFF)};
    }
    if (!palette_initialized) init_palette256();
    return palette256[color_value & 0xFF];
}

/* Color for a PALETTE value in the current mode: VGA modes (12, 13 and
 * 256-color images) take red + 256*green + 65536*blue with 0-63 components;
 * SCREEN 0 and 9 take a 6-bit EGA rgbRGB value; other modes take one of the
 * 16 default colors. Returns 0 for an out-of-range value. */
static int palette_value_to_color(long value, SDL_Color *out) {
    if (!palette_initialized) init_palette256();
    if (palette_mode_256 || current_screen_mode == 12) {
        if (value < 0 || value > 0x3F3F3F || (value & 0xC0C0C0)) return 0;
        out->r = (Uint8)((value & 0x3F) * 255 / 63);
        out->g = (Uint8)(((value >> 8) & 0x3F) * 255 / 63);
        out->b = (Uint8)(((value >> 16) & 0x3F) * 255 / 63);
    } else if (current_screen_mode == 0 || current_screen_mode == 9) {
        if (value < 0 || value > 63) return 0;
        out->r = (Uint8)(((value >> 2) & 1) * 170 + ((value >> 5) & 1) * 85);
        out->g = (Uint8)(((value >> 1) & 1) * 170 + ((value >> 4) & 1) * 85);
        out->b = (Uint8)((value & 1) * 170 + ((value >> 3) & 1) * 85);
    } else {
        if (value < 0 || value > 15) return 0;
        *out = default_palette256[value];
    }
    out->a = 255;
    return 1;
}

/* A new SCREEN mode starts with the default palette. */
static void restore_default_palette(void) {
    if (palette_initialized) memcpy(palette256, default_palette256, sizeof(palette256));
}

int graphics_palette_size(void) {
    return palette_mode_256 ? 256 : 16;
}

/* Pixels on screen keep their palette attribute, so after entries change
 * the canvas is recolored: every pixel showing an old color takes the new
 * color of the first entry that had it. */
static void apply_palette_change(const SDL_Color *old_palette) {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderClipRect(renderer, NULL);
    SDL_Surface *raw = SDL_RenderReadPixels(renderer, NULL);
    if (!raw) return;
    SDL_Surface *surf = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA8888);
    SDL_DestroySurface(raw);
    if (!surf) return;
    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surf->format);
    int limit = graphics_palette_size();
    /* Small open-addressed map from old RGB to the new pixel value. */
    enum { MAP_SIZE = 1024 };
    Uint32 keys[MAP_SIZE], values[MAP_SIZE];
    Uint8 used[MAP_SIZE] = {0};
    int changed = 0;
    for (int i = 0; i < limit; i++) {
        Uint32 key = ((Uint32)old_palette[i].r << 16) | ((Uint32)old_palette[i].g << 8) | old_palette[i].b;
        unsigned slot = (key * 2654435761u) % MAP_SIZE;
        while (used[slot] && keys[slot] != key) slot = (slot + 1) % MAP_SIZE;
        if (used[slot]) continue; // the first entry with this color owns it
        used[slot] = 1;
        keys[slot] = key;
        values[slot] = SDL_MapRGBA(details, NULL, palette256[i].r, palette256[i].g, palette256[i].b, 255);
        if (memcmp(&old_palette[i], &palette256[i], sizeof(SDL_Color)) != 0) changed = 1;
    }
    if (changed) {
        int pitch = surf->pitch / 4;
        Uint32 *pixels = (Uint32 *)surf->pixels;
        for (int y = 0; y < surf->h; y++) {
            for (int x = 0; x < surf->w; x++) {
                Uint8 r, g, b;
                SDL_GetRGB(pixels[y * pitch + x], details, NULL, &r, &g, &b);
                Uint32 key = ((Uint32)r << 16) | ((Uint32)g << 8) | b;
                unsigned slot = (key * 2654435761u) % MAP_SIZE;
                while (used[slot] && keys[slot] != key) slot = (slot + 1) % MAP_SIZE;
                if (used[slot]) pixels[y * pitch + x] = values[slot];
            }
        }
        SDL_UpdateTexture(canvas, NULL, surf->pixels, surf->pitch);
        graphics_present_if_autodisplay();
    }
    SDL_DestroySurface(surf);
}

/* PALETTE attribute, value for count entries (a value of -1 leaves its entry
 * unchanged). Returns 0 if an attribute or value is out of range. */
int graphics_set_palette(int count, const int *attributes, const long *values) {
    if (color_mode_32) return 0;
    if (!palette_initialized) init_palette256();
    SDL_Color old_palette[256];
    memcpy(old_palette, palette256, sizeof(palette256));
    for (int i = 0; i < count; i++) {
        if (attributes[i] < 0 || attributes[i] >= graphics_palette_size()) return 0;
        if (values[i] == -1) continue;
        SDL_Color c;
        if (!palette_value_to_color(values[i], &c)) {
            memcpy(palette256, old_palette, sizeof(palette256));
            return 0;
        }
        palette256[attributes[i]] = c;
    }
    current_text_color = get_graphics_color(fg_color);
    apply_palette_change(old_palette);
    return 1;
}

/* PALETTE with no arguments restores the default colors. */
void graphics_reset_palette(void) {
    if (color_mode_32) return;
    if (!palette_initialized) init_palette256();
    SDL_Color old_palette[256];
    memcpy(old_palette, palette256, sizeof(palette256));
    memcpy(palette256, default_palette256, sizeof(palette256));
    current_text_color = get_graphics_color(fg_color);
    apply_palette_change(old_palette);
}

static void reset_draw_colors(int mode) {
    if (color_mode_32) {
        fg_color = 0xFFFFFFFFu;
        bg_color = 0xFF000000u;
    } else {
        fg_color = (mode == 1) ? 3 : 15;
        bg_color = 0;
    }
    current_text_color = get_graphics_color(fg_color);
}

int graphics_is_32bit(void) {
    return color_mode_32;
}

void graphics_set_draw_colors(int has_fg, unsigned int fg, int has_bg, unsigned int bg) {
    if (has_fg) fg_color = fg;
    if (has_bg) bg_color = bg;
}

unsigned int graphics_get_foreground(void) {
    return fg_color;
}

unsigned int graphics_get_background(void) {
    return bg_color;
}

int graphics_width(void) {
    return mode_res_w;
}

int graphics_height(void) {
    return mode_res_h;
}

/* _RGB/_RGBA: a 32-bit color in 32-bit mode, otherwise the nearest palette index. */
unsigned int graphics_match_color(int r, int g, int b, int a) {
    if (color_mode_32) {
        return ((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
    }
    if (!palette_initialized) init_palette256();
    int limit = palette_mode_256 ? 256 : 16;
    int closest = 0;
    int closest_distance = 3 * 255 * 255 + 1;
    for (int i = 0; i < limit; i++) {
        int dr = (int)palette256[i].r - r;
        int dg = (int)palette256[i].g - g;
        int db = (int)palette256[i].b - b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < closest_distance) {
            closest = i;
            closest_distance = distance;
            if (distance == 0) break;
        }
    }
    return (unsigned int)closest;
}

/* _RED/_GREEN/_BLUE/_ALPHA: components of a color value in the current mode. */
void graphics_color_components(unsigned int color_value, int *r, int *g, int *b, int *a) {
    SDL_Color c = get_graphics_color(color_value);
    if (r) *r = c.r;
    if (g) *g = c.g;
    if (b) *b = c.b;
    if (a) *a = c.a;
}

/* ---- Logical-pixel rasterization ----
 * Primitives plot each logical pixel exactly once so translucent colors blend
 * evenly, and outlines stay 8-connected so PAINT cannot leak through them. */

static SDL_FRect *pixel_batch = NULL;
static int pixel_batch_count = 0;
static int pixel_batch_capacity = 0;

static void pixel_rect(int px, int py, SDL_FRect *r) {
    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    int x0 = (int)floor(px * xs), x1 = (int)floor((px + 1) * xs);
    int y0 = (int)floor(py * ys), y1 = (int)floor((py + 1) * ys);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    *r = (SDL_FRect){(float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0)};
}

static void batch_flush(void) {
    if (pixel_batch_count > 0 && renderer) {
        SDL_RenderFillRects(renderer, pixel_batch, pixel_batch_count);
    }
    pixel_batch_count = 0;
}

static void batch_plot(int px, int py) {
    if (!clip_point(px, py)) return;
    if (pixel_batch_count >= pixel_batch_capacity) {
        if (pixel_batch_count > 0 && pixel_batch_capacity >= 4096) {
            batch_flush();
        } else {
            int capacity = pixel_batch_capacity ? pixel_batch_capacity * 2 : 256;
            SDL_FRect *grown = realloc(pixel_batch, (size_t)capacity * sizeof(*grown));
            if (!grown) {
                batch_flush();
                if (!pixel_batch_capacity) return;
            } else {
                pixel_batch = grown;
                pixel_batch_capacity = capacity;
            }
        }
    }
    pixel_rect(px, py, &pixel_batch[pixel_batch_count++]);
}

/* Selects the draw color; returns 0 when the color is fully transparent. */
static int begin_draw(unsigned int color) {
    if (!renderer || !canvas) return 0;
    SDL_Color c = get_graphics_color(color);
    if (c.a == 0) return 0;
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderDrawBlendMode(renderer, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    pixel_batch_count = 0;
    return 1;
}

static void end_draw(void) {
    batch_flush();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* Bresenham line in logical pixels. style is the QBasic 16-bit mask whose
 * most significant bit is tested first; *style_bit carries the pattern
 * position across connected segments. skip_last omits the end point. */
static void raster_line(int x1, int y1, int x2, int y2, unsigned int style, int *style_bit, int skip_last) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        int last = (x1 == x2 && y1 == y2);
        if (last && skip_last) break;
        int bit = *style_bit;
        *style_bit = (bit + 1) & 15;
        if (style & (0x8000u >> bit)) batch_plot(x1, y1);
        if (last) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void set_pixel(double user_x, double user_y, unsigned int color) {
    gfx_cursor_x = user_x;
    gfx_cursor_y = user_y;
    int x, y;
    transform_coords(user_x, user_y, &x, &y);
    if (!begin_draw(color)) return;
    batch_plot(x, y);
    end_draw();
}

double get_pixel(double user_x, double user_y) {
    int x, y;
    transform_coords(user_x, user_y, &x, &y);
    if (!clip_point(x, y)) return -1;
    if (!renderer || !canvas) return 0;

    SDL_FRect cell;
    pixel_rect(x, y, &cell);
    int sx = (int)cell.x;
    int sy = (int)cell.y;
    if (sx < 0 || sx >= canvas_width || sy < 0 || sy >= canvas_height) return 0;

    SDL_SetRenderTarget(renderer, canvas);
    SDL_Surface *surf = SDL_RenderReadPixels(renderer, &(SDL_Rect){sx, sy, 1, 1});
    if (!surf) return 0;

    Uint8 r, g, b, a;
    SDL_ReadSurfacePixel(surf, 0, 0, &r, &g, &b, &a);
    SDL_DestroySurface(surf);

    if (color_mode_32) {
        // Images keep real alpha (transparent pixels read as &H00RRGGBB); the screen is opaque.
        return (double)(((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | b);
    }
    // Return the nearest palette entry so blended pixels remain readable.
    if (!palette_initialized) init_palette256();
    int closest = 0;
    int closest_distance = 3 * 255 * 255 + 1;
    for (int i = 0; i < 256; i++) {
        SDL_Color c = palette256[i];
        int dr = (int)c.r - r;
        int dg = (int)c.g - g;
        int db = (int)c.b - b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < closest_distance) {
            closest = i;
            closest_distance = distance;
            if (distance == 0) break;
        }
    }
    return closest;
}

void get_graphics_cursor_physical(int *x, int *y) {
    transform_coords(gfx_cursor_x, gfx_cursor_y, x, y);
}

void draw_line(double ux1, double uy1, double ux2, double uy2, unsigned int color, int box, unsigned int style) {
    int x1, y1, x2, y2;
    transform_coords(ux1, uy1, &x1, &y1);
    transform_coords(ux2, uy2, &x2, &y2);
    gfx_cursor_x = ux2;
    gfx_cursor_y = uy2;
    if (!begin_draw(color)) return;
    style &= 0xFFFFu;
    int style_bit = 0;

    int left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
    int top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
    if (box == 2) {
        // BF fills the clipped rectangle in one call; the style mask does not apply.
        int min_x = view_active ? view_x1 : 0, max_x = view_active ? view_x2 : mode_res_w - 1;
        int min_y = view_active ? view_y1 : 0, max_y = view_active ? view_y2 : mode_res_h - 1;
        if (left < min_x) left = min_x;
        if (top < min_y) top = min_y;
        if (right > max_x) right = max_x;
        if (bottom > max_y) bottom = max_y;
        if (left <= right && top <= bottom) {
            SDL_FRect a, b;
            pixel_rect(left, top, &a);
            pixel_rect(right, bottom, &b);
            SDL_FRect r = {a.x, a.y, b.x + b.w - a.x, b.y + b.h - a.y};
            SDL_RenderFillRect(renderer, &r);
        }
    } else if (box == 1) {
        raster_line(left, top, right, top, style, &style_bit, 0);
        if (bottom != top) raster_line(left, bottom, right, bottom, style, &style_bit, 0);
        if (bottom - top >= 2) {
            raster_line(left, top + 1, left, bottom - 1, style, &style_bit, 0);
            if (right != left) raster_line(right, top + 1, right, bottom - 1, style, &style_bit, 0);
        }
    } else {
        raster_line(x1, y1, x2, y2, style, &style_bit, 0);
    }
    end_draw();
    update_graphics();
}

/* QBasic's default aspect makes circles look round on a 4:3 display. */
static double default_circle_aspect(void) {
    if (canvas_width == mode_res_w && canvas_height == mode_res_h) return 1.0;
    return get_target_aspect_ratio() * (double)mode_res_h / (double)mode_res_w;
}

typedef struct {
    int cx, cy;
    double rx, ry;
    int is_arc;
    double start, end;
} EllipseRaster;

static void ellipse_point(const EllipseRaster *e, int dx, int dy) {
    if (e->is_arc) {
        double t = atan2(-(double)dy * (e->rx > 0 ? e->rx : 1), (double)dx * (e->ry > 0 ? e->ry : 1));
        if (t < 0) t += 2.0 * M_PI;
        int inside = e->start <= e->end ? (t >= e->start && t <= e->end)
                                        : (t >= e->start || t <= e->end);
        if (!inside) return;
    }
    batch_plot(e->cx + dx, e->cy + dy);
}

static void ellipse_point4(const EllipseRaster *e, int x, int y) {
    ellipse_point(e, x, y);
    if (x != 0) ellipse_point(e, -x, y);
    if (y != 0) {
        ellipse_point(e, x, -y);
        if (x != 0) ellipse_point(e, -x, -y);
    }
}

/* Midpoint ellipse: each outline pixel is produced exactly once. */
static void raster_ellipse(const EllipseRaster *e) {
    int rx = (int)lround(e->rx);
    int ry = (int)lround(e->ry);
    if (rx <= 0 && ry <= 0) {
        ellipse_point(e, 0, 0);
        return;
    }
    if (ry <= 0) {
        for (int x = -rx; x <= rx; x++) ellipse_point(e, x, 0);
        return;
    }
    if (rx <= 0) {
        for (int y = -ry; y <= ry; y++) ellipse_point(e, 0, y);
        return;
    }
    double rx2 = (double)rx * rx, ry2 = (double)ry * ry;
    int x = 0, y = ry;
    double dx = 0, dy = 2.0 * rx2 * y;
    double d1 = ry2 - rx2 * ry + 0.25 * rx2;
    while (dx < dy) {
        ellipse_point4(e, x, y);
        x++;
        dx += 2.0 * ry2;
        if (d1 < 0) {
            d1 += dx + ry2;
        } else {
            y--;
            dy -= 2.0 * rx2;
            d1 += dx - dy + ry2;
        }
    }
    double d2 = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1.0) * (y - 1.0) - rx2 * ry2;
    while (y >= 0) {
        ellipse_point4(e, x, y);
        y--;
        dy -= 2.0 * rx2;
        if (d2 > 0) {
            d2 += rx2 - dy;
        } else {
            x++;
            dx += 2.0 * ry2;
            d2 += dx - dy + rx2;
        }
    }
}

/* CIRCLE (x,y), radius, color, start, end, aspect. Negative start/end angles
 * also draw a radius line to that end of the arc, as in QBasic. */
void draw_circle(double ucx, double ucy, double uradius, unsigned int color,
                 int has_start, double start, int has_end, double end,
                 int has_aspect, double aspect) {
    gfx_cursor_x = ucx;
    gfx_cursor_y = ucy;
    int cx, cy, edge_x, edge_y;
    transform_coords(ucx, ucy, &cx, &cy);
    transform_coords(ucx + fabs(uradius), ucy, &edge_x, &edge_y);
    double radius = fabs((double)(edge_x - cx));
    if (!has_aspect || aspect <= 0) aspect = default_circle_aspect();

    EllipseRaster e = {cx, cy, radius, radius, has_start || has_end, 0, 2.0 * M_PI};
    if (aspect < 1.0) e.ry = radius * aspect;
    else e.rx = radius / aspect;
    int pie_start = has_start && start < 0;
    int pie_end = has_end && end < 0;
    if (has_start) e.start = fabs(start);
    if (has_end) e.end = fabs(end);

    if (!begin_draw(color)) return;
    raster_ellipse(&e);
    int style_bit = 0;
    if (pie_start) {
        raster_line(cx, cy, cx + (int)lround(e.rx * cos(e.start)), cy - (int)lround(e.ry * sin(e.start)),
                    0xFFFFu, &style_bit, 0);
    }
    if (pie_end) {
        raster_line(cx, cy, cx + (int)lround(e.rx * cos(e.end)), cy - (int)lround(e.ry * sin(e.end)),
                    0xFFFFu, &style_bit, 0);
    }
    end_draw();
    update_graphics();
}

typedef struct {
    int x, y;
} Point;

static Uint32 blend_paint_pixel(Uint32 destination, SDL_Color source,
                                const SDL_PixelFormatDetails *details, SDL_Palette *palette) {
    if (source.a == 255) return SDL_MapRGBA(details, palette, source.r, source.g, source.b, 255);
    Uint8 dr, dg, db;
    SDL_GetRGB(destination, details, palette, &dr, &dg, &db);
    int alpha = source.a;
    Uint8 r = (Uint8)((source.r * alpha + dr * (255 - alpha) + 127) / 255);
    Uint8 g = (Uint8)((source.g * alpha + dg * (255 - alpha) + 127) / 255);
    Uint8 b = (Uint8)((source.b * alpha + db * (255 - alpha) + 127) / 255);
    return SDL_MapRGBA(details, palette, r, g, b, 255);
}


/* Color of a PAINT tile at logical pixel (lx, ly). Tile rows use the screen
 * mode's packing: 1 bit per pixel (SCREEN 2 and 32-bit images), 2 bits
 * (SCREEN 1), four bit planes (SCREEN 7-12), or a byte per pixel (256 colors). */
static unsigned int tile_color(const unsigned char *tile, int length, int lx, int ly) {
    int bytes_per_row;
    if (color_mode_32 || current_screen_mode == 2 || current_screen_mode == 0) bytes_per_row = 1;
    else if (current_screen_mode == 1) bytes_per_row = 1;
    else if (palette_mode_256) bytes_per_row = 8;
    else bytes_per_row = 4;
    int rows = length / bytes_per_row;
    if (rows < 1) rows = 1;
    const unsigned char *row = tile + (ly % rows) * bytes_per_row;
    int available = length - (ly % rows) * bytes_per_row;

    if (bytes_per_row == 8) {
        int column = lx % 8;
        return column < available ? row[column] : 0;
    }
    if (bytes_per_row == 4) {
        unsigned int c = 0;
        for (int plane = 0; plane < 4 && plane < available; plane++) {
            if (row[plane] & (0x80 >> (lx % 8))) c |= 1u << plane;
        }
        return c;
    }
    if (!color_mode_32 && current_screen_mode == 1) {
        return (row[0] >> (6 - 2 * (lx % 4))) & 3;
    }
    return (row[0] & (0x80 >> (lx % 8))) ? fg_color : bg_color;
}

/* Flood fill from (ux, uy) up to border_color. With a tile, the fill paints
 * the tile pattern; otherwise it paints paint_color. */
static void flood_fill(double ux, double uy, unsigned int paint_color, unsigned int border_color,
                       const unsigned char *tile, int tile_length) {
    if (!renderer || !canvas) return;
    int x, y;
    transform_coords(ux, uy, &x, &y);
    gfx_cursor_x = ux;
    gfx_cursor_y = uy;
    if (!clip_point(x, y)) return;
    SDL_Color sc_paint = get_graphics_color(paint_color);
    if (!tile && sc_paint.a == 0) return;

    // Only the active VIEW can change, so read back just that region.
    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    SDL_Rect region = {0, 0, canvas_width, canvas_height};
    if (view_active) {
        SDL_FRect a, b;
        pixel_rect(view_x1, view_y1, &a);
        pixel_rect(view_x2, view_y2, &b);
        int x0 = (int)a.x, y0 = (int)a.y;
        int x1 = (int)(b.x + b.w), y1 = (int)(b.y + b.h);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > canvas_width) x1 = canvas_width;
        if (y1 > canvas_height) y1 = canvas_height;
        if (x1 <= x0 || y1 <= y0) return;
        region = (SDL_Rect){x0, y0, x1 - x0, y1 - y0};
    }
    SDL_FRect start_cell;
    pixel_rect(x, y, &start_cell);
    int sx = (int)start_cell.x - region.x;
    int sy = (int)start_cell.y - region.y;
    if (sx < 0 || sx >= region.w || sy < 0 || sy >= region.h) return;

    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderClipRect(renderer, NULL);
    SDL_Surface *raw_surf = SDL_RenderReadPixels(renderer, &region);
    if (!raw_surf) return;
    // Convert to canvas format to ensure color mapping matches the texture
    SDL_Surface *surf = SDL_ConvertSurface(raw_surf, SDL_PIXELFORMAT_RGBA8888);
    SDL_DestroySurface(raw_surf);
    if (!surf) return;

    SDL_Color sc_border = get_graphics_color(border_color);
    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surf->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(surf);
    Uint32 u_paint = SDL_MapRGBA(details, palette, sc_paint.r, sc_paint.g, sc_paint.b, 255);
    Uint32 u_border = SDL_MapRGBA(details, palette, sc_border.r, sc_border.g, sc_border.b, 255);
    // Mask out the alpha channel to make comparisons robust against driver-specific
    // variations in how alpha is handled in the render target or ReadPixels.
    Uint32 mask = ~details->Amask;
    int min_x = 0, min_y = 0, max_x = surf->w - 1, max_y = surf->h - 1;

    int pitch_pixels = surf->pitch / details->bytes_per_pixel;
    Uint32 *pixels = (Uint32 *)surf->pixels;
    Uint32 start_color = pixels[sy * pitch_pixels + sx];
    // A solid fill also stops at pixels already in the paint color, as in QBasic.
    int stop_at_paint = !tile && sc_paint.a == 255;
    if ((start_color & mask) == (u_border & mask) ||
        (stop_at_paint && (start_color & mask) == (u_paint & mask))) {
        SDL_DestroySurface(surf);
        return;
    }

    size_t capacity = (size_t)surf->w * (size_t)surf->h;
    Point *queue = malloc(capacity * sizeof(Point));
    Uint8 *visited = calloc(capacity, sizeof(Uint8));
    if (!queue || !visited) {
        free(queue);
        free(visited);
        SDL_DestroySurface(surf);
        return;
    }
    size_t head = 0, tail = 0;
    visited[(size_t)sy * (size_t)surf->w + (size_t)sx] = 1;
    queue[tail++] = (Point){sx, sy};

    while (head < tail) {
        Point p = queue[head++];
        Uint32 *pixel = &pixels[p.y * pitch_pixels + p.x];
        SDL_Color fill = sc_paint;
        if (tile) fill = get_graphics_color(tile_color(tile, tile_length, (int)((p.x + region.x) / xs),
                                                       (int)((p.y + region.y) / ys)));
        if (fill.a > 0) *pixel = blend_paint_pixel(*pixel, fill, details, palette);

        Point neighbors[4] = {
            {p.x + 1, p.y}, {p.x - 1, p.y}, {p.x, p.y + 1}, {p.x, p.y - 1}
        };
        for (int i = 0; i < 4; i++) {
            int nx = neighbors[i].x;
            int ny = neighbors[i].y;
            if (nx < min_x || nx > max_x || ny < min_y || ny > max_y) continue;
            size_t index = (size_t)ny * (size_t)surf->w + (size_t)nx;
            if (visited[index]) continue;
            Uint32 c = pixels[ny * pitch_pixels + nx];
            if ((c & mask) == (u_border & mask)) continue;
            if (stop_at_paint && (c & mask) == (u_paint & mask)) continue;
            visited[index] = 1;
            queue[tail++] = (Point){nx, ny};
        }
    }

    SDL_UpdateTexture(canvas, &region, surf->pixels, surf->pitch);
    SDL_DestroySurface(surf);
    free(queue);
    free(visited);
    graphics_present_if_autodisplay();
}

void draw_paint(double ux, double uy, unsigned int paint_color, unsigned int border_color) {
    flood_fill(ux, uy, paint_color, border_color, NULL, 0);
}

void draw_paint_tile(double ux, double uy, const unsigned char *tile, int tile_length, unsigned int border_color) {
    if (!tile || tile_length <= 0) return;
    flood_fill(ux, uy, fg_color, border_color, tile, tile_length);
}

int graphics_save_screenshot(const char *filename) {
    if (!renderer || !canvas || !filename) return 0;

    // Capture what is on screen: the visual page.
    SDL_SetRenderTarget(renderer, pages[visual_page] ? pages[visual_page] : canvas);
    SDL_Surface *surf = SDL_RenderReadPixels(renderer, &(SDL_Rect){0, 0, canvas_width, canvas_height});
    if (!surf) {
        fprintf(stderr, "RenderReadPixels failed: %s\n", SDL_GetError());
        return 0;
    }

    int result = 0;
    const char *ext = strrchr(filename, '.');
    
    if (ext && strcasecmp(ext, ".png") == 0) {
        if (IMG_SavePNG(surf, filename)) result = 1;
        else fprintf(stderr, "IMG_SavePNG failed: %s\n", SDL_GetError());
    } else if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        if (IMG_SaveJPG(surf, filename, 90)) result = 1;
        else fprintf(stderr, "IMG_SaveJPG failed: %s\n", SDL_GetError());
    }

    SDL_DestroySurface(surf);
    
    // Restore render target to canvas (common practice after read/write ops)
    SDL_SetRenderTarget(renderer, canvas);

    return result;
}

void set_window_title(const char *title) {
    if (!window || !title) return;
    SDL_SetWindowTitle(window, title);
    graphics_present_now();
}

void update_graphics() {
    if (!renderer || !canvas || !autodisplay_enabled) return;

    Uint64 now = SDL_GetTicks();
    if (present_interval_ms > 0 && (now - last_present < (Uint64)present_interval_ms)) {
        return;
    }
    handle_events(); // Process events only when a frame is presented

    if (window) {
        render_canvas_to_window();
    }
}

void graphics_readline(char *buffer, int size) {
    graphics_readline_initial(buffer, size, "");
}

/* Reads a line in the window, starting with editable text (used by EDIT). */
void graphics_readline_initial(char *buffer, int size, const char *initial) {
    snprintf(buffer, (size_t)size, "%s", initial ? initial : "");
    int pos = (int)strlen(buffer);
    SDL_StartTextInput(window);
    
    // Store the starting position for the current input line
    int line_start_x = cursor_x;
    int line_start_y = cursor_y;
    if (pos > 0) graphics_print(buffer);

    while (!stop_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) exit(0);
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                if (pos < size - 2) {
                    // Append the new character
                    strcat(buffer, e.text.text);
                    pos = strlen(buffer); 
                    buffer[pos] = '\0';

                    // Clear the current input line area and re-render
                    SDL_SetRenderTarget(renderer, canvas);
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_FRect clear_rect = {(float)line_start_x, (float)line_start_y, (float)(canvas_width - line_start_x), (float)current_row_height};
                    SDL_RenderFillRect(renderer, &clear_rect);

                    // Reset cursor for rendering the buffer
                    cursor_x = line_start_x;
                    cursor_y = line_start_y;
                    graphics_print(buffer); 
                }
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_RETURN) {
                    graphics_print("\n");
                    SDL_StopTextInput(window);
                    return;
                } else if (e.key.key == SDLK_BACKSPACE && pos > 0) {
                    buffer[--pos] = '\0'; 

                    // Clear the current input line area and re-render
                    SDL_SetRenderTarget(renderer, canvas);
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_FRect clear_rect = {(float)line_start_x, (float)line_start_y, (float)(canvas_width - line_start_x), (float)current_row_height};
                    SDL_RenderFillRect(renderer, &clear_rect);

                    // Reset cursor for rendering the buffer
                    cursor_x = line_start_x;
                    cursor_y = line_start_y;
                    graphics_print(buffer); 
                } else if (e.key.key == SDLK_C && (e.key.mod & SDL_KMOD_CTRL)) {
                    stop_running = 1;
                    SDL_StopTextInput(window);
                    return;
                }
            }
        }
        SDL_Delay(10);
    }
    SDL_StopTextInput(window);
}

/* ---- Mouse as light pen (PEN) and joysticks (STICK/STRIG) ---- */

static int pen_x = 0, pen_y = 0;           /* current position, screen pixels */
static int pen_down_x = 0, pen_down_y = 0; /* position of the last press */
static int pen_is_down = 0;
static int pen_pressed_since_poll = 0;
#define MAX_STICKS 2
static SDL_Joystick *sticks[MAX_STICKS] = {NULL};

static void update_pen_position(float window_x, float window_y) {
    if (presented_area.w <= 0 || presented_area.h <= 0) return;
    double fx = (window_x - presented_area.x) / presented_area.w;
    double fy = (window_y - presented_area.y) / presented_area.h;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;
    if (fy < 0) fy = 0;
    if (fy > 1) fy = 1;
    pen_x = (int)(fx * (mode_res_w - 1) + 0.5);
    pen_y = (int)(fy * (mode_res_h - 1) + 0.5);
}

/* PEN(n): 0 pressed since the last PEN(0); 1/2 x/y of the last press;
 * 3 button down now; 4/5 current x/y; 6/7 text row/column of the last press;
 * 8/9 current text row/column. Pixel values are screen coordinates. */
int graphics_pen(int n) {
    handle_events();
    double ys = (double)canvas_height / mode_res_h, xs = (double)canvas_width / mode_res_w;
    int row_height = current_row_height > 0 ? current_row_height : 16;
    int col_width = current_col_width > 0 ? current_col_width : 8;
    switch (n) {
        case 0: {
            int pressed = pen_pressed_since_poll;
            pen_pressed_since_poll = 0;
            return pressed ? -1 : 0;
        }
        case 1: return pen_down_x;
        case 2: return pen_down_y;
        case 3: return pen_is_down ? -1 : 0;
        case 4: return pen_x;
        case 5: return pen_y;
        case 6: return (int)(pen_down_y * ys) / row_height + 1;
        case 7: return (int)(pen_down_x * xs) / col_width + 1;
        case 8: return (int)(pen_y * ys) / row_height + 1;
        case 9: return (int)(pen_x * xs) / col_width + 1;
    }
    return 0;
}

/* STICK(n): x (even n) or y (odd n) of joystick A (0, 1) or B (2, 3),
 * scaled to QBasic's 1-200 range with 100 at rest; 0 when none is attached. */
int graphics_stick(int n) {
    handle_events();
    int index = n / 2;
    if (n < 0 || n > 3 || !sticks[index]) return 0;
    Sint16 axis = SDL_GetJoystickAxis(sticks[index], n % 2);
    return 1 + (int)((axis + 32768) * 199L / 65535);
}

/* Joystick buttons feed STRIG: button 1 of A and B is trigger 0 and 2,
 * button 2 is trigger 4 and 6. */
static void handle_stick_button(SDL_JoystickID id, int button, int pressed) {
    for (int i = 0; i < MAX_STICKS; i++) {
        if (!sticks[i] || SDL_GetJoystickID(sticks[i]) != id || button > 1) continue;
        basika_trigger_strig_event(button * 4 + i * 2, pressed);
    }
}

/* ---- QB64 keyboard (_KEYDOWN/_KEYHIT) and mouse (_MOUSE*) ---- */

#define KEYHIT_QUEUE_SIZE 64
static int keyhit_queue[KEYHIT_QUEUE_SIZE];
static int keyhit_head = 0, keyhit_tail = 0;
static int held_key_code[SDL_SCANCODE_COUNT];
static int mouse_buttons[4] = {0};
static int mouse_events_pending = 0;
static int mouse_wheel_total = 0;

/* QB64 key codes: ASCII for printable keys (letters follow Shift), 256 *
 * scan code for cursor and function keys, and 1003xx for modifier keys. */
static int qb64_key_code(SDL_Keycode key, SDL_Keymod mod) {
    switch (key) {
        case SDLK_BACKSPACE: return 8;
        case SDLK_TAB: return 9;
        case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
        case SDLK_ESCAPE: return 27;
        case SDLK_HOME: return 71 * 256;
        case SDLK_UP: return 72 * 256;
        case SDLK_PAGEUP: return 73 * 256;
        case SDLK_LEFT: return 75 * 256;
        case SDLK_RIGHT: return 77 * 256;
        case SDLK_END: return 79 * 256;
        case SDLK_DOWN: return 80 * 256;
        case SDLK_PAGEDOWN: return 81 * 256;
        case SDLK_INSERT: return 82 * 256;
        case SDLK_DELETE: return 83 * 256;
        case SDLK_F11: return 133 * 256;
        case SDLK_F12: return 134 * 256;
        case SDLK_LSHIFT: return 100304;
        case SDLK_RSHIFT: return 100303;
        case SDLK_LCTRL: return 100306;
        case SDLK_RCTRL: return 100305;
        case SDLK_LALT: return 100308;
        case SDLK_RALT: return 100307;
        default: break;
    }
    if (key >= SDLK_F1 && key <= SDLK_F10) return (int)(59 + (key - SDLK_F1)) * 256;
    if (key >= 'a' && key <= 'z') return (mod & SDL_KMOD_SHIFT) ? (int)key - 32 : (int)key;
    if (key >= 32 && key < 127) return (int)key;
    return 0;
}

static void push_keyhit(int code) {
    int next = (keyhit_tail + 1) % KEYHIT_QUEUE_SIZE;
    if (next == keyhit_head) return; // full: drop the newest
    keyhit_queue[keyhit_tail] = code;
    keyhit_tail = next;
}

static void track_key(const SDL_KeyboardEvent *event, int down) {
    if (event->scancode >= SDL_SCANCODE_COUNT) return;
    if (down) {
        int code = qb64_key_code(event->key, event->mod);
        if (!code) return;
        held_key_code[event->scancode] = code;
        push_keyhit(code);
    } else {
        int code = held_key_code[event->scancode];
        held_key_code[event->scancode] = 0;
        if (code) push_keyhit(-code);
    }
}

/* _KEYHIT: the next key press (positive) or release (negative), or 0. */
int graphics_keyhit(void) {
    handle_events();
    if (keyhit_head == keyhit_tail) return 0;
    int code = keyhit_queue[keyhit_head];
    keyhit_head = (keyhit_head + 1) % KEYHIT_QUEUE_SIZE;
    return code;
}

/* _KEYDOWN(code): whether the key with that QB64 code is held. */
int graphics_keydown(int code) {
    handle_events();
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        if (held_key_code[i] == code) return 1;
    }
    return 0;
}

/* _MOUSEINPUT: -1 while mouse events remain to be read, then 0. */
int graphics_mouse_input(void) {
    handle_events();
    if (mouse_events_pending > 0) {
        mouse_events_pending--;
        return -1;
    }
    return 0;
}

int graphics_mouse_x(void) { handle_events(); return pen_x; }
int graphics_mouse_y(void) { handle_events(); return pen_y; }

/* _MOUSEBUTTON(n): 1 left, 2 right, 3 middle. */
int graphics_mouse_button(int n) {
    handle_events();
    return (n >= 1 && n <= 3) ? mouse_buttons[n] : 0;
}

/* _MOUSEWHEEL: wheel movement since the last call (positive is toward the user). */
int graphics_mouse_wheel(void) {
    handle_events();
    int total = mouse_wheel_total;
    mouse_wheel_total = 0;
    return total;
}

void handle_events() {
    if (!window) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) exit(0);
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (e.text.text[0]) last_key_char = (unsigned char)e.text.text[0];
        }
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            track_key(&e.key, e.type == SDL_EVENT_KEY_DOWN);
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            update_pen_position(e.motion.x, e.motion.y);
            mouse_events_pending++;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            int button = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2
                       : e.button.button == SDL_BUTTON_MIDDLE ? 3 : 0;
            if (button) mouse_buttons[button] = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? -1 : 0;
            update_pen_position(e.button.x, e.button.y);
            mouse_events_pending++;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            mouse_wheel_total -= (int)e.wheel.y;
            mouse_events_pending++;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            update_pen_position(e.button.x, e.button.y);
            pen_down_x = pen_x;
            pen_down_y = pen_y;
            pen_is_down = 1;
            pen_pressed_since_poll = 1;
            basika_trigger_pen_event();
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) pen_is_down = 0;
        if (e.type == SDL_EVENT_JOYSTICK_ADDED) {
            for (int i = 0; i < MAX_STICKS; i++) {
                if (!sticks[i]) {
                    sticks[i] = SDL_OpenJoystick(e.jdevice.which);
                    break;
                }
            }
        }
        if (e.type == SDL_EVENT_JOYSTICK_REMOVED) {
            for (int i = 0; i < MAX_STICKS; i++) {
                if (sticks[i] && SDL_GetJoystickID(sticks[i]) == e.jdevice.which) {
                    SDL_CloseJoystick(sticks[i]);
                    sticks[i] = NULL;
                }
            }
        }
        if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
            handle_stick_button(e.jbutton.which, e.jbutton.button, e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN);
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            int trap_idx = map_key_to_trap_index(e.key.key);
            switch (e.key.key) {
                case SDLK_RETURN: last_key_char = 13; break;
                case SDLK_UP:    last_key_code = 1; break;
                case SDLK_RIGHT: last_key_code = 2; break;
                case SDLK_DOWN:  last_key_code = 3; break;
                case SDLK_LEFT:  last_key_code = 4; break;
                case SDLK_ESCAPE: stop_running = 1; break;
                case SDLK_C:
                    if (e.key.mod & SDL_KMOD_CTRL) stop_running = 1;
                    break;
                default:
                    if (e.key.key >= 32 && e.key.key < 127) {
                        last_key_char = (int)e.key.key;
                    }
                    break;
            }
            if (trap_idx > 0) {
                basika_trigger_key_event(trap_idx);
            }
        }
    }
}

int get_graphics_key(void) {
    handle_events();
    int key = last_key_code;
    last_key_code = 0;
    return key;
}

int get_graphics_char(void) {
    handle_events();
    int c = last_key_char;
    last_key_char = 0;
    return c;
}

void wait_for_keypress() {
    if (!window) return;
    SDL_Event e;
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) exit(0);
        if (e.type == SDL_EVENT_KEY_DOWN) break;
        if (e.type == SDL_EVENT_WINDOW_EXPOSED ||
            e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
            e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            graphics_present_now();
        }
    }
}

void close_graphics() {
    /* Restore font to default before cleanup to avoid double-close */
    if (active_font_slot >= 0 && default_font) {
        font = default_font;
    }
    active_font_slot = -1;
    default_font = NULL;

    /* Free user-loaded font slots */
    for (int s = 0; s < MAX_FONT_SLOTS; s++) {
        if (font_slots[s].in_use) {
            for (int i = 0; i < 128; i++) {
                if (font_slots[s].glyph_cache[i]) SDL_DestroyTexture(font_slots[s].glyph_cache[i]);
            }
            TTF_CloseFont(font_slots[s].font);
            font_slots[s].in_use = 0;
        }
    }

    for (int i = 0; i < 128; i++) {
        if (glyph_cache[i]) SDL_DestroyTexture(glyph_cache[i]);
    }
    for (int i = 0; i < MAX_IMAGE_SLOTS; i++) {
        if (image_slots[i].in_use && image_slots[i].state.canvas) SDL_DestroyTexture(image_slots[i].state.canvas);
        image_slots[i].state.canvas = NULL;
        image_slots[i].in_use = 0;
    }
    if (font) TTF_CloseFont(font);
    for (int i = 0; i < MAX_SCREEN_PAGES; i++) {
        if (pages[i]) SDL_DestroyTexture(pages[i]);
        pages[i] = NULL;
    }
    canvas = NULL;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
