
#include "common.h"
#include "system.h"

/******************************************************************************/
/* Functions from board.h */
/******************************************************************************/

int board_id_is_mismatched(void)
{
	/* Anything else would disable the TPM functionality. */
	return 0;
}

int ap_is_on(void)
{
	return 0; // DEVICE_STATE_INIT
}

int chip_factory_mode(void)
{
	/* For fuzzing the interesting code is reached when not in factory mode. */
	return 0;
}

/******************************************************************************/
/* Functions from tpm2 */
/******************************************************************************/

void ExecuteCommand(unsigned int requestSize, unsigned char*request,
		unsigned int*responseSize, unsigned char **response) {
	/* The tpm2 stack because it is already covered by oss-fuzz. Do nothing. */
}

/******************************************************************************/
/* Functions from system.h */
/******************************************************************************/

int system_process_retry_counter(void)
{
	/* Not going to test roll back functionality. */
	return EC_SUCCESS;
}

int system_rolling_reboot_suspected(void)
{
	return 0;
}

enum system_image_copy_t system_get_ro_image_copy(void) {
	return SYSTEM_IMAGE_RW;
}
