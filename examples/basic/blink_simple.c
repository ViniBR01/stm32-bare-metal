#include "led2.h"
#include "systick.h"

int main(void)
{
    led2_init();

    while(1)
    {
        led2_toggle();
	    systick_delay_ms(500);
    }
}
