/*
 * lcdterm - 480x272 8bpp RGB332 framebuffer terminal for AX7010 A2
 *
 * Target: Zynq / Debian (armhf), PL framebuffer at 0x40000000.
 * Build on board:  gcc -O2 -std=gnu99 -Wall -Wextra -o lcdterm lcdterm.c -lutil
 *
 * Host selftest:   gcc -O2 -std=gnu99 -Wall -Wextra -o lcdterm_host lcdterm.c -lutil
 *                  ./lcdterm_host --selftest --fb-file /tmp/lcdterm.raw
 *
 * Options:
 *   --fb-file PATH     use a regular file instead of /dev/mem (testing)
 *   --shell PATH       shell to run (default /bin/bash, fallback /bin/sh)
 *   --no-kbd           do not open evdev keyboard
 *   --no-grab          do not EVIOCGRAB keyboard
 *   --stdin            also forward stdin to the pty
 *   --selftest         run host-side checks and draw test page
 *   --demo             draw test page and exit
 *   --install-service  write /etc/systemd/system/lcdterm.service
 *   --help
 *
 * Keyboard policy: if HDMI status is connected, do not grab the keyboard;
 * if HDMI is absent, grab it for the 4.3" terminal.  Poll status every 2 s.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pty.h>
#include <glob.h>
#include <linux/input.h>

#define FB_W 480
#define FB_H 272
#define FB_BYTES (FB_W * FB_H)
#define FB_PHYS 0x40000000UL
#define GPIO_PHYS 0x41220000UL
#define DEFAULT_BG (-1)
#define DEFAULT_FG 0xDB

#define COLS 58
#define ROWS 15
#define VIEW_X 8
#define VIEW_Y 32
#define CELL_W 8
#define CELL_H 16

/* Linux console font Lat15-VGA16, ASCII 0x20..0x7E. */
/* Generated from Linux console font Lat15-VGA16 (8x16), ASCII 0x20-0x7E. */
static const unsigned char font8x16[95][16] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 20   */
  {0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x10,0x00,0x00,0x00,0x00}, /* 21 ! */
  {0x00,0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 22 " */
  {0x00,0x00,0x24,0x24,0x24,0x7E,0x24,0x24,0x7E,0x24,0x24,0x24,0x00,0x00,0x00,0x00}, /* 23 # */
  {0x00,0x10,0x10,0x7C,0x92,0x90,0x90,0x7C,0x12,0x12,0x92,0x7C,0x10,0x10,0x00,0x00}, /* 24 $ */
  {0x00,0x00,0x64,0x94,0x68,0x08,0x10,0x10,0x20,0x2C,0x52,0x4C,0x00,0x00,0x00,0x00}, /* 25 % */
  {0x00,0x00,0x18,0x24,0x24,0x18,0x30,0x4A,0x44,0x44,0x44,0x3A,0x00,0x00,0x00,0x00}, /* 26 & */
  {0x00,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 27 ' */
  {0x00,0x00,0x08,0x10,0x20,0x20,0x20,0x20,0x20,0x20,0x10,0x08,0x00,0x00,0x00,0x00}, /* 28 ( */
  {0x00,0x00,0x20,0x10,0x08,0x08,0x08,0x08,0x08,0x08,0x10,0x20,0x00,0x00,0x00,0x00}, /* 29 ) */
  {0x00,0x00,0x00,0x00,0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2A * */
  {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2B + */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00}, /* 2C , */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 2D - */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x00}, /* 2E . */
  {0x00,0x00,0x04,0x04,0x08,0x08,0x10,0x10,0x20,0x20,0x40,0x40,0x00,0x00,0x00,0x00}, /* 2F / */
  {0x00,0x00,0x3C,0x42,0x42,0x46,0x4A,0x52,0x62,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 30 0 */
  {0x00,0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00}, /* 31 1 */
  {0x00,0x00,0x3C,0x42,0x42,0x02,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00}, /* 32 2 */
  {0x00,0x00,0x3C,0x42,0x42,0x02,0x1C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 33 3 */
  {0x00,0x00,0x02,0x06,0x0A,0x12,0x22,0x42,0x7E,0x02,0x02,0x02,0x00,0x00,0x00,0x00}, /* 34 4 */
  {0x00,0x00,0x7E,0x40,0x40,0x40,0x7C,0x02,0x02,0x02,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 35 5 */
  {0x00,0x00,0x1C,0x20,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 36 6 */
  {0x00,0x00,0x7E,0x02,0x02,0x04,0x04,0x08,0x08,0x10,0x10,0x10,0x00,0x00,0x00,0x00}, /* 37 7 */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 38 8 */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x04,0x38,0x00,0x00,0x00,0x00}, /* 39 9 */
  {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x00}, /* 3A : */
  {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00}, /* 3B ; */
  {0x00,0x00,0x00,0x04,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00,0x00}, /* 3C < */
  {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 3D = */
  {0x00,0x00,0x00,0x40,0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00,0x00}, /* 3E > */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x04,0x08,0x08,0x00,0x08,0x08,0x00,0x00,0x00,0x00}, /* 3F ? */
  {0x00,0x00,0x7C,0x82,0x9E,0xA2,0xA2,0xA2,0xA6,0x9A,0x80,0x7E,0x00,0x00,0x00,0x00}, /* 40 @ */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00}, /* 41 A */
  {0x00,0x00,0x7C,0x42,0x42,0x42,0x7C,0x42,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00}, /* 42 B */
  {0x00,0x00,0x3C,0x42,0x42,0x40,0x40,0x40,0x40,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 43 C */
  {0x00,0x00,0x78,0x44,0x42,0x42,0x42,0x42,0x42,0x42,0x44,0x78,0x00,0x00,0x00,0x00}, /* 44 D */
  {0x00,0x00,0x7E,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00}, /* 45 E */
  {0x00,0x00,0x7E,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00}, /* 46 F */
  {0x00,0x00,0x3C,0x42,0x42,0x40,0x40,0x4E,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 47 G */
  {0x00,0x00,0x42,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00}, /* 48 H */
  {0x00,0x00,0x38,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00}, /* 49 I */
  {0x00,0x00,0x0E,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00,0x00,0x00,0x00}, /* 4A J */
  {0x00,0x00,0x42,0x44,0x48,0x50,0x60,0x60,0x50,0x48,0x44,0x42,0x00,0x00,0x00,0x00}, /* 4B K */
  {0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00}, /* 4C L */
  {0x00,0x00,0x82,0xC6,0xAA,0x92,0x92,0x82,0x82,0x82,0x82,0x82,0x00,0x00,0x00,0x00}, /* 4D M */
  {0x00,0x00,0x42,0x42,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42,0x00,0x00,0x00,0x00}, /* 4E N */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 4F O */
  {0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00}, /* 50 P */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x4A,0x3C,0x02,0x00,0x00,0x00}, /* 51 Q */
  {0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x50,0x48,0x44,0x42,0x00,0x00,0x00,0x00}, /* 52 R */
  {0x00,0x00,0x3C,0x42,0x40,0x40,0x3C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 53 S */
  {0x00,0x00,0xFE,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00}, /* 54 T */
  {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 55 U */
  {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x24,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00}, /* 56 V */
  {0x00,0x00,0x82,0x82,0x82,0x82,0x82,0x92,0x92,0xAA,0xC6,0x82,0x00,0x00,0x00,0x00}, /* 57 W */
  {0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x18,0x24,0x24,0x42,0x42,0x00,0x00,0x00,0x00}, /* 58 X */
  {0x00,0x00,0x82,0x82,0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00}, /* 59 Y */
  {0x00,0x00,0x7E,0x02,0x02,0x04,0x08,0x10,0x20,0x40,0x40,0x7E,0x00,0x00,0x00,0x00}, /* 5A Z */
  {0x00,0x00,0x38,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x38,0x00,0x00,0x00,0x00}, /* 5B [ */
  {0x00,0x00,0x40,0x40,0x20,0x20,0x10,0x10,0x08,0x08,0x04,0x04,0x00,0x00,0x00,0x00}, /* 5C \ */
  {0x00,0x00,0x38,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x38,0x00,0x00,0x00,0x00}, /* 5D ] */
  {0x00,0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 5E ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00}, /* 5F _ */
  {0x10,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 60 ` */
  {0x00,0x00,0x00,0x00,0x00,0x3C,0x02,0x3E,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00}, /* 61 a */
  {0x00,0x00,0x40,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00}, /* 62 b */
  {0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 63 c */
  {0x00,0x00,0x02,0x02,0x02,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00}, /* 64 d */
  {0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x7E,0x40,0x40,0x3C,0x00,0x00,0x00,0x00}, /* 65 e */
  {0x00,0x00,0x0E,0x10,0x10,0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00}, /* 66 f */
  {0x00,0x00,0x00,0x00,0x00,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x3C,0x00}, /* 67 g */
  {0x00,0x00,0x40,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00}, /* 68 h */
  {0x00,0x00,0x10,0x10,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00}, /* 69 i */
  {0x00,0x00,0x04,0x04,0x00,0x0C,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00}, /* 6A j */
  {0x00,0x00,0x40,0x40,0x40,0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00,0x00,0x00}, /* 6B k */
  {0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00}, /* 6C l */
  {0x00,0x00,0x00,0x00,0x00,0xFC,0x92,0x92,0x92,0x92,0x92,0x92,0x00,0x00,0x00,0x00}, /* 6D m */
  {0x00,0x00,0x00,0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00}, /* 6E n */
  {0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00}, /* 6F o */
  {0x00,0x00,0x00,0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x42,0x7C,0x40,0x40,0x40,0x00}, /* 70 p */
  {0x00,0x00,0x00,0x00,0x00,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x02,0x00}, /* 71 q */
  {0x00,0x00,0x00,0x00,0x00,0x5E,0x60,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00}, /* 72 r */
  {0x00,0x00,0x00,0x00,0x00,0x3E,0x40,0x40,0x3C,0x02,0x02,0x7C,0x00,0x00,0x00,0x00}, /* 73 s */
  {0x00,0x00,0x10,0x10,0x10,0x7C,0x10,0x10,0x10,0x10,0x10,0x0E,0x00,0x00,0x00,0x00}, /* 74 t */
  {0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00}, /* 75 u */
  {0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00}, /* 76 v */
  {0x00,0x00,0x00,0x00,0x00,0x82,0x82,0x92,0x92,0x92,0x92,0x7C,0x00,0x00,0x00,0x00}, /* 77 w */
  {0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x24,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00}, /* 78 x */
  {0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x3C,0x00}, /* 79 y */
  {0x00,0x00,0x00,0x00,0x00,0x7E,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00}, /* 7A z */
  {0x00,0x00,0x0C,0x10,0x10,0x10,0x20,0x10,0x10,0x10,0x10,0x0C,0x00,0x00,0x00,0x00}, /* 7B { */
  {0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00}, /* 7C | */
  {0x00,0x00,0x30,0x08,0x08,0x08,0x04,0x08,0x08,0x08,0x08,0x30,0x00,0x00,0x00,0x00}, /* 7D } */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x62,0x92,0x8C,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 7E ~ */
};


static const unsigned char ansi_rgb332[16] = {
    0x00, 0xE9, 0x76, 0xF5, 0x4F, 0xB3, 0x3B, 0xDB,
    0x6E, 0xED, 0x9A, 0xFA, 0x73, 0xD3, 0x7F, 0xFF
};

typedef struct {
    unsigned char ch;
    unsigned char fg;
    int bg;
} Cell;

static Cell screen[ROWS][COLS];

static volatile uint32_t *fb;
static int fb_fd = -1;
static volatile uint32_t *gpio;
static int gpio_fd = -1;
static int pl_handoff = 0;
static unsigned char base_bg = 0x00;
static unsigned char *bg_copy;
static int boot_done_sent = 0;

static const char *fb_file_path = NULL;
static const char *bg_file_path = NULL;

static int cur_x = 0, cur_y = 0;
static int wrap_pending = 0;
static unsigned char attr_fg = DEFAULT_FG;
static int attr_bg = DEFAULT_BG;
static int attr_bold = 0, attr_dim = 0, attr_reverse = 0;
static int cursor_visible = 1;
static int scroll_top = 0, scroll_bottom = ROWS - 1;
static int saved_x = 0, saved_y = 0;
static unsigned char saved_fg = DEFAULT_FG;
static int saved_bg = DEFAULT_BG;
static int saved_bold = 0, saved_dim = 0, saved_reverse = 0;

static int master_fd = -1;
static pid_t child_pid = -1;
static int keyboard_fd = -1;
static int keyboard_grabbed = 0;
static int lcd_keyboard_selected = 0;
static int keyboard_toggle_pending = 0;
static int no_grab = 0;
static int no_kbd = 0;
static int forward_stdin = 0;
static int hdmi_present = 0;
static time_t last_hdmi_check = 0;
static time_t last_keyboard_scan = 0;
static const char *shell_path = "/bin/bash";
static volatile sig_atomic_t running = 1;

/* ---------- terminal state ---------- */


static unsigned char brighten(unsigned char c)
{
    int r = (c >> 5) & 7, g = (c >> 2) & 7, b = c & 3;
    r = r > 4 ? 7 : r + 3;
    g = g > 4 ? 7 : g + 3;
    b = b > 1 ? 3 : b + 2;
    return (unsigned char)((r << 5) | (g << 2) | b);
}

static unsigned char darken(unsigned char c)
{
    int r = (c >> 5) & 7, g = (c >> 2) & 7, b = c & 3;
    r = r > 1 ? r - 2 : 0;
    g = g > 1 ? g - 2 : 0;
    b = b > 0 ? b - 1 : 0;
    return (unsigned char)((r << 5) | (g << 2) | b);
}

static unsigned char eff_fg(void)
{
    unsigned char c = attr_fg;
    if (attr_bold) c = brighten(c);
    if (attr_dim) c = darken(c);
    if (attr_reverse) c = attr_bg == DEFAULT_BG ? base_bg : attr_bg;
    return c;
}

static int eff_bg(void)
{
    return attr_reverse ? attr_fg : attr_bg;
}

static void clear_cell(Cell *c)
{
    c->ch = ' ';
    c->fg = eff_fg();
    c->bg = eff_bg();
}

static void screen_clear(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            clear_cell(&screen[r][c]);
    cur_x = cur_y = 0;
    wrap_pending = 0;
    scroll_top = 0;
    scroll_bottom = ROWS - 1;
    cursor_visible = 1;
}

static void scroll_up(void)
{
    for (int r = scroll_top; r < scroll_bottom; r++)
        memcpy(&screen[r][0], &screen[r + 1][0], sizeof(Cell) * COLS);
    for (int c = 0; c < COLS; c++)
        clear_cell(&screen[scroll_bottom][c]);
}

static void index_line(void)
{
    wrap_pending = 0;
    if (cur_y >= scroll_bottom)
        scroll_up();
    else if (cur_y < ROWS - 1)
        cur_y++;
}

static void put_char(unsigned char ch)
{
    if (ch < 0x20 || ch > 0x7e)
        return;
    if (wrap_pending) {
        cur_x = 0;
        index_line();
    }
    screen[cur_y][cur_x].ch = ch;
    screen[cur_y][cur_x].fg = eff_fg();
    screen[cur_y][cur_x].bg = eff_bg();
    if (cur_x == COLS - 1) wrap_pending = 1;
    else cur_x++;
}

static void erase_line(int mode)
{
    int from = 0, to = COLS - 1;
    if (mode == 0) from = cur_x;
    else if (mode == 1) to = cur_x;
    for (int c = from; c <= to && c < COLS; c++)
        clear_cell(&screen[cur_y][c]);
}

static void erase_display(int mode)
{
    if (mode == 0) {
        erase_line(0);
        for (int r = cur_y + 1; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) clear_cell(&screen[r][c]);
    } else if (mode == 1) {
        erase_line(1);
        for (int r = 0; r < cur_y; r++)
            for (int c = 0; c < COLS; c++) clear_cell(&screen[r][c]);
    } else if (mode == 2 || mode == 3) {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) clear_cell(&screen[r][c]);
    }
}

static void save_cursor(void)
{
    saved_x = cur_x; saved_y = cur_y;
    saved_fg = attr_fg; saved_bg = attr_bg;
    saved_bold = attr_bold; saved_dim = attr_dim; saved_reverse = attr_reverse;
}

static void restore_cursor(void)
{
    cur_x = saved_x; cur_y = saved_y;
    attr_fg = saved_fg; attr_bg = saved_bg;
    attr_bold = saved_bold; attr_dim = saved_dim; attr_reverse = saved_reverse;
}

/* ---------- ANSI parser ---------- */

static int state = 0; /* 0 normal, 1 esc, 2 csi, 3 osc */
static int csi_p[16], csi_np, csi_cur, csi_have, csi_priv;

static void csi_begin(void)
{
    csi_np = 0; csi_cur = 0; csi_have = 0; csi_priv = 0;
}

static void csi_push(void)
{
    if (csi_np < 16)
        csi_p[csi_np++] = csi_have ? csi_cur : 0;
    csi_cur = 0; csi_have = 0;
}

static int csi_get(int i, int def)
{
    return i < csi_np ? csi_p[i] : def;
}

static void csi_reply(const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (master_fd >= 0 && n > 0)
        (void)!write(master_fd, buf, (size_t)n);
}

static unsigned char rgb332(int r, int g, int b)
{
    return (unsigned char)((((r * 7 + 127) / 255) << 5) |
                           (((g * 7 + 127) / 255) << 2) |
                           ((b * 3 + 127) / 255));
}

static unsigned char indexed_color(int index)
{
    static const int levels[] = {0, 95, 135, 175, 215, 255};
    if (index < 16) return ansi_rgb332[index];
    if (index >= 232) {
        int gray = 8 + 10 * (index - 232);
        return rgb332(gray, gray, gray);
    }
    index -= 16;
    return rgb332(levels[index / 36], levels[(index / 6) % 6], levels[index % 6]);
}

static void sgr(void)
{
    for (int i = 0; i < csi_np; i++) {
        int p = csi_p[i];
        if (p == 0) {
            attr_fg = DEFAULT_FG; attr_bg = DEFAULT_BG;
            attr_bold = attr_dim = attr_reverse = 0;
        } else if (p == 1) attr_bold = 1;
        else if (p == 2) attr_dim = 1;
        else if (p == 22) attr_bold = attr_dim = 0;
        else if (p == 7) attr_reverse = 1;
        else if (p == 27) attr_reverse = 0;
        else if (p >= 30 && p <= 37) attr_fg = ansi_rgb332[p - 30];
        else if (p == 39) attr_fg = DEFAULT_FG;
        else if (p >= 40 && p <= 47) attr_bg = ansi_rgb332[p - 40];
        else if (p == 49) attr_bg = DEFAULT_BG;
        else if (p >= 90 && p <= 97) attr_fg = ansi_rgb332[p - 90 + 8];
        else if (p >= 100 && p <= 107) attr_bg = ansi_rgb332[p - 100 + 8];
        else if (p == 38 || p == 48) {
            int is_fg = (p == 38);
            if (i + 1 < csi_np && csi_p[i + 1] == 5 && i + 2 < csi_np) {
                int index = csi_p[i + 2];
                if (index >= 0 && index <= 255) {
                    if (is_fg) attr_fg = indexed_color(index);
                    else attr_bg = indexed_color(index);
                }
                i += 2;
            } else if (i + 1 < csi_np && csi_p[i + 1] == 2 && i + 4 < csi_np) {
                int r = csi_p[i + 2], g = csi_p[i + 3], b = csi_p[i + 4];
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    if (is_fg) attr_fg = rgb332(r, g, b);
                    else attr_bg = rgb332(r, g, b);
                }
                i += 4;
            }
        }
    }
}

static void csi_dispatch(unsigned char final)
{
    if (strchr("HfABCDGderu", final)) wrap_pending = 0;
    switch (final) {
    case 'H': case 'f': {
        int row = csi_get(0, 1), col = csi_get(1, 1);
        if (row < 1) row = 1;
        if (row > ROWS) row = ROWS;
        if (col < 1) col = 1;
        if (col > COLS) col = COLS;
        cur_y = row - 1; cur_x = col - 1;
        break;
    }
    case 'A': { int n = csi_get(0, 1); cur_y -= (n ? n : 1); if (cur_y < scroll_top) cur_y = scroll_top; break; }
    case 'B': { int n = csi_get(0, 1); cur_y += (n ? n : 1); if (cur_y > scroll_bottom) cur_y = scroll_bottom; break; }
    case 'C': { int n = csi_get(0, 1); cur_x += (n ? n : 1); if (cur_x >= COLS) cur_x = COLS - 1; break; }
    case 'D': { int n = csi_get(0, 1); cur_x -= (n ? n : 1); if (cur_x < 0) cur_x = 0; break; }
    case 'G': { int n = csi_get(0, 1); cur_x = (n ? n : 1) - 1; if (cur_x < 0) cur_x = 0; if (cur_x >= COLS) cur_x = COLS - 1; break; }
    case 'd': { int n = csi_get(0, 1); cur_y = (n ? n : 1) - 1; if (cur_y < 0) cur_y = 0; if (cur_y >= ROWS) cur_y = ROWS - 1; break; }
    case 'J': erase_display(csi_get(0, 0)); break;
    case 'K': erase_line(csi_get(0, 0)); break;
    case 'X': {
        int count = csi_get(0, 1);
        if (count == 0) count = 1;
        for (int col = cur_x; col < COLS && col-cur_x < count; col++)
            clear_cell(&screen[cur_y][col]);
        break;
    }
    case 'm': sgr(); break;
    case 's': save_cursor(); break;
    case 'u': restore_cursor(); break;
    case 'r': {
        int top = csi_get(0, 1), bot = csi_get(1, ROWS);
        if (top < 1) top = 1;
        if (bot > ROWS) bot = ROWS;
        if (top < bot) { scroll_top = top - 1; scroll_bottom = bot - 1; cur_x = 0; cur_y = scroll_top; }
        break;
    }
    case 'h': case 'l':
        if (csi_priv && csi_get(0, 0) == 25) cursor_visible = (final == 'h');
        break;
    case 'n':
        if (csi_get(0, 0) == 6) csi_reply("\x1b[%d;%dR", cur_y + 1, cur_x + 1);
        else if (csi_get(0, 0) == 5) csi_reply("\x1b[0n");
        break;
    case 'c':
        csi_reply("\x1b[?1;2c");
        break;
    case 'P': {
        int n = csi_get(0, 1);
        if (n < 1) n = 1;
        for (int c = cur_x; c < COLS; c++) {
            if (c + n < COLS) screen[cur_y][c] = screen[cur_y][c + n];
            else clear_cell(&screen[cur_y][c]);
        }
        break;
    }
    case '@': {
        int n = csi_get(0, 1);
        if (n < 1) n = 1;
        for (int c = COLS - 1; c >= cur_x; c--) {
            if (c - n >= cur_x) screen[cur_y][c] = screen[cur_y][c - n];
            else clear_cell(&screen[cur_y][c]);
        }
        break;
    }
    case 'L': {
        int n = csi_get(0, 1);
        if (n < 1) n = 1;
        for (int i = 0; i < n; i++) {
            for (int r = scroll_bottom; r > cur_y; r--) memcpy(&screen[r][0], &screen[r - 1][0], sizeof(Cell) * COLS);
            for (int c = 0; c < COLS; c++) clear_cell(&screen[cur_y][c]);
        }
        break;
    }
    case 'M': {
        int n = csi_get(0, 1);
        if (n < 1) n = 1;
        for (int i = 0; i < n; i++) {
            for (int r = cur_y; r < scroll_bottom; r++) memcpy(&screen[r][0], &screen[r + 1][0], sizeof(Cell) * COLS);
            for (int c = 0; c < COLS; c++) clear_cell(&screen[scroll_bottom][c]);
        }
        break;
    }
    default:
        break;
    }
}

static void term_feed_byte(unsigned char ch)
{
    if (state == 0) {
        if (ch == 0x1b) { state = 1; return; }
        if (ch == '\r') { cur_x = 0; wrap_pending = 0; return; }
        if (ch == '\n') { index_line(); return; }
        if (ch == '\b') { wrap_pending = 0; if (cur_x > 0) cur_x--; return; }
        if (ch == '\t') { int n = 8 - (cur_x % 8); while (n-- > 0 && cur_x < COLS) put_char(' '); return; }
        if (ch == 0x07) return;
        if (ch >= 0x20 && ch <= 0x7e) { put_char(ch); return; }
        return;
    }
    if (state == 1) {
        if (ch == '[') { state = 2; csi_begin(); }
        else if (ch == ']' || ch == 'P' || ch == '_' || ch == '^') state = 3;
        else if (ch == '(' || ch == ')' || ch == '*' || ch == '+' || ch == '#') state = 5;
        else if (ch == 'D') { index_line(); state = 0; }
        else if (ch == 'E') { cur_x = 0; index_line(); state = 0; }
        else if (ch == 'M') {
            wrap_pending = 0;
            if (cur_y == scroll_top) {
                for (int r = scroll_bottom; r > scroll_top; r--)
                    memcpy(screen[r], screen[r-1], sizeof(screen[r]));
                for (int col = 0; col < COLS; col++) clear_cell(&screen[scroll_top][col]);
            } else if (cur_y > 0) cur_y--;
            state = 0;
        }
        else if (ch == '7') { save_cursor(); state = 0; }
        else if (ch == '8') { restore_cursor(); state = 0; }
        else if (ch == 'c') { attr_fg = DEFAULT_FG; attr_bg = DEFAULT_BG; attr_bold = attr_dim = attr_reverse = 0; screen_clear(); state = 0; }
        else state = 0;
        return;
    }
    if (state == 2) {
        if (ch >= '0' && ch <= '9') { if (csi_cur < 1000000) csi_cur = csi_cur * 10 + (ch - '0'); csi_have = 1; return; }
        if (ch == ';') { csi_push(); return; }
        if (ch == '?') { csi_priv = 1; return; }
        if (ch >= 0x40 && ch <= 0x7e) { csi_push(); csi_dispatch(ch); state = 0; return; }
        if (ch == 0x1b) { state = 1; return; }
        state = 0;
        return;
    }
    if (state == 3) {
        if (ch == 0x07) state = 0;
        else if (ch == 0x1b) state = 4;
    } else if (state == 4) {
        state = (ch == '\\') ? 0 : 3;
    } else if (state == 5) state = 0;
}

static void term_feed(const unsigned char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        term_feed_byte(buf[i]);
}

/* ---------- framebuffer ---------- */

static int fb_open(void)
{
    void *p;
    if (fb_file_path) {
        fb_fd = open(fb_file_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fb_fd < 0) { perror("open fb-file"); return -1; }
        if (ftruncate(fb_fd, FB_BYTES) != 0) { perror("ftruncate"); close(fb_fd); fb_fd = -1; return -1; }
        p = mmap(NULL, FB_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    } else {
        fb_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (fb_fd < 0) { perror("open /dev/mem (need root)"); return -1; }
        p = mmap(NULL, FB_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, (off_t)FB_PHYS);
    }
    if (p == MAP_FAILED) { perror("mmap fb"); close(fb_fd); fb_fd = -1; return -1; }
    fb = (volatile uint32_t *)p;
    return 0;
}

static void fb_close(void)
{
    if (fb) { munmap((void *)fb, FB_BYTES); fb = NULL; }
    if (fb_fd >= 0) { close(fb_fd); fb_fd = -1; }
}

static void bg_close(void)
{
    free(bg_copy);
    bg_copy = NULL;
}

static void bg_load(void)
{
    if (!bg_file_path) return;
    bg_copy = malloc(FB_BYTES);
    if (!bg_copy) return;
    memset(bg_copy, 0x00, FB_BYTES);
    int fd = open(bg_file_path, O_RDONLY);
    if (fd < 0) return;
    size_t got = 0;
    while (got < FB_BYTES) {
        ssize_t n = read(fd, bg_copy + got, FB_BYTES - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
}

static void boot_done_open(void)
{
    gpio_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (gpio_fd < 0) return;
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_fd, (off_t)GPIO_PHYS);
    if (p == MAP_FAILED) {
        close(gpio_fd);
        gpio_fd = -1;
        return;
    }
    gpio = (volatile uint32_t *)p;
}

static void boot_done_set(int value)
{
    if (gpio) gpio[0] = value ? 1u : 0u;
}

static void boot_done_close(void)
{
    if (gpio) { munmap((void *)gpio, 4096); gpio = NULL; }
    if (gpio_fd >= 0) { close(gpio_fd); gpio_fd = -1; }
}

static void draw_glyph(int x0, int y0, unsigned char ch, unsigned char fg, int bg)
{
    if (ch < 0x20 || ch > 0x7e) ch = '?';
    const unsigned char *g = font8x16[ch - 0x20];
    int transparent = (bg == DEFAULT_BG) && bg_copy;
    if (x0 < 0 || x0 + CELL_W > FB_W || y0 < 0 || y0 + CELL_H > FB_H) return;
    for (int y = 0; y < CELL_H; y++) {
        unsigned char bits = g[y];
        for (int x = 0; x < CELL_W; x += 4) {
            uint32_t w = 0;
            for (int b = 0; b < 4; b++) {
                unsigned char pix = bg == DEFAULT_BG ? base_bg : bg;
                if (bits & (0x80 >> (x + b)))
                    pix = fg;
                else if (transparent)
                    pix = bg_copy[(y0 + y) * FB_W + x0 + x + b];
                w |= (uint32_t)pix << (8 * b);
            }
            fb[(y0 + y) * (FB_W / 4) + (x0 + x) / 4] = w;
        }
    }
}

static void draw_frame(void)
{
    for (int y = 0; y < FB_H; y++) {
        unsigned char color = y < 24 ? 0x2A : (y == 24 ? 0x2B : base_bg);
        uint32_t pixels = (uint32_t)color * 0x01010101u;
        for (int x = 0; x < FB_W/4; x++) fb[y*(FB_W/4)+x] = pixels;
    }
    const char *title = ">_ Console";
    const char *mode = "Shared";
    for (int i = 0; title[i]; i++) draw_glyph(8+i*8, 4, title[i], i<2 ? 0x7F : DEFAULT_FG, 0x2A);
    for (int i = 0; mode[i]; i++) draw_glyph(424+i*8, 4, mode[i], 0x2C, 0x2A);
}

static void render_screen(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Cell *cell = &screen[r][c];
            draw_glyph(VIEW_X + c*CELL_W, VIEW_Y + r*CELL_H, cell->ch, cell->fg, cell->bg);
        }
    if (cursor_visible && cur_x >= 0 && cur_x < COLS && cur_y >= 0 && cur_y < ROWS) {
        Cell *cell = &screen[cur_y][cur_x];
        draw_glyph(VIEW_X + cur_x*CELL_W, VIEW_Y + cur_y*CELL_H, cell->ch, cell->bg == DEFAULT_BG ? base_bg : cell->bg, cell->fg);
    }
}

/* ---------- keyboard ---------- */

static int hdmi_connected(void)
{
    glob_t paths;
    int connected = 0;
    if (glob("/sys/class/drm/card*-HDMI-A-*/status", 0, NULL, &paths) != 0) return 0;
    for (size_t i = 0; i < paths.gl_pathc && !connected; i++) {
        FILE *f = fopen(paths.gl_pathv[i], "r");
        if (!f) continue;
        char status[32];
        if (fscanf(f, "%31s", status) == 1)
            connected = strcmp(status, "connected") == 0;
        fclose(f);
    }
    globfree(&paths);
    return connected;
}

static int has_keyboard_keys(int fd)
{
    unsigned long keybit[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) return 0;
    if (!(keybit[KEY_A / (8 * sizeof(unsigned long))] & (1UL << (KEY_A % (8 * sizeof(unsigned long)))))) return 0;
    if (!(keybit[KEY_SPACE / (8 * sizeof(unsigned long))] & (1UL << (KEY_SPACE % (8 * sizeof(unsigned long)))))) return 0;
    if (!(keybit[KEY_ENTER / (8 * sizeof(unsigned long))] & (1UL << (KEY_ENTER % (8 * sizeof(unsigned long)))))) return 0;
    return 1;
}

static int key_count(int fd)
{
    unsigned long keybit[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) return -1;
    int n = 0;
    for (int i = 0; i <= KEY_MAX; i++) {
        if (keybit[i / (8 * sizeof(unsigned long))] & (1UL << (i % (8 * sizeof(unsigned long)))))
            n++;
    }
    return n;
}

static int open_keyboard(void)
{
    glob_t g;
    if (glob("/dev/input/event*", 0, NULL, &g) != 0) return -1;
    int found = -1;
    int best_count = -1;
    char best_path[256] = "";
    for (size_t i = 0; i < g.gl_pathc; i++) {
        int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long evbit[8] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0 || !(evbit[0] & (1UL << EV_KEY))) {
            close(fd); continue;
        }
        if (!has_keyboard_keys(fd)) {
            close(fd); continue;
        }
        int count = key_count(fd);
        if (count > best_count) {
            if (found >= 0) close(found);
            found = fd;
            best_count = count;
            snprintf(best_path, sizeof(best_path), "%s", g.gl_pathv[i]);
        } else {
            close(fd);
        }
    }
    globfree(&g);
    if (found < 0) return -1;
    keyboard_fd = found;
    fprintf(stderr, "lcdterm: keyboard %s (%d keys)\n", best_path, best_count);
    return 0;
}

static void update_keyboard_grab(void)
{
    if (keyboard_fd < 0) return;
    int want = !no_grab && (!hdmi_present || lcd_keyboard_selected);
    if (want == keyboard_grabbed) return;
    if (ioctl(keyboard_fd, EVIOCGRAB, want) == 0) {
        keyboard_grabbed = want;
        fprintf(stderr, "lcdterm: keyboard routed to %s\n", want ? "LCD" : "desktop");
    } else {
        perror("lcdterm: EVIOCGRAB");
    }
}

static const char *shift_map[] = {
    [KEY_1] = "!", [KEY_2] = "@", [KEY_3] = "#", [KEY_4] = "$", [KEY_5] = "%",
    [KEY_6] = "^", [KEY_7] = "&", [KEY_8] = "*", [KEY_9] = "(", [KEY_0] = ")",
    [KEY_MINUS] = "_", [KEY_EQUAL] = "+", [KEY_LEFTBRACE] = "{", [KEY_RIGHTBRACE] = "}",
    [KEY_BACKSLASH] = "|", [KEY_SEMICOLON] = ":", [KEY_APOSTROPHE] = "\"",
    [KEY_GRAVE] = "~", [KEY_COMMA] = "<", [KEY_DOT] = ">", [KEY_SLASH] = "?",
};

static const char letter_map[KEY_MAX + 1] = {
    [KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd', [KEY_E] = 'e',
    [KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h', [KEY_I] = 'i', [KEY_J] = 'j',
    [KEY_K] = 'k', [KEY_L] = 'l', [KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o',
    [KEY_P] = 'p', [KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
    [KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x', [KEY_Y] = 'y',
    [KEY_Z] = 'z',
};

static const char *plain_map[] = {
    [KEY_MINUS] = "-", [KEY_EQUAL] = "=", [KEY_LEFTBRACE] = "[", [KEY_RIGHTBRACE] = "]",
    [KEY_BACKSLASH] = "\\", [KEY_SEMICOLON] = ";", [KEY_APOSTROPHE] = "'",
    [KEY_GRAVE] = "`", [KEY_COMMA] = ",", [KEY_DOT] = ".", [KEY_SLASH] = "/",
    [KEY_SPACE] = " ",
};

static int shift_down = 0, ctrl_down = 0, alt_down = 0;

static void send_key(const char *s)
{
    if (master_fd >= 0 && s && *s)
        (void)!write(master_fd, s, strlen(s));
}

static void send_char(int c)
{
    if (ctrl_down && c >= 'a' && c <= 'z') c = c - 'a' + 1;
    else if (ctrl_down && c >= 'A' && c <= 'Z') c = c - 'A' + 1;
    char buf[8];
    int n = 0;
    if (alt_down) buf[n++] = 0x1b;
    if (c >= 0 && c < 128) buf[n++] = (char)c;
    if (n && master_fd >= 0) (void)!write(master_fd, buf, (size_t)n);
}

static void handle_key_event(unsigned int code, int value)
{
    if (value == 0) {
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) shift_down = 0;
        else if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) ctrl_down = 0;
        else if (code == KEY_LEFTALT || code == KEY_RIGHTALT) alt_down = 0;
        if (keyboard_toggle_pending && !shift_down && !ctrl_down) {
            keyboard_toggle_pending = 0;
            lcd_keyboard_selected = !lcd_keyboard_selected;
            update_keyboard_grab();
        }
        return;
    }
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { shift_down = 1; return; }
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) { ctrl_down = 1; return; }
    if (code == KEY_LEFTALT || code == KEY_RIGHTALT) { alt_down = 1; return; }

    if (code == KEY_F12 && ctrl_down && shift_down && !alt_down && hdmi_present && !no_grab) {
        if (value == 1) keyboard_toggle_pending = 1;
        return;
    }
    if (!keyboard_grabbed) return;

    if (code == KEY_ENTER || code == KEY_KPENTER) { send_char('\r'); return; }
    if (code == KEY_BACKSPACE) { send_char(0x7f); return; }
    if (code == KEY_TAB) { send_char('\t'); return; }
    if (code == KEY_ESC) { send_char(0x1b); return; }
    if (code == KEY_UP) { send_key("\x1b[A"); return; }
    if (code == KEY_DOWN) { send_key("\x1b[B"); return; }
    if (code == KEY_RIGHT) { send_key("\x1b[C"); return; }
    if (code == KEY_LEFT) { send_key("\x1b[D"); return; }
    if (code == KEY_HOME) { send_key("\x1b[H"); return; }
    if (code == KEY_END) { send_key("\x1b[F"); return; }
    if (code == KEY_INSERT) { send_key("\x1b[2~"); return; }
    if (code == KEY_DELETE) { send_key("\x1b[3~"); return; }
    if (code == KEY_PAGEUP) { send_key("\x1b[5~"); return; }
    if (code == KEY_PAGEDOWN) { send_key("\x1b[6~"); return; }
    if (code >= KEY_F1 && code <= KEY_F12) {
        static const char *fkeys[] = {"\x1bOP","\x1bOQ","\x1bOR","\x1bOS","\x1b[15~","\x1b[17~",
                                      "\x1b[18~","\x1b[19~","\x1b[20~","\x1b[21~","\x1b[23~","\x1b[24~"};
        send_key(fkeys[code - KEY_F1]);
        return;
    }

    if (code <= KEY_MAX && letter_map[code]) {
        int c = letter_map[code];
        if (shift_down) c -= 32;
        send_char(c);
        return;
    }
    if (code >= KEY_1 && code <= KEY_0) {
        int idx = (code == KEY_0) ? 0 : (code - KEY_1 + 1);
        if (shift_down && shift_map[code]) send_key(shift_map[code]);
        else send_char('0' + idx);
        return;
    }
    if (code == KEY_KP1 || code == KEY_KP2 || code == KEY_KP3 || code == KEY_KP4 ||
        code == KEY_KP5 || code == KEY_KP6 || code == KEY_KP7 || code == KEY_KP8 ||
        code == KEY_KP9 || code == KEY_KP0) {
        static const int kp[] = {KEY_KP1,KEY_KP2,KEY_KP3,KEY_KP4,KEY_KP5,KEY_KP6,KEY_KP7,KEY_KP8,KEY_KP9,KEY_KP0};
        static const char *digits[] = {"1","2","3","4","5","6","7","8","9","0"};
        for (int i = 0; i < 10; i++) if (code == (unsigned int)kp[i]) { send_key(digits[i]); return; }
    }
    if (code < (unsigned int)(sizeof(plain_map)/sizeof(plain_map[0])) && plain_map[code]) {
        if (shift_down && code < (unsigned int)(sizeof(shift_map)/sizeof(shift_map[0])) && shift_map[code])
            send_key(shift_map[code]);
        else
            send_key(plain_map[code]);
        return;
    }
}

static void handle_keyboard(void)
{
    struct input_event ev;
    while (keyboard_fd >= 0) {
        ssize_t n = read(keyboard_fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY) handle_key_event(ev.code, ev.value);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (keyboard_grabbed) ioctl(keyboard_fd, EVIOCGRAB, 0);
        close(keyboard_fd);
        keyboard_fd = -1;
        keyboard_grabbed = 0;
        keyboard_toggle_pending = 0;
        shift_down = ctrl_down = alt_down = 0;
        break;
    }
}

/* ---------- pty / child ---------- */

static int spawn_shell(void)
{
    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_iflag = ICRNL | IXON;
    tio.c_oflag = OPOST | ONLCR;
    tio.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
    tio.c_cc[VINTR] = 3;
    tio.c_cc[VERASE] = 127;
    tio.c_cc[VKILL] = 21;
    tio.c_cc[VEOF] = 4;
    tio.c_cc[VSTART] = 17;
    tio.c_cc[VSTOP] = 19;
    tio.c_cc[VSUSP] = 26;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = ROWS;
    ws.ws_col = COLS;
    ws.ws_xpixel = COLS*CELL_W;
    ws.ws_ypixel = ROWS*CELL_H;

    child_pid = forkpty(&master_fd, NULL, &tio, &ws);
    if (child_pid < 0) { perror("forkpty"); return -1; }
    if (child_pid == 0) {
        const char *wrapper = "/usr/local/bin/lcdterm-bash";
        setenv("TERM", "xterm-256color", 1);
        if (!strcmp(shell_path, "/bin/bash") && access(wrapper, X_OK) == 0)
            shell_path = wrapper;
        execl(shell_path, shell_path, "-l", (char *)NULL);
        execl("/bin/sh", "sh", "-l", (char *)NULL);
        _exit(127);
    }
    fcntl(master_fd, F_SETFL, fcntl(master_fd, F_GETFL, 0) | O_NONBLOCK);
    return 0;
}

static void stop_child(void)
{
    if (child_pid > 0) {
        kill(child_pid, SIGHUP);
        waitpid(child_pid, NULL, WNOHANG);
        child_pid = -1;
    }
    if (master_fd >= 0) { close(master_fd); master_fd = -1; }
}

/* ---------- demo / selftest ---------- */

static void draw_demo_page(void)
{
    screen_clear();
    attr_fg = 0xFF; attr_bg = 0x00; attr_bold = 0;
    const char *lines[] = {
        "4.3 LCD 480x272 8bpp",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789",
        "abcdefghijklmnopqrstuvwxyz .,:;!?",
        "The quick brown fox jumps over the lazy dog.",
        "Pack my box with five dozen liquor jugs.",
        "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    };
    int row = 0;
    for (size_t i = 0; i < sizeof(lines)/sizeof(lines[0]); i++) {
        cur_y = row++;
        cur_x = 0;
        for (const char *p = lines[i]; *p; p++) put_char((unsigned char)*p);
    }
    cur_x = 0; cur_y = 8;
    for (int i = 0; i < 8; i++) {
        attr_bg = ansi_rgb332[i]; attr_fg = 0x00;
        for (int n = 0; n < 7; n++) put_char(' ');
    }
    cur_x = 0; cur_y = 9;
    attr_bg = 0x00; attr_fg = DEFAULT_FG;
    const char *s = "ANSI 0..7 colors; 8x16 font; 58x15 cells";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 0; cur_y = 10;
    s = "Cursor test -> ";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 0; cur_y = 12;
    s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 0; cur_y = 13;
    s = "0123456789 0123456789 0123456789 0123456789 0123456789";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 0; cur_y = 14;
    s = "The five boxing wizards jump quickly. 1234567890";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 0; cur_y = ROWS - 1;
    s = "Bottom row: 58 chars -> 123456789012345678901234567890123";
    for (const char *p = s; *p; p++) put_char((unsigned char)*p);
    cur_x = 14; cur_y = 10;
    render_screen();
}

static int run_selftest(void)
{
    int fails = 0;
    screen_clear();
    term_feed((const unsigned char *)"Hello\r\n", 7);
    const char *hello = "Hello";
    for (int i = 0; i < 5; i++)
        if (screen[0][i].ch != (unsigned char)hello[i]) { fprintf(stderr, "FAIL hello[%d]\n", i); fails++; break; }
    if (cur_y != 1 || cur_x != 0) { fprintf(stderr, "FAIL newline\n"); fails++; }
    term_feed((const unsigned char *)"\x1b[2;3HX", 7);
    if (screen[1][2].ch != 'X') { fprintf(stderr, "FAIL cup\n"); fails++; }
    term_feed((const unsigned char *)"\x1b[31mR", 6);
    if (screen[1][3].fg != ansi_rgb332[1]) { fprintf(stderr, "FAIL sgr color %02x\n", screen[1][3].fg); fails++; }
    screen_clear();
    static const unsigned char bottom[] = "\x1b[15;58HZ";
    term_feed(bottom, sizeof(bottom)-1);
    if (screen[ROWS-1][COLS-1].ch != 'Z' || !wrap_pending) {
        fprintf(stderr, "FAIL bottom-right wrap\n");
        fails++;
    }
    screen_clear();
    term_feed((const unsigned char *)"\x1b[2;5H", 6);
    static const unsigned char colors[] = "\x1b[38;5;196;40mR\x1b[38;2;0;255;0;49mG\x1b[0m";
    term_feed(colors, sizeof(colors) - 1);
    if (screen[1][4].fg != 0xE0 || screen[1][4].bg != 0x00 ||
        screen[1][5].fg != 0x1C || screen[1][5].bg != DEFAULT_BG) {
        fprintf(stderr, "FAIL extended colors or background reset\n");
        fails++;
    }
    screen_clear();
    for (int i = 0; i < ROWS; i++) {
        term_feed((const unsigned char *)"\r\n", 2);
    }
    if (cur_y != ROWS - 1) { fprintf(stderr, "FAIL scroll end %d\n", cur_y); fails++; }
    term_feed((const unsigned char *)"\r\n", 2);
    if (cur_y != ROWS - 1) { fprintf(stderr, "FAIL scroll clamp %d\n", cur_y); fails++; }
    screen_clear();
    unsigned int nz_a = 0, nz_lower = 0;
    for (int y = 0; y < 16; y++) {
        nz_a |= font8x16['A' - 0x20][y];
        nz_lower |= font8x16['a' - 0x20][y];
    }
    if (!nz_a || !nz_lower) { fprintf(stderr, "FAIL font blank\n"); fails++; }
    if (sizeof(font8x16) != 95 * 16) { fprintf(stderr, "FAIL font size\n"); fails++; }
    draw_demo_page();
    printf("selftest: %s; fb=%dx%d bytes=%d cells=%dx%d font=8x16\n",
           fails ? "FAIL" : "PASS", FB_W, FB_H, FB_BYTES, COLS, ROWS);
    return fails ? 1 : 0;
}

/* ---------- service ---------- */

static int install_service(void)
{
    const char *path = "/etc/systemd/system/lcdterm.service";
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fputs("[Unit]\n"
          "Description=4.3 LCD terminal\n"
          "After=multi-user.target\n"
          "\n"
          "[Service]\n"
          "Type=simple\n"
          "ExecStart=/usr/local/bin/lcdterm\n"
          "Restart=always\n"
          "RestartSec=2\n"
          "\n"
          "[Install]\n"
          "WantedBy=multi-user.target\n", f);
    fclose(f);
    printf("wrote %s\n", path);
    printf("Run: systemctl daemon-reload && systemctl enable --now lcdterm\n");
    return 0;
}

/* ---------- main ---------- */

static void on_signal(int sig) { (void)sig; running = 0; }

static void usage(const char *argv0)
{
    printf("Usage: %s [options]\n", argv0);
    printf("  --fb-file PATH    use regular framebuffer file (host test)\n");
    printf("  --bg-file PATH    optional raw RGB332 background\n");
    printf("  --charcoal        use the matching PL palette background\n");
    printf("  --pl-handoff      signal boot overlay on the matching PL build\n");
    printf("  --shell PATH      shell (default /bin/bash)\n");
    printf("  --no-kbd          do not use evdev keyboard\n");
    printf("  --no-grab         do not EVIOCGRAB keyboard\n");
    printf("  --stdin           forward stdin to pty\n");
    printf("  --selftest        run checks and draw test page\n");
    printf("  --demo            draw test page and exit\n");
    printf("  --install-service install systemd unit\n");
}

int main(int argc, char **argv)
{
    int selftest = 0, demo = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fb-file") && i + 1 < argc) fb_file_path = argv[++i];
        else if (!strcmp(argv[i], "--bg-file") && i + 1 < argc) bg_file_path = argv[++i];
        else if (!strcmp(argv[i], "--shell") && i + 1 < argc) shell_path = argv[++i];
        else if (!strcmp(argv[i], "--no-kbd")) no_kbd = 1;
        else if (!strcmp(argv[i], "--no-grab")) no_grab = 1;
        else if (!strcmp(argv[i], "--stdin")) forward_stdin = 1;
        else if (!strcmp(argv[i], "--charcoal")) base_bg = 0x24;
        else if (!strcmp(argv[i], "--pl-handoff")) pl_handoff = 1;
        else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--install-service")) return install_service();
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); usage(argv[0]); return 2; }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGCHLD, SIG_DFL);

    if (fb_open() != 0) return 1;
    bg_load();
    draw_frame();
    screen_clear();

    if (selftest) {
        int r = run_selftest();
        bg_close();
        fb_close();
        return r;
    }
    if (demo) {
        draw_demo_page();
        bg_close();
        fb_close();
        return 0;
    }

    if (!no_kbd && !no_grab) open_keyboard();
    if (pl_handoff && !fb_file_path) boot_done_open();
    hdmi_present = hdmi_connected();
    update_keyboard_grab();

    if (spawn_shell() != 0) { fb_close(); return 1; }

    unsigned char buf[4096];


    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (master_fd >= 0) { FD_SET(master_fd, &rfds); if (master_fd > maxfd) maxfd = master_fd; }
        if (keyboard_fd >= 0) { FD_SET(keyboard_fd, &rfds); if (keyboard_fd > maxfd) maxfd = keyboard_fd; }
        if (forward_stdin) { FD_SET(STDIN_FILENO, &rfds); if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO; }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        int did_read = 0;
        if (master_fd >= 0 && FD_ISSET(master_fd, &rfds)) {
            ssize_t got = read(master_fd, buf, sizeof(buf));
            if (got > 0) { term_feed(buf, (size_t)got); did_read = 1; }
            else if (got == 0) { running = 0; }
            else if (errno == EIO || errno == EBADF) { running = 0; }
        }
        if (keyboard_fd >= 0 && FD_ISSET(keyboard_fd, &rfds)) handle_keyboard();
        if (forward_stdin && FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
            if (got > 0 && master_fd >= 0) (void)!write(master_fd, buf, (size_t)got);
            else if (got == 0) forward_stdin = 0;
        }

        time_t now = time(NULL);
        if (now != last_hdmi_check) {
            last_hdmi_check = now;
            int h = hdmi_connected();
            if (h != hdmi_present) {
                hdmi_present = h;
                keyboard_toggle_pending = 0;
                lcd_keyboard_selected = 0;
                update_keyboard_grab();
            }
        }
        if (!no_kbd && !no_grab && keyboard_fd < 0 && now != last_keyboard_scan) {
            last_keyboard_scan = now;
            if (open_keyboard() == 0) update_keyboard_grab();
        }
        if (did_read) {
            render_screen();
            if (!boot_done_sent) {
                boot_done_set(1);
                boot_done_sent = 1;
            }
        }
    }

    if (keyboard_grabbed) ioctl(keyboard_fd, EVIOCGRAB, 0);
    if (keyboard_fd >= 0) close(keyboard_fd);
    stop_child();
    boot_done_close();
    bg_close();
    fb_close();
    return 0;
}



