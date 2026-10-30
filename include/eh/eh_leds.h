/* Nine-LED colour model: each LED follows the frame colour near the rim at
 * its angular position (LED 0 at the top, then clockwise), scaled by a global
 * brightness cap because WS2812 current at full white adds up quickly. */
#ifndef EH_LEDS_H
#define EH_LEDS_H

#include <stdint.h>

#include "eh_types.h"

#define EH_LED_DEFAULT_MAX 64 /* 25% of full scale */

void eh_leds_from_frame(const eh_rgb565 *fb, eh_rgb out[EH_LED_COUNT], uint8_t max_brightness);

#endif
