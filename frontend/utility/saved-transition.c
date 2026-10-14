#include "saved-transition.h"

#include <stddef.h>

void *saved_transition_find(const struct saved_transition_ops *ops, const char *name, const char *id)
{
	void *transition = NULL;

	if (name && *name)
		transition = ops->find_by_name(ops->param, name);

	/* A built-in transition saved under its name in another UI language:
	 * there is only one of each type, so its type id finds it. */
	if (!transition && id && *id)
		transition = ops->find_builtin_by_id(ops->param, id);

	return transition ? transition : ops->fallback;
}
