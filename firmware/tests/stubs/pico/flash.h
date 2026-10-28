#pragma once
#define PICO_OK 0
int flash_safe_execute(void (*op)(void *), void *arg, unsigned timeout);
