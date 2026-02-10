#include "cli_commands.h"
#include "led2.h"
#include "printf.h"

// Command implementations
// NOTE: These are called from main loop context (not ISR),
// so we can safely use printf() which accumulates to buffer
static int cmd_led_on(const char* args) {
    (void)args;
    led2_on();
    printf("LED2 turned on\n");
    return 0;  // Success
}

static int cmd_led_off(const char* args) {
    (void)args;
    led2_off();
    printf("LED2 turned off\n");
    return 0;  // Success
}

static int cmd_led_toggle(const char* args) {
    (void)args;
    led2_toggle();
    printf("LED2 toggled\n");
    return 0;  // Success
}

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"led_on",     "Turn on LED2",      cmd_led_on},
    {"led_off",    "Turn off LED2",     cmd_led_off},
    {"led_toggle", "Toggle LED2 state", cmd_led_toggle},
};

const cli_command_t* cli_commands_get_table(size_t* num_commands) {
    *num_commands = sizeof(commands) / sizeof(commands[0]);
    return commands;
}

