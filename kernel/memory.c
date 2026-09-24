#include "memory.h"
#include "lib.h"

#define PHYS_LIMIT (64u * 1024u * 1024u)
#define FRAME_COUNT (PHYS_LIMIT / PAGE_SIZE)
#define BITMAP_BYTES (FRAME_COUNT / 8u)
#define MULTIBOOT_FLAG_MMAP (1u << 6)
#define PAGE_PRESENT 0x001u
#define PAGE_WRITABLE 0x002u
#define PAGE_USER 0x004u
#define KERNEL_PAGE_FLAGS (PAGE_PRESENT | PAGE_WRITABLE)

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
static unsigned char frame_user[BITMAP_BYTES];
static unsigned int free_frames;
static unsigned int page_directory[1024] __attribute__((aligned(PAGE_SIZE)));
static unsigned int kernel_page_tables[PHYS_LIMIT / (4u * 1024u * 1024u)][1024]
    __attribute__((aligned(PAGE_SIZE)));

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

static int frame_is_user(unsigned int frame)
{
    return (frame_user[frame / 8u] & (1u << (frame % 8u))) != 0;
}

static void set_user_frame(unsigned int frame, int user)
{
    unsigned char mask;

    if (frame >= FRAME_COUNT)
        return;
    mask = (unsigned char)(1u << (frame % 8u));
    if (user)
        frame_user[frame / 8u] |= mask;
    else
        frame_user[frame / 8u] &= (unsigned char)~mask;
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
    kmemset(frame_user, 0, sizeof(frame_user));
    free_frames = 0;
    if (multiboot_valid && (info->flags & MULTIBOOT_FLAG_MMAP))
        release_multiboot_memory(info);

    /* Keep real-mode/BIOS pages and all kernel-owned bootstrap data. */
    mark_range(0, 0x100000u, 1);
    mark_range(0x100000u, (unsigned int)&kernel_end - 0x100000u, 1);
    if (multiboot_valid)
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

unsigned int memory_alloc_user_page(void)
{
    unsigned int physical_address = memory_alloc_page();

    if (physical_address)
        set_user_frame(physical_address / PAGE_SIZE, 1);
    return physical_address;
}

void memory_free_page(unsigned int physical_address)
{
    if ((physical_address & (PAGE_SIZE - 1u)) == 0) {
        set_user_frame(physical_address / PAGE_SIZE, 0);
        set_frame(physical_address / PAGE_SIZE, 0);
    }
}

unsigned int memory_free_pages(void)
{
    return free_frames;
}

void paging_init(void)
{
    unsigned int directory_index;
    unsigned int table_index;

    for (directory_index = 0; directory_index < 1024; directory_index++)
        page_directory[directory_index] = 0;
    for (directory_index = 0;
         directory_index < PHYS_LIMIT / (4u * 1024u * 1024u);
         directory_index++) {
        for (table_index = 0; table_index < 1024; table_index++)
            kernel_page_tables[directory_index][table_index] =
                ((directory_index * 1024u + table_index) * PAGE_SIZE) |
                KERNEL_PAGE_FLAGS;
        page_directory[directory_index] =
            ((unsigned int)kernel_page_tables[directory_index]) |
            KERNEL_PAGE_FLAGS;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory));
    __asm__ volatile("mov %%cr0, %%eax; or $0x80000000, %%eax; mov %%eax, %%cr0"
                     : : : "eax", "memory");
}

unsigned int paging_kernel_directory(void)
{
    return (unsigned int)page_directory;
}

unsigned int paging_create_user_directory(void)
{
    unsigned int *directory;
    unsigned int physical_address;
    unsigned int i;

    physical_address = memory_alloc_page();
    if (!physical_address)
        return 0;
    directory = (unsigned int *)physical_address;
    for (i = 0; i < 1024; i++)
        directory[i] = 0;

    /* Every address space can enter the same mapped kernel safely. */
    for (i = 0; i < PHYS_LIMIT / (4u * 1024u * 1024u); i++)
        directory[i] = page_directory[i];
    return physical_address;
}

void paging_destroy_user_directory(unsigned int directory_address)
{
    unsigned int *directory = (unsigned int *)directory_address;
    unsigned int i;

    if (!directory_address || directory_address == (unsigned int)page_directory)
        return;
    for (i = USER_VIRTUAL_BASE >> 22; i < USER_VIRTUAL_LIMIT >> 22; i++) {
        if (directory[i] & PAGE_PRESENT)
            memory_free_page(directory[i] & ~(PAGE_SIZE - 1u));
    }
    memory_free_page(directory_address);
}

void paging_switch_directory(unsigned int directory_address)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(directory_address) : "memory");
}

int paging_map_user_page(unsigned int directory_address,
                         unsigned int virtual_address,
                         unsigned int physical_address, int writable)
{
    unsigned int *directory = (unsigned int *)directory_address;
    unsigned int *table;
    unsigned int directory_index = virtual_address >> 22;
    unsigned int table_index = (virtual_address >> 12) & 0x3ffu;
    unsigned int table_address;
    unsigned int flags = PAGE_PRESENT | PAGE_USER;

    if (!directory_address ||
        virtual_address < USER_VIRTUAL_BASE || virtual_address >= USER_VIRTUAL_LIMIT ||
        (virtual_address & (PAGE_SIZE - 1u)) ||
        (physical_address & (PAGE_SIZE - 1u)) ||
        physical_address >= PHYS_LIMIT ||
        !frame_is_user(physical_address / PAGE_SIZE))
        return -1;
    if (writable)
        flags |= PAGE_WRITABLE;

    if (!(directory[directory_index] & PAGE_PRESENT)) {
        table_address = memory_alloc_page();
        if (!table_address)
            return -1;
        table = (unsigned int *)table_address;
        kmemset(table, 0, PAGE_SIZE);
        directory[directory_index] = table_address | PAGE_PRESENT |
                                   PAGE_WRITABLE | PAGE_USER;
    }
    table = (unsigned int *)(directory[directory_index] & ~(PAGE_SIZE - 1u));
    if (table[table_index] & PAGE_PRESENT)
        return -1;
    table[table_index] = physical_address | flags;
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 0;
}

unsigned int paging_unmap_user_page(unsigned int directory_address,
                                    unsigned int virtual_address)
{
    unsigned int *directory = (unsigned int *)directory_address;
    unsigned int *table;
    unsigned int directory_index = virtual_address >> 22;
    unsigned int table_index = (virtual_address >> 12) & 0x3ffu;
    unsigned int physical_address;

    if (!directory_address ||
        virtual_address < USER_VIRTUAL_BASE || virtual_address >= USER_VIRTUAL_LIMIT ||
        (virtual_address & (PAGE_SIZE - 1u)) ||
        !(directory[directory_index] & PAGE_PRESENT))
        return 0;
    table = (unsigned int *)(directory[directory_index] & ~(PAGE_SIZE - 1u));
    if (!(table[table_index] & PAGE_PRESENT))
        return 0;
    physical_address = table[table_index] & ~(PAGE_SIZE - 1u);
    table[table_index] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return physical_address;
}
