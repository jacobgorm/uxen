/*
 * Copyright 2019, Bromium, Inc.
 * Author: Simon Haggett <simon.haggett@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#pragma once

#define VM_DIAGNOSTICS_MSG_TYPE_STAT_SYSTEM 0
#define VM_DIAGNOSTICS_MSG_TYPE_STAT_CPU_SUMMARY 1
#define VM_DIAGNOSTICS_MSG_TYPE_STAT_CPU 2
#define VM_DIAGNOSTICS_MSG_TYPE_STAT_TASK 3

#define VM_DIAGNOSTICS_MSG_TYPE_ERROR 0x8000
#define VM_DIAGNOSTICS_MSG_TYPE_ERROR_INVALID_REQUEST (VM_DIAGNOSTICS_MSG_TYPE_ERROR + 0)

#define VM_DIAGNOSTICS_V4V_PORT 44461
#define VM_DIAGNOSTICS_V4V_RING_SIZE_BYTES (256 * 1024)

#define VM_DIAGNOSTICS_MSG_MAX_PAYLOAD_BYTES 4089

#define VM_DIAGNOSTICS_MAX_TASK_NAME_BYTES 16

/* Pack structures, using directives that GCC and MSVC both understand. */
#pragma pack(push, 1)

struct vm_diagnostics_hdr
{
    uint16_t type;
    uint32_t payload_size;

};

struct vm_diagnostics_msg
{
    struct vm_diagnostics_hdr header;
    uint8_t payload[VM_DIAGNOSTICS_MSG_MAX_PAYLOAD_BYTES];

};

struct vm_diagnostics_stat_system
{
    uint64_t current_time_sec;
    uint64_t current_time_nsec;

    uint64_t boot_time_sec;
    uint64_t boot_time_nsec;

    uint32_t num_cpus;
    uint32_t num_tasks;

};

struct vm_diagnostics_stat_cpu
{
    uint32_t cpu_id;

    uint64_t user_nsec;
    uint64_t nice_nsec;
    uint64_t system_nsec;
    uint64_t idle_nsec;
    uint64_t iowait_nsec;
    uint64_t irq_nsec;
    uint64_t softirq_nsec;
    uint64_t steal_nsec;
    uint64_t guest_nsec;
    uint64_t guest_nice_nsec;

};

struct vm_diagnostics_stat_task
{
    int32_t pid;
    int32_t parent_pid;

    uint32_t uid;
    uint32_t gid;

    char name[VM_DIAGNOSTICS_MAX_TASK_NAME_BYTES];

    char state;
    int32_t num_threads;
    uint64_t start_time_nsec;
    int32_t last_run_cpu_id;

    uint64_t user_nsec;
    uint64_t system_nsec;

    uint64_t user_vm_size_bytes;
    int64_t user_rss;

};

#pragma pack(pop)     /* pack(push, 1) */
