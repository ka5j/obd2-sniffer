/*
 * Short critical sections: code that must not be interrupted.
 *
 * The previous interrupt state is saved and restored rather than blindly
 * re-enabling interrupts, so critical sections can nest safely.
 */
#ifndef CRITICAL_H
#define CRITICAL_H

#include <stdint.h>

#include "stm32f4xx.h"

static inline uint32_t critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

#endif /* CRITICAL_H */
