/* kernel/sysinfo.h - informacoes de CPU e controle de energia (usado pelas shells C e Ruby) */
#ifndef RUBYOS_SYSINFO_H
#define RUBYOS_SYSINFO_H

/* Preenche out (>= 49 bytes) com o nome comercial da CPU ou, sem ele, o fabricante. */
void sysinfo_cpu_name(char *out);
/* Preenche out (>= 13 bytes) com o fabricante (ex.: "GenuineIntel"). */
void sysinfo_cpu_vendor(char *out);

void system_reboot(void) __attribute__((noreturn));
void system_shutdown(void) __attribute__((noreturn));

#endif
