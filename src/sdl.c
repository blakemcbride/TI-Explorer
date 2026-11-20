/*
 * sdl.c - SDL3 display, keyboard and mouse front end for Meroko
 *
 * Originally written for SDL 1.2 by Brad Parker (2005); rewritten for SDL3.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * The SIB emulation (sib.c) draws into an 8-bit-per-pixel buffer that is
 * video_width x video_height bytes.  Here that buffer is copied, dirty rows
 * only, into a streaming texture and presented at most ~60 times a second.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <SDL3/SDL.h>

#include "keysym.h"

#if defined(FB_WIDTH) && defined(FB_HEIGHT)
#define VIDEO_WIDTH FB_WIDTH
#define VIDEO_HEIGHT FB_HEIGHT
#else
#define VIDEO_HEIGHT 1024
#define VIDEO_WIDTH 1280
#endif

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static unsigned char *pixels;	/* 8bpp, video_width * video_height */
static int video_width = VIDEO_WIDTH;
static int video_height = VIDEO_HEIGHT;

#define MAX_BITMAP_OFFSET	((VIDEO_WIDTH * VIDEO_HEIGHT) / 32)
static unsigned int bitmap[MAX_BITMAP_OFFSET];

#define COLOR_WHITE	0xff
#define COLOR_BLACK	0

#define REFRESH_MS	16	/* minimum time between presents */

extern void kbd_handle_char(int sc, int sym, int updown, int shifted);

/* Dirty rows, [dirty_min, dirty_max).  Empty when dirty_min >= dirty_max. */
static int dirty_min = 0x7fffffff, dirty_max = 0;
static int need_present;
static Uint64 last_present;

static void
mark_dirty(int offset, int len)
{
	int v0, v1;

	if (len <= 0)
		return;
	v0 = offset / video_width;
	v1 = (offset + len - 1) / video_width + 1;
	if (v0 < 0) v0 = 0;
	if (v1 > video_height) v1 = video_height;
	if (v0 < dirty_min) dirty_min = v0;
	if (v1 > dirty_max) dirty_max = v1;
}

static void
mark_all_dirty(void)
{
	dirty_min = 0;
	dirty_max = video_height;
}

/* Copy the dirty rows to the texture and show it. */
static void
present(int force)
{
	static Uint32 *rowbuf;
	Uint64 now = SDL_GetTicks();

	if (!renderer)
		return;
	if (dirty_min >= dirty_max && !need_present && !force)
		return;
	if (!force && now - last_present < REFRESH_MS)
		return;

	if (dirty_min < dirty_max) {
		SDL_Rect r;
		int x, y, n = 0;

		if (!rowbuf)
			rowbuf = malloc(sizeof(Uint32) * video_width *
					video_height);
		for (y = dirty_min; y < dirty_max; y++) {
			unsigned char *p = pixels + y * video_width;
			for (x = 0; x < video_width; x++)
				rowbuf[n++] = p[x] ? 0xffffffff : 0xff000000;
		}
		r.x = 0;
		r.y = dirty_min;
		r.w = video_width;
		r.h = dirty_max - dirty_min;
		SDL_UpdateTexture(texture, &r, rowbuf,
				  video_width * sizeof(Uint32));
		dirty_min = 0x7fffffff;
		dirty_max = 0;
	}

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	need_present = 0;
	last_present = now;
}

void
sdl_video_read(int offset, unsigned int *pv)
{
	*pv = 0;

	/* the real h/w has memory for 768x1024 */
	if (offset >= 0 && offset < MAX_BITMAP_OFFSET)
		*pv = bitmap[offset];
}

/* Translate an SDL3 keycode into the key numbers used by sib.c's map. */
static int
translate_key(SDL_Keycode k)
{
	if (k >= 0x20 && k < 0x7f)
		return (int)k;

	switch (k) {
	case SDLK_RETURN:	return MKEY_RETURN;
	case SDLK_KP_ENTER:	return MKEY_RETURN;
	case SDLK_BACKSPACE:	return MKEY_BACKSPACE;
	case SDLK_DELETE:	return MKEY_BACKSPACE;
	case SDLK_TAB:		return MKEY_TAB;
	case SDLK_ESCAPE:	return MKEY_ESCAPE;
	case SDLK_UP:		return MKEY_UP;
	case SDLK_DOWN:		return MKEY_DOWN;
	case SDLK_RIGHT:	return MKEY_RIGHT;
	case SDLK_LEFT:		return MKEY_LEFT;
	case SDLK_HOME:		return MKEY_HOME;
	case SDLK_INSERT:	return MKEY_INSERT;
	case SDLK_PAGEUP:	return MKEY_PAGEUP;
	case SDLK_PAGEDOWN:	return MKEY_PAGEDOWN;
	case SDLK_F1:		return MKEY_F1;
	case SDLK_F2:		return MKEY_F2;
	case SDLK_F3:		return MKEY_F3;
	case SDLK_F4:		return MKEY_F4;
	case SDLK_F5:		return MKEY_F5;
	case SDLK_F6:		return MKEY_F6;
	case SDLK_CAPSLOCK:	return MKEY_CAPSLOCK;
	case SDLK_RSHIFT:	return MKEY_RSHIFT;
	case SDLK_LSHIFT:	return MKEY_LSHIFT;
	case SDLK_RCTRL:	return MKEY_RCTRL;
	case SDLK_LCTRL:	return MKEY_LCTRL;
	case SDLK_RALT:		return MKEY_RALT;
	case SDLK_LALT:		return MKEY_LALT;
	case SDLK_RGUI:		return MKEY_RSUPER;
	case SDLK_LGUI:		return MKEY_LSUPER;
	default:		return 0;
	}
}

static void
sdl_process_key(SDL_KeyboardEvent *ev, int updown)
{
	int sym;

	if (ev->repeat)
		return;		/* the Explorer does its own auto-repeat */

	sym = translate_key(ev->key);
	if (sym == 0)
		return;

	kbd_handle_char(ev->scancode, sym, updown,
			(ev->mod & SDL_KMOD_SHIFT) ? 1 : 0);
}

static void
sdl_set_buttons(SDL_MouseButtonFlags state)
{
	extern unsigned char sib_mouse_motion_reg;

	if (state & SDL_BUTTON_LMASK)
		sib_mouse_motion_reg |= 1<<6;
	else
		sib_mouse_motion_reg &= ~(1<<6);

	if (state & SDL_BUTTON_MMASK)
		sib_mouse_motion_reg |= 1<<5;
	else
		sib_mouse_motion_reg &= ~(1<<5);

	if (state & SDL_BUTTON_RMASK)
		sib_mouse_motion_reg |= 1<<4;
	else
		sib_mouse_motion_reg &= ~(1<<4);
}

/* x,y are in emulated-screen coordinates */
static void
sdl_set_mouse_pos(float x, float y)
{
	extern unsigned int sib_mouse_x_pos;
	extern unsigned int sib_mouse_y_pos;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > video_width - 1) x = video_width - 1;
	if (y > video_height - 1) y = video_height - 1;

	sib_mouse_x_pos = (unsigned int)x;
	sib_mouse_y_pos = (unsigned int)y;
}

void
sdl_system_shutdown_request(void)
{
	exit(0);
}

static char video_bow_mode = 1;	/* 1 => White on Black, 0 => Black on White */

void
sdl_set_bow_mode(char new_mode)
{
	int i;

	if (!pixels)
		return;
	if (video_bow_mode == new_mode)
		return;

	/* Need to complement it */
	video_bow_mode = new_mode;

	for (i = 0; i < video_width * video_height; i++)
		pixels[i] = ~pixels[i];

	mark_all_dirty();
}

void
sdl_video_write(int offset, unsigned int bits)
{
	int i;

	if (!pixels)
		return;
	if (offset < 0 || offset >= MAX_BITMAP_OFFSET)
		return;

	bitmap[offset] = bits;

	offset *= 32;

	for (i = 0; i < 32; i++) {
		pixels[offset + i] = (bits & 1) ? COLOR_WHITE : COLOR_BLACK;
		if (video_bow_mode == 0)
			pixels[offset + i] ^= ~0;
		bits >>= 1;
	}

	mark_dirty(offset, 32);
}

/* hack, but it works */
void
sdl_video_write_byte(int offset, int bytenr, unsigned char bits)
{
	if (pixels && offset >= 0 && offset < MAX_BITMAP_OFFSET) {
		unsigned int word = bitmap[offset];
		word &= ~(0xffu << 8*bytenr);
		word |= ((unsigned int)bits << 8*bytenr);
		sdl_video_write(offset, word);
	}
}

/* sib.c wrote len bytes at fb directly into the pixel buffer */
void
sdl_sync(unsigned char *fb, int len)
{
	if (!pixels)
		return;
	mark_dirty((int)(fb - pixels), len);
}

void
sdl_refresh(void)
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			need_present = 1;
			break;

		case SDL_EVENT_KEY_DOWN:
			sdl_process_key(&ev.key, 1);
			break;

		case SDL_EVENT_KEY_UP:
			sdl_process_key(&ev.key, 0);
			break;

		case SDL_EVENT_QUIT:
			sdl_system_shutdown_request();
			break;

		case SDL_EVENT_MOUSE_MOTION:
			SDL_ConvertEventToRenderCoordinates(renderer, &ev);
			sdl_set_mouse_pos(ev.motion.x, ev.motion.y);
			sdl_set_buttons(ev.motion.state);
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			SDL_ConvertEventToRenderCoordinates(renderer, &ev);
			sdl_set_mouse_pos(ev.button.x, ev.button.y);
			sdl_set_buttons(SDL_GetMouseState(NULL, NULL));
			break;

		default:
			break;
		}
	}

	present(0);
}

static void
sdl_cleanup(void)
{
	SDL_Quit();
}

static void
sdl_display_init(void)
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL initialization failed: %s\n",
			SDL_GetError());
		exit(1);
	}

	/* NOTE: we still want Ctrl-C to work - undo the SDL redirections */
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);

	if (!SDL_CreateWindowAndRenderer("Meroko - TI Explorer I",
					 video_width, video_height,
					 SDL_WINDOW_RESIZABLE,
					 &window, &renderer)) {
		fprintf(stderr, "Could not open SDL display: %s\n",
			SDL_GetError());
		exit(1);
	}

	/* Keep the Explorer's aspect ratio whatever the window size is */
	SDL_SetRenderLogicalPresentation(renderer, video_width, video_height,
					 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(renderer, 0);	/* never block the CPU loop */

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
				    SDL_TEXTUREACCESS_STREAMING,
				    video_width, video_height);
	if (!texture) {
		fprintf(stderr, "Could not create SDL texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);

	pixels = malloc(video_width * video_height);
	if (!pixels) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	memset(pixels, COLOR_WHITE, video_width * video_height);
	mark_all_dirty();
	present(1);

	/* The Explorer draws its own mouse cursor */
	SDL_HideCursor();

	atexit(sdl_cleanup);
}

void
display_poll(void)
{
	sdl_refresh();
}

int
sdl_init(unsigned int *pscreensize, char **pframebuffer, int width, int height)
{
	if ((width > VIDEO_WIDTH) || (height > VIDEO_HEIGHT)) {
		fprintf(stderr, "**** Too large screen requested: "
			"%d x %d (max is %d x %d)\n",
			width, height, VIDEO_WIDTH, VIDEO_HEIGHT);
		return 1;
	}

	video_width = width;
	video_height = height;

	sdl_display_init();

	*pscreensize = video_width * video_height;
	*pframebuffer = (char *)pixels;

	return 0;
}
