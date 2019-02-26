/*
 * Copyright 2019, Bromium, Inc.
 * Author: Tomasz Wroblewski <tomasz.wroblewski@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#ifndef _ATTO_AGENT_H_
#define _ATTO_AGENT_H_

int atto_agent_init(void);
void atto_agent_cleanup(void);
int atto_agent_send_resize_event(unsigned xres, unsigned yres);
int atto_agent_window_ready(void);
void atto_agent_change_kbd_layout(unsigned win_kbd_layout);

#endif
