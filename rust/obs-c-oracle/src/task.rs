use core::ffi::c_void;

/// `struct os_task_queue`, opaque in `util/task.h`.
pub enum OracleTaskQueue {}

/// `os_task_t`.
pub type OracleOsTask = Option<unsafe extern "C" fn(*mut c_void)>;

unsafe extern "C" {
    pub fn oracle_os_task_queue_create() -> *mut OracleTaskQueue;
    pub fn oracle_os_task_queue_queue_task(
        tq: *mut OracleTaskQueue,
        task: OracleOsTask,
        param: *mut c_void,
    ) -> bool;
    pub fn oracle_os_task_queue_destroy(tq: *mut OracleTaskQueue);
    pub fn oracle_os_task_queue_wait(tq: *mut OracleTaskQueue) -> bool;
    pub fn oracle_os_task_queue_inside(tq: *mut OracleTaskQueue) -> bool;
}
