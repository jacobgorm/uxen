/*
 * Copyright 2016-2019, Bromium, Inc.
 * Author: Paulian Marinca <paulian@marinca.net>
 * SPDX-License-Identifier: ISC
 */

#include <linux/init.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <xen/xen.h>
#include <uxen/platform_interface.h>
#include <uxen-platform.h>
#include <uxen-v4vlib.h>
#include <uxen-hypercall.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>

#define UXENSTOR_BITMAP_PORT    0x330
#define V4V_STOR_RING_LEN (1 << 20)
#define V4V_STOR_PORT_BASE 0xd0000
#define V4V_STOR_PARTNER_DOMAIN 0

#define MAX_STACK_IOV   12

#define ROUNDUP_16(x) (((unsigned long)(x) + 0xf) & ~(unsigned long)0xf)

#define UXENSTOR_DEBUG 0

#if UXENSTOR_DEBUG
#define STORDBG(fmt, ...) printk(KERN_DEBUG "(uxenstor) %s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)
#else
#define STORDBG(fmt, ...) do { } while(0)
#endif

#define MAX_HOSTS   4

struct uxenstor_dev {
    struct Scsi_Host *shost;
    int shost_added;
    unsigned host_id;
    uxen_v4v_ring_t *recv_ring;
    struct tasklet_struct tasklet;
    v4v_addr_t dest_addr;
    struct idr seq_map;
    spinlock_t lk_seq_map;
    wait_queue_head_t wq;
};

typedef struct _XFER_HEADER {
    u64 seq;
    u32 cdb_size;
    u32 write_size;
    u32 pagelist_size;
    u32 read_size;
    u32 sense_size;
    u32 status;
    u8  data[];
} XFER_HEADER, *PXFER_HEADER;

struct uxenstor_state {
    struct Scsi_Host *hosts[MAX_HOSTS];
};

static unsigned v4v_storage = 0;

static void uxenstor_irq(void *opaque)
{
    struct uxenstor_dev *uxstor = opaque;

    tasklet_schedule(&uxstor->tasklet);
}

static void uxenstor_softirq(unsigned long opaque)
{
    struct uxenstor_dev *uxstor = (struct uxenstor_dev *) opaque;
    XFER_HEADER hdr;
    ssize_t len;
    size_t readlen = 0;
    int req_id;
    struct scsi_cmnd *sc;

    if (!uxstor->recv_ring)
      return;

    while (readlen <= V4V_STOR_RING_LEN) {
        sc = NULL;
        len = uxen_v4v_copy_out(uxstor->recv_ring, NULL, NULL, &hdr, sizeof(hdr), 0);
        if (len < 0)
            break;
        if (len < sizeof(hdr)) {
            printk(KERN_ERR "wrong dgram received!");
            goto sc_done;
        }

        req_id = (int) hdr.seq;

        spin_lock(&uxstor->lk_seq_map);
        sc = idr_find(&uxstor->seq_map, req_id);
        spin_unlock(&uxstor->lk_seq_map);

        BUG_ON(!sc);

        if (hdr.sense_size > 0) {
            len = 0;
            if (sc->sense_buffer && hdr.sense_size <= SCSI_SENSE_BUFFERSIZE) {
                uxen_v4v_copy_out_offset(uxstor->recv_ring, NULL, NULL,
                                         sc->sense_buffer,
                                         sizeof(hdr) + hdr.sense_size,
                                         0, sizeof(hdr));
                set_driver_byte(sc, DRIVER_SENSE);
            }

            set_host_byte(sc, DID_ERROR);
            goto sc_done;
        }

        if (hdr.read_size > 0) {
            size_t buflen, copied = 0;
            unsigned sg_count, j;
            struct scatterlist *sg;
            void *buf;
            size_t to_read;

            buflen = scsi_bufflen(sc);
            if (hdr.read_size > buflen) {
                printk(KERN_ERR "recv data length too large!");

                set_host_byte(sc, DID_ERROR);
                goto sc_done;
            }
            buflen = hdr.read_size;

            sg_count = scsi_sg_count(sc);
            if (sg_count) {
                scsi_for_each_sg(sc, sg, sg_count, j) {
                    if (!buflen)
                        break;

                    buf = sg_virt(sg);
                    to_read = sg->length;
                    if (to_read > buflen)
                        to_read = buflen;

                    uxen_v4v_copy_out_offset(uxstor->recv_ring, NULL, NULL,
                                             buf, sizeof(hdr) + copied + to_read,
                                             0, sizeof(hdr) + copied);
                    copied += to_read;
                    buflen -= to_read;
                }
            } else if (buflen && scsi_sglist(sc)) {
                sg = scsi_sglist(sc);
                buf = sg_virt(sg);
                to_read = sg->length;
                if (to_read > buflen)
                    to_read = buflen;

                uxen_v4v_copy_out_offset(uxstor->recv_ring, NULL, NULL,
                                         buf, sizeof(hdr) + copied + to_read,
                                         0, sizeof(hdr) + copied);
                copied += to_read;
                buflen -= to_read;

            }
        }

        set_host_byte(sc, DID_OK);

        sc_done:
        len = uxen_v4v_copy_out(uxstor->recv_ring, NULL, NULL, NULL, 0, 1);
        if (len > 0)
            readlen += len;
        if (sc) {
            spin_lock(&uxstor->lk_seq_map);
            idr_remove(&uxstor->seq_map, req_id);
            spin_unlock(&uxstor->lk_seq_map);
            sc->scsi_done(sc);
            wake_up(&uxstor->wq);
        }
    }

    if (readlen)
        uxen_v4v_notify();
}

static int uxenstor_v4v_ring_init(struct uxenstor_dev *dev)
{
    dev->dest_addr.port = V4V_STOR_PORT_BASE + dev->host_id;
    dev->dest_addr.domain = V4V_DOMID_DM;
    dev->recv_ring = uxen_v4v_ring_bind(dev->dest_addr.port, dev->dest_addr.domain,
                                        V4V_STOR_RING_LEN, uxenstor_irq, dev);
    if (!dev->recv_ring)
        return -ENOMEM;

    if (IS_ERR(dev->recv_ring)) {
        int ret = PTR_ERR(dev->recv_ring);
        dev->recv_ring = NULL;
        return ret;
    }

    return 0;
}

static void uxenstor_v4v_ring_free(struct uxenstor_dev *dev)
{
    if (dev->recv_ring)
        uxen_v4v_ring_free(dev->recv_ring);
    dev->recv_ring = NULL;
}

static void uxenstor_remove_all(struct uxen_device *dev)
{
    struct uxenstor_state *s = dev->priv;
    int i;

    for (i = 0; i < MAX_HOSTS; i++) {
        struct uxenstor_dev *uxstor;

        if (!s->hosts[i])
            continue;
        uxstor = shost_priv(s->hosts[i]);
        if (!uxstor)
            continue;
        if (uxstor->recv_ring)
            uxenstor_v4v_ring_free(uxstor);
        if (uxstor->shost_added)
            scsi_remove_host(uxstor->shost);
        idr_destroy(&uxstor->seq_map);
        scsi_host_put(uxstor->shost);
        s->hosts[i] = NULL;
    }
}

static void uxenstor_scan_all(struct uxen_device *dev)
{
    struct uxenstor_state *s = dev->priv;
    int i;

    for (i = 0; i < MAX_HOSTS; i++) {
        struct uxenstor_dev *uxstor;

        if (!s->hosts[i])
            continue;
        uxstor = shost_priv(s->hosts[i]);
        if (!uxstor)
            continue;
        if (!uxstor->recv_ring)
            continue;
        scsi_scan_host(uxstor->shost);
        STORDBG("scsi_scan_host done");
    }
}

static int uxenstor_queuecommand(struct Scsi_Host *sh, struct scsi_cmnd *sc)
{

    int ret = 0;
    struct uxenstor_dev *uxstor;
    XFER_HEADER *hdr;
    u8 hdr_data[ROUNDUP_16(sizeof(*hdr) + 16)];
    v4v_iov_t _iov[MAX_STACK_IOV];
    v4v_iov_t *iov = &_iov[0];
    u32 req_id;
    size_t req_size, buflen, i;
    struct scatterlist *sg;
    unsigned sg_count = 0, j;

    STORDBG("cmd %p cdb %02x", sc, (unsigned) (sc->cmnd ? *(sc->cmnd) : -1));

    uxstor = shost_priv(sh);
    if (!uxstor || !uxstor->recv_ring) {
        ret = -EINVAL;
        goto out;
    }

    hdr = (XFER_HEADER *)&hdr_data[0];
    memset(hdr, 0, sizeof(*hdr));

    hdr->cdb_size = sc->cmd_len;
    memcpy(&hdr->data[0], sc->cmnd, sc->cmd_len);
    req_size = ROUNDUP_16(sizeof(*hdr) + sc->cmd_len);

    hdr->pagelist_size = 0;
    hdr->sense_size = SCSI_SENSE_BUFFERSIZE;

    i = 1;
    buflen = scsi_bufflen(sc);
    if (sc->sc_data_direction == DMA_FROM_DEVICE) {
        hdr->read_size = buflen;
    } else if (sc->sc_data_direction == DMA_TO_DEVICE) {
        hdr->write_size = buflen;
        sg_count = scsi_sg_count(sc);
        if (sg_count + 2 > ARRAY_SIZE(_iov) &&
            !(iov = kmalloc(sizeof(*iov) * (sg_count + 2), GFP_KERNEL))) {

            ret = -ENOMEM;
            goto out;
        }

        if (sg_count) {
            scsi_for_each_sg(sc, sg, sg_count, j) {
                iov[i].iov_base = (u64) sg_virt(sg);
                iov[i].iov_len = sg->length;
                i++;
            }
        } else if (scsi_sglist(sc)) {
            sg = scsi_sglist(sc);
            iov[i].iov_base = (u64) sg_virt(sg);
            iov[i].iov_len = sg->length;
            i++;
        }
    }

    iov[0].iov_base = (u64) hdr;
    iov[0].iov_len = req_size;

    spin_lock(&uxstor->lk_seq_map);
    ret = idr_alloc_cyclic(&uxstor->seq_map, sc, 0, 0, GFP_NOWAIT);
    spin_unlock(&uxstor->lk_seq_map);
    if (ret < 0)
        goto out;
    req_id = ret;

    hdr->seq = (u64) req_id;
    mb();
    ret = uxen_v4v_sendv_from_ring(uxstor->recv_ring, &uxstor->dest_addr, iov, i,
                             V4V_PROTO_DGRAM);
    if (ret < 0) {
        spin_lock(&uxstor->lk_seq_map);
        idr_remove(&uxstor->seq_map, req_id);
        spin_unlock(&uxstor->lk_seq_map);
        printk(KERN_ERR "uxen_v4v_sendv_from_ring failed %d", (int) ret);
        if (ret == -EAGAIN)
            ret = SCSI_MLQUEUE_DEVICE_BUSY;
        goto out;
    }

    ret = 0;

out:
    if (iov && iov != &_iov[0])
        kfree(iov);
    return ret;
}

#if 0
static int uxenstor_abort(struct scsi_cmnd *sc)
{
    return 0;
}
#endif

static int uxenstor_device_reset(struct scsi_cmnd *sc)
{
    return 0;
}

static struct scsi_host_template uxenstor_scsi_template = {
        .module = THIS_MODULE,
        .name = "uXen SCSI HBA",
        .proc_name = "uxenstor",
        .queuecommand = uxenstor_queuecommand,
        .this_id = -1,
#if 0
        .eh_abort_handler = uxenstor_abort,
#endif
        .eh_device_reset_handler = uxenstor_device_reset,

        .can_queue = 1024,
        .dma_boundary = UINT_MAX,
};

static int uxenstor_has_pending(struct uxenstor_dev *uxstor)
{
    return !(idr_is_empty(&uxstor->seq_map));
}

static int uxenstor_suspend_one(struct uxenstor_dev *uxstor)
{
    printk("uxenstor suspend host %d\n", uxstor->host_id);
    scsi_block_requests(uxstor->shost);
    /* wait for pending requests to complete */
    wait_event_interruptible(uxstor->wq, !uxenstor_has_pending(uxstor));
    tasklet_disable(&uxstor->tasklet);
    return 0;
}

static int uxenstor_suspend(struct uxen_device *dev)
{
    struct uxenstor_state *s = dev->priv;
    int i, err;

    for (i = 0; i < MAX_HOSTS; i++) {
        struct uxenstor_dev *uxstor;

        if (!s->hosts[i])
            continue;
        uxstor = shost_priv(s->hosts[i]);
        if (!uxstor)
            continue;
        err = uxenstor_suspend_one(uxstor);
        if (err)
            return err;
    }
    return 0;
}

static int uxenstor_resume_one(struct uxenstor_dev *uxstor)
{
    printk("uxenstor resume host %d\n", uxstor->host_id);
    tasklet_enable(&uxstor->tasklet);
    scsi_unblock_requests(uxstor->shost);
    return 0;
}

static int uxenstor_resume(struct uxen_device *dev)
{
    struct uxenstor_state *s = dev->priv;
    int i, err;

    for (i = 0; i < MAX_HOSTS; i++) {
        struct uxenstor_dev *uxstor;

        if (!s->hosts[i])
            continue;
        uxstor = shost_priv(s->hosts[i]);
        if (!uxstor)
            continue;
        err = uxenstor_resume_one(uxstor);
        if (err)
            return err;
    }
    return 0;
}

static int uxenstor_probe(struct uxen_device *dev)
{
    int ret;
    unsigned i;
    struct uxenstor_state *state;

    state = vzalloc(sizeof(*state));
    if (!state)
        return -ENOMEM;
    dev->priv = state;

    v4v_storage = inw(UXENSTOR_BITMAP_PORT);

    if (!v4v_storage) {
        printk(KERN_INFO "%s: no v4v storage found\n", __FUNCTION__);
        ret = -ENODEV;
        goto fail;
    }
    STORDBG("v4v-storage bitmap 0x%x", v4v_storage);

    for (i = 0; i < MAX_HOSTS; i++) {
        struct Scsi_Host *shost = NULL;
        struct uxenstor_dev *uxstor;

        if (!((v4v_storage >> i) & 0x1))
            continue;

        shost = scsi_host_alloc(&uxenstor_scsi_template, sizeof(*uxstor));
        if (!shost) {
            ret = -ENOMEM;
            goto fail;
        }

        uxstor = shost_priv(shost);
        memset(uxstor, 0, sizeof(*uxstor));
        uxstor->shost = shost;
        uxstor->host_id = i;
        spin_lock_init(&uxstor->lk_seq_map);
        idr_init(&uxstor->seq_map);
        tasklet_init(&uxstor->tasklet, uxenstor_softirq, (unsigned long) uxstor);
        init_waitqueue_head(&uxstor->wq);

        /* tweak ? */
        shost->sg_tablesize = 168; // FIXME
        shost->cmd_per_lun = 1;
        shost->max_lun = 1;
        shost->max_id = 1;
        shost->max_channel = 0;
        shost->max_cmd_len = 16;

        state->hosts[i] = shost;

        ret = uxenstor_v4v_ring_init(uxstor);
        if (ret)
            goto fail;

        ret = scsi_add_host(shost, NULL);
        if (ret)
            goto fail;
        uxstor->shost_added = 1;
    }

    uxenstor_scan_all(dev);

    ret = 0;
out:
    return ret;

fail:
    uxenstor_remove_all(dev);
    goto out;
}

static int uxenstor_remove(struct uxen_device *dev)
{
    struct uxenstor_state *state = dev->priv;

    uxenstor_remove_all(dev);
    if (state) {
        vfree(state);
    }
    return 0;
}

static struct uxen_driver uxenstor_drv = {
    .drv = {
        .name = "uxenstor",
        .owner = THIS_MODULE,
    },
    .type = UXENBUS_DEVICE_TYPE_STOR,
    .probe = uxenstor_probe,
    .remove = uxenstor_remove,
    .suspend = uxenstor_suspend,
    .resume = uxenstor_resume,
};

int __init uxenstor_init(void)
{
    return uxen_driver_register(&uxenstor_drv);
}

static void __exit uxenstor_exit(void)
{
    uxen_driver_unregister(&uxenstor_drv);
}

EXPORT_SYMBOL(uxenstor_init);

module_init(uxenstor_init);
module_exit(uxenstor_exit);
MODULE_AUTHOR("paulian.marinca@bromium.com");
MODULE_DESCRIPTION("uXen storage driver");
MODULE_LICENSE("GPL");
