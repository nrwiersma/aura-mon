//
// Created by Nicholas Wiersma on 2026/03/17.
//

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include "crashhandler.h"
#include "auramon.h"

// Sentinel written to scratch[0] so the firmware can detect a crash reboot.
static constexpr uint32_t CRASH_MAGIC = 0xDEADC0DEu;

static constexpr uint32_t FLASH_SCAN_END = XIP_BASE + 0x400000; // 4MB

// Cortex-M basic exception frame pushed by hardware on fault entry:
//   [0] r0   [1] r1   [2] r2   [3] r3
//   [4] r12  [5] LR   [6] PC   [7] xPSR
static constexpr uint32_t FRAME_LR = 5u;
static constexpr uint32_t FRAME_PC = 6u;
static constexpr uint32_t FRAME_XPSR = 7;

static constexpr size_t MAX_BACKTRACE = 4u;

void restartReasonLog() {
    uint32_t magic = watchdog_hw->scratch[0];
    if (magic != CRASH_MAGIC) {
        if (watchdog_hw->scratch[4] != 0) {
            LOGE("**** WATCHDOG RESTART DETECTED ****");
        }
        return;
    }

    uint32_t scratch[8];
    for (uint32_t i = 0; i < 8; i++) {
        scratch[i] = watchdog_hw->scratch[i];
        watchdog_hw->scratch[i] = 0;
    }

    const uint32_t pc = scratch[1];
    const uint32_t lr = scratch[2];

    LOGE("**** CRASH DETECTED ****");
    LOGE("  PC: %08" PRIx32 " (fault location)", pc);
    LOGE("  LR: %08" PRIx32 " (return address)", lr);
    LOGE("  SP: %08" PRIx32, scratch[3]);
    for (uint32_t i = 0; i < MAX_BACKTRACE; i++) {
        LOGE("  BT[%lu]: 0x%08" PRIx32, static_cast<unsigned long>(i), scratch[4 + i]);
    }

    char hint[200];
    int  pos = snprintf(hint, sizeof(hint), "Use: addr2line -pfiaC -e firmware.elf 0x%08" PRIx32 " 0x%08" PRIx32,
                       pc, lr);
    for (uint32_t i = 0; i < MAX_BACKTRACE && pos < (int) sizeof(hint) - 1; i++) {
        if (scratch[4 + i] == 0) {
            continue;
        }
        pos += snprintf(hint + pos, sizeof(hint) - pos, " 0x%08" PRIx32, scratch[4 + i]);
    }
    LOGE("%s", hint);
}

static inline bool is_code_addr(uint32_t val) {
    uint32_t cleared = val & ~1u; // Clear Thumb bit
    return cleared >= XIP_BASE && cleared < FLASH_SCAN_END;
}

// Check that the pointer falls within SRAM and that the basic exception frame
// can be safely read.
static inline bool is_valid_sram_ptr(const uint32_t *ptr) {
    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    return addr % 4 == 0 && addr >= SRAM_BASE && addr + 32 <= SRAM_END;
}

// Called from the naked isr_hardfault with:
//   r0 = faulting stack frame pointer (MSP or PSP)
extern "C" __attribute__((noreturn, used)) void hard_fault_handler_c(uint32_t *frame) {
    // watchdog_reboot overwrites scratch[4:7], so call it first giving enough
    // time for this to complete.
    watchdog_reboot(0, 0, 10);

    if (!is_valid_sram_ptr(frame)) {
        watchdog_hw->scratch[0] = CRASH_MAGIC;
        watchdog_hw->scratch[1] = 0;
        watchdog_hw->scratch[2] = 0;
        watchdog_hw->scratch[3] = reinterpret_cast<uintptr_t>(frame);
        watchdog_hw->scratch[4] = 0;
        watchdog_hw->scratch[5] = 0;
        watchdog_hw->scratch[6] = 0;
        watchdog_hw->scratch[7] = 0;
        for (;;) {
            __asm volatile("nop");
        }
    }

    // Stack pointer is outside SRAM – likely a stack overflow or wild branch.
    // Zero PC/LR and record the raw SP value to aid diagnosis.
    const uint32_t pc = frame[FRAME_PC];
    const uint32_t lr = frame[FRAME_LR];
    const uint32_t align = (frame[FRAME_XPSR] & (1u << 9)) ? 1 : 0;
    uint32_t *     post_frame = frame + 8 + align;
    uint32_t       pre_fault_sp = reinterpret_cast<uintptr_t>(post_frame);

    watchdog_hw->scratch[0] = CRASH_MAGIC;
    watchdog_hw->scratch[1] = pc;
    watchdog_hw->scratch[2] = lr;
    watchdog_hw->scratch[3] = pre_fault_sp;

    // Scan up to 64 words above the post-fault SP to build a better backtrace.
    // The exception frame is 8 words with
    uint32_t *scan_start = post_frame;
    uint32_t *stack_top = reinterpret_cast<uint32_t *>(SRAM_END);
    uint32_t  bt_n = 0;

    for (uint32_t *p = scan_start; p < stack_top && p < scan_start + 64 && bt_n < MAX_BACKTRACE; p++) {
        uint32_t v = *p;
        if (is_code_addr(v) && v != pc && v != lr) {
            watchdog_hw->scratch[4 + bt_n] = v;
            bt_n++;
        }
    }
    for (uint32_t i = bt_n; i < MAX_BACKTRACE; i++) {
        watchdog_hw->scratch[4 + i] = 0;
    }

    for (;;) {
        __asm volatile("nop");
    }
}

// Naked HardFault handler – must not touch the stack itself.
//
// Checks bit 2 of EXC_RETURN (held in LR on exception entry):
//   0 → fault used MSP  →  load MSP into r0
//   1 → fault used PSP  →  load PSP into r0
// Then branches directly to hard_fault_handler_c with the frame pointer in r0.
extern "C" __attribute__((naked,used)) void isr_hardfault() {
    __asm volatile(
        "movs   r0, #4              \n"// r0 = 4 (bit mask for EXC_RETURN bit 2)
        "mov    r1, lr              \n"// r1 = EXC_RETURN
        "tst    r0, r1              \n"// test bit 2
        "beq    1f                  \n"// 0 -> MSP was active
        "mrs    r0, psp             \n"// 1 -> PSP was active
        "b      hard_fault_handler_c\n"
        "1:                         \n"
        "mrs    r0, msp             \n"
        "b      hard_fault_handler_c\n"
    );
}
