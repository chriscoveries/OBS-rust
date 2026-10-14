#include "output-rescale.h"

/* The resolution box only matters when a filter is selected, and it is locked
 * together with the filter box. */
static void update_res(const struct output_rescale_ops *ops)
{
	ops->set_res_enabled(ops->param, ops->filter_enabled(ops->param) && ops->rescales(ops->param));
}

void output_rescale_filter_changed(const struct output_rescale_ops *ops)
{
	update_res(ops);
}

void output_rescale_set_locked(const struct output_rescale_ops *ops, bool locked)
{
	ops->set_filter_enabled(ops->param, !locked);
	update_res(ops);
}
