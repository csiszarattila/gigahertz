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
    Display *display;
    Window window;
    XftDraw *xft_draw;
    XftColor *color;
    XftFont *font;
} StatusBar;

StatusBar* StatusBar_Create(Display *display, Window window)
{
	XftDraw *draw;
    XftColor *color;
    XftFont *font;
	Colormap colorMap;
	Visual *visual;
    const char *fontName = "Liberation Mono-16:style=Bold";
    StatusBar *bar;

    int screen = XDefaultScreen(display);

    visual = XDefaultVisual(display, screen);
    colorMap = XDefaultColormap(display, screen);
	
    draw = XftDrawCreate(display, window, visual, colorMap);

    XRenderColor render_color = { .red=0x0000, .green=0x0000, .blue=0x0000, .alpha=0xffff };
    color = malloc(sizeof(XftColor));
    XftColorAllocValue(display, visual, colorMap, &render_color, color);

    font = XftFontOpenName(display, screen, fontName);

    bar = malloc(sizeof(StatusBar));
    
    bar->display = display;
    bar->window = window;
    bar->xft_draw = draw;
    bar->color = color;
    bar->font = font;

    return bar;
}

void StatusbarUIDrawCurrenttime(StatusBar *bar)
{
    char out_buffer[50];
    char with_format[50];
    time_t current_time;
    struct tm* timeinfo;

    strcpy(with_format, "%H:%M:%S");

    time(&current_time);
    timeinfo = localtime(&current_time);

    strftime(out_buffer, sizeof(out_buffer), with_format, timeinfo);

    XClearWindow(bar->display, bar->window);

    int y = bar->font->ascent;

    XftDrawString8(bar->xft_draw, bar->color, bar->font, 100, 50, (FcChar8*) out_buffer, strlen(out_buffer));
}

void StatusbarUIDraw(StatusBar *bar)
{
    StatusbarUIDrawCurrenttime(bar);
}

int main() {

    int screenWidth = 3840;
    int screenHeight = 2160;

    setlocale(LC_ALL, "");

    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen_num = DefaultScreen(display);
    Window root = RootWindow(display, screen_num);

    // Create window
    Window win = XCreateSimpleWindow(display, root, 0, 0, screenWidth, screenHeight, 2,
                                       BlackPixel(display, screen_num),
                                       WhitePixel(display, screen_num));
    
    // Select input events
    XSelectInput(display, win, ExposureMask | KeyPressMask | StructureNotifyMask);

    // Cursor
    Cursor cursor = XCreateFontCursor(display, XC_cross);
    XStoreName(display, win, "X11 test window");
    // Map (show) window
    XMapRaised(display, win);
    XSetInputFocus(display, win, RevertToParent, CurrentTime);
    XDefineCursor(display, win, cursor);
    XFlush(display);

    StatusBar *statusBar;
    
    statusBar = StatusBar_Create(display, win);

    // Non-blocking UI refresh
    int ui_epoll_fd;
    struct epoll_event ui_epoll_ev;
    struct epoll_event ui_timer_events[1];
    int ui_timer_fd;

    struct itimerspec timer = {
        .it_interval = {1, 0},
        .it_value = {1, 0}
    };

    ui_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    timerfd_settime(ui_timer_fd, 0, &timer, NULL);
    
    ui_epoll_fd = epoll_create1(0);
    ui_epoll_ev.events = EPOLLIN;
    ui_epoll_ev.data.fd = ui_timer_fd;

    epoll_ctl(ui_epoll_fd, EPOLL_CTL_ADD, ui_timer_fd, &ui_epoll_ev);

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
        
        		    if (key == XK_c) {
        			    Window win2 = XCreateSimpleWindow(
                                display, 
                                root, 
                                200, 200, 
                                400, 200, 
                                2,
        					    BlackPixel(display, screen_num),
        					    WhitePixel(display, screen_num));
        			    XMapRaised(display, win2);
        			    XFlush(display);
        		    }
                    break;
        
        	    case Expose:
        		    StatusbarUIDraw(statusBar);
                    XFlush(display);
                    break;
        
        	    case MapNotify:
        	       XClearWindow(display, win);
           	       XFlush(display);
        	       break;	   
        	}
        }
        
        int nevents = epoll_wait(ui_epoll_fd, ui_timer_events, 1, -1);
        if (nevents && ui_timer_events[0].data.fd == ui_timer_fd) {
            uint64_t expirations;
            read(ui_timer_fd, &expirations, sizeof(expirations));
            StatusbarUIDraw(statusBar);
            XFlush(display);
        }
       

    }

    // Cleanup
    XDestroyWindow(display, win);
    XCloseDisplay(display);
    close(ui_timer_fd);
    close(ui_epoll_fd);
    return 0;
}
