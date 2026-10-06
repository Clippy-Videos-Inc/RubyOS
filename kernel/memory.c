/* kernel/memory.c - mapa de memoria Multiboot -> PMM -> heap do kernel */
#include "memory.h"
#include "pmm.h"
#include "heap.h"

#define ONE_MIB          0x100000u
#define HEAP_MAX_FRAMES  (32u * 256u)     /* 32 MiB */
#define HEAP_MIN_FRAMES  64u              /* 256 KiB */

extern char _kernel_start[];
extern char _kernel_end[];

static struct mm_info info;

void mm_init(const struct multiboot_info *mbi)
{
    uint32_t want;
    uint32_t addr = 0;

    pmm_reset();

    if (mbi->flags & MULTIBOOT_INFO_MMAP) {
        uintptr_t p = mbi->mmap_addr;
        uintptr_t end = (uintptr_t)mbi->mmap_addr + mbi->mmap_length;

        while (p + sizeof(struct multiboot_mmap_entry) <= end) {
            const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)p;
            if (e->type == MULTIBOOT_MMAP_USABLE)
                pmm_add_free_region(e->addr, e->len);
            p += (uintptr_t)e->size + sizeof(e->size);       /* "size" nao conta a si mesmo */
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        pmm_add_free_region(0, (uint64_t)mbi->mem_lower * 1024u);
        pmm_add_free_region(ONE_MIB, (uint64_t)mbi->mem_upper * 1024u);
    } else {
        kernel_panic("bootloader nao informou a memoria");
    }

    if (pmm_total_frames() == 0)
        kernel_panic("nenhuma RAM utilizavel encontrada");

    /* Reserva o que ja esta em uso (sempre depois de informar a RAM livre). */
    pmm_reserve_region(0, ONE_MIB);                          /* BIOS, VGA, EBDA, ROMs */
    pmm_reserve_region((uintptr_t)_kernel_start, (uintptr_t)(_kernel_end - _kernel_start));
    pmm_reserve_region((uintptr_t)mbi, sizeof(*mbi));
    if (mbi->flags & MULTIBOOT_INFO_MMAP)
        pmm_reserve_region(mbi->mmap_addr, mbi->mmap_length);

    /* Heap: metade da RAM livre (ate 32 MiB), em um bloco fisico contiguo. */
    want = pmm_free_frames() / 2u;
    if (want > HEAP_MAX_FRAMES)
        want = HEAP_MAX_FRAMES;
    while (want >= HEAP_MIN_FRAMES) {
        addr = pmm_alloc_contiguous(want);
        if (addr != 0)
            break;
        want /= 2u;
    }
    if (addr == 0)
        kernel_panic("sem memoria contigua suficiente para o heap");
    heap_init((void *)(uintptr_t)addr, (size_t)want * PMM_FRAME_SIZE);

    info.ram_kb = pmm_total_frames() * (PMM_FRAME_SIZE / 1024u);
    info.kernel_start = (uint32_t)(uintptr_t)_kernel_start;
    info.kernel_end = (uint32_t)(uintptr_t)_kernel_end;
    info.heap_base = addr;
    info.heap_size = want * PMM_FRAME_SIZE;
}

void mm_get_info(struct mm_info *out)
{
    *out = info;
    out->free_kb = pmm_free_frames() * (PMM_FRAME_SIZE / 1024u);
}
