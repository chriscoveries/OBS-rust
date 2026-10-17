/* Test-only oracle: the original libobs/util/platform-cocoa.m, unmodified,
 * with its global symbols renamed to oracle_* so it can link next to the
 * Rust implementation. Compiled on macOS only, with -fobjc-arc.
 *
 * cfstr_copy_dstr returns NULL from a bool. The build suppresses
 * -Wint-conversion for this file alone. That is the original source.
 */
#define os_gettime_ns oracle_os_gettime_ns
#define os_get_config_path oracle_os_get_config_path
#define os_get_config_path_ptr oracle_os_get_config_path_ptr
#define os_get_program_data_path oracle_os_get_program_data_path
#define os_get_program_data_path_ptr oracle_os_get_program_data_path_ptr
#define os_get_executable_path_ptr oracle_os_get_executable_path_ptr
#define os_cpu_usage_info_start oracle_os_cpu_usage_info_start
#define os_cpu_usage_info_query oracle_os_cpu_usage_info_query
#define os_cpu_usage_info_destroy oracle_os_cpu_usage_info_destroy
#define os_request_high_performance oracle_os_request_high_performance
#define os_end_high_performance oracle_os_end_high_performance
#define os_inhibit_sleep_create oracle_os_inhibit_sleep_create
#define os_inhibit_sleep_set_active oracle_os_inhibit_sleep_set_active
#define os_inhibit_sleep_destroy oracle_os_inhibit_sleep_destroy
#define os_get_emulation_status oracle_os_get_emulation_status
#define os_get_physical_cores oracle_os_get_physical_cores
#define os_get_logical_cores oracle_os_get_logical_cores
#define os_get_sys_free_size oracle_os_get_sys_free_size
#define os_get_free_space oracle_os_get_free_space
#define os_get_free_disk_space oracle_os_get_free_disk_space
#define os_get_sys_total_size oracle_os_get_sys_total_size
#define os_get_proc_memory_usage oracle_os_get_proc_memory_usage
#define os_get_proc_resident_size oracle_os_get_proc_resident_size
#define os_get_proc_virtual_size oracle_os_get_proc_virtual_size
#define cfstr_copy_cstr oracle_cfstr_copy_cstr
#define cfstr_copy_dstr oracle_cfstr_copy_dstr

#include "util/platform-cocoa.m.h"
