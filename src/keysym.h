/*
 * keysym.h - host key numbers shared by sdl.c and sib.c
 *
 * Printable keys are their ASCII codes.  The rest keep the numbers the
 * SDL 1.2 keysyms had, since sib.c's translation table (map[512]) was
 * written around them.  sdl.c converts SDL3 keycodes to these.
 */

#ifndef MEROKO_KEYSYM_H
#define MEROKO_KEYSYM_H

#define MKEY_BACKSPACE	8
#define MKEY_TAB	9
#define MKEY_RETURN	13
#define MKEY_ESCAPE	27

#define MKEY_UP		273
#define MKEY_DOWN	274
#define MKEY_RIGHT	275
#define MKEY_LEFT	276
#define MKEY_INSERT	277
#define MKEY_HOME	278
#define MKEY_END	279
#define MKEY_PAGEUP	280
#define MKEY_PAGEDOWN	281

#define MKEY_F1		282
#define MKEY_F2		283
#define MKEY_F3		284
#define MKEY_F4		285
#define MKEY_F5		286
#define MKEY_F6		287

#define MKEY_CAPSLOCK	301
#define MKEY_RSHIFT	303
#define MKEY_LSHIFT	304
#define MKEY_RCTRL	305
#define MKEY_LCTRL	306
#define MKEY_RALT	307
#define MKEY_LALT	308
#define MKEY_LSUPER	311
#define MKEY_RSUPER	312

#endif
