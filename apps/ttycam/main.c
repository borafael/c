/* ttycam — live webcam in the terminal. Captures from /dev/video0 via
 * V4L2 (YUYV, mmap streaming) and pipes each frame through libs/term,
 * the same glyph/colour pipeline tty3d uses. POSIX-only.
 *
 *   --device /dev/videoN     pick a different camera
 *   --glyph halfblock|ascii  same auto-detect rules as tty3d
 *   --color truecolor|256|mono
 *   --fps  N
 *
 * Live toggles (key events):
 *   G  cycle glyph mode
 *   C  cycle colour mode
 *   R  force full redraw
 *   Q / ESC  quit
 *
 * The conversion path is YUYV -> ARGB into a framebuffer sized for the
 * cell grid; aspect ratio is preserved with letterbox bars (terminal
 * cells are ~2x tall, so the maths differs between halfblock and ascii
 * fb pixel shapes). The image is mirrored horizontally so it behaves
 * like a vanity mirror. */

#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define V4L_BUFS 4

struct cap_buf { void *start; size_t length; };

struct cam {
    int            fd;
    int            w, h;
    struct cap_buf bufs[V4L_BUFS];
    int            n_bufs;
};

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static int cam_open(struct cam *c, const char *dev, int want_w, int want_h) {
    c->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (c->fd < 0) { perror(dev); return -1; }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (xioctl(c->fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("VIDIOC_QUERYCAP"); return -1; }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s: not a video capture device\n", dev); return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s: streaming not supported\n", dev); return -1;
    }

    /* Request YUYV at the desired resolution. The driver picks the
     * closest match it can deliver and writes the granted size back. */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = want_w;
    fmt.fmt.pix.height      = want_h;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(c->fd, VIDIOC_S_FMT, &fmt) < 0) { perror("VIDIOC_S_FMT"); return -1; }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "%s: driver doesn't support YUYV\n", dev); return -1;
    }
    c->w = (int)fmt.fmt.pix.width;
    c->h = (int)fmt.fmt.pix.height;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count  = V4L_BUFS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(c->fd, VIDIOC_REQBUFS, &req) < 0) { perror("VIDIOC_REQBUFS"); return -1; }
    if (req.count < 2) { fprintf(stderr, "not enough buffers\n"); return -1; }
    c->n_bufs = (int)req.count;

    for (int i = 0; i < c->n_bufs; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof b);
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(c->fd, VIDIOC_QUERYBUF, &b) < 0) { perror("VIDIOC_QUERYBUF"); return -1; }
        c->bufs[i].length = b.length;
        c->bufs[i].start  = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, c->fd, b.m.offset);
        if (c->bufs[i].start == MAP_FAILED) { perror("mmap"); return -1; }
        if (xioctl(c->fd, VIDIOC_QBUF, &b) < 0) { perror("VIDIOC_QBUF"); return -1; }
    }

    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(c->fd, VIDIOC_STREAMON, &t) < 0) { perror("VIDIOC_STREAMON"); return -1; }
    return 0;
}

static void cam_close(struct cam *c) {
    if (c->fd < 0) return;
    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(c->fd, VIDIOC_STREAMOFF, &t);
    for (int i = 0; i < c->n_bufs; i++) {
        if (c->bufs[i].start && c->bufs[i].start != MAP_FAILED) {
            munmap(c->bufs[i].start, c->bufs[i].length);
        }
    }
    close(c->fd);
    c->fd = -1;
}

struct blit_target {
    uint32_t *dst;
    int       dw, dh;
    int       ascii_pixels;  /* fb pixels are ~2:1 tall in ascii mode */
};

/* YUYV macropixel: Y0 U Y1 V — 4 bytes for 2 horizontal pixels. */
static inline uint32_t yuv2rgb(int Y, int U, int V) {
    int C = Y - 16;
    int D = U - 128;
    int E = V - 128;
    int R = (298 * C + 409 * E + 128) >> 8;
    int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
    int B = (298 * C + 516 * D + 128) >> 8;
    if (R < 0) R = 0; else if (R > 255) R = 255;
    if (G < 0) G = 0; else if (G > 255) G = 255;
    if (B < 0) B = 0; else if (B > 255) B = 255;
    return ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

static void blit_yuyv(const uint8_t *src, int sw, int sh, struct blit_target *t) {
    int       dw  = t->dw;
    int       dh  = t->dh;
    uint32_t *dst = t->dst;

    /* Effective fb-pixel aspect: halfblock pixels are square, ASCII
     * pixels are ~2x taller than wide. row_units is height per fb row
     * in "square units" relative to a column. */
    float row_units  = t->ascii_pixels ? 2.0f : 1.0f;
    float src_aspect = (float)sw / (float)sh;
    float dst_aspect = (float)dw / ((float)dh * row_units);

    int box_w, box_h;
    if (src_aspect > dst_aspect) {
        box_w = dw;
        box_h = (int)((float)dw / src_aspect / row_units);
        if (box_h > dh) box_h = dh;
    } else {
        box_h = dh;
        box_w = (int)((float)dh * row_units * src_aspect);
        if (box_w > dw) box_w = dw;
    }
    int box_x = (dw - box_w) / 2;
    int box_y = (dh - box_h) / 2;

    memset(dst, 0, (size_t)dw * (size_t)dh * sizeof(uint32_t));

    for (int y = 0; y < box_h; y++) {
        int sy = (int)((float)y * (float)sh / (float)box_h);
        if (sy >= sh) sy = sh - 1;
        const uint8_t *row = src + (size_t)sy * (size_t)sw * 2u;
        uint32_t      *out = dst + (size_t)(box_y + y) * (size_t)dw + (size_t)box_x;
        for (int x = 0; x < box_w; x++) {
            int sx = (int)((float)(box_w - 1 - x) * (float)sw / (float)box_w);
            if (sx >= sw) sx = sw - 1;
            int pair = sx & ~1;
            int Y    = row[pair * 2 + ((sx & 1) ? 2 : 0)];
            int U    = row[pair * 2 + 1];
            int V    = row[pair * 2 + 3];
            out[x]   = yuv2rgb(Y, U, V);
        }
    }
}

/* Block briefly waiting for the next frame, blit it into the fb, and
 * re-queue the V4L2 buffer. Returns 1 if a new frame landed. */
static int cam_grab(struct cam *c, struct blit_target *t) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(c->fd, &fds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
    int r = select(c->fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return 0;

    struct v4l2_buffer b;
    memset(&b, 0, sizeof b);
    b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (xioctl(c->fd, VIDIOC_DQBUF, &b) < 0) {
        if (errno == EAGAIN) return 0;
        perror("VIDIOC_DQBUF"); return 0;
    }
    blit_yuyv((const uint8_t *)c->bufs[b.index].start, c->w, c->h, t);
    xioctl(c->fd, VIDIOC_QBUF, &b);
    return 1;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--device /dev/video0] [--glyph halfblock|ascii|auto]\n"
        "          [--color truecolor|256|mono|auto] [--fps N]\n"
        "Controls:\n"
        "  G  cycle glyph mode (halfblock <-> ascii)\n"
        "  C  cycle colour mode (truecolor -> 256 -> mono)\n"
        "  R  force full redraw\n"
        "  Q / ESC  quit\n",
        argv0);
}

int main(int argc, char *argv[]) {
    term_caps caps        = term_caps_detect();
    int       target_fps  = 30;
    const char *device    = "/dev/video0";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device = argv[++i];
        } else if (strcmp(argv[i], "--glyph") == 0 && i + 1 < argc) {
            if (!term_caps_parse_glyph(argv[++i], &caps.glyph)) {
                fprintf(stderr, "unknown glyph mode: %s\n", argv[i]); return 2;
            }
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (!term_caps_parse_color(argv[++i], &caps.color)) {
                fprintf(stderr, "unknown color mode: %s\n", argv[i]); return 2;
            }
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            target_fps = atoi(argv[++i]);
            if (target_fps < 5)  target_fps = 5;
            if (target_fps > 60) target_fps = 60;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]); usage(argv[0]); return 2;
        }
    }

    struct cam cam = { .fd = -1 };
    if (cam_open(&cam, device, 640, 480) < 0) return 1;

    term_screen *screen = term_screen_open();
    if (!screen) {
        fprintf(stderr, "term_screen_open failed (not a tty?)\n");
        cam_close(&cam); return 1;
    }
    term_render_ctx *r = term_render_create();

    int cols = 0, rows = 0;
    term_screen_size(&cols, &rows);
    int draw_rows = rows > 1 ? rows - 1 : rows;

    int fb_w = 0, fb_h = 0;
    term_render_pixel_size(&caps, cols, draw_rows, &fb_w, &fb_h);
    uint32_t *pixels = calloc((size_t)fb_w * (size_t)fb_h, sizeof(uint32_t));

    int    running        = 1;
    double frame_target   = 1.0 / (double)target_fps;
    double next_frame     = now_sec();
    double last_fps_t     = now_sec();
    int    fps_frames     = 0;
    int    last_fps       = 0;
    size_t last_bytes     = 0;

    #define RECONFIGURE() do {                                              \
        term_screen_size(&cols, &rows);                                     \
        draw_rows = rows > 1 ? rows - 1 : rows;                             \
        term_render_pixel_size(&caps, cols, draw_rows, &fb_w, &fb_h);       \
        free(pixels);                                                       \
        pixels = calloc((size_t)fb_w * (size_t)fb_h, sizeof(uint32_t));     \
        term_render_force_full_redraw(r);                                   \
        const char _clr[] = "\x1b[2J\x1b[H";                                \
        term_screen_present(screen, _clr, sizeof(_clr) - 1);                \
    } while (0)

    while (running) {
        if (term_screen_consume_resize(screen)) RECONFIGURE();

        term_key key;
        while (term_input_poll(&key)) {
            if (key.kind == TERM_KEY_ESC) { running = 0; break; }
            if (key.kind == TERM_KEY_CHAR) {
                if (key.ch == 'q' || key.ch == 'Q' || key.ch == 0x03) {
                    running = 0; break;
                }
                if (key.ch == 'r' || key.ch == 'R') RECONFIGURE();
                if (key.ch == 'g' || key.ch == 'G') {
                    caps.glyph = (caps.glyph == TERM_GLYPH_HALFBLOCK)
                                 ? TERM_GLYPH_ASCII : TERM_GLYPH_HALFBLOCK;
                    RECONFIGURE();
                }
                if (key.ch == 'c' || key.ch == 'C') {
                    caps.color = (caps.color == TERM_COLOR_TRUECOLOR)
                                 ? TERM_COLOR_PALETTE256
                                 : (caps.color == TERM_COLOR_PALETTE256)
                                 ? TERM_COLOR_MONO
                                 : TERM_COLOR_TRUECOLOR;
                    RECONFIGURE();
                }
            }
        }
        if (!running) break;

        struct blit_target t = {
            .dst = pixels, .dw = fb_w, .dh = fb_h,
            .ascii_pixels = (caps.glyph == TERM_GLYPH_ASCII),
        };
        cam_grab(&cam, &t);

        size_t n = term_render_frame(r, &caps, pixels, fb_w, fb_h, cols, draw_rows);
        last_bytes = n;
        term_screen_present(screen, term_render_buffer(r), n);

        char status[256];
        int sl = snprintf(status, sizeof(status),
            "\x1b[%d;1H\x1b[0m\x1b[2K"
            "%dx%d cells (fb %dx%d) | cam %dx%d | %s+%s | %d fps | %zu B/frame | "
            "g=glyph c=color r=redraw q=quit",
            rows, cols, draw_rows, fb_w, fb_h, cam.w, cam.h,
            term_caps_glyph_name(caps.glyph),
            term_caps_color_name(caps.color),
            last_fps, last_bytes);
        if (sl > 0) term_screen_present(screen, status, (size_t)sl);

        next_frame += frame_target;
        double slack = next_frame - now_sec();
        if (slack > 0) {
            struct timespec ts = {
                .tv_sec  = (time_t)slack,
                .tv_nsec = (long)((slack - (double)(time_t)slack) * 1e9),
            };
            nanosleep(&ts, NULL);
        } else if (slack < -0.5) {
            next_frame = now_sec();
        }

        fps_frames++;
        double tnow = now_sec();
        if (tnow - last_fps_t >= 1.0) {
            last_fps   = fps_frames;
            fps_frames = 0;
            last_fps_t = tnow;
        }
    }

    term_render_destroy(r);
    term_screen_close(screen);
    free(pixels);
    cam_close(&cam);
    return 0;
}
