/* sizeof the public obs-data enums. The handles themselves are opaque. */
#include <stddef.h>

#include "obs-data.h"

size_t oracle_obs_data_type_size(void);
size_t oracle_obs_data_number_type_size(void);

size_t oracle_obs_data_type_size(void)
{
	return sizeof(enum obs_data_type);
}

size_t oracle_obs_data_number_type_size(void)
{
	return sizeof(enum obs_data_number_type);
}
