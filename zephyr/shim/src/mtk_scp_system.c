#include "panic.h"
#include "system.h"

const char *system_get_chip_vendor(void)
{
	return "Mediatek";
}

const char *system_get_chip_name(void)
{
	return "MT81xx SCP";
}

const char *system_get_chip_revision(void)
{
	return "";
}

void system_reset(int flags)
{
	while (1); /* TODO: reset not supported */
	__builtin_unreachable();
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
}

uint32_t crec_flash_get_protect(void)
{
	return 0;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}
