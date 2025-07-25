#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

typedef struct {
    int w;
    int h;
    Display *dsp;
    Window root;
    int screen;
} Gigahertz;

Gigahertz* gh_initialize(Display *dsp, Window root, int screen, int w, int h)
{
    Gigahertz *gh = malloc(sizeof(Gigahertz));

    gh->w = w;
    gh->h = h;
    gh->dsp = dsp;
    gh->root = root;
    gh->screen = screen;

    return gh;
}

static Display *display;
static Gigahertz *ghwm;

typedef struct {
    int h;
    Window window;
    XftDraw *xft_draw;
    XftColor *color;
    XftFont *font;
} Statusbar;


static Statusbar *statusbar;

Statusbar* statusbar_initialize()
{
    int height = 40;

    Window window = XCreateSimpleWindow(display, ghwm->root, 0, 0, ghwm->w, height, 2,
                                       BlackPixel(display, ghwm->screen),
                                       WhitePixel(display, ghwm->screen));
	XftDraw *draw;
    XftColor *color;
    XftFont *font;
	Colormap colorMap;
	Visual *visual;
    const char *fontName = "Liberation Mono-16:style=Bold";
    Statusbar *bar;

    visual = XDefaultVisual(ghwm->dsp, ghwm->screen);
    colorMap = XDefaultColormap(ghwm->dsp, ghwm->screen);
	
    draw = XftDrawCreate(ghwm->dsp, window, visual, colorMap);

    XRenderColor render_color = { .red=0x0000, .green=0x0000, .blue=0x0000, .alpha=0xffff };
    color = malloc(sizeof(XftColor));
    XftColorAllocValue(ghwm->dsp, visual, colorMap, &render_color, color);

    font = XftFontOpenName(ghwm->dsp, ghwm->screen, fontName);

    bar = malloc(sizeof(Statusbar));
    
    bar->h = height;
    bar->window = window;
    bar->xft_draw = draw;
    bar->color = color;
    bar->font = font;

    return bar;
}

void statusbar_draw_currenttime()
{
    char out_buffer[50];
    char with_format[50];
    time_t current_time;
    struct tm* timeinfo;

    strcpy(with_format, "%H:%M:%S");

    time(&current_time);
    timeinfo = localtime(&current_time);

    strftime(out_buffer, sizeof(out_buffer), with_format, timeinfo);

    XClearWindow(ghwm->dsp, statusbar->window);

    int text_baseline = (statusbar->h + statusbar->font->ascent - statusbar->font->descent) / 2;

    XftDrawString8(statusbar->xft_draw, statusbar->color, statusbar->font, 10, text_baseline, (FcChar8*) out_buffer, strlen(out_buffer));
}

void statusbar_draw()
{
    statusbar_draw_currenttime();
}

void setup()
{
    int screen = DefaultScreen(display);
    int dpw = XDisplayWidth(display, screen);
    int dph = XDisplayHeight(display, screen);

    Window root = RootWindow(display, screen);
    
    ghwm = gh_initialize(display, root, screen, dpw, dph);

    Cursor cursor = XCreateFontCursor(display, XC_cross);
    XDefineCursor(display, root, cursor);
    
    XSelectInput(display, root, ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask);
}

void setup_statusbar()
{
    statusbar = statusbar_initialize();

    XMapWindow(display, statusbar->window);
}

static int ui_epoll_fd;
static int ui_tick_fd;

void ui_tick_setup()
{
    // Non-blocking UI refresh
    struct epoll_event ui_epoll_ev;

    struct itimerspec timer = {
        .it_interval = {1, 0},
        .it_value = {1, 0}
    };

    ui_tick_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    timerfd_settime(ui_tick_fd, 0, &timer, NULL);
    
    ui_epoll_fd = epoll_create1(0);
    ui_epoll_ev.events = EPOLLIN;
    ui_epoll_ev.data.fd = ui_tick_fd;

    epoll_ctl(ui_epoll_fd, EPOLL_CTL_ADD, ui_tick_fd, &ui_epoll_ev);
}

void ui_tick_stop()
{
    close(ui_epoll_fd);
    close(ui_tick_fd);
}

int ui_ticked()
{
    int nevents;
    struct epoll_event ui_timer_events[1];

    nevents = epoll_wait(ui_epoll_fd, ui_timer_events, 1, -1);
    if (nevents && ui_timer_events[0].data.fd == ui_tick_fd) {
        int64_t expirations;
        read(ui_tick_fd, &expirations, sizeof(expirations));

        return 1;
    }

    return 0;
}

int main() {

    setlocale(LC_ALL, "");

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    setup();

    setup_statusbar();

    ui_tick_setup();

    // Event loop
    XEvent ev;

    int run = 1;

    while (run) {

        while(XPending(display)) {
            XNextEvent(display, &ev);
    	
        	switch (ev.type) {
                case KeyPress:
        		    KeySym key = XLookupKeysym(&ev.xkey, 0);
        		    
                    if (key == XK_Escape) {
        			    run = 0;
        		    }
        
        		    if (key == XK_1) {
                        if (fork() == 0) {
                            execlp("alacritty", "alacritty", NULL);
                        }
        		    }
                    break;
        
        	    case Expose:
        		    statusbar_draw();
                    XFlush(display);
                    break;

                case MapRequest:
                    XMapWindow(display, ev.xmaprequest.window);
                    break;

                case MapNotify:
                    Window target = ev.xmap.window;
                    if (target != statusbar->window) {
                        XMoveResizeWindow(ghwm->dsp, target, 0, statusbar->h, ghwm->w, ghwm->h);
                        XSetInputFocus(ghwm->dsp, target, RevertToParent, CurrentTime);
                    }
                    XFlush(ghwm->dsp);
        	        break;
        	}
        }
    
        if (ui_ticked()) {
            statusbar_draw();

            XFlush(display);
        } 
    }

    // Cleanup
    XCloseDisplay(display);
    free(statusbar);
    free(ghwm);
    ui_tick_stop();

    return 0;
}
