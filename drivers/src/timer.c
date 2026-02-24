#include "timer.h"
#include "rcc.h"
#include "stm32f4xx.h"

/* ---- Bit definitions --------------------------------------------------- */
#define CR1_CEN     (1U << 0)
#define CR1_OPM     (1U << 3)   /* One-pulse mode */
#define DIER_UIE    (1U << 0)
#define SR_UIF      (1U << 0)

/* ---- Per-instance hardware descriptor ---------------------------------- */
typedef struct {
    TIM_TypeDef *regs;
    uint32_t     rcc_enr_bit;   /* Bit position in RCC->APB1ENR */
    IRQn_Type    irqn;
} timer_hw_t;

static const timer_hw_t hw[TIMER_COUNT] = {
    [TIMER_2] = { TIM2, RCC_APB1ENR_TIM2EN, TIM2_IRQn },
    [TIMER_3] = { TIM3, RCC_APB1ENR_TIM3EN, TIM3_IRQn },
    [TIMER_4] = { TIM4, RCC_APB1ENR_TIM4EN, TIM4_IRQn },
    [TIMER_5] = { TIM5, RCC_APB1ENR_TIM5EN, TIM5_IRQn },
};

/* ---- Callback storage -------------------------------------------------- */
static timer_callback_t callbacks[TIMER_COUNT];

/* ---- Helpers ----------------------------------------------------------- */

static inline TIM_TypeDef *get_regs(timer_instance_t tim)
{
    return hw[tim].regs;
}

/**
 * Return a pointer to the CCR register for the given channel.
 */
static volatile uint32_t *ccr_reg(TIM_TypeDef *regs, timer_channel_t ch)
{
    switch (ch) {
        case TIMER_CH1: return &regs->CCR1;
        case TIMER_CH2: return &regs->CCR2;
        case TIMER_CH3: return &regs->CCR3;
        default:        return &regs->CCR4;
    }
}

/* ---- Basic timer API --------------------------------------------------- */

void timer_init(timer_instance_t tim, uint32_t prescaler, uint32_t period)
{
    /* Enable peripheral clock */
    RCC->APB1ENR |= hw[tim].rcc_enr_bit;

    TIM_TypeDef *r = get_regs(tim);
    r->PSC = prescaler;
    r->ARR = period;
    r->CNT = 0;
}

void timer_start(timer_instance_t tim)
{
    get_regs(tim)->CR1 |= CR1_CEN;
}

void timer_stop(timer_instance_t tim)
{
    get_regs(tim)->CR1 &= ~CR1_CEN;
}

void timer_set_period(timer_instance_t tim, uint32_t period)
{
    get_regs(tim)->ARR = period;
}

void timer_register_callback(timer_instance_t tim, timer_callback_t cb)
{
    callbacks[tim] = cb;

    TIM_TypeDef *r = get_regs(tim);

    if (cb) {
        r->DIER |= DIER_UIE;
        NVIC_EnableIRQ(hw[tim].irqn);
    } else {
        r->DIER &= ~DIER_UIE;
        NVIC_DisableIRQ(hw[tim].irqn);
    }
}

/* ---- PWM API ----------------------------------------------------------- */

void timer_pwm_init(timer_instance_t tim, timer_channel_t ch,
                    uint32_t pwm_freq_hz, uint32_t steps)
{
    /* Enable peripheral clock */
    RCC->APB1ENR |= hw[tim].rcc_enr_bit;

    TIM_TypeDef *r = get_regs(tim);

    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    r->PSC = (timer_clk / (pwm_freq_hz * steps)) - 1;
    r->ARR = steps - 1;

    /*
     * Configure PWM mode 1 with preload on the selected channel.
     *
     * CCMR1 covers CH1 (bits 6:4 = OC1M, bit 3 = OC1PE)
     *                CH2 (bits 14:12 = OC2M, bit 11 = OC2PE)
     * CCMR2 covers CH3 (bits 6:4) and CH4 (bits 14:12) with same layout.
     */
    volatile uint32_t *ccmr;
    uint32_t shift;

    switch (ch) {
        case TIMER_CH1: ccmr = &r->CCMR1; shift = 0;  break;
        case TIMER_CH2: ccmr = &r->CCMR1; shift = 8;  break;
        case TIMER_CH3: ccmr = &r->CCMR2; shift = 0;  break;
        default:        ccmr = &r->CCMR2; shift = 8;  break;
    }

    /* OC mode = PWM mode 1 (110), preload enable */
    *ccmr |= (6U << (4 + shift));  /* OCxM = 110 */
    *ccmr |= (1U << (3 + shift));  /* OCxPE = 1  */

    /* Enable output on the channel (CCxE in CCER) */
    r->CCER |= (1U << (ch * 4));

    /* Initial duty cycle = 0 */
    *ccr_reg(r, ch) = 0;
}

void timer_pwm_set_duty(timer_instance_t tim, timer_channel_t ch,
                        uint32_t duty_percent)
{
    if (duty_percent > 100) duty_percent = 100;

    TIM_TypeDef *r = get_regs(tim);
    uint32_t arr = r->ARR;
    *ccr_reg(r, ch) = (arr * duty_percent) / 100;
}

/* ---- Microsecond delay (TIM5, 32-bit) --------------------------------- */

void timer_delay_us(uint32_t us)
{
    if (us == 0) return;

    /* Enable TIM5 clock */
    RCC->APB1ENR |= hw[TIMER_5].rcc_enr_bit;

    TIM_TypeDef *r = get_regs(TIMER_5);

    /* Stop timer, clear any pending flags */
    r->CR1 = 0;
    r->SR  = 0;

    /* 1 MHz tick -> 1 us per tick */
    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    r->PSC = (timer_clk / 1000000U) - 1;
    r->ARR = us - 1;
    r->CNT = 0;

    /* One-pulse mode + enable */
    r->CR1 = CR1_OPM | CR1_CEN;

    /* Busy-wait until UIF is set */
    while (!(r->SR & SR_UIF))
        ;

    r->SR  = 0;
    r->CR1 = 0;
}

/* ---- IRQ handlers (override weak aliases from startup) ----------------- */

static inline void timer_irq_common(timer_instance_t tim)
{
    TIM_TypeDef *r = get_regs(tim);
    if (r->SR & SR_UIF) {
        r->SR &= ~SR_UIF;
        if (callbacks[tim])
            callbacks[tim]();
    }
}

void TIM2_IRQHandler(void) { timer_irq_common(TIMER_2); }
void TIM3_IRQHandler(void) { timer_irq_common(TIMER_3); }
void TIM4_IRQHandler(void) { timer_irq_common(TIMER_4); }
void TIM5_IRQHandler(void) { timer_irq_common(TIMER_5); }
