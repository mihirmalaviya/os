#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "terminal/terminal.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/pic.h"
#include "arch/pit.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "kernel.h"
#include "tty/tty.h"
#include "sched/task.h"
#include "arch/isr.h"
#include "fs/vfs.h"
#include "fs/tar.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/keyboard.h"
// #include "fs/fat.h"

char *fb;
int scanline;
uint64_t fb_size;

volatile uint64_t b_counter = 0;

void task_b_fn(void) {
    unlock_scheduler();
    for (;;) {
        b_counter++;
    }
}

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        panic("unsupported limine base revision");
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
            || framebuffer_request.response->framebuffer_count < 1) {
        panic("no framebuffer available");
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    fb = (char*)framebuffer->address;
    scanline = framebuffer->pitch;
    fb_size = framebuffer->pitch * framebuffer->height;

    gdt_init();
    idt_init();
    pic_init();
    pit_init();
    keyboard_init();
    pmm_init();
    vmm_init();
    heap_init();
    vfs_init();

    kprintf("hello kernel world!\n");

    pci_scan();
    // pci_print_devices();

    sched_init();
    task_create(task_b_fn);

    __asm__ volatile ("sti");

    // uint64_t *heap_test = (uint64_t *)0x444444440000ULL;
    // *heap_test = 42;
    // kprintf("heap test: %d\n", (int)*heap_test);

    // int *a = (int *)kmalloc(sizeof(int));
    // *a = 10;
    // int *b = (int *)kmalloc(sizeof(int));
    // *b = 20;
    // kprintf("a: %d, b: %d, a_ptr: %x, b_ptr: %x\n", *a, *b, (uint64_t)a, (uint64_t)b);

    // for (;;) {
    //     kprintf("a %d\n", (int)b_counter);
    //     ms_sleep(300);
    // }

    //   uint16_t boot_sector[256];
    //   ata_read_sectors(ATA_DRIVE_SLAVE, 0, 1, boot_sector);
    // kprintf("%x\n", boot_sector[255]);
    //   for (int i = 0; i < 256; i++) {
    //       kprintf("%x ", (uint64_t)boot_sector[i]);
    //       if ((i + 1) % 8 == 0) kprintf("\n");
    //   }

    // fat_init(ATA_DRIVE_SLAVE);
    // fat_read_root_dir();

    // int tar_fd = tar_open("testfile.txt", 0);
    // kprintf("tar_open(\"testfile.txt\") = %d\n", tar_fd);
    //
    // if (tar_fd >= 0) {
    //     char read_buf[64];
    //     int64_t n = tar_read(tar_fd, read_buf, sizeof(read_buf) - 1);
    //     read_buf[n] = '\0';
    //     kprintf("tar_read returned %d bytes: \"%s\"\n", (int)n, read_buf);
    // }

    tty_init();
    for (;;) {
        tty_poll();
        asm ("hlt");
    }
}
