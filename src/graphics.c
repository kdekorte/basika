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
static SDL_Color current_text_color = {255, 255, 255, 255};
static int canvas_width = 1280;
static int canvas_height = 400;
static int cursor_x = 0;
static int cursor_y = 0;

static double gfx_cursor_x = 0;
static double gfx_cursor_y = 0;

#define MAX_IMAGE_SLOTS 64

typedef struct {
    SDL_Texture *texture;
    int width;
    int height;
    int in_use;
} ImageSlot;

static ImageSlot image_slots[MAX_IMAGE_SLOTS];


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

static SDL_Color get_graphics_color(int color_value);

static int view_active = 0;
static int view_screen = 0;
static int view_x1 = 0, view_y1 = 0, view_x2 = -1, view_y2 = -1;

static int window_active = 0;
static int window_screen = 0;
static double win_x1 = 0, win_y1 = 0, win_x2 = 0, win_y2 = 0;

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
        *px = (int)x + view_x1;
        *py = (int)y + view_y1;
    } else {
        *px = (int)x;
        *py = (int)y;
    }
}


static void apply_clipping() {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    SDL_Rect r;
    if (view_active) {
        r.x = (int)(view_x1 * xs);
        r.y = (int)(view_y1 * ys);
        r.w = (int)((view_x2 - view_x1 + 1) * xs + 0.99);
        r.h = (int)((view_y2 - view_y1 + 1) * ys + 0.99);
    } else {
        r.x = 0; r.y = 0; r.w = canvas_width; r.h = canvas_height;
    }
    SDL_SetRenderClipRect(renderer, &r);
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
void graphics_set_view(int use_screen, int x1, int y1, int x2, int y2, int color, int boundary) {
    if (!renderer || !canvas) return;
    SDL_SetRenderTarget(renderer, canvas);
    view_active = 1;
    view_screen = use_screen;
    view_x1 = x1; view_y1 = y1; view_x2 = x2; view_y2 = y2;
    // We should draw background color and boundary if needed
    // But for now, just set viewport
    // If color >= 0, fill viewport
    // If boundary >= 0, draw border
    if (color >= 0) {
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
    if (boundary >= 0) {
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

void set_screen_mode(int mode) {
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

    // Recreate the canvas at the new doubled resolution
    if (renderer) {
        if (canvas) SDL_DestroyTexture(canvas);
        canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, canvas_width, canvas_height);
        SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST); // Pixel-perfect retro graphics
    }

    if (renderer && canvas) {
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (window) render_canvas_to_window();
    }
}

void set_screen_newimage(int width, int height, int colors) {
    (void)colors;
    if (width <= 0 || height <= 0) return;

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

    if (renderer) {
        if (canvas) SDL_DestroyTexture(canvas);
        canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, canvas_width, canvas_height);
        SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
    }

    if (renderer && canvas) {
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (window) render_canvas_to_window();
    }
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
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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

int graphics_loadimage(const char *filename, int mode) {
    (void)mode;
    if (!filename || !renderer) return 0;

    int slot_idx = -1;
    for (int i = 0; i < MAX_IMAGE_SLOTS; i++) {
        if (!image_slots[i].in_use) { slot_idx = i; break; }
    }
    if (slot_idx < 0) return 0;

    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) return 0;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return 0;
    }
    image_slots[slot_idx].texture = texture;
    image_slots[slot_idx].width = surface->w;
    image_slots[slot_idx].height = surface->h;
    image_slots[slot_idx].in_use = 1;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(surface);
    return slot_idx + 1;
}

int graphics_freeimage(int handle) {
    if (!renderer || handle < 1 || handle > MAX_IMAGE_SLOTS ||
        !image_slots[handle - 1].in_use) return 0;

    ImageSlot *slot = &image_slots[handle - 1];
    SDL_DestroyTexture(slot->texture);
    slot->texture = NULL;
    slot->width = 0;
    slot->height = 0;
    slot->in_use = 0;
    return 1;
}

int graphics_putimage(int x1, int y1, int x2, int y2, int handle,
                      int sx1, int sy1, int sx2, int sy2, int has_source) {
    if (!renderer || !canvas || handle < 1 || handle > MAX_IMAGE_SLOTS ||
        !image_slots[handle - 1].in_use) return 0;

    ImageSlot *slot = &image_slots[handle - 1];
    if (!has_source) {
        sx1 = 0; sy1 = 0;
        sx2 = slot->width - 1; sy2 = slot->height - 1;
    }
    if (sx1 > sx2) { int temp = sx1; sx1 = sx2; sx2 = temp; }
    if (sy1 > sy2) { int temp = sy1; sy1 = sy2; sy2 = temp; }
    if (x2 < x1 || y2 < y1) {
        x2 = x1 + (sx2 - sx1);
        y2 = y1 + (sy2 - sy1);
    }
    if (sx1 < 0 || sy1 < 0 || sx2 >= slot->width || sy2 >= slot->height) return 0;

    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    SDL_FRect source = {(float)sx1, (float)sy1,
                        (float)(sx2 - sx1 + 1), (float)(sy2 - sy1 + 1)};
    SDL_FRect dest = {(float)(x1 * xs), (float)(y1 * ys),
                      (float)((x2 - x1 + 1) * xs),
                      (float)((y2 - y1 + 1) * ys)};
    SDL_SetRenderTarget(renderer, canvas);
    SDL_RenderTexture(renderer, slot->texture, &source, &dest);
    update_graphics();
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
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, canvas, NULL, &dst);
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

void set_text_color(int color_value) {
    if (color_value < 0) color_value = 0;
    if (color_value > 15) color_value = 15;
    switch (color_value) {
        case 0: current_text_color = (SDL_Color){0, 0, 0, 255}; break;
        case 1: current_text_color = (SDL_Color){0, 0, 170, 255}; break;
        case 2: current_text_color = (SDL_Color){0, 170, 0, 255}; break;
        case 3: current_text_color = (SDL_Color){0, 170, 170, 255}; break;
        case 4: current_text_color = (SDL_Color){170, 0, 0, 255}; break;
        case 5: current_text_color = (SDL_Color){170, 0, 170, 255}; break;
        case 6: current_text_color = (SDL_Color){170, 85, 0, 255}; break;
        case 7: current_text_color = (SDL_Color){170, 170, 170, 255}; break;
        case 8: current_text_color = (SDL_Color){85, 85, 85, 255}; break;
        case 9: current_text_color = (SDL_Color){85, 85, 255, 255}; break;
        case 10: current_text_color = (SDL_Color){85, 255, 85, 255}; break;
        case 11: current_text_color = (SDL_Color){85, 255, 255, 255}; break;
        case 12: current_text_color = (SDL_Color){255, 85, 85, 255}; break;
        case 13: current_text_color = (SDL_Color){255, 85, 255, 255}; break;
        case 14: current_text_color = (SDL_Color){255, 255, 85, 255}; break;
        case 15: current_text_color = (SDL_Color){255, 255, 255, 255}; break;
        default: current_text_color = (SDL_Color){255, 255, 255, 255}; break;
    }
}

static SDL_Color palette256[256];
static int palette_initialized = 0;

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
    palette_initialized = 1;
}

static SDL_Color get_graphics_color(int color_value) {
    if (!palette_initialized) init_palette256();
    color_value = (color_value % 256 + 256) % 256;
    return palette256[color_value];
}

void set_pixel_alpha(double user_x, double user_y, int color, int alpha) {
    if (!renderer || !canvas) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    if (alpha == 0) return;
    SDL_SetRenderTarget(renderer, canvas);
    int x, y;
    transform_coords(user_x, user_y, &x, &y);
    if (!clip_point(x, y)) return;
    SDL_SetRenderDrawBlendMode(renderer, alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_Color draw_color = get_graphics_color(color);
    draw_color.a = (Uint8)alpha;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);
    
    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    
    SDL_FRect r = { (float)(x * xs), (float)(y * ys), (float)xs, (float)ys };
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    gfx_cursor_x = x;
    gfx_cursor_y = y;
}

void set_pixel(double user_x, double user_y, int color) {
    set_pixel_alpha(user_x, user_y, color, 255);
}

int get_pixel(double user_x, double user_y) {
    int x, y;
    transform_coords(user_x, user_y, &x, &y);
    if (!clip_point(x, y)) return -1;
    if (!renderer || !canvas) return 0;
    
    // Map logical coordinates to canvas resolution
    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    int sx = (int)(x * xs);
    int sy = (int)(y * ys);

    if (sx < 0 || sx >= canvas_width || sy < 0 || sy >= canvas_height) return 0;

    SDL_SetRenderTarget(renderer, canvas);
    SDL_Surface *surf = SDL_RenderReadPixels(renderer, &(SDL_Rect){sx, sy, 1, 1});
    if (!surf) return 0;

    Uint8 r, g, b, a;
    SDL_ReadSurfacePixel(surf, 0, 0, &r, &g, &b, &a);
    SDL_DestroySurface(surf);

    // Return the nearest palette entry so blended pixels remain readable.
    int closest = 0;
    int closest_distance = 3 * 255 * 255 + 1;
    for (int i = 0; i < 256; i++) {
        SDL_Color c = get_graphics_color(i);
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

int graphics_save_screenshot(const char *filename) {
    if (!renderer || !canvas || !filename) return 0;

    SDL_SetRenderTarget(renderer, canvas);
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

void draw_line_alpha(double ux1, double uy1, double ux2, double uy2, int color, int fill, int alpha) {
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    if (alpha == 0) return;
    int x1, y1, x2, y2;
    transform_coords(ux1, uy1, &x1, &y1);
    transform_coords(ux2, uy2, &x2, &y2);
    gfx_cursor_x = ux2; gfx_cursor_y = uy2;
    apply_clipping();

    if (!renderer || !canvas) return;
    SDL_SetRenderDrawBlendMode(renderer, alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_Color draw_color = get_graphics_color(color);
    draw_color.a = (Uint8)alpha;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);

    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;

    int sx1 = (int)(x1 * xs + 0.5), sy1 = (int)(y1 * ys + 0.5);
    int sx2 = (int)(x2 * xs + 0.5), sy2 = (int)(y2 * ys + 0.5);
    
    if (fill) {
        int left = (sx1 < sx2 ? sx1 : sx2);
        int top = (sy1 < sy2 ? sy1 : sy2);
        int width = abs(sx1 - sx2) + (int)(xs + 0.5);
        int height = abs(sy1 - sy2) + (int)(ys + 0.5);
        SDL_FRect r = {
            (float)left, (float)top, (float)width, (float)height
        };
        if (fill == 2) {
            SDL_RenderFillRect(renderer, &r);
        } else {
            // Draw 4 thick borders matching logical pixel size to prevent PAINT leaks
            float inner_height = (float)height - 2.0f * (float)ys;
            if (inner_height < 0.0f) inner_height = 0.0f;
            SDL_FRect edges[4] = {
                { (float)left, (float)top, (float)width, (float)ys },
                { (float)left, (float)(top + height - ys), (float)width, height > ys ? (float)ys : 0.0f },
                { (float)left, (float)(top + ys), (float)xs, inner_height },
                { (float)(left + width - xs), (float)(top + ys), (float)xs, inner_height }
            };
            for (int i = 0; i < 4; i++) SDL_RenderFillRect(renderer, &edges[i]);
        }
    } else {
        SDL_RenderLine(renderer, (float)sx1, (float)sy1, (float)sx2, (float)sy2);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    remove_clipping();
    update_graphics();
}

void draw_line(double ux1, double uy1, double ux2, double uy2, int color, int fill) {
    draw_line_alpha(ux1, uy1, ux2, uy2, color, fill, 255);
}

static void draw_circle_with_alpha(double ucx, double ucy, double uradius, int color, int fill, int alpha) {
    if (!renderer || !canvas) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    if (alpha == 0) return;

    int cx, cy;
    transform_coords(ucx, ucy, &cx, &cy);
    // Approximate physical radius from x scale
    int p_cx, p_cy;
    transform_coords(ucx + uradius, ucy, &p_cx, &p_cy);
    int radius = abs(p_cx - cx);
    if (radius == 0 && uradius > 0) radius = 1;
    if (radius <= 0) return;

    apply_clipping();

    SDL_SetRenderDrawBlendMode(renderer, alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_Color draw_color = get_graphics_color(color);
    draw_color.a = (Uint8)alpha;
    SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a);

    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;

    if (fill == 2 && alpha < 255) {
        for (int row = -radius; row <= radius; row++) {
            int row_sq = row * row;
            int row_half = (int)sqrt((double)radius * radius - (double)row_sq);
            SDL_FRect span = {
                (float)((cx - row_half) * xs),
                (float)((cy + row) * ys),
                (float)((2 * row_half + 1) * xs + 0.5f),
                (float)(ys + 0.5f)
            };
            SDL_RenderFillRect(renderer, &span);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        gfx_cursor_x = cx;
        gfx_cursor_y = cy;
        remove_clipping();
        update_graphics();
        return;
    }

    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (x <= y) {
        if (fill == 2) {
            // Filled circle: draw horizontal spans of logical pixels
            SDL_FRect r1 = { (float)((cx - x) * xs), (float)((cy + y) * ys), (float)((2 * x + 1) * xs), (float)ys };
            SDL_FRect r2 = { (float)((cx - x) * xs), (float)((cy - y) * ys), (float)((2 * x + 1) * xs), (float)ys };
            SDL_FRect r3 = { (float)((cx - y) * xs), (float)((cy + x) * ys), (float)((2 * y + 1) * xs), (float)ys };
            SDL_FRect r4 = { (float)((cx - y) * xs), (float)((cy - x) * ys), (float)((2 * y + 1) * xs), (float)ys };
            SDL_RenderFillRect(renderer, &r1);
            SDL_RenderFillRect(renderer, &r2);
            SDL_RenderFillRect(renderer, &r3);
            SDL_RenderFillRect(renderer, &r4);
        } else {
            // Outline: draw logical pixels as thick blocks to ensure water-tight boundaries
            int px[8] = {cx + x, cx - x, cx + x, cx - x, cx + y, cx - y, cx + y, cx - y};
            int py[8] = {cy + y, cy + y, cy - y, cy - y, cy + x, cy + x, cy - x, cy - x};
            for (int i = 0; i < 8; i++) {
                // Draw slightly larger blocks (xs+1) to ensure logical pixels
                // overlap at corners, creating a water-tight border for PAINT
                // commands in high-resolution modes.
                SDL_FRect r = { (float)(px[i] * xs), (float)(py[i] * ys), (float)(xs + 1.0f), (float)(ys + 1.0f) };
                SDL_RenderFillRect(renderer, &r);
            }
        }

        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
    gfx_cursor_x = cx;
    gfx_cursor_y = cy;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    remove_clipping();
    update_graphics();
}

void draw_circle(double cx, double cy, double radius, int color, int fill) {
    draw_circle_with_alpha(cx, cy, radius, color, fill, 255);
}

void draw_circle_alpha(double cx, double cy, double radius, int color, int fill, int alpha) {
    draw_circle_with_alpha(cx, cy, radius, color, fill, alpha);
}

typedef struct {
    int x, y;
} Point;

static Uint32 blend_paint_pixel(Uint32 destination, Uint32 source, int alpha,
                                const SDL_PixelFormatDetails *details, SDL_Palette *palette) {
    Uint8 dr, dg, db;
    Uint8 sr, sg, sb;
    SDL_GetRGB(destination, details, palette, &dr, &dg, &db);
    SDL_GetRGB(source, details, palette, &sr, &sg, &sb);
    Uint8 r = (Uint8)((sr * alpha + dr * (255 - alpha) + 127) / 255);
    Uint8 g = (Uint8)((sg * alpha + dg * (255 - alpha) + 127) / 255);
    Uint8 b = (Uint8)((sb * alpha + db * (255 - alpha) + 127) / 255);
    return SDL_MapRGBA(details, palette, r, g, b, 255);
}

void draw_paint_alpha(double ux, double uy, int paint_color, int border_color, int alpha) {
    if (!renderer || !canvas) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    if (alpha == 0) return;
    int x, y;
    transform_coords(ux, uy, &x, &y);
    if (!clip_point(x, y)) return;
    apply_clipping();
    
    SDL_Surface *raw_surf = SDL_RenderReadPixels(renderer, NULL);
    remove_clipping();
    if (!raw_surf) return;
    // Convert to canvas format to ensure color mapping matches the texture
    SDL_Surface *surf = SDL_ConvertSurface(raw_surf, SDL_PIXELFORMAT_RGBA8888);
    SDL_DestroySurface(raw_surf);
    if (!surf) return;

    SDL_Color sc_paint = get_graphics_color(paint_color);
    SDL_Color sc_border = get_graphics_color(border_color);
    
    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surf->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(surf);
    Uint32 u_paint = SDL_MapRGBA(details, palette, sc_paint.r, sc_paint.g, sc_paint.b, sc_paint.a);
    Uint32 u_border = SDL_MapRGBA(details, palette, sc_border.r, sc_border.g, sc_border.b, sc_border.a);

    double xs = (double)canvas_width / mode_res_w;
    double ys = (double)canvas_height / mode_res_h;
    int sx = (int)(x * xs);
    int sy = (int)(y * ys);

    if (sx < 0 || sx >= surf->w || sy < 0 || sy >= surf->h) {
        SDL_DestroySurface(surf);
        return;
    }

    int bpp = details->bytes_per_pixel;
    int pitch_pixels = surf->pitch / bpp;
    Uint32 *pixels = (Uint32 *)surf->pixels;
    Uint32 start_color = pixels[sy * pitch_pixels + sx];

    // Mask out the alpha channel to make comparisons robust against driver-specific
    // variations in how alpha is handled in the render target or ReadPixels.
    Uint32 mask = ~details->Amask;
    if ((start_color & mask) == (u_border & mask) || (start_color & mask) == (u_paint & mask)) {
        SDL_DestroySurface(surf);
        return;
    }

    int capacity = surf->w * surf->h;
    Point *queue = malloc(capacity * sizeof(Point));
    Uint8 *visited = alpha < 255 ? calloc((size_t)capacity, sizeof(Uint8)) : NULL;
    if (!queue || (alpha < 255 && !visited)) {
        free(queue);
        free(visited);
        SDL_DestroySurface(surf);
        return;
    }
    int head = 0, tail = 0;

    pixels[sy * pitch_pixels + sx] = alpha < 255
        ? blend_paint_pixel(start_color, u_paint, alpha, details, palette)
        : u_paint;
    if (visited) visited[(size_t)sy * (size_t)surf->w + (size_t)sx] = 1;
    queue[tail++] = (Point){sx, sy};

    while (head < tail) {
        Point p = queue[head++];
        
        Point neighbors[4] = {
            {p.x + 1, p.y}, {p.x - 1, p.y}, {p.x, p.y + 1}, {p.x, p.y - 1}
        };

        for (int i = 0; i < 4; i++) {
            int nx = neighbors[i].x;
            int ny = neighbors[i].y;

            if (nx >= 0 && nx < surf->w && ny >= 0 && ny < surf->h) {
                size_t index = (size_t)ny * (size_t)surf->w + (size_t)nx;
                if (visited && visited[index]) continue;
                Uint32 c = pixels[ny * pitch_pixels + nx];
                if ((c & mask) != (u_border & mask) && (c & mask) != (u_paint & mask)) {
                    pixels[ny * pitch_pixels + nx] = alpha < 255
                        ? blend_paint_pixel(c, u_paint, alpha, details, palette)
                        : u_paint;
                    if (visited) visited[index] = 1;
                    queue[tail++] = (Point){nx, ny};
                }
            }
        }
    }

    SDL_UpdateTexture(canvas, NULL, surf->pixels, surf->pitch);
    SDL_DestroySurface(surf);
    free(queue);
    free(visited);
    
    gfx_cursor_x = x;
    gfx_cursor_y = y;
    graphics_present_if_autodisplay();
}

void draw_paint(double ux, double uy, int paint_color, int border_color) {
    draw_paint_alpha(ux, uy, paint_color, border_color, 255);
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
    int pos = 0;
    buffer[0] = '\0';
    SDL_StartTextInput(window);
    
    // Store the starting position for the current input line
    int line_start_x = cursor_x;
    int line_start_y = cursor_y;

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

void handle_events() {
    if (!window) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) exit(0);
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (e.text.text[0]) last_key_char = (unsigned char)e.text.text[0];
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
        if (image_slots[i].texture) SDL_DestroyTexture(image_slots[i].texture);
        image_slots[i].texture = NULL;
        image_slots[i].in_use = 0;
    }
    if (font) TTF_CloseFont(font);
    if (canvas) SDL_DestroyTexture(canvas);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
