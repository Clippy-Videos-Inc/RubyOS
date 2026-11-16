/* kernel/syscall.c - despacho das chamadas de sistema do anel 3 (ver syscall.h) */
#include "syscall.h"
#include "process.h"
#include "terminal.h"

/* true se [addr, addr+len) estiver inteiramente dentro da (unica) pagina de usuario do
 * processo chamador. Nao ha copy_from_user/verificacao de paginas de verdade nesta
 * versao -- so essa checagem de limites -- mas ja impede um processo de anel 3 de usar
 * SYS_WRITE para ler memoria do kernel ou de outro processo. */
static bool in_user_page(const struct process *p, uint32_t addr, uint32_t len)
{
    uint32_t base;
    uint32_t limit;
    uint32_t end;

    if (!p->user_mode || len == 0)
        return false;
    base = p->user_page_addr;
    limit = base + p->user_page_size;
    end = addr + len;
    if (end < addr)                /* estouro */
        return false;
    return addr >= base && end <= limit;
}

void syscall_dispatch(struct regs *r)
{
    const struct process *p = process_current();

    switch (r->eax) {
    case SYS_EXIT:
        process_exit();                        /* nunca retorna */
        break;

    case SYS_WRITE:
        if (in_user_page(p, r->ebx, r->ecx)) {
            const char *s = (const char *)(uintptr_t)r->ebx;
            uint32_t i;

            for (i = 0; i < r->ecx; i++)
                terminal_putc(s[i]);
            r->eax = r->ecx;
        } else {
            r->eax = (uint32_t)-1;
        }
        break;

    case SYS_YIELD:
        process_yield();
        r->eax = 0;
        break;

    default:
        r->eax = (uint32_t)-1;
        break;
    }
}
