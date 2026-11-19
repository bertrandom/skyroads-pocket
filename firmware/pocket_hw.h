/* The SkyRoads SoC as the firmware sees it (see ../rtl/sky_soc.v).
 *
 * Address map (CPU side):
 *   0x1000_0000  SDRAM, 64 MiB, cached.  The firmware is loaded at its start
 *                (data slot 0), the game data pack at +8 MiB (data slot 1).
 *   0x2000_0000  save RAM, 1 KiB (the nonvolatile data slot 2: skyroads.cfg)
 *   0x4000_0000  registers (below)
 *   VRAM_BASE    64 KiB window onto the video RAM, anywhere in the SDRAM
 *                range: the firmware points it at g_mem + 0xA0000, the DOS
 *                VGA memory, so the game draws straight onto the screen.
 *
 * Built with POCKET_HOST the same names are functions (host_hal.c) so the
 * platform layer can be tested natively. */
#ifndef POCKET_HW_H
#define POCKET_HW_H
#include <stdint.h>

#define SDRAM_BASE     0x10000000u
#define PACK_BASE      0x10800000u   /* data slot 1: the game files */
#define STAGE_BASE     0x12000000u   /* save states: the blob, both ways (32 MiB) */
#define STAGE_SIZE     0x01000000u
#define EXC_BASE       0x13000000u   /* the game, while the music player runs */
#define SAVE_BASE      0x20000000u
#define SAVE_SIZE      1024u
#define REG_BASE       0x40000000u

/* registers, offsets from REG_BASE */
#define R_ID           0x000   /* RO 'SKY1' */
#define R_SYS_HZ       0x004   /* RO system clock in Hz */
#define R_CYCLES_LO    0x008   /* RO free running cycle counter; reading LO */
#define R_CYCLES_HI    0x00c   /*    latches HI */
#define R_TIMER_PERIOD 0x010   /* RW timer interrupt period in cycles, 0 off */
#define R_TIMER_ACK    0x014   /* W  any value: clear the timer interrupt */
#define R_RETRACE      0x018   /* RO counts the game's 70 Hz vertical retraces */
#define R_KEYS         0x01c   /* RO controller 1 buttons (APF cont1_key) */
#define R_VRAM_BASE    0x020   /* RW CPU address of the VRAM window */
#define R_DEBUG_PUTC   0x024   /* W  simulation: print a character */
#define R_SIM_EXIT     0x028   /* W  simulation: end with this exit code */
#define R_OPL          0x030   /* W  OPL2 write: reg << 8 | value (queued) */
#define R_OPL_FREE     0x034   /* RO free entries in the OPL queue */
#define R_PCM          0x040   /* W  8-bit unsigned sample (queued) */
#define R_PCM_FREE     0x044   /* RO free entries in the sample queue */
#define R_PCM_PERIOD   0x048   /* RW cycles per sample */
#define R_PCM_FLUSH    0x04c   /* W  drop the queued samples */
#define R_SPK_HALF     0x050   /* RW PC speaker: half period in cycles */
#define R_SPK_ON       0x054   /* RW PC speaker gate */
#define R_VOLUME       0x058   /* RW master volume, 256 = unity */
#define R_SS_STATUS    0x060   /* RO bit 0: save state requested, bit 1: load requested */
#define R_SS_SIZE      0x064   /* RW the save state's size in bytes (before SS_CTRL bit 0) */
#define R_SS_CTRL      0x068   /* W  bit 0: saved (STAGE_BASE, SS_SIZE), bit 1: save failed,
                                         bit 2: load request seen */
#define R_SETTINGS     0x06c   /* RO Core Settings (interact.json): bit 0 the music player */
#define R_PALETTE      0x400   /* W  256 x 0x00RRGGBB */

/* APF cont1_key bits */
#define KEY_UP     (1u << 0)
#define KEY_DOWN   (1u << 1)
#define KEY_LEFT   (1u << 2)
#define KEY_RIGHT  (1u << 3)
#define KEY_A      (1u << 4)
#define KEY_B      (1u << 5)
#define KEY_X      (1u << 6)
#define KEY_Y      (1u << 7)
#define KEY_L1     (1u << 8)
#define KEY_R1     (1u << 9)
#define KEY_SELECT (1u << 14)
#define KEY_START  (1u << 15)

#ifdef POCKET_HOST
uint32_t hw_rd(uint32_t reg);
void     hw_wr(uint32_t reg, uint32_t val);
uint8_t *hw_pack(void);
uint8_t *hw_save(void);
#else
static inline uint32_t hw_rd(uint32_t reg) { return *(volatile uint32_t *)(uintptr_t)(REG_BASE + reg); }
static inline void hw_wr(uint32_t reg, uint32_t val) { *(volatile uint32_t *)(uintptr_t)(REG_BASE + reg) = val; }
static inline uint8_t *hw_pack(void) { return (uint8_t *)(uintptr_t)PACK_BASE; }
static inline uint8_t *hw_save(void) { return (uint8_t *)(uintptr_t)SAVE_BASE; }
#endif

#endif
