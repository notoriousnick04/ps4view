#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>
#include <turbojpeg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <sys/time.h>

#define MAX_DEVICES 64
#define DEFAULT_W 1920
#define DEFAULT_H 1080
#define DEFAULT_FPS 60

typedef struct { void *start; size_t length; } Buffer;

static int g_fd = -1;
static Buffer *g_bufs = NULL;
static int g_nbuf = 0;
static int g_width = DEFAULT_W;
static int g_height = DEFAULT_H;
static int g_fps = DEFAULT_FPS;
static int g_mjpg = 0;
static int g_vsync = 1;
static int g_fullscreen = 1;
static int g_stats = 0;
static int g_stats_overlay = 0;
static int g_glossary = 1;
static int g_glossary_flash = 0;
static char g_toast[128] = "";
static Uint32 g_toast_until = 0;
static Uint32 g_toast_ms = 3000;
static int g_toast_enabled = 1;
static SDL_Texture *g_toast_tex = NULL;
static int g_toast_w = 0, g_toast_h = 0;
static Uint32 g_glossary_timeout_ms = 0;
static Uint32 g_glossary_shown_at = 0;
static int g_cursor_hide = 1;
static Uint32 g_cursor_hide_ms = 3000;
static Uint32 g_last_mouse = 0;
static int g_cursor_hidden = 0;
static int g_running = 1;

static tjhandle g_tj = NULL;
static unsigned char *g_rgb = NULL;
static FT_Library g_ft = NULL;
static FT_Face g_face = NULL;
static SDL_AudioDeviceID g_audio_cap = 0;
static SDL_AudioDeviceID g_audio_play = 0;
static SDL_AudioStream *g_audio_stream = NULL;
static int g_audio_on = 0;
static int g_no_audio = 0;
static int g_audio_flag = 0;
static char g_device_path[256] = "";
static int g_config_enabled = 1;
static int g_reset_pending = 0;
static Uint32 g_reset_pending_until = 0;
static void overlay_init(void);
static void overlay_shutdown(void);
static void build_stats(void);
static void build_glossary(void);
static void show_toast(const char *msg);
static void build_toast(void);
static void audio_init(void);
static void audio_shutdown(void);
static void audio_toggle(void);
static void load_config(void);
static void save_config(void);
static void reset_config(void);
static void config_path(char *buf, size_t sz);

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture *g_tex = NULL;
static SDL_Texture *g_stats_tex = NULL;
static int g_stats_w = 0, g_stats_h = 0;
static SDL_Texture *g_glossary_tex = NULL;
static int g_glossary_w = 0, g_glossary_h = 0;

static unsigned long frames_rendered = 0;
static unsigned long frames_dropped = 0;
static double g_stat_fps = 0;
static double g_stat_ms = 0;
static unsigned long g_stat_dropped = 0;

static void die(const char *msg) {
    fprintf(stderr, "ps4view: %s: %s\n", msg, strerror(errno));
    exit(1);
}


static void print_device_busy(const char *dev) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "fuser %s 2>/dev/null", dev);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            fprintf(stderr, "ps4view: %s is in use by PID(s): %s", dev, buf);
            fprintf(stderr, "ps4view: stop the other process or pick another device (--device)\n");
        } else {
            fprintf(stderr, "ps4view: %s is busy (could not identify holder)\n", dev);
        }
        pclose(fp);
    }
}

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

static int is_capture(int fd) {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) return 0;
    unsigned caps = cap.device_caps ? cap.device_caps : cap.capabilities;
    return (caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING);
}

static long score_device(const char *path) {
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) return -1;
    if (!is_capture(fd)) { close(fd); return -1; }

    long best = 0;
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        struct v4l2_frmsizeenum fs;
        memset(&fs, 0, sizeof(fs));
        fs.pixel_format = fmtdesc.pixelformat;
        while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0) {
            int w = 0, h = 0;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                w = fs.discrete.width; h = fs.discrete.height;
            } else {
                w = fs.stepwise.max_width; h = fs.stepwise.max_height;
            }
            struct v4l2_frmivalenum fi;
            memset(&fi, 0, sizeof(fi));
            fi.pixel_format = fmtdesc.pixelformat;
            fi.width = w; fi.height = h;
            int got = 0;
            while (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0) {
                double fps = 30.0;
                if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                    if (fi.discrete.numerator > 0)
                        fps = (double)fi.discrete.denominator / (double)fi.discrete.numerator;
                } else {
                    if (fi.stepwise.min.numerator > 0)
                        fps = (double)fi.stepwise.min.denominator / (double)fi.stepwise.min.numerator;
                }
                long score = (long)w * h * (long)(fps + 0.5);
                if (score > best) best = score;
                got = 1;
                fi.index++;
            }
            if (!got) {
                long score = (long)w * h * 30;
                if (score > best) best = score;
            }
            fs.index++;
        }
        fmtdesc.index++;
    }
    if (best == 0) best = 640 * 480 * 30;
    close(fd);
    return best;
}

static void list_devices(void) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        if (access(path, F_OK) != 0) continue;
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        if (!is_capture(fd)) { close(fd); continue; }
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        xioctl(fd, VIDIOC_QUERYCAP, &cap);
        printf("%s  %-32s  score=%ld\n", path, (char*)cap.card, score_device(path));
        close(fd);
    }
}

static char *autoscan_device(void) {
    char bestpath[64] = {0};
    long bestscore = -1;
    for (int i = 0; i < MAX_DEVICES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        if (access(path, F_OK) != 0) continue;
        long s = score_device(path);
        if (s > bestscore) {
            bestscore = s;
            strcpy(bestpath, path);
        }
    }
    if (bestpath[0] == 0) return NULL;
    return strdup(bestpath);
}

static void v4l2_open(const char *dev) {
    g_fd = open(dev, O_RDWR | O_NONBLOCK, 0);
    if (g_fd < 0) {
        if (errno == EBUSY || errno == EACCES) print_device_busy(dev);
        die("open device");
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = g_width;
    fmt.fmt.pix.height = g_height;
    fmt.fmt.pix.pixelformat = g_mjpg ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) {
        if (errno == EBUSY) print_device_busy(dev);
        die("VIDIOC_S_FMT");
    }
    g_width = fmt.fmt.pix.width;
    g_height = fmt.fmt.pix.height;

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = g_fps;
    xioctl(g_fd, VIDIOC_S_PARM, &parm);
    if (parm.parm.capture.timeperframe.denominator > 0)
        g_fps = parm.parm.capture.timeperframe.denominator /
                (parm.parm.capture.timeperframe.numerator ? parm.parm.capture.timeperframe.numerator : 1);

    fprintf(stderr, "ps4view: %s  %dx%d  %s  %dfps\n", dev, g_width, g_height,
            g_mjpg ? "MJPG" : "YUYV", g_fps);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");
    if (req.count < 2) {
        fprintf(stderr, "ps4view: insufficient buffers (%d)\n", req.count);
        exit(1);
    }
    g_nbuf = req.count;
    g_bufs = calloc(g_nbuf, sizeof(Buffer));
    for (int i = 0; i < g_nbuf; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");
        g_bufs[i].length = buf.length;
        g_bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, buf.m.offset);
        if (g_bufs[i].start == MAP_FAILED) die("mmap");
    }
    for (int i = 0; i < g_nbuf; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(g_fd, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");
}

static void v4l2_close(void) {
    if (g_fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(g_fd, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < g_nbuf; i++) {
            if (g_bufs[i].start && g_bufs[i].start != MAP_FAILED)
                munmap(g_bufs[i].start, g_bufs[i].length);
        }
        free(g_bufs);
        close(g_fd);
        g_fd = -1;
    }
}

static void sdl_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    Uint32 wflags = g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    g_win = SDL_CreateWindow("PS4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            g_width, g_height, wflags);
    if (!g_win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); exit(1); }
    Uint32 rflags = SDL_RENDERER_ACCELERATED;
    if (g_vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;
    g_ren = SDL_CreateRenderer(g_win, -1, rflags);
    if (!g_ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); exit(1); }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    overlay_init();

    Uint32 pixfmt = g_mjpg ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_YUY2;
    g_tex = SDL_CreateTexture(g_ren, pixfmt, SDL_TEXTUREACCESS_STREAMING, g_width, g_height);
    if (!g_tex) { fprintf(stderr, "CreateTexture: %s\n", SDL_GetError()); exit(1); }

    if (g_mjpg) {
        g_rgb = malloc((size_t)g_width * g_height * 3);
        g_tj = tjInitDecompress();
    }
}

static void sdl_shutdown(void) {
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    if (g_tj) tjDestroy(g_tj);
    SDL_ShowCursor(SDL_ENABLE);
    audio_shutdown();
    overlay_shutdown();
    free(g_rgb);
    SDL_Quit();
}

static void handle_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_running = 0;
        else if (e.type == SDL_MOUSEMOTION) {
            g_last_mouse = SDL_GetTicks();
            if (g_cursor_hidden) { SDL_ShowCursor(SDL_ENABLE); g_cursor_hidden = 0; }
        }
        else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) g_running = 0;
            else if (e.key.keysym.sym == SDLK_r && (e.key.keysym.mod & KMOD_SHIFT)) {
                Uint32 now = SDL_GetTicks();
                if (g_reset_pending && now < g_reset_pending_until) {
                    g_reset_pending = 0;
                    reset_config();
                } else {
                    g_reset_pending = 1;
                    g_reset_pending_until = now + 3000;
                    show_toast("Press Shift+R again to reset settings");
                }
            }
            else if (e.key.keysym.sym == SDLK_d && (e.key.keysym.mod & KMOD_SHIFT)) {
                g_stats_overlay = !g_stats_overlay;
                if (g_stats_overlay) build_stats();
                else if (g_stats_tex) { SDL_DestroyTexture(g_stats_tex); g_stats_tex = NULL; }
                save_config();
                show_toast(g_stats_overlay ? "Stats overlay: on (saved)" : "Stats overlay: off (saved)");
            }
            else if (e.key.keysym.sym == SDLK_s && (e.key.keysym.mod & KMOD_SHIFT)) {
                g_glossary = !g_glossary;
                if (g_glossary) { build_glossary(); g_glossary_shown_at = SDL_GetTicks(); }
                else { if (g_glossary_tex) { SDL_DestroyTexture(g_glossary_tex); g_glossary_tex = NULL; } g_glossary_shown_at = 0; }
                save_config();
                show_toast(g_glossary ? "Glossary: on (saved)" : "Glossary: off (saved)");
            }
            else if (e.key.keysym.sym == SDLK_v && (e.key.keysym.mod & KMOD_SHIFT)) {
                g_vsync = !g_vsync;
                SDL_RenderSetVSync(g_ren, g_vsync);
                if (g_stats_overlay) build_stats();
                if (g_glossary) build_glossary();
                save_config();
                show_toast(g_vsync ? "Vsync: on (saved)" : "Vsync: off (saved)");
            }
            else if (e.key.keysym.sym == SDLK_a && (e.key.keysym.mod & KMOD_SHIFT)) {
                audio_toggle();
                if (g_stats_overlay) build_stats();
                if (g_glossary) build_glossary();
                save_config();
                show_toast(g_audio_on ? "Audio: on (saved)" : "Audio: off (saved)");
            }
        }
    }
}

static void render_one(struct v4l2_buffer *buf) {
    void *ptr = g_bufs[buf->index].start;
    if (g_mjpg) {
        int w = 0, h = 0, subsamp = 0, colorspace = 0;
        if (tjDecompressHeader3(g_tj, (unsigned char*)ptr, buf->bytesused,
                                &w, &h, &subsamp, &colorspace) == 0) {
            if (w == g_width && h == g_height) {
                tjDecompress2(g_tj, (unsigned char*)ptr, buf->bytesused,
                              g_rgb, g_width, 0, g_height, TJPF_RGB, TJFLAG_FASTDCT);
                SDL_UpdateTexture(g_tex, NULL, g_rgb, g_width * 3);
            }
        }
    } else {
        SDL_UpdateTexture(g_tex, NULL, ptr, g_width * 2);
    }
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    if (g_stats_overlay) {
        if (!g_stats_tex) build_stats();
        if (g_stats_tex) {
            SDL_Rect dst = { 8, 8, g_stats_w, g_stats_h };
            SDL_RenderCopy(g_ren, g_stats_tex, NULL, &dst);
        }
    }
    if (g_glossary) {
        if (!g_glossary_tex) build_glossary();
        if (g_glossary_tex) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(g_win, &ww, &wh);
            SDL_Rect dst = { ww - g_glossary_w - 8, 8, g_glossary_w, g_glossary_h };
            SDL_RenderCopy(g_ren, g_glossary_tex, NULL, &dst);
        }
    }
    if (g_toast_enabled && g_toast_until && SDL_GetTicks() < g_toast_until) {
        if (!g_toast_tex && g_toast[0]) build_toast();
        if (g_toast_tex) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(g_win, &ww, &wh);
            SDL_Rect dst = { (ww - g_toast_w)/2, wh - g_toast_h - 16, g_toast_w, g_toast_h };
            SDL_RenderCopy(g_ren, g_toast_tex, NULL, &dst);
        }
    } else if (g_toast_tex) {
        SDL_DestroyTexture(g_toast_tex); g_toast_tex = NULL;
        g_toast[0] = '\0';
    }
    SDL_RenderPresent(g_ren);
    frames_rendered++;
}


/* ---------- overlay (FreeType) ---------- */

static const char *find_font(void) {
    static const char *candidates[] = {
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaMono-Bold.ttf",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], R_OK) == 0) return candidates[i];
    }
    return NULL;
}

static void overlay_init(void) {
    if (FT_Init_FreeType(&g_ft) != 0) { g_ft = NULL; return; }
    const char *fp = find_font();
    if (!fp) { FT_Done_FreeType(g_ft); g_ft = NULL; return; }
    if (FT_New_Face(g_ft, fp, 0, &g_face) != 0) { FT_Done_FreeType(g_ft); g_ft = NULL; return; }
    FT_Set_Pixel_Sizes(g_face, 0, 16);
}

static void overlay_shutdown(void) {
    if (g_stats_tex) { SDL_DestroyTexture(g_stats_tex); g_stats_tex = NULL; }
    if (g_face) { FT_Done_Face(g_face); g_face = NULL; }
    if (g_ft) { FT_Done_FreeType(g_ft); g_ft = NULL; }
}

static void build_stats(void) {
    if (!g_face || !g_stats_overlay) return;
    if (g_stats_tex) { SDL_DestroyTexture(g_stats_tex); g_stats_tex = NULL; }

    char lines[8][128];
    int nlines = 0;
    snprintf(lines[nlines++], 128, "FPS: %.1f (%.1f ms)   DROP: %lu", g_stat_fps, g_stat_ms, g_stat_dropped);
    snprintf(lines[nlines++], 128, "%s  %dx%d", g_mjpg ? "MJPG" : "YUYV", g_width, g_height);
    snprintf(lines[nlines++], 128, "vsync: %s", g_vsync ? "on" : "off");

    FT_Load_Char(g_face, 'M', FT_LOAD_RENDER);
    int adv = (g_face->glyph->advance.x >> 6);
    if (adv < 4) adv = 8;
    int cell_h = 16 + 4;
    int pad = 6;

    int maxw = 0;
    for (int i = 0; i < nlines; i++) {
        int lw = (int)strlen(lines[i]) * adv;
        if (lw > maxw) maxw = lw;
    }
    int tw = maxw + pad * 2;
    int th = nlines * cell_h + pad * 2;
    if (tw < 10) tw = 10;
    if (th < 10) th = 10;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 170));

    for (int li = 0; li < nlines; li++) {
        int pen_x = pad;
        int baseline = pad + (li + 1) * cell_h - 4;
        for (char *q = lines[li]; *q; q++) {
            if (FT_Load_Char(g_face, (unsigned char)*q, FT_LOAD_RENDER) != 0) { pen_x += adv; continue; }
            FT_GlyphSlot g = g_face->glyph;
            int gx = pen_x + g->bitmap_left;
            int gy = baseline - g->bitmap_top;
            for (unsigned int row = 0; row < g->bitmap.rows; row++) {
                for (unsigned int col = 0; col < g->bitmap.width; col++) {
                    unsigned char v = g->bitmap.buffer[row * g->bitmap.pitch + col];
                    if (v == 0) continue;
                    int x = gx + col;
                    int y = gy + row;
                    if (x < 0 || y < 0 || x >= tw || y >= th) continue;
                    Uint32 *px = (Uint32 *)((Uint8 *)surf->pixels + y * surf->pitch + x * 4);
                    *px = SDL_MapRGBA(surf->format, 255, 255, 255, v);
                }
            }
            pen_x += g->advance.x >> 6;
        }
    }

    g_stats_tex = SDL_CreateTextureFromSurface(g_ren, surf);
    if (g_stats_tex) {
        SDL_SetTextureBlendMode(g_stats_tex, SDL_BLENDMODE_BLEND);
        g_stats_w = tw;
        g_stats_h = th;
    }
    SDL_FreeSurface(surf);
}


static void build_glossary(void) {
    if (!g_face || !g_glossary) return;
    if (g_glossary_tex) { SDL_DestroyTexture(g_glossary_tex); g_glossary_tex = NULL; }
    if (g_toast_tex) { SDL_DestroyTexture(g_toast_tex); g_toast_tex = NULL; }

    char items[8][128];
    int nlines = 0;
    snprintf(items[nlines++], 128, "Keybinds");
    snprintf(items[nlines++], 128, "Shift+S  glossary");
    snprintf(items[nlines++], 128, "Shift+D  stats overlay");
    snprintf(items[nlines++], 128, "Shift+V  toggle vsync (%s)", g_vsync ? "on" : "off");
    snprintf(items[nlines++], 128, "Shift+A  toggle audio (%s)", g_audio_on ? "on" : "off");
    snprintf(items[nlines++], 128, "Shift+R  reset settings");
    snprintf(items[nlines++], 128, "q / Esc  quit");
    snprintf(items[nlines++], 128, "mouse    show cursor");

    FT_Load_Char(g_face, 'M', FT_LOAD_RENDER);
    int adv = (g_face->glyph->advance.x >> 6);
    if (adv < 4) adv = 8;
    int cell_h = 16 + 4;
    int pad = 6;

    int maxw = 0;
    for (int i = 0; i < nlines; i++) {
        int lw = (int)strlen(items[i]) * adv;
        if (lw > maxw) maxw = lw;
    }
    int tw = maxw + pad * 2;
    int th = nlines * cell_h + pad * 2;
    if (tw < 10) tw = 10;
    if (th < 10) th = 10;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 170));

    for (int li = 0; li < nlines; li++) {
        int pen_x = pad;
        int baseline = pad + (li + 1) * cell_h - 4;
        for (const char *q = items[li]; *q; q++) {
            if (FT_Load_Char(g_face, (unsigned char)*q, FT_LOAD_RENDER) != 0) { pen_x += adv; continue; }
            FT_GlyphSlot g = g_face->glyph;
            int gx = pen_x + g->bitmap_left;
            int gy = baseline - g->bitmap_top;
            for (unsigned int row = 0; row < g->bitmap.rows; row++) {
                for (unsigned int col = 0; col < g->bitmap.width; col++) {
                    unsigned char v = g->bitmap.buffer[row * g->bitmap.pitch + col];
                    if (v == 0) continue;
                    int x = gx + col;
                    int y = gy + row;
                    if (x < 0 || y < 0 || x >= tw || y >= th) continue;
                    Uint32 *px = (Uint32 *)((Uint8 *)surf->pixels + y * surf->pitch + x * 4);
                    *px = SDL_MapRGBA(surf->format, 255, 255, 255, v);
                }
            }
            pen_x += g->advance.x >> 6;
        }
    }

    g_glossary_tex = SDL_CreateTextureFromSurface(g_ren, surf);
    if (g_glossary_tex) {
        SDL_SetTextureBlendMode(g_glossary_tex, SDL_BLENDMODE_BLEND);
        g_glossary_w = tw;
        g_glossary_h = th;
    }
    SDL_FreeSurface(surf);
}



/* ---------- audio (SDL) ---------- */

static void audio_capture_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    if (g_audio_stream) SDL_AudioStreamPut(g_audio_stream, stream, len);
}

static void audio_play_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    int got = 0;
    if (g_audio_stream) got = SDL_AudioStreamGet(g_audio_stream, stream, len);
    if (got < len) memset(stream + got, 0, len - got);
}

static void audio_init(void) {
    if (g_audio_cap || g_audio_play) return; /* already */
    SDL_AudioSpec want, have_cap, have_play;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_capture_cb;

    const char *cap_name = NULL;
    int n = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < n; i++) {
        const char *nm = SDL_GetAudioDeviceName(i, 1);
        if (nm && (strstr(nm, "Video") || strstr(nm, "USB3"))) { cap_name = nm; break; }
    }
    g_audio_cap = SDL_OpenAudioDevice(cap_name, 1, &want, &have_cap, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!g_audio_cap) {
        fprintf(stderr, "ps4view: audio capture open failed: %s\n", SDL_GetError());
        return;
    }

    SDL_zero(want);
    want.freq = have_cap.freq;
    want.format = have_cap.format;
    want.channels = have_cap.channels;
    want.samples = 1024;
    want.callback = audio_play_cb;
    g_audio_play = SDL_OpenAudioDevice(NULL, 0, &want, &have_play, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!g_audio_play) {
        fprintf(stderr, "ps4view: audio playback open failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(g_audio_cap); g_audio_cap = 0;
        return;
    }

    g_audio_stream = SDL_NewAudioStream(have_cap.format, have_cap.channels, have_cap.freq,
                                       have_play.format, have_play.channels, have_play.freq);
    if (!g_audio_stream) {
        fprintf(stderr, "ps4view: audio stream create failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(g_audio_cap); g_audio_cap = 0;
        SDL_CloseAudioDevice(g_audio_play); g_audio_play = 0;
        return;
    }

    SDL_PauseAudioDevice(g_audio_cap, 0);
    SDL_PauseAudioDevice(g_audio_play, 0);
    g_audio_on = 1;
    fprintf(stderr, "ps4view: audio enabled (%s)\n", cap_name ? cap_name : "default capture");
}

static void audio_shutdown(void) {
    if (g_audio_cap) { SDL_PauseAudioDevice(g_audio_cap, 1); SDL_CloseAudioDevice(g_audio_cap); g_audio_cap = 0; }
    if (g_audio_play) { SDL_PauseAudioDevice(g_audio_play, 1); SDL_CloseAudioDevice(g_audio_play); g_audio_play = 0; }
    if (g_audio_stream) { SDL_FreeAudioStream(g_audio_stream); g_audio_stream = NULL; }
    g_audio_on = 0;
}

static void audio_toggle(void) {
    if (!g_audio_cap || !g_audio_play) {
        audio_init();
        if (!g_audio_cap || !g_audio_play) {
            show_toast("Audio: unavailable");
            return;
        }
    } else {
        g_audio_on = !g_audio_on;
        SDL_PauseAudioDevice(g_audio_cap, !g_audio_on);
        SDL_PauseAudioDevice(g_audio_play, !g_audio_on);
    }
    fprintf(stderr, "ps4view: audio %s\n", g_audio_on ? "on" : "off");
}


static void build_toast(void) {
    if (!g_face || g_toast[0] == '\0') return;
    if (g_toast_tex) { SDL_DestroyTexture(g_toast_tex); g_toast_tex = NULL; }
    FT_Load_Char(g_face, 'M', FT_LOAD_RENDER);
    int adv = (g_face->glyph->advance.x >> 6);
    if (adv < 4) adv = 8;
    int cell_h = 16 + 4;
    int pad = 6;
    int tw = (int)strlen(g_toast) * adv + pad * 2;
    int th = cell_h + pad * 2;
    if (tw < 10) tw = 10;
    if (th < 10) th = 10;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 180));
    int pen_x = pad;
    int baseline = pad + cell_h - 4;
    for (char *q = g_toast; *q; q++) {
        if (FT_Load_Char(g_face, (unsigned char)*q, FT_LOAD_RENDER) != 0) { pen_x += adv; continue; }
        FT_GlyphSlot g = g_face->glyph;
        int gx = pen_x + g->bitmap_left;
        int gy = baseline - g->bitmap_top;
        for (unsigned int row = 0; row < g->bitmap.rows; row++) {
            for (unsigned int col = 0; col < g->bitmap.width; col++) {
                unsigned char v = g->bitmap.buffer[row * g->bitmap.pitch + col];
                if (v == 0) continue;
                int x = gx + col;
                int y = gy + row;
                if (x < 0 || y < 0 || x >= tw || y >= th) continue;
                Uint32 *px = (Uint32 *)((Uint8 *)surf->pixels + y * surf->pitch + x * 4);
                *px = SDL_MapRGBA(surf->format, 255, 255, 255, v);
            }
        }
        pen_x += g->advance.x >> 6;
    }
    g_toast_tex = SDL_CreateTextureFromSurface(g_ren, surf);
    if (g_toast_tex) {
        SDL_SetTextureBlendMode(g_toast_tex, SDL_BLENDMODE_BLEND);
        g_toast_w = tw;
        g_toast_h = th;
    }
    SDL_FreeSurface(surf);
}

static void show_toast(const char *msg) {
    if (!g_toast_enabled) return;
    snprintf(g_toast, sizeof(g_toast), "%s", msg);
    if (g_toast_tex) { SDL_DestroyTexture(g_toast_tex); g_toast_tex = NULL; }
    build_toast();
    g_toast_until = SDL_GetTicks() + g_toast_ms;
}


/* ---------- config persistence ---------- */

static void config_path(char *buf, size_t sz) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, sz, "%s/.config/ps4view/config", home);
}

static void load_config(void) {
    if (!g_config_enabled) return;
    char path[512];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0';
        if (!strcmp(key, "width")) g_width = atoi(val);
        else if (!strcmp(key, "height")) g_height = atoi(val);
        else if (!strcmp(key, "fps")) g_fps = atoi(val);
        else if (!strcmp(key, "format")) g_mjpg = (!strcmp(val, "mjpg") || !strcmp(val, "mjpeg"));
        else if (!strcmp(key, "device")) snprintf(g_device_path, sizeof(g_device_path), "%s", val);
        else if (!strcmp(key, "vsync")) g_vsync = atoi(val);
        else if (!strcmp(key, "audio")) g_audio_flag = atoi(val);
        else if (!strcmp(key, "cursor_hide")) g_cursor_hide = atoi(val);
        else if (!strcmp(key, "cursor_hide_ms")) g_cursor_hide_ms = (Uint32)atoi(val);
        else if (!strcmp(key, "toast")) g_toast_enabled = atoi(val);
        else if (!strcmp(key, "toast_ms")) g_toast_ms = (Uint32)atoi(val);
        else if (!strcmp(key, "glossary_flash")) g_glossary_flash = atoi(val);
        else if (!strcmp(key, "stats_overlay")) g_stats_overlay = atoi(val);
        else if (!strcmp(key, "glossary")) g_glossary = atoi(val);
    }
    fclose(f);
}

static void save_config(void) {
    if (!g_config_enabled) return;
    char path[512];
    config_path(path, sizeof(path));
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "width=%d\n", g_width);
    fprintf(f, "height=%d\n", g_height);
    fprintf(f, "fps=%d\n", g_fps);
    fprintf(f, "format=%s\n", g_mjpg ? "mjpg" : "yuyv");
    fprintf(f, "device=%s\n", g_device_path);
    fprintf(f, "vsync=%d\n", g_vsync);
    fprintf(f, "audio=%d\n", g_audio_on ? 1 : g_audio_flag);
    fprintf(f, "cursor_hide=%d\n", g_cursor_hide);
    fprintf(f, "cursor_hide_ms=%u\n", g_cursor_hide_ms);
    fprintf(f, "toast=%d\n", g_toast_enabled);
    fprintf(f, "toast_ms=%u\n", g_toast_ms);
    fprintf(f, "glossary_flash=%d\n", g_glossary_flash);
    fprintf(f, "stats_overlay=%d\n", g_stats_overlay);
    fprintf(f, "glossary=%d\n", g_glossary);
    fclose(f);
}

static void reset_config(void) {
    g_width = DEFAULT_W;
    g_height = DEFAULT_H;
    g_fps = DEFAULT_FPS;
    g_mjpg = 0;
    g_vsync = 1;
    g_audio_flag = 0;
    if (g_audio_cap || g_audio_play) audio_shutdown();
    g_cursor_hide = 1;
    g_cursor_hide_ms = 3000;
    g_toast_enabled = 1;
    g_toast_ms = 3000;
    g_glossary_flash = 0;
    g_glossary = 1;
    g_stats_overlay = 0;
    g_device_path[0] = '\0';
    save_config();
    show_toast("Settings reset to defaults");
}

static void loop(void) {
    struct sched_param sp;
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    sched_setscheduler(0, SCHED_FIFO, &sp);

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);

    while (g_running) {
        handle_events();
        if (g_reset_pending && SDL_GetTicks() > g_reset_pending_until) { g_reset_pending = 0; }
        if (g_cursor_hide && !g_cursor_hidden && (SDL_GetTicks() - g_last_mouse) > g_cursor_hide_ms) {
            SDL_ShowCursor(SDL_DISABLE);
            g_cursor_hidden = 1;
        }
        if (g_glossary && g_glossary_timeout_ms > 0 && g_glossary_shown_at && (SDL_GetTicks() - g_glossary_shown_at) > g_glossary_timeout_ms) {
            g_glossary = 0;
            if (g_glossary_tex) { SDL_DestroyTexture(g_glossary_tex); g_glossary_tex = NULL; }
    if (g_toast_tex) { SDL_DestroyTexture(g_toast_tex); g_toast_tex = NULL; }
            g_glossary_shown_at = 0;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_fd, &fds);
        struct timeval tv = {0, 1000};
        if (select(g_fd + 1, &fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            die("select");
        }
        if (!FD_ISSET(g_fd, &fds)) continue;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            die("VIDIOC_DQBUF");
        }

        while (1) {
            fd_set fds2;
            FD_ZERO(&fds2);
            FD_SET(g_fd, &fds2);
            struct timeval tv2 = {0, 0};
            if (select(g_fd + 1, &fds2, NULL, NULL, &tv2) <= 0) break;
            if (!FD_ISSET(g_fd, &fds2)) break;
            struct v4l2_buffer tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            tmp.memory = V4L2_MEMORY_MMAP;
            if (xioctl(g_fd, VIDIOC_DQBUF, &tmp) < 0) break;
            xioctl(g_fd, VIDIOC_QBUF, &buf);
            buf = tmp;
            frames_dropped++;
        }

        render_one(&buf);

        if (xioctl(g_fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF (requeue)");

        if (g_stats || g_stats_overlay) {
            gettimeofday(&t1, NULL);
            double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
            if (elapsed >= 1.0) {
                g_stat_fps = frames_rendered / elapsed;
                g_stat_ms = (frames_rendered > 0) ? (elapsed * 1000.0 / frames_rendered) : 0.0;
                g_stat_dropped = frames_dropped;
                if (g_stats) {
                    fprintf(stderr, "\rps4view: %.1f fps (%.1f ms)  rendered=%lu  dropped=%lu      ",
                            frames_rendered / elapsed, g_stat_ms, frames_rendered, frames_dropped);
                    fflush(stderr);
                }
                frames_rendered = 0;
                frames_dropped = 0;
                t0 = t1;
                if (g_stats_overlay) build_stats();
            }
        }
    }
}

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --device PATH     V4L2 device (default: auto-scan)\n"
        "  --list            list capture devices and exit\n"
        "  --res WxH         resolution (default 1920x1080)\n"
        "  --fps N           framerate (default 60)\n"
        "  --format FMT      yuyv (default) or mjpg\n"
        "  --vsync           enable vsync (default off)\n"
        "  --windowed        windowed mode (default fullscreen)\n"
        "  --stats           print fps/drop stats\n"
        "  --overlay         show on-screen stats (toggle: Shift+D)\n"
        "  --glossary        show keybind glossary (toggle: Shift+S)\n"
        "  --glossary-timeout-ms N  auto-hide glossary after N ms (default 5000)\n"
        "  --no-glossary-flash      don't flash glossary on startup\n"
        "  --toast-ms N             toast duration ms (default 3000)\n"
        "  --no-toast              disable toast notifications\n"
        "  --no-audio              disable audio capture/playback\n"
        "  --no-config             disable loading/saving settings\n"
        "  --audio                 enable audio capture/playback at startup\n"
        "  Shift+V toggles vsync at runtime\n"
        "  --cursor-hide-ms N  hide cursor after N ms idle (default 5000)\n"
        "  --no-cursor-hide    never hide the cursor\n"
        "  --help            this help\n"
        "  --version         print version\n", p);
}

static void on_signal(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    int no_config = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--no-config")) no_config = 1;
    if (!no_config) load_config();
    else g_config_enabled = 0;
    char *device = NULL;
    int do_list = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) { device = argv[++i]; snprintf(g_device_path, sizeof(g_device_path), "%s", device); }
        else if (!strcmp(argv[i], "--list")) { do_list = 1; }
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &g_width, &g_height);
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            g_fps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--format") && i + 1 < argc) {
            i++;
            g_mjpg = (!strcmp(argv[i], "mjpg") || !strcmp(argv[i], "mjpeg"));
        } else if (!strcmp(argv[i], "--vsync")) { g_vsync = 1; }
        else if (!strcmp(argv[i], "--windowed")) { g_fullscreen = 0; }
        else if (!strcmp(argv[i], "--stats")) { g_stats = 1; }
        else if (!strcmp(argv[i], "--overlay")) { g_stats_overlay = 1; }
        else if (!strcmp(argv[i], "--glossary")) { g_glossary = 1; }
        else if (!strcmp(argv[i], "--glossary-timeout-ms") && i + 1 < argc) { g_glossary_timeout_ms = (Uint32)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--no-glossary-flash")) { g_glossary_flash = 0; }
        else if (!strcmp(argv[i], "--toast-ms") && i + 1 < argc) { g_toast_ms = (Uint32)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--no-toast")) { g_toast_enabled = 0; }
        else if (!strcmp(argv[i], "--no-audio")) { g_no_audio = 1; }
        else if (!strcmp(argv[i], "--audio")) { g_audio_flag = 1; }
        else if (!strcmp(argv[i], "--no-config")) { g_config_enabled = 0; }
        else if (!strcmp(argv[i], "--cursor-hide-ms") && i + 1 < argc) { g_cursor_hide_ms = (Uint32)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--no-cursor-hide")) { g_cursor_hide = 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (!strcmp(argv[i], "--version")) { printf("ps4view 1.0.0\n"); return 0; }
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    if (do_list) { list_devices(); return 0; }

    if (!device && g_device_path[0]) device = strdup(g_device_path);
    if (!device) {
        device = autoscan_device();
        if (!device) { fprintf(stderr, "ps4view: no capture device found\n"); return 1; }
        fprintf(stderr, "ps4view: autoscan selected %s\n", device);
        snprintf(g_device_path, sizeof(g_device_path), "%s", device);
    }

    if (!g_stats && !g_stats_overlay) fprintf(stderr, "ps4view: press Shift+S for on-screen stats\n");
    v4l2_open(device);
    sdl_init();
    if (g_audio_flag && !g_no_audio) audio_init();
    g_last_mouse = SDL_GetTicks();
    if (g_stats_overlay) build_stats();
    if (g_glossary) { build_glossary(); g_glossary_shown_at = SDL_GetTicks(); }
    else if (g_glossary_flash) { g_glossary = 1; build_glossary(); g_glossary_shown_at = SDL_GetTicks(); }
    signal(SIGHUP, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    loop();
    sdl_shutdown();
    v4l2_close();
    save_config();
    return 0;
}
