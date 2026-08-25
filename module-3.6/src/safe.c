#include "safe.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buzzer.h"

#define MAX_CODE_LENGTH 8U
#define MAX_ATTEMPTS    3U

static const uint8_t correct_code[] = {3, 7, 2, 5};

#define CODE_LENGTH (sizeof(correct_code) / sizeof(correct_code[0]))

_Static_assert(CODE_LENGTH > 0U, "PIN code must contain at least one digit");
_Static_assert(CODE_LENGTH <= MAX_CODE_LENGTH, "PIN code cannot exceed 8 digits");

typedef enum
{
    INPUT_DIRECTION_NONE,
    INPUT_DIRECTION_CLOCKWISE,
    INPUT_DIRECTION_COUNTERCLOCKWISE
} input_direction_t;

static safe_state_t safe_state;
static uint8_t entered_code[MAX_CODE_LENGTH];
static size_t current_position;
static uint8_t current_digit;
static uint8_t attempts_remaining;
static input_direction_t active_direction;

static input_direction_t event_to_direction(encoder_event_t event)
{
    return (event == ENCODER_EVENT_CLOCKWISE)
               ? INPUT_DIRECTION_CLOCKWISE
               : INPUT_DIRECTION_COUNTERCLOCKWISE;
}

static void begin_attempt(void)
{
    memset(entered_code, 0, sizeof(entered_code));
    current_position = 0U;
    current_digit = 0U;
    active_direction = INPUT_DIRECTION_NONE;

    printf("Enter PIN: 0");
    fflush(stdout);
}

static void lock_safe(void)
{
    safe_state = SAFE_STATE_LOCKED;
    printf("Safe locked. Restart required.\n");
    buzzer_play_alarm();
}

static void consume_failed_attempt(const char *reason)
{
    if (attempts_remaining > 0U)
    {
        --attempts_remaining;
    }

    printf("%s Attempts remaining: %u\n", reason, (unsigned)attempts_remaining);

    if (attempts_remaining == 0U)
    {
        lock_safe();
        return;
    }

    begin_attempt();
}

static void verify_code(void)
{
    if (memcmp(entered_code, correct_code, CODE_LENGTH) == 0)
    {
        safe_state = SAFE_STATE_OPEN;
        printf("Access granted\n");
        buzzer_play_success();
        return;
    }

    consume_failed_attempt("Incorrect PIN.");
}

void safe_init(void)
{
    safe_state = SAFE_STATE_INPUT;
    attempts_remaining = MAX_ATTEMPTS;
    begin_attempt();
}

void safe_process_event(encoder_event_t event)
{
    if ((safe_state != SAFE_STATE_INPUT) || (event == ENCODER_EVENT_NONE))
    {
        return;
    }

    if (event == ENCODER_EVENT_BUTTON)
    {
        printf("\n");
        consume_failed_attempt("Attempt reset by button.");
        return;
    }

    const input_direction_t event_direction = event_to_direction(event);

    if (active_direction == INPUT_DIRECTION_NONE)
    {
        active_direction = event_direction;
        current_digit = 1U;
        printf("\b%u", (unsigned)current_digit);
        fflush(stdout);
        return;
    }

    if (event_direction == active_direction)
    {
        current_digit = (uint8_t)((current_digit + 1U) % 10U);
        printf("\b%u", (unsigned)current_digit);
        fflush(stdout);
        return;
    }

    entered_code[current_position] = current_digit;
    active_direction = event_direction;

    if (current_position == (CODE_LENGTH - 1U))
    {
        printf("\n");
        verify_code();
        return;
    }

    ++current_position;
    current_digit = 0U;
    printf(" 0");
    fflush(stdout);
}
