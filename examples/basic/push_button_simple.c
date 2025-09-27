#include "led2.h"
#include "user_button.h"

int main(void)
{
    led2_init();
    user_button_init();

    while(1)
    {
        if (user_button_get_state() == 0) // Button pressed
        {
            led2_on();
        }
        else
        {
            led2_off();
        }
    }
}
