/* libc minima do RubyOS - setjmp.h (implementado em kernel/libc/setjmp.asm) */
#ifndef RUBYOS_LIBC_SETJMP_H
#define RUBYOS_LIBC_SETJMP_H

/* ebx, esi, edi, ebp, esp, eip */
typedef unsigned int jmp_buf[6];

int  setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
