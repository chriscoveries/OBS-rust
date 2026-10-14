/* The mapped-image half of os_is_obs_plugin (libobs/util/platform-windows.c).
 * A truncated image faults on the reads. MSVC __try treats that as a plugin,
 * which is what the C file does. MinGW has no __try; the same checks run
 * without the handler.
 */
#include <windows.h>

static int image_has_obs_export(const BYTE *base, const IMAGE_NT_HEADERS *nt_headers)
{
	const IMAGE_DATA_DIRECTORY *data_dir;
	const IMAGE_SECTION_HEADER *section;
	const IMAGE_SECTION_HEADER *last_section;
	const IMAGE_SECTION_HEADER *export_section;
	const IMAGE_EXPORT_DIRECTORY *export_dir;
	const DWORD *names;
	DWORD i;
	DWORD j;

	data_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (data_dir->Size == 0)
		return 0;

	section = IMAGE_FIRST_SECTION(nt_headers);
	last_section = section;

	for (i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
		if (section->VirtualAddress <= data_dir->VirtualAddress) {
			last_section = section;
			section++;
			continue;
		}
		break;
	}

	if (last_section->VirtualAddress > data_dir->VirtualAddress ||
	    section->VirtualAddress <= data_dir->VirtualAddress)
		return 0;

	export_section = last_section;
	export_dir = (const IMAGE_EXPORT_DIRECTORY *)(base + data_dir->VirtualAddress - export_section->VirtualAddress +
						      export_section->PointerToRawData);
	if (export_dir->NumberOfNames == 0)
		return 0;

	names = (const DWORD *)(base + export_dir->AddressOfNames - export_section->VirtualAddress +
				export_section->PointerToRawData);

	for (j = 0; j < export_dir->NumberOfNames; j++) {
		const char *name = (const char *)base + names[j] - export_section->VirtualAddress +
				   export_section->PointerToRawData;
		if (strcmp(name, "obs_module_load") == 0)
			return 1;
	}
	return 0;
}

static int image_is_obs_plugin(const void *base)
{
	const IMAGE_DOS_HEADER *dos;
	const IMAGE_NT_HEADERS *nt;

	dos = (const IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return 0;

	nt = (const IMAGE_NT_HEADERS *)((const BYTE *)dos + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return 0;

	return image_has_obs_export((const BYTE *)base, nt);
}

int obs_util_image_is_obs_plugin(const void *base)
{
#if defined(_MSC_VER)
	__try {
		return image_is_obs_plugin(base);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 1;
	}
#else
	return image_is_obs_plugin(base);
#endif
}
