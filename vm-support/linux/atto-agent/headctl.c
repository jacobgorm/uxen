/*
 * Copyright 2019, Bromium, Inc.
 * Author: Tomasz Wroblewski <tomasz.wroblewski@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#include <inttypes.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uxen-v4vlib.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#include "atto-agent.h"

/* dr tracking */
#include "../../../common/include/uxendisp-common.h"

#ifndef DEFAULT_USER_NAME
#define DEFAULT_USER_NAME "user"
#endif

#ifndef DEFAULT_VT
#define DEFAULT_VT "1"
#endif

#define DEFAULT_XORG_PARAMS "-wr -pn"

#define X_CONNECT_TIMEOUT_MS 5000

// 10ms
#define DR_PERIOD_NS 10000000ULL

/* fb ioctl */
#define UXEN_FB_IO_HEAD_IDENTIFY 0x5000
#define UXEN_FB_IO_HEAD_INIT 0x5001

#define HEADCTL_ERROR(fmt, ...) { fprintf(stderr, fmt, ## __VA_ARGS__); fflush(stderr); }

typedef struct head_dr {
    Display *display;
    int dr;
    int dr_pending;
    struct drc dr_rect;
    int64_t dr_ts;
} head_dr_t;

static head_dr_t head_dr[HEADMAX];

static void headctl_usage(void)
{
    fprintf(stderr, "usage: atto-agent headctl [list|create <head>|initx <head> [xorg params...]|device <head>|activate <head>]\n");
    exit(1);
}

static head_id_t str_to_head_id(const char *s)
{
    return (head_id_t) atoi(s);
}

static int64_t timestamp_ns(void)
{
    struct timespec ts = { 0 };

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (int64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void update_heads(void)
{
    int i, fd, ret;
    volatile head_t *heads;

    lock_shared_state();
    heads = &shared_state->heads[0];

    /* scan only new heads */
    for (i = shared_state->heads_num; i < HEADMAX; i++) {
        head_t h;
        memset(&h, 0, sizeof(h));
        snprintf(h.dev, sizeof(h.dev), "/dev/fb%d", i);
        fd = open(h.dev, O_RDONLY|O_CLOEXEC);
        if (fd < 0)
            break;
        ret = ioctl(fd, UXEN_FB_IO_HEAD_IDENTIFY, &h.id);
        if (ret)
            err(1, "head identify failed: %d", ret);
        close(fd);

        h.index = i;
        heads[i] = h;
    }
    shared_state->heads_num = i;
    sync_shared_state();
    unlock_shared_state();
}

static head_t *get_head_by_id(head_id_t id)
{
    int i;

    for (i = 0; i < shared_state->heads_num; i++) {
        if (shared_state->heads[i].id == id)
            return (head_t *) &shared_state->heads[i];
    }

    return NULL;
}

static int x_toggleinput(head_id_t head, int enable)
{
    char cmd[256];
    int err;

    snprintf(cmd, sizeof(cmd),
        "DISPLAY=:%d.0 xinput %s 6", head, enable ? "enable" : "disable");
    err = system(cmd);
    if (err)
        goto out;
    snprintf(cmd, sizeof(cmd),
        "DISPLAY=:%d.0 xinput %s 7", head, enable ? "enable" : "disable");
    err = system(cmd);

out:
    return err;
}

/* sync keyboard layout for given head with currently active global one */
static int x_sync_kb_layout(head_id_t head)
{
    char cmd[256];
    int err;

    kbd_layout_t layout = get_active_kbd_layout();
    if (layout == KBD_LAYOUT_INVALID)
        return -EINVAL;
    err = get_x_update_kbd_layout_command(layout, cmd, sizeof(cmd));
    if (err)
        return err;

    /* note: input needs to be enabled on that X for layout change to work */

    return headctl_system_cmd(head, cmd);
}

static void x_run_server(head_id_t head, char *extra_params)
{
    char cmd[512];
    char vtopt[64] = { 0 };
    struct head *h;

    h = get_head_by_id(head);
    if (!h) {
        HEADCTL_ERROR("head not found\n");
        exit(1);
    }

    if (head != 0)
        /* head > 0 need sharevt */
        strncpy(vtopt, "-novtswitch -sharevts", sizeof(vtopt));
    else
        strncpy(vtopt, "-novtswitch", sizeof(vtopt));

    /* into /dev/null it goes because it spams setxbmap compiler warnings */
    snprintf(cmd, sizeof(cmd),
        "ATTO_HEAD_ID=%d FRAMEBUFFER=%s xinit /etc/X11/Xsession -- "
        "/usr/bin/Xorg :%d %s vt%s %s -logfile /var/log/%s/Xorg.%d.log &> /dev/null",
        head, h->dev, head, extra_params, DEFAULT_VT, vtopt, DEFAULT_USER_NAME, head);

    printf("starting x server: %s\n", cmd);
    fflush(stdout);

    execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);

    /* if we're here it means execl failed */
    HEADCTL_ERROR("error starting X: %d\n", errno);
    exit(1);
}

/* execute command on display corresponding to given head */
int headctl_system_cmd(head_id_t head, const char *cmd)
{
    char buf[256];

    snprintf(buf, sizeof(buf), "DISPLAY=:%d.0 %s", (int) head, cmd);

    int ret = system(buf);

    return ret;
}

int headctl_activate(head_id_t new)
{
    head_id_t old;
    struct head *h;
    int err = 0;

    if (!(new >= 0 && new < HEADMAX))
        return -EINVAL;

    err = lock_shared_state();
    if (err)
        return err;

    /* mark pending active head even if it doesn't exist yet */
    shared_state->active_head_request = new;
    sync_shared_state();

    h = get_head_by_id(new);
    if (!h) {
        err = -EINVAL;
        goto out;
    }

    old = shared_state->active_head;
    if (old == new)
        goto out; /* nothing to do  */

    /* toggle input old -> new */
    err = x_toggleinput(old, 0);
    if (err)
        goto out;

    err = x_toggleinput(new, 1);
    if (err)
        goto out;

    /* sync kb layout */
    x_sync_kb_layout(new);

    /* update state */
    shared_state->active_head = new;
    sync_shared_state();

out:
    unlock_shared_state();

    if (err)
        HEADCTL_ERROR("activate head %d failed: %d\n", new, err);

    return err;
}

void headctl_for_each_head(void (*f)(head_id_t head, void *opaque), void *opaque)
{
    int i;

    for (i = 0; i < shared_state->heads_num; i++) {
        head_id_t id = shared_state->heads[i].id;
        f(id, opaque);
    }
}

static Display *connectx(head_id_t head)
{
    char display_str[32];

    sprintf(display_str, ":%d", head);

    Display *d = XOpenDisplay(display_str);

    return d;
}

static Display *connectx_timeout(head_id_t head, int timeout_ms)
{
    int iters = (timeout_ms + 99) / 100;
    Display *d;

    for (;;) {
        d = connectx(head);
        if (d)
            return d;
        iters--;
        if (iters <= 0)
            break;
        // wait 100 ms
        usleep(100 * 1000);
    }

    return NULL;
}

static void cmd_headctl_list(void)
{
    int i;

    head_id_t active = shared_state->active_head;
    char actstr[32];
    strcpy(actstr, "            ");
    printf("%10s | %10s | %10s\n", "HEADID", "DEVICE", "ACTIVE");
    printf("-----------------------------------\n");
    for (i = 0 ; i < shared_state->heads_num; i++) {
        int id = shared_state->heads[i].id;
        actstr[6] = (active == id) ? '*' : ' ';
        printf("%10d | %10s | %10s\n", id, shared_state->heads[i].dev, actstr);
    }
}

static void cmd_headctl_create(char *headstr)
{
    head_id_t head = str_to_head_id(headstr);
    int fd, ret, iters;

    if (head >= 0 && head < HEADMAX) {
        fd = open("/dev/fb0", O_RDONLY|O_CLOEXEC);
        if (fd < 0)
            err(1, "fb open");
        ret = ioctl(fd, UXEN_FB_IO_HEAD_INIT, &head);
        if (ret)
            err(1, "head %d init failed: %d", (int) head, ret);
        close(fd);

        // wait until new device node appears
        iters = 100;
        for (;;) {
            update_heads();
            struct head *hnew = get_head_by_id(head);
            if (hnew)
                break;
            iters--;
            if (iters == 0)
                errx(1, "head %d init failed: couldn't open device", (int) head);
            usleep(20 * 1000);
        }
    } else
        errx(1, "head id out of range\n");
}

static void stringify_params(char *buf, int bufsz, int count, char **params)
{
    int i = 0;

    while (count) {
        char *par  = params[i];
        int parlen = strlen(par);

        if (parlen >= bufsz - 1)
            break;

        sprintf(buf, "%s ", par);

        buf   += parlen + 1;
        bufsz -= parlen + 1;

        count--;
        i++;
    }
}

static void cmd_headctl_initx(char *headstr, int num_x_params, char **x_params, int wait)
{
    head_id_t head = str_to_head_id(headstr);
    char x_params_str[256] = { 0 };
    Display *d;
    int err;

    if (num_x_params > 0)
      stringify_params(x_params_str, sizeof(x_params_str), num_x_params, x_params);
    else
      strncpy(x_params_str, DEFAULT_XORG_PARAMS, sizeof(x_params_str));

    cmd_headctl_create(headstr);

    d = connectx(head);
    if (d) {
        XCloseDisplay(d);
        printf("X already running on head %s\n", headstr);
        exit(0);
    }

    pid_t child = fork();
    if (child == 0) {
        if (!wait) {
            child = fork();
            if (child == 0)
                exit(0);
        }
        x_run_server(head, x_params_str);
    } else {
        /* in parent, wait for X to init */
        d = connectx_timeout(head, X_CONNECT_TIMEOUT_MS);
        if (d) {
            /* setup default kbd layout */
            err = x_sync_kb_layout(head);
            if (err)
                HEADCTL_ERROR("FAILED to setup x kb layout: %d\n", err);
            /* input off by default if not active already */
            err = lock_shared_state();
            if (err)
                HEADCTL_ERROR("FAILED to lock shared state\n");
            if (shared_state->active_head != head) {
                err = x_toggleinput(head, 0);
                if (err)
                    HEADCTL_ERROR("FAILED to toggle xinput: %d\n", err);
            }
            unlock_shared_state();
            XCloseDisplay(d);

            if (wait) {
                int wstatus;

                waitpid(child, &wstatus, 0);
            }
        } else {
            HEADCTL_ERROR("FAILED to connect to X server head %d\n", head);
            exit(1);
        }
    }
}

static void cmd_headctl_activate(char *headstr)
{
    head_id_t head = str_to_head_id(headstr);
    int err;

    err = headctl_activate(head);
    if (err) {
        HEADCTL_ERROR("FAILED to activate head %d: %d\n",
            head, err);
        exit(err);
    }
}

static void cmd_headctl_device(char *headstr)
{
    head_id_t head = str_to_head_id(headstr);
    struct head *h;
    
    h = get_head_by_id(head);

    if (h) {
        printf("%s\n", h->dev);
    } else {
        HEADCTL_ERROR("invalid head id\n");
        exit(1);
    }
}

static int drc_empty(struct drc *r)
{
    return (r->x0 > r->x1 || r->y0 > r->y1);
}

static void drc_reset(struct drc *r)
{
    r->x0 = 0xffff;
    r->y0 = 0xffff;
    r->x1 = 0;
    r->y1 = 0;
}

static int process_damage_ev(Display *d, Damage damage, XDamageNotifyEvent *dev, struct drc *r)
{
    if (dev->damage != damage)
        return -1; // not ours

    XserverRegion region = XFixesCreateRegion(d, NULL, 0);
    XDamageSubtract(d, damage, None, region);
    int count = 0;
    XRectangle bounds;
    XRectangle *rects = XFixesFetchRegionAndBounds(d, region, &count, &bounds);
    if (rects) {
        for (int i = 0; i < count; i++) {
            int rx0 = rects[i].x;
            int ry0 = rects[i].y;

            int rx1 = rx0 + rects[i].width  - 1;
            int ry1 = ry0 + rects[i].height - 1;

            if (rx0 < r->x0) r->x0 = rx0;
            if (ry0 < r->y0) r->y0 = ry0;
            if (rx1 > r->x1) r->x1 = rx1;
            if (ry1 > r->y1) r->y1 = ry1;
        }

        XFree(rects);
    }
    XFixesDestroyRegion(d, region);

    return 0;
}

static int send_dr(struct head *head, struct drc *r)
{
    head_dr_t *hdr = &head_dr[head->index];
    /* send dr to backend */
    struct dirty_rect_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.left = r->x0;
    msg.top = r->y0;
    msg.right = r->x1 + 1;
    msg.bottom = r->y1 + 1;
    msg.rect_id = __atomic_fetch_add(&shared_state->rect_id, 1, __ATOMIC_SEQ_CST);
    msg.head_id = head->id;
    hdr->dr_ts = timestamp_ns();
    sync_shared_state();
    ssize_t len = send(shared_state->dr_fd, &msg, sizeof(msg), 0);
    if (len < 0) {
        HEADCTL_ERROR("dr send error %d\n", errno);
        return -errno;
    }

    return 0;
}

/* send pending dr to backend if needed */
static void pending_dr_sync(struct head *head)
{
    head_dr_t *hdr = &head_dr[head->index];
    if (hdr->dr_pending) {
        int64_t now = timestamp_ns();
        if (now - hdr->dr_ts < DR_PERIOD_NS) {
            /* too early since last send, do nothing and keep rect pending, send later */
        } else if (drc_empty(&hdr->dr_rect)) {
            /* empty rect, should not happen */
            hdr->dr_pending = 0;
        } else {
            XSync(hdr->display, False); /* sync before backend starts copying or will artifact */

            /* send dr to backend */
            if (send_dr(head, &hdr->dr_rect) == 0) {
                /* send OK, reset pending rectangle */
                drc_reset(&hdr->dr_rect);
                hdr->dr_pending = 0;
            }
        }
    }
}

static void* run_dr_(void *head_)
{
    struct head *head = head_;
    head_dr_t *hdr = &head_dr[head->index];
    Display *d = NULL;

    d = connectx_timeout(head->id, X_CONNECT_TIMEOUT_MS);
    if (!d) {
        HEADCTL_ERROR("FAILED to connect to X server for head %d\n", head->id);
        return 0;
    }

    Window root = DefaultRootWindow(d);
    int damage_event_base, damage_error;
    XDamageQueryExtension(d, &damage_event_base, &damage_error);
    Damage damage = XDamageCreate(d, root, XDamageReportNonEmpty);

    hdr->dr_pending = 0;
    hdr->display = d;
    drc_reset(&hdr->dr_rect);

    int xfd = XConnectionNumber(d);
    fd_set fds;

    for (;;) {
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == damage_event_base + XDamageNotify) {
                XDamageNotifyEvent *dev = (XDamageNotifyEvent*) &ev;

                if (process_damage_ev(d, damage, dev, &hdr->dr_rect) == 0)
                    hdr->dr_pending = 1;
            }
        }

        pending_dr_sync(head);

        XFlush(d);
        if (XPending(d))
            continue;

        int timeout_ms = 10000;
        if (hdr->dr_pending) {
            /* short timeout if we have DR pending */
            int64_t now = timestamp_ns();
            int64_t dt = now - hdr->dr_ts;
            if (dt <= DR_PERIOD_NS)
                timeout_ms = (int)((DR_PERIOD_NS - dt) / 1000000);
            else
                timeout_ms = 0;
        }

        FD_ZERO(&fds);
        FD_SET(xfd, &fds);

        struct timeval tv;
        memset(&tv, 0, sizeof(tv));
        tv.tv_usec = 1000 * timeout_ms;

        select(xfd + 1, &fds, NULL, NULL, &tv);
    }
}

static void run_dr(volatile struct head *h)
{
    pthread_t tid;

    int err = pthread_create(&tid, NULL, run_dr_, (void*)h);
    if (err) {
        HEADCTL_ERROR("couldn't create dr thread for head %d: %d\n", h->id, err);
    }
}

void headctl_wakeup(int *timeout)
{
    int t = *timeout, i, num;

    if (shared_state->active_head_request != shared_state->active_head) {
        int err = headctl_activate(shared_state->active_head_request);

        if (err) {
            /* try to activate it again later */
            if (t == -1 || t > 50)
                t = 50;
        }
    }

    if (t == -1 || t > 1000)
        t = 1000;

    num = shared_state->heads_num;
    for (i = 0; i < num; i++) {
        volatile struct head *head = &shared_state->heads[i];
        head_dr_t *hdr = &head_dr[head->index];
        /* check for missing head dr and run dr tracking if needed */
        if (!hdr->dr) {
            memset(hdr, 0, sizeof(*hdr));
            hdr->dr = 1;
            printf("running dr on head %d\n", head->id);
            run_dr(head);
        }
    }

    *timeout = t;
}

void headctl_event(int fd)
{
    if (fd == shared_state->dr_fd) {
        ssize_t len;
        struct update_msg msg;

        for (;;) {
            len = recv(fd, &msg, sizeof (msg), MSG_DONTWAIT);
            if (len < 0)
                break;

            if (len < (ssize_t) sizeof(msg))
                break;
            /* do nothing with the dr ack message, we just need to be emptying buffer from them */
        }
    }
}

void headctl_init(void)
{
    struct sockaddr_vm addr;

    update_heads();

    /* connect DR tracking port */
    int fd = socket(AF_VSOCK, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        err(1, "socket");

    memset(&addr, 0, sizeof(addr));
    addr.family = AF_VSOCK;
    addr.partner = V4V_DOMID_DM;
    addr.v4v.domain = V4V_DOMID_DM;
    addr.v4v.port = UXENDISP_PORT;

    if (bind(fd, (const struct sockaddr *) &addr, sizeof(addr)) < 0)
        err(1, "bind %d", (int) errno);

    if (connect(fd, (const struct sockaddr *) &addr, sizeof(addr)) < 0)
        err(1, "connect %d", (int) errno);

    pollfd_add(fd);

    shared_state->dr_fd = fd;
    shared_state->rect_id = 0;

    sync_shared_state();
}

void headctl(int argc, char **argv)
{
    char *cmd;

    if (argc < 3)
        headctl_usage();

    cmd = argv[2];

    if (!strcmp(cmd, "list"))
        cmd_headctl_list();
    else if (!strcmp(cmd, "create")) {
        if (argc < 4)
            headctl_usage();
        cmd_headctl_create(argv[3]);
    } else if (!strcmp(cmd, "device")) {
        if (argc < 4)
            headctl_usage();
        cmd_headctl_device(argv[3]);
    } else if (!strcmp(cmd, "initx")) {
        if (argc < 4)
            headctl_usage();
        cmd_headctl_initx(argv[3], argc - 4, argv + 4, 0);
    }  else if (!strcmp(cmd, "initx-wait")) {
        if (argc < 4)
            headctl_usage();
        cmd_headctl_initx(argv[3], argc - 4, argv + 4, 1);
    } else if (!strcmp(cmd, "activate")) {
        if (argc < 4)
            headctl_usage();
        cmd_headctl_activate(argv[3]);
    } else
        headctl_usage();
}

