/*
 * Copyright (C) 2009      Citrix Ltd.
 * Author Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include "libxl_osdeps.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <xs.h>
#include <xenctrl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include "libxl_utils.h"
#include "libxl_internal.h"

struct schedid_name {
    char *name;
    int id;
};

static struct schedid_name schedid_name[] = {
    { "credit", XEN_SCHEDULER_CREDIT },
    { "sedf", XEN_SCHEDULER_SEDF },
    { "credit2", XEN_SCHEDULER_CREDIT2 },
    { NULL, -1 }
};

const char *libxl_basename(const char *name)
{
    const char *filename;
    if (name == NULL)
        return strdup(".");
    if (name[0] == '\0')
        return strdup(".");

    filename = strrchr(name, '/');
    if (filename)
        return strdup(filename+1);
    return strdup(name);
}

unsigned long libxl_get_required_shadow_memory(unsigned long maxmem_kb, unsigned int smp_cpus)
{
    /* 256 pages (1MB) per vcpu,
       plus 1 page per MiB of RAM for the P2M map,
       plus 1 page per MiB of RAM to shadow the resident processes.
       This is higher than the minimum that Xen would allocate if no value
       were given (but the Xen minimum is for safety, not performance).
     */
    return 4 * (256 * smp_cpus + 2 * (maxmem_kb / 1024));
}

char *libxl_domid_to_name(libxl_ctx *ctx, uint32_t domid)
{
    unsigned int len;
    char path[strlen("/local/domain") + 12];
    char *s;

    snprintf(path, sizeof(path), "/local/domain/%d/name", domid);
    s = xs_read(ctx->xsh, XBT_NULL, path, &len);
    return s;
}

char *libxl__domid_to_name(libxl__gc *gc, uint32_t domid)
{
    char *s = libxl_domid_to_name(libxl__gc_owner(gc), domid);
    if ( s )
        libxl__ptr_add(gc, s);
    return s;
}

int libxl_name_to_domid(libxl_ctx *ctx, const char *name,
                        uint32_t *domid)
{
    int i, nb_domains;
    char *domname;
    libxl_dominfo *dominfo;
    int ret = ERROR_INVAL;

    dominfo = libxl_list_domain(ctx, &nb_domains);
    if (!dominfo)
        return ERROR_NOMEM;

    for (i = 0; i < nb_domains; i++) {
        domname = libxl_domid_to_name(ctx, dominfo[i].domid);
        if (!domname)
            continue;
        if (strcmp(domname, name) == 0) {
            *domid = dominfo[i].domid;
            ret = 0;
            free(domname);
            break;
        }
        free(domname);
    }
    free(dominfo);
    return ret;
}

char *libxl_cpupoolid_to_name(libxl_ctx *ctx, uint32_t poolid)
{
    unsigned int len;
    char path[strlen("/local/pool") + 12];
    char *s;

    snprintf(path, sizeof(path), "/local/pool/%d/name", poolid);
    s = xs_read(ctx->xsh, XBT_NULL, path, &len);
    if (!s && (poolid == 0))
        return strdup("Pool-0");
    return s;
}

char *libxl__cpupoolid_to_name(libxl__gc *gc, uint32_t poolid)
{
    char *s = libxl_cpupoolid_to_name(libxl__gc_owner(gc), poolid);
    if ( s )
        libxl__ptr_add(gc, s);
    return s;
}

int libxl_name_to_cpupoolid(libxl_ctx *ctx, const char *name,
                        uint32_t *poolid)
{
    int i, nb_pools;
    char *poolname;
    libxl_cpupoolinfo *poolinfo;
    int ret = ERROR_INVAL;

    poolinfo = libxl_list_cpupool(ctx, &nb_pools);
    if (!poolinfo)
        return ERROR_NOMEM;

    for (i = 0; i < nb_pools; i++) {
        if (ret && ((poolname = libxl_cpupoolid_to_name(ctx,
            poolinfo[i].poolid)) != NULL)) {
            if (strcmp(poolname, name) == 0) {
                *poolid = poolinfo[i].poolid;
                ret = 0;
            }
            free(poolname);
        }
        libxl_cpupoolinfo_dispose(poolinfo + i);
    }
    free(poolinfo);
    return ret;
}

int libxl_name_to_schedid(libxl_ctx *ctx, const char *name)
{
    int i;

    for (i = 0; schedid_name[i].name != NULL; i++)
        if (strcmp(name, schedid_name[i].name) == 0)
            return schedid_name[i].id;

    return ERROR_INVAL;
}

char *libxl_schedid_to_name(libxl_ctx *ctx, int schedid)
{
    int i;

    for (i = 0; schedid_name[i].name != NULL; i++)
        if (schedid_name[i].id == schedid)
            return schedid_name[i].name;

    return "unknown";
}

int libxl_get_stubdom_id(libxl_ctx *ctx, int guest_domid)
{
    libxl__gc gc = LIBXL_INIT_GC(ctx);
    char * stubdom_id_s;
    int ret;

    stubdom_id_s = libxl__xs_read(&gc, XBT_NULL,
                                 libxl__sprintf(&gc, "%s/image/device-model-domid",
                                               libxl__xs_get_dompath(&gc, guest_domid)));
    if (stubdom_id_s)
        ret = atoi(stubdom_id_s);
    else
        ret = 0;
    libxl__free_all(&gc);
    return ret;
}

int libxl_is_stubdom(libxl_ctx *ctx, uint32_t domid, uint32_t *target_domid)
{
    libxl__gc gc = LIBXL_INIT_GC(ctx);
    char *target, *endptr;
    uint32_t value;
    int ret = 0;

    target = libxl__xs_read(&gc, XBT_NULL, libxl__sprintf(&gc, "%s/target", libxl__xs_get_dompath(&gc, domid)));
    if (!target)
        goto out;
    value = strtol(target, &endptr, 10);
    if (*endptr != '\0')
        goto out;
    if (target_domid)
        *target_domid = value;
    ret = 1;
out:
    libxl__free_all(&gc);
    return ret;
}

static int logrename(libxl__gc *gc, const char *old, const char *new)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    int r;

    r = rename(old, new);
    if (r) {
        if (errno == ENOENT) return 0; /* ok */

        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to rotate logfile - could not"
                     " rename %s to %s", old, new);
        return ERROR_FAIL;
    }
    return 0;
}

int libxl_create_logfile(libxl_ctx *ctx, char *name, char **full_name)
{
    libxl__gc gc = LIBXL_INIT_GC(ctx);
    struct stat stat_buf;
    char *logfile, *logfile_new;
    int i, rc;

    logfile = libxl__sprintf(&gc, "/var/log/xen/%s.log", name);
    if (stat(logfile, &stat_buf) == 0) {
        /* file exists, rotate */
        logfile = libxl__sprintf(&gc, "/var/log/xen/%s.log.10", name);
        unlink(logfile);
        for (i = 9; i > 0; i--) {
            logfile = libxl__sprintf(&gc, "/var/log/xen/%s.log.%d", name, i);
            logfile_new = libxl__sprintf(&gc, "/var/log/xen/%s.log.%d", name, i + 1);
            rc = logrename(&gc, logfile, logfile_new);
            if (rc)
                goto out;
        }
        logfile = libxl__sprintf(&gc, "/var/log/xen/%s.log", name);
        logfile_new = libxl__sprintf(&gc, "/var/log/xen/%s.log.1", name);

        rc = logrename(&gc, logfile, logfile_new);
        if (rc)
            goto out;
    } else {
        if (errno != ENOENT)
            LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_WARNING, "problem checking existence of"
                         " logfile %s, which might have needed to be rotated",
                         name);
    }
    *full_name = strdup(logfile);
    rc = 0;
out:
    libxl__free_all(&gc);
    return rc;
}

int libxl_string_to_backend(libxl_ctx *ctx, char *s, libxl_disk_backend *backend)
{
    char *p;
    int rc = 0;

    if (!strcmp(s, "phy")) {
        *backend = LIBXL_DISK_BACKEND_PHY;
    } else if (!strcmp(s, "file")) {
        *backend = LIBXL_DISK_BACKEND_TAP;
    } else if (!strcmp(s, "tap")) {
        p = strchr(s, ':');
        if (!p) {
            rc = ERROR_INVAL;
            goto out;
        }
        p++;
        if (!strcmp(p, "vhd")) {
            *backend = LIBXL_DISK_BACKEND_TAP;
        } else if (!strcmp(p, "qcow")) {
            *backend = LIBXL_DISK_BACKEND_QDISK;
        } else if (!strcmp(p, "qcow2")) {
            *backend = LIBXL_DISK_BACKEND_QDISK;
        }
    }
out:
    return rc;
}

int libxl_read_file_contents(libxl_ctx *ctx, const char *filename,
                             void **data_r, int *datalen_r) {
    FILE *f = 0;
    uint8_t *data = 0;
    int datalen = 0;
    int e;
    struct stat stab;
    ssize_t rs;

    f = fopen(filename, "r");
    if (!f) {
        if (errno == ENOENT) return ENOENT;
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to open %s", filename);
        goto xe;
    }

    if (fstat(fileno(f), &stab)) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to fstat %s", filename);
        goto xe;
    }

    if (!S_ISREG(stab.st_mode)) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "%s is not a plain file", filename);
        errno = ENOTTY;
        goto xe;
    }

    if (stab.st_size > INT_MAX) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "file %s is far too large", filename);
        errno = EFBIG;
        goto xe;
    }

    datalen = stab.st_size;

    if (stab.st_size && data_r) {
        data = malloc(datalen);
        if (!data) goto xe;

        rs = fread(data, 1, datalen, f);
        if (rs != datalen) {
            if (ferror(f))
                LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to read %s", filename);
            else if (feof(f))
                LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "%s changed size while we"
                       " were reading it", filename);
            else
                abort();
            goto xe;
        }
    }

    if (fclose(f)) {
        f = 0;
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to close %s", filename);
        goto xe;
    }

    if (data_r) *data_r = data;
    if (datalen_r) *datalen_r = datalen;

    return 0;

 xe:
    e = errno;
    assert(e != ENOENT);
    if (f) fclose(f);
    if (data) free(data);
    return e;
}

#define READ_WRITE_EXACTLY(rw, zero_is_eof, constdata)                    \
                                                                          \
  int libxl_##rw##_exactly(libxl_ctx *ctx, int fd,                 \
                           constdata void *data, ssize_t sz,              \
                           const char *filename, const char *what) {      \
      ssize_t got;                                                        \
                                                                          \
      while (sz > 0) {                                                    \
          got = rw(fd, data, sz);                                         \
          if (got == -1) {                                                \
              if (errno == EINTR) continue;                               \
              if (!ctx) return errno;                                     \
              LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "failed to " #rw " %s%s%s", \
                           what?what:"", what?" from ":"", filename);     \
              return errno;                                               \
          }                                                               \
          if (got == 0) {                                                 \
              if (!ctx) return EPROTO;                                    \
              LIBXL__LOG(ctx, LIBXL__LOG_ERROR,                                   \
                     zero_is_eof                                          \
                     ? "file/stream truncated reading %s%s%s"             \
                     : "file/stream write returned 0! writing %s%s%s",    \
                     what?what:"", what?" from ":"", filename);           \
              return EPROTO;                                              \
          }                                                               \
          sz -= got;                                                      \
          data = (char*)data + got;                                       \
      }                                                                   \
      return 0;                                                           \
  }

READ_WRITE_EXACTLY(read, 1, /* */)
READ_WRITE_EXACTLY(write, 0, const)


int libxl_ctx_postfork(libxl_ctx *ctx) {
    if (ctx->xsh) xs_daemon_destroy_postfork(ctx->xsh);
    ctx->xsh = xs_daemon_open();
    if (!ctx->xsh) return ERROR_FAIL;
    return 0;
}

pid_t libxl_fork(libxl_ctx *ctx)
{
    pid_t pid;

    pid = fork();
    if (pid == -1) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "fork failed");
        return -1;
    }

    if (!pid) {
        if (ctx->xsh) xs_daemon_destroy_postfork(ctx->xsh);
        ctx->xsh = 0;
        /* This ensures that anyone who forks but doesn't exec,
         * and doesn't reinitialise the libxl_ctx, is OK.
         * It also means they can safely call libxl_ctx_free. */
    }

    return pid;
}

int libxl_pipe(libxl_ctx *ctx, int pipes[2])
{
    if (pipe(pipes) < 0) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "Failed to create a pipe");
        return -1;
    }
    return 0;
}

int libxl_mac_to_device_nic(libxl_ctx *ctx, uint32_t domid,
                            const char *mac, libxl_device_nic *nic)
{
    libxl_device_nic *nics;
    int nb, rc, i;
    libxl_mac mac_n;

    rc = libxl__parse_mac(mac, mac_n);
    if (rc)
        return rc;

    nics = libxl_device_nic_list(ctx, domid, &nb);
    if (!nics)
        return ERROR_FAIL;

    memset(nic, 0, sizeof (libxl_device_nic));

    rc = ERROR_INVAL;
    for (i = 0; i < nb; ++i) {
        if (!libxl__compare_macs(&mac_n, &nics[i].mac)) {
            *nic = nics[i];
            rc = 0;
            i++; /* Do not dispose this NIC on exit path */
            break;
        }
        libxl_device_nic_dispose(&nics[i]);
    }

    for (; i<nb; i++)
        libxl_device_nic_dispose(&nics[i]);

    free(nics);
    return rc;
}

int libxl_cpumap_alloc(libxl_ctx *ctx, libxl_cpumap *cpumap)
{
    int max_cpus;
    int sz;

    max_cpus = libxl_get_max_cpus(ctx);
    if (max_cpus == 0)
        return ERROR_FAIL;

    sz = (max_cpus + 7) / 8;
    cpumap->map = calloc(sz, sizeof(*cpumap->map));
    if (!cpumap->map)
        return ERROR_NOMEM;
    cpumap->size = sz;
    return 0;
}

void libxl_cpumap_dispose(libxl_cpumap *map)
{
    free(map->map);
}

int libxl_cpumap_test(libxl_cpumap *cpumap, int cpu)
{
    if (cpu >= cpumap->size * 8)
        return 0;
    return (cpumap->map[cpu / 8] & (1 << (cpu & 7))) ? 1 : 0;
}

void libxl_cpumap_set(libxl_cpumap *cpumap, int cpu)
{
    if (cpu >= cpumap->size * 8)
        return;
    cpumap->map[cpu / 8] |= 1 << (cpu & 7);
}

void libxl_cpumap_reset(libxl_cpumap *cpumap, int cpu)
{
    if (cpu >= cpumap->size * 8)
        return;
    cpumap->map[cpu / 8] &= ~(1 << (cpu & 7));
}

int libxl_cpuarray_alloc(libxl_ctx *ctx, libxl_cpuarray *cpuarray)
{
    int max_cpus;
    int i;

    max_cpus = libxl_get_max_cpus(ctx);
    if (max_cpus == 0)
        return ERROR_FAIL;

    cpuarray->array = calloc(max_cpus, sizeof(*cpuarray->array));
    if (!cpuarray->array)
        return ERROR_NOMEM;
    cpuarray->entries = max_cpus;
    for (i = 0; i < max_cpus; i++)
        cpuarray->array[i] = LIBXL_CPUARRAY_INVALID_ENTRY;

    return 0;
}

void libxl_cpuarray_dispose(libxl_cpuarray *array)
{
    free(array->array);
}

int libxl_get_max_cpus(libxl_ctx *ctx)
{
    return xc_get_max_cpus(ctx->xch);
}

int libxl__enum_from_string(const libxl_enum_string_table *t,
                            const char *s, int *e)
{
    if (!t) return ERROR_INVAL;

    for( ; t->s; t++) {
        if (!strcasecmp(t->s, s)) {
                *e = t->v;
                return 0;
        }
    }
    return ERROR_FAIL;
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
