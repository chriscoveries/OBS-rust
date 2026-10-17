#include <jansson.h>

/* json_decref is a static inline in jansson.h. libobs already links
 * jansson, so this glue must not pull a second copy of the library.
 * json_delete stays unresolved until the final link. */
void obs_data_json_decref(json_t *json)
{
	json_decref(json);
}
