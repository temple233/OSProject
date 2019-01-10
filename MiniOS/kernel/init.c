#include <arch.h>
#include <driver/ps2.h>
#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <page.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/fs/fat.h>
#include <zjunix/fs/impl/impl.h>
#include <zjunix/fs/ext2.h>
#include <zjunix/log.h>
#include <zjunix/pc.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/time.h>
#include "../usr/ps.h"

int init_done = 0;

// from vfs.c
extern struct master_boot_record *MBR;

void machine_info() {
    int row;
    int col;
    kernel_printf("\n%s\n", "83+666-OS");
    row = cursor_row;
    col = cursor_col;
    cursor_row = 28;
    kernel_printf("%s\n", "Author 83+666 (C) Copyright 2019-01-01");
    kernel_printf("%s\n", "Thanks to ZJUNIX & TAs");
    cursor_row = row;
    cursor_col = col;
    kernel_set_cursor();
}

#pragma GCC push_options
#pragma GCC optimize("O0")
void create_startup_process() {
    int res;

    res =  task_create("kernel_shell", (void*)ps, 0, 0, 0, 0);
    if (res != 0)
        kernel_printf("create startup process failed!\n");
    else
        kernel_printf("kernel shell created!\n");
}
#pragma GCC pop_options

void init_kernel() {
    // init_done = 0;
    kernel_clear_screen(31);
    // Exception
    init_exception();
    // Page table
    init_pgtable();
    // Drivers
    init_vga();
    init_ps2();
    // Memory management
    log(LOG_START, "Memory Modules.");
    init_bootmm();
    log(LOG_OK, "Bootmem.");
    init_buddy();
    log(LOG_OK, "Buddy.");
    init_slab();
    log(LOG_OK, "Slab.");
    log(LOG_END, "Memory Modules.");
    init_vfs();
#ifdef FAT_DEBUG
    // FAT File system
    log(LOG_START, "FAT File System.");
    init_fs_fat();
    log(LOG_END, "FAT File System.");
#endif
#ifdef EXT2_DEBUG
    // EXT2 File system
    log(LOG_START, "EXT2 File System.");
    init_fs_ext2(MBR->m_base[1]);
    log(LOG_END, "EXT2 File System.");    
#endif
    // System call
    log(LOG_START, "System Calls.");
    init_syscall();
    log(LOG_END, "System Calls.");

    // Process control
    log(LOG_START, "PID Module.");
    init_pid();
    log(LOG_END, "PID Module.");
    log(LOG_START, "Process Control Module.");
    init_pc();
    create_startup_process();
    log(LOG_END, "Process Control Module.");
    // Interrupts
    log(LOG_START, "Enable Interrupts.");
    init_interrupts();
    log(LOG_END, "Enable Interrupts.");
    // Init finished
    machine_info();
    // init_done = 1;
    *GPIO_SEG = 0x11223344;
    // Enter shell
    while (1)
        ;
}
