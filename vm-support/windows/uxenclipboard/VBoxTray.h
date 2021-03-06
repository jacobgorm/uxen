/* $Id: VBoxTray.h 35863 2011-02-07 10:59:08Z vboxsync $ */
/** @file
 * VBoxTray - Guest Additions Tray, Internal Header.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
/*
 * uXen changes:
 *
 * Copyright 2013-2015, Bromium, Inc.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ___VBOXTRAY_H
#define ___VBOXTRAY_H

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <process.h>

#include <iprt/initterm.h>
#include <iprt/string.h>

#include <VBox/version.h>
#include <VBox/Log.h>
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>
//#include <VBoxDisplay.h>

//#include "VBoxDispIf.h"

/*
 * Windows messsages.
 */

/**
 * General VBoxTray messages.
 */
#define WM_VBOXTRAY_TRAY_ICON                   WM_APP + 40
/**
 * VM/VMMDev related messsages.
 */
#define WM_VBOXTRAY_VM_RESTORED                 WM_APP + 100
/**
 * VRDP messages.
 */
#define WM_VBOXTRAY_VRDP_CHECK                  WM_APP + 301


/* The tray icon's ID. */
#define ID_TRAYICON                             2000


/*
 * Timer IDs.
 */
#define TIMERID_VBOXTRAY_CHECK_HOSTVERSION      1000

/* The environment information for services. */
typedef struct _VBOXSERVICEENV
{
    HINSTANCE hInstance;
    HANDLE    hDriver;
    HANDLE    hStopEvent;
    /* display driver interface, XPDM - WDDM abstraction see VBOXDISPIF** definitions above */
//    VBOXDISPIF dispIf;
} VBOXSERVICEENV;

/* The service initialization info and runtime variables. */
typedef struct _VBOXSERVICEINFO
{
    char     *pszName;
    int      (* pfnInit)             (const VBOXSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
    unsigned (__stdcall * pfnThread) (void *pInstance);
    void     (* pfnDestroy)          (const VBOXSERVICEENV *pEnv, void *pInstance);

    /* Variables. */
    HANDLE   hThread;
    void    *pInstance;
    bool     fStarted;
} VBOXSERVICEINFO;

/* Globally unique (system wide) message registration. */
typedef struct _VBOXGLOBALMESSAGE
{
    /** Message name. */
    char    *pszName;
    /** Function pointer for handling the message. */
    int      (* pfnHandler)          (WPARAM wParam, LPARAM lParam);

    /* Variables. */

    /** Message ID;
     *  to be filled in when registering the actual message. */
    UINT     uMsgID;
} VBOXGLOBALMESSAGE, *PVBOXGLOBALMESSAGE;

extern HWND         ghwndToolWindow;
extern HINSTANCE    ghInstance;

#endif /* !___VBOXTRAY_H */

