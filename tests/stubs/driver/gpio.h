#pragma once
using gpio_num_t = int;
extern int fakePins[64];
inline int gpio_set_level(gpio_num_t pin, int level) { fakePins[pin] = level; return 0; }
