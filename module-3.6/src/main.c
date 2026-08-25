#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "encoder.h"
#include "safe.h"

#define ENCODER_POLL_PERIOD_MS 5U

void app_main(void)
{
    const TickType_t poll_delay_ticks =
        pdMS_TO_TICKS(ENCODER_POLL_PERIOD_MS) > 0
            ? pdMS_TO_TICKS(ENCODER_POLL_PERIOD_MS)
            : 1;

    encoder_init();
    buzzer_init();
    safe_init();

    while (true)
    {
        const encoder_event_t event = encoder_get_event();

        if (event != ENCODER_EVENT_NONE)
        {
            safe_process_event(event);
        }

        vTaskDelay(poll_delay_ticks);
    }
}
