/*
 * Copyright 2012-2018, Bromium, Inc.
 * Author: Christian Limpach <Christian.Limpach@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#include "config.h"
#include "dm.h"
#include "vm.h"
#include <err.h>
#include <stdint.h>

#include "input.h"

static input_kbd_fn *kbd_handler = NULL;
static void *kbd_opaque = NULL;

static input_mouse_fn *mouse_handler = NULL;
static void *mouse_opaque = NULL;
static int mouse_absolute = 0;
static int kbd_ledstate = 0;
static void (*kbd_ledstate_notify_fn)(int ledstate, void *opaque) = NULL;
static void *kbd_ledstate_notify_opaque = NULL;

void
input_set_kbd_handler(input_kbd_fn *fn, void *opaque)
{

    kbd_handler = fn;
    kbd_opaque = opaque;
}

void
input_kbd_event(int keycode)
{
    if (kbd_handler)
	kbd_handler(kbd_opaque, keycode);
}

void input_kbd_ledstate_register(void (*fn)(int ledstate, void *opaque),
                                 void *opaque)
{
    kbd_ledstate_notify_fn = fn;
    kbd_ledstate_notify_opaque = opaque;
}

void
input_kbd_ledstate(int ledstate)
{
    kbd_ledstate = ledstate;
    if (kbd_ledstate_notify_fn)
        kbd_ledstate_notify_fn(ledstate, kbd_ledstate_notify_opaque);
}

int
input_get_kbd_ledstate(void)
{
    return kbd_ledstate;
}

void
input_set_mouse_handler(input_mouse_fn *fn, int absolute, void *opaque)
{

    mouse_handler = fn;
    mouse_opaque = opaque;
    mouse_absolute = absolute;
}

void
input_mouse_event(int dx, int dy, int dz, int button_state)
{
    /* prevent crash during access to mouse shared page in ps2_absolute_event(), when
     * the page is freed */
    if (whpx_enable && vm_get_run_mode() != RUNNING_VM)
        return;

    if (mouse_handler)
	mouse_handler(mouse_opaque, dx, dy, dz, button_state);
}

int
input_mouse_is_absolute(void)
{

    return mouse_absolute;
}

void
input_event_cb(void *opaque)
{
    struct input_event *event = (struct input_event *)opaque;

    switch (event->type) {
    case KEYBOARD_INPUT_EVENT:
        if (event->extended)
            input_kbd_event(0xe0);
        input_kbd_event(event->keycode);
        return;
    case MOUSE_INPUT_EVENT:
        input_mouse_event(event->x, event->y, event->dz, event->button_state);
        return;
    default:
        return;
    }
}
