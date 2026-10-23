/* Minimal nRF52840 app at 0x26000: request the UF2 bootloader (GPREGRET 0x57, OTAFIX DFU_MAGIC_UF2_RESET) and reset. */
#include <stdint.h>
void reset_handler(void) {
  *(volatile uint32_t *)0x4000051C = 0x57;          /* NRF_POWER->GPREGRET */
  __asm volatile ("dsb");
  *(volatile uint32_t *)0xE000ED0C = 0x05FA0004;    /* SCB->AIRCR = SYSRESETREQ */
  for (;;) {}
}
__attribute__((section(".vectors"), used)) const uint32_t vectors[2] = { 0x20040000, (uint32_t)reset_handler };
