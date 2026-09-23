#include "memory.h"
#include "lib.h"

#define PAGE_SIZE 4096u
#define PHYS_LIMIT (64u * 1024u * 1024u)
#define FRAME_COUNT (PHYS_LIMIT / PAGE_SIZE)
#define BITMAP_BYTES (FRAME_COUNT / 8u)
#define MULTIBOOT_FLAG_MMAP (1u << 6)

struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
    unsigned int boot_device;
    unsigned int cmdline;
    unsigned int mods_count;
    unsigned int mods_addr;
    unsigned int syms[4];
    unsigned int mmap_length;
    unsigned int mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
    unsigned int size;
    unsigned int addr_low;
    unsigned int addr_high;
    unsigned int len_low;
    unsigned int len_high;
    unsigned int type;
} __attribute__((packed));

extern char kernel_end;

static unsigned char frame_used[BITMAP_BYTES];
static unsigned int free_frames;
static unsigned int page_directory[1024] __attribute__((aligned(PAGE_SIZE)));

static unsigned int align_down(unsigned int value)
{
    return value & ~(PAGE_SIZE - 1u);
}

static unsigned int align_up(unsigned int value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static int frame_is_used(unsigned int frame)
{
    return (frame_used[frame / 8u] & (1u << (frame % 8u))) != 0;
}

static void set_frame(unsigned int frame, int used)
{
    unsigned char mask;
    if (frame >= FRAME_COUNT)
        return;
    mask = (unsigned char)(1u << (frame % 8u));
    if (used && !frame_is_used(frame)) {
        frame_used[frame / 8u] |= mask;
        free_frames--;
    } else if (!used && frame_is_used(frame)) {
        frame_used[frame / 8u] &= (unsigned char)~mask;
        free_frames++;
    }
}

static void mark_range(unsigned int start, unsigned int length, int used)
{
    unsigned int first = align_down(start) / PAGE_SIZE;
    unsigned int end;
    unsigned int frame;

    if (start >= PHYS_LIMIT || length == 0)
        return;
    if (length > PHYS_LIMIT - start)
        length = PHYS_LIMIT - start;
    end = align_up(start + length) / PAGE_SIZE;
    for (frame = first; frame < end; frame++)
        set_frame(frame, used);
}

static void release_multiboot_memory(struct multiboot_info *info)
{
    unsigned int cursor = info->mmap_addr;
    unsigned int end = cursor + info->mmap_length;

    while (cursor < end) {
        struct multiboot_mmap_entry *entry =
            (struct multiboot_mmap_entry *)cursor;
        if (entry->size < 20u || cursor + entry->size + 4u > end)
            break;
        if (entry->type == 1u && entry->addr_high == 0u)
            mark_range(entry->addr_low, entry->len_high ? PHYS_LIMIT : entry->len_low,
                       0);
        cursor += entry->size + 4u;
    }
}

void memory_init(void *multiboot_info, int multiboot_valid)
{
    struct multiboot_info *info = (struct multiboot_info *)multiboot_info;

    kmemset(frame_used, 0xFF, sizeof(frame_used));
    free_frames = 0;
    if (multiboot_valid && (info->flags & MULTIBOOT_FLAG_MMAP))
        release_multiboot_memory(info);

    /* Keep real-mode/BIOS pages and all kernel-owned bootstrap data. */
    mark_range(0, 0x100000u, 1);
    mark_range(0x100000u, (unsigned int)&kernel_end - 0x100000u, 1);
    mark_range((unsigned int)info, sizeof(*info), 1);
    if (multiboot_valid && (info->flags & MULTIBOOT_FLAG_MMAP))
        mark_range(info->mmap_addr, info->mmap_length, 1);
}

unsigned int memory_alloc_page(void)
{
    unsigned int frame;
    for (frame = 0; frame < FRAME_COUNT; frame++) {
        if (!frame_is_used(frame)) {
            set_frame(frame, 1);
            return frame * PAGE_SIZE;
        }
    }
    return 0;
}

void memory_free_page(unsigned int physical_address)
{
    if ((physical_address & (PAGE_SIZE - 1u)) == 0)
        set_frame(physical_address / PAGE_SIZE, 0);
}

unsigned int memory_free_pages(void)
{
    return free_frames;
}

void paging_init(void)
{
    unsigned int i;
    for (i = 0; i < 1024; i++)
        page_directory[i] = 0;
    for (i = 0; i < PHYS_LIMIT / (4u * 1024u * 1024u); i++)
        page_directory[i] = (i * 0x400000u) | 0x083u;

    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory));
    __asm__ volatile("mov %%cr4, %%eax; or $0x10, %%eax; mov %%eax, %%cr4"
                     : : : "eax");
    __asm__ volatile("mov %%cr0, %%eax; or $0x80000000, %%eax; mov %%eax, %%cr0"
                     : : : "eax", "memory");
}
