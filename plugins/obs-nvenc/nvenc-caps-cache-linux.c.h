#include "build/obs-nvenc-caps-cache-linux.pch.h"

static bool read_line(const char *sys_root, const char *rel, char *buf, size_t size)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", sys_root, rel);

	FILE *f = fopen(path, "r");
	if (!f)
		return false;

	bool ok = fgets(buf, (int)size, f) != NULL;
	fclose(f);
	if (!ok)
		return false;

	buf[strcspn(buf, "\r\n")] = '\0';
	return *buf != '\0';
}

static bool append(char *key, size_t size, size_t *len, const char *text)
{
	size_t n = strlen(text);
	if (*len + n + 1 > size)
		return false;
	memcpy(key + *len, text, n + 1);
	*len += n;
	return true;
}

static int bus_order(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

char *nvenc_caps_cache_key(const char *sys_root, const char *test_exe)
{
	/* The kernel driver's version: the user-space driver libraries have to
	 * match it. */
	char version[64];
	if (!read_line(sys_root, "sys/module/nvidia/version", version, sizeof(version)))
		return NULL;

	struct stat st;
	if (!test_exe || stat(test_exe, &st) != 0)
		return NULL;

	char key[4096];
	size_t len = 0;
	char part[512]; /* a slot name is up to 255 bytes */
	snprintf(part, sizeof(part), "driver=%s test=%lld:%lld", version, (long long)st.st_size,
		 (long long)st.st_mtime);
	bool ok = append(key, sizeof(key), &len, part);

	/* Every NVIDIA display controller (PCI base class 0x03), in bus order. */
	char devices_dir[1024];
	snprintf(devices_dir, sizeof(devices_dir), "%s/sys/bus/pci/devices", sys_root);
	struct dirent **devices = NULL;
	int count = scandir(devices_dir, &devices, NULL, bus_order);
	for (int i = 0; i < count; i++) {
		const char *slot = devices[i]->d_name;
		char rel[512];
		char vendor[16];
		char class_id[16];
		char device[16];

		if (slot[0] != '.') {
			snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/vendor", slot);
			bool nvidia = read_line(sys_root, rel, vendor, sizeof(vendor)) && strcmp(vendor, "0x10de") == 0;
			snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/class", slot);
			bool display = read_line(sys_root, rel, class_id, sizeof(class_id)) &&
				       strncmp(class_id, "0x03", 4) == 0;
			snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/device", slot);
			if (nvidia && display && read_line(sys_root, rel, device, sizeof(device))) {
				snprintf(part, sizeof(part), " %s=%s", slot, device);
				ok = ok && append(key, sizeof(key), &len, part);
			}
		}
		free(devices[i]);
	}
	free(devices);

	return ok ? bstrdup(key) : NULL;
}
