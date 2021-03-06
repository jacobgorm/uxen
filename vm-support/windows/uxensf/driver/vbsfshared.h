/** @file
 *
 * VirtualBox Windows Guest Shared Folders
 *
 * File System Driver header file shared with the network provider dll
 */

/*
 * Copyright (C) 2012 Oracle Corporation
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
 * Copyright 2013-2019, Bromium, Inc.
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

#ifndef VBSFSHARED_H
#define VBSFSHARED_H

/* The network provider name for shared folders. */
#define MRX_VBOX_PROVIDER_NAME_U L"VirtualBox Shared Folders"

/* The filesystem name for shared folders. */
#define MRX_VBOX_FILESYS_NAME_U L"VBoxSharedFolderFS"

/* The redirector device name. */
#define DD_MRX_VBOX_FS_DEVICE_NAME_U L"\\Device\\uxenMiniRdr"

#define VBOX_VOLNAME_PREFIX     L"UXEN_"
#define VBOX_VOLNAME_PREFIX_SIZE  (sizeof(VBOX_VOLNAME_PREFIX) - sizeof(VBOX_VOLNAME_PREFIX[0]))

/* Name of symbolic link, which is used by the user mode dll to open the driver. */
#define DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U     L"\\??\\uxenMiniRdrDN"
#define DD_MRX_VBOX_USERMODE_DEV_NAME_U            L"\\\\.\\uxenMiniRdrDN"

#define MRX_VBOX_SERVER_NAME_LENGTH 7
/* if you change MRX_VBOX_SERVER_NAME_U you should update MRX_VBOX_SERVER_NAME_LENGTH */
#define MRX_VBOX_SERVER_NAME_U     L"UXENSVR"

#define IOCTL_MRX_VBOX_BASE FILE_DEVICE_NETWORK_FILE_SYSTEM

#define _MRX_VBOX_CONTROL_CODE(request, method, access) \
                CTL_CODE(IOCTL_MRX_VBOX_BASE, request, method, access)

/* VBoxSF IOCTL codes. */
#define IOCTL_MRX_VBOX_ADDCONN       _MRX_VBOX_CONTROL_CODE(100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETCONN       _MRX_VBOX_CONTROL_CODE(101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_DELCONN       _MRX_VBOX_CONTROL_CODE(102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETLIST       _MRX_VBOX_CONTROL_CODE(103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETGLOBALLIST _MRX_VBOX_CONTROL_CODE(104, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETGLOBALCONN _MRX_VBOX_CONTROL_CODE(105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_START         _MRX_VBOX_CONTROL_CODE(106, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_STOP          _MRX_VBOX_CONTROL_CODE(107, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_SERVERADDR    _MRX_VBOX_CONTROL_CODE(108, METHOD_BUFFERED, FILE_ANY_ACCESS)

#endif /* VBSFSHARED_H */
