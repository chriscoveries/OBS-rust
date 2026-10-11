/* Test-only oracle: libobs/media-io/audio-math.h is header-only (static
 * inline), so this file exposes non-inline oracle_* wrappers that call the
 * real header functions. */
#include "media-io/audio-math.h"

float oracle_mul_to_db(float mul)
{
	return mul_to_db(mul);
}

float oracle_db_to_mul(float db)
{
	return db_to_mul(db);
}
