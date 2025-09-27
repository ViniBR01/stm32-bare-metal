#include "led2.h"

int main(void)
{
    led2_init();

    while(1)
    {
        led2_toggle();
        for(int i = 0; i < 2000000; i++) {}
    }
}
