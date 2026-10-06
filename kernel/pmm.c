/* kernel/pmm.c - bitmap de quadros fisicos (ver pmm.h) */
#include "pmm.h"

static uint32_t bitmap[PMM_MAX_FRAMES / 32u];   /* 128 KiB no .bss */
static uint32_t total_frames;
static uint32_t free_frames;
static uint32_t frame_limit;                    /* 1 + maior quadro ja informado como RAM */

static bool test_used(uint32_t f)
{
    return (bitmap[f >> 5] >> (f & 31u)) & 1u;
}

static void set_used(uint32_t f)
{
    bitmap[f >> 5] |= 1u << (f & 31u);
}

static void set_free(uint32_t f)
{
    bitmap[f >> 5] &= ~(1u << (f & 31u));
}

void pmm_reset(void)
{
    uint32_t i;

    for (i = 0; i < PMM_MAX_FRAMES / 32u; i++)
        bitmap[i] = 0xFFFFFFFFu;
    total_frames = 0;
    free_frames = 0;
    frame_limit = 0;
}

void pmm_add_free_region(uint64_t addr, uint64_t len)
{
    uint64_t end;
    uint64_t first;
    uint64_t last;
    uint64_t f;

    if (len == 0 || addr >= (1ull << 32))
        return;
    end = addr + len;
    if (end < addr || end > (1ull << 32))
        end = 1ull << 32;

    first = (addr + PMM_FRAME_SIZE - 1u) / PMM_FRAME_SIZE;   /* alinha para dentro */
    last = end / PMM_FRAME_SIZE;                             /* exclusivo */
    for (f = first; f < last; f++) {
        if (test_used((uint32_t)f)) {                        /* so conta uma vez */
            set_free((uint32_t)f);
            total_frames++;
            free_frames++;
        }
    }
    if (last > frame_limit)
        frame_limit = (uint32_t)last;
}

void pmm_reserve_region(uint64_t addr, uint64_t len)
{
    uint64_t end;
    uint64_t first;
    uint64_t last;
    uint64_t f;

    if (len == 0 || addr >= (1ull << 32))
        return;
    end = addr + len;
    if (end < addr || end > (1ull << 32))
        end = 1ull << 32;

    first = addr / PMM_FRAME_SIZE;                           /* alinha para fora */
    last = (end + PMM_FRAME_SIZE - 1u) / PMM_FRAME_SIZE;
    for (f = first; f < last; f++) {
        if (!test_used((uint32_t)f)) {
            set_used((uint32_t)f);
            free_frames--;
        }
    }
}

uint32_t pmm_alloc_contiguous(uint32_t frames)
{
    uint32_t run = 0;
    uint32_t start = 0;
    uint32_t f;

    if (frames == 0 || frames > free_frames)
        return 0;
    for (f = 1; f < frame_limit; f++) {                      /* quadro 0 nunca e entregue */
        if (test_used(f)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = f;
        if (++run == frames) {
            uint32_t i;
            for (i = start; i < start + frames; i++)
                set_used(i);
            free_frames -= frames;
            return start * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

bool pmm_free_region(uint32_t addr, uint32_t frames)
{
    uint32_t first;
    uint32_t i;

    if (frames == 0 || (addr % PMM_FRAME_SIZE) != 0)
        return false;
    first = addr / PMM_FRAME_SIZE;
    if (first == 0 || first >= frame_limit || frames > frame_limit - first)
        return false;
    for (i = first; i < first + frames; i++) {
        if (!test_used(i))
            return false;                                    /* double free */
    }
    for (i = first; i < first + frames; i++)
        set_free(i);
    free_frames += frames;
    return true;
}

uint32_t pmm_total_frames(void)
{
    return total_frames;
}

uint32_t pmm_free_frames(void)
{
    return free_frames;
}
