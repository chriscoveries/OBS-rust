/* The one libobs core symbol the Rust obs_property_button_clicked needs,
 * copied from obs.c so cargo tests link without the libobs core. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "obs-internal.h"

/* obs.c */
void *obs_obj_get_data(void *obj)
{
	struct obs_context_data *context = obj;
	if (!context)
		return NULL;

	return context->data;
}
