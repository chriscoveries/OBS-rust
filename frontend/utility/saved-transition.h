#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Finds the transition a scene collection saved as its current one, kept
 * free of Qt so it can be unit tested. The collection stores the
 * transition's name ("current_transition") and, since built-in transitions
 * such as Cut and Fade are named in the UI language, its type id
 * ("current_transition_id"). */
struct saved_transition_ops {
	void *param;

	/* The transition with this name, or NULL. */
	void *(*find_by_name)(void *param, const char *name);

	/* The built-in transition (one without settings, like Cut and Fade) of
	 * this type id, or NULL. */
	void *(*find_builtin_by_id)(void *param, const char *id);

	/* Used when nothing matches (Fade). */
	void *fallback;
};

void *saved_transition_find(const struct saved_transition_ops *ops, const char *name, const char *id);

#ifdef __cplusplus
}
#endif
