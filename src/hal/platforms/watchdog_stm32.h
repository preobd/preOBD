/*
 * watchdog_stm32.h - STM32F4 independent watchdog (IWDG)
 * Part of the preOBD Hardware Abstraction Layer
 *
 * The IWDG runs from the internal LSI oscillator, which the F4 datasheet
 * allows anywhere from 17 to 47 kHz. Programmed for the nominal 32 kHz, a
 * "2 s" timeout could fire after 1.4 s. So the first enable measures the LSI
 * against TIM5 (whose channel 4 can capture the LSI directly) and sizes the
 * reload from that, plus 10 % for drift with temperature: the watchdog never
 * fires before the timeout the policy asked for.
 *
 * Once started the IWDG can't be stopped, only fed or retimed, which is
 * exactly the policy (watchdog_policy.h): always on in every mode. It has no
 * early-warning interrupt, so the Teensy 4 "hung at PC" trace (#542) has no
 * equivalent here.
 */

#ifndef HAL_WATCHDOG_STM32_H
#define HAL_WATCHDOG_STM32_H

#include <Arduino.h>

namespace hal {

namespace detail {
    // Measured LSI frequency in Hz; 0 until the first enable.
    inline uint32_t& lsiHz() { static uint32_t hz = 0; return hz; }

    // LSI frequency from TIM5 input capture: the timer counts its own clock
    // between captures taken every 8 LSI periods, averaged over 8 intervals.
    // ~2 ms. TIM5 is left reset afterwards for whatever uses it next.
    inline uint32_t measureLsi() {
        RCC->CSR |= RCC_CSR_LSION;
        uint32_t t0 = millis();
        while (!(RCC->CSR & RCC_CSR_LSIRDY)) {
            if (millis() - t0 > 5) return 0;
        }

        // TIM5 runs at PCLK1, doubled when APB1 is divided (it is: 42 MHz).
        uint32_t timClk = HAL_RCC_GetPCLK1Freq();
        if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) timClk *= 2;

        __HAL_RCC_TIM5_CLK_ENABLE();
        TIM5->CR1 = 0;
        TIM5->PSC = 0;
        TIM5->ARR = 0xFFFFFFFFUL;
        TIM5->OR = TIM_OR_TI4_RMP_0;                         // TI4 <- LSI
        TIM5->CCMR2 = TIM_CCMR2_CC4S_0 | TIM_CCMR2_IC4PSC;   // IC4 on TI4, every 8th edge
        TIM5->CCER = TIM_CCER_CC4E;
        TIM5->EGR = TIM_EGR_UG;
        TIM5->SR = 0;
        TIM5->CR1 = TIM_CR1_CEN;

        uint32_t first = 0, last = 0;
        uint8_t got = 0;
        t0 = millis();
        while (got < 9) {
            if (TIM5->SR & TIM_SR_CC4IF) {
                last = TIM5->CCR4;   // reading clears CC4IF
                if (got == 0) first = last;
                got++;
            }
            if (millis() - t0 > 20) break;
        }
        __HAL_RCC_TIM5_FORCE_RESET();
        __HAL_RCC_TIM5_RELEASE_RESET();
        __HAL_RCC_TIM5_CLK_DISABLE();

        if (got < 9 || last == first) return 0;
        // 8 intervals x 8 LSI periods each
        return (uint32_t)(((uint64_t)timClk * 64) / (uint32_t)(last - first));
    }
}

inline void watchdogEnable(uint16_t timeout_ms) {
    uint32_t& lsi = detail::lsiHz();
    if (lsi == 0) {
        lsi = detail::measureLsi();
        // Plausible range from the datasheet; outside it, assume the fastest.
        if (lsi < 17000 || lsi > 47000) lsi = 47000;
    }
    // Size for an LSI 10 % faster than measured, so drift can only lengthen it.
    const uint64_t ticks = (uint64_t)timeout_ms * (lsi + lsi / 10) / 1000;

    // Smallest prescaler (/4 << pr) whose 12-bit reload holds the timeout.
    uint8_t pr = 0;
    while (pr < 6 && (ticks >> (2 + pr)) > 0xFFF) pr++;
    uint32_t rl = (uint32_t)(ticks >> (2 + pr));
    if (rl > 0xFFF) rl = 0xFFF;
    if (rl == 0) rl = 1;

    IWDG->KR = 0xCCCC;           // start (a no-op when already running)
    IWDG->KR = 0x5555;           // unlock PR/RLR
    // A previous update must have landed before PR/RLR can be written again.
    const uint32_t t0 = millis();
    while (IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
        if (millis() - t0 > 50) break;
    }
    IWDG->PR = pr;
    IWDG->RLR = rl;
    IWDG->KR = 0xAAAA;           // reload, which also relocks
}

inline void watchdogReset() {
    IWDG->KR = 0xAAAA;
}

inline void watchdogDisable() {
    // The IWDG can't be stopped once started; the policy never asks to.
}

} // namespace hal

#endif // HAL_WATCHDOG_STM32_H
