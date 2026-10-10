#pragma once
#include <stdint.h>
namespace PulseTrace {
void sample(uint16_t raw, uint32_t ms);
void poll(uint32_t ms);
}
