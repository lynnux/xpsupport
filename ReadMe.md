## Usage
Make sure use gnu toolchain (it's OK with msvc, but need more steps not described here).

add this to your Cargo.toml:
```
[dependencies]
xpsupport-sys = "0.1"
```
and this to your crate root:
```
extern crate xpsupport_sys;
```
add `xpsupport_sys::init();` to `fn main` like:
```
fn main()
{
    xpsupport_sys::init();
	//...
}	
```
then `cargo build`, you will see error like: `ld: cannot find -lxpsupport`, this relate to a cargo bug [3674](https://github.com/rust-lang/cargo/issues/3674).

please find `xpsupport-xxxxxxxxxxx.dll` in `target\debug\deps`, copy the file and rename to `xpsupport.dll` in the same directory,

rebuild, this should be OK now, copy `xpsupport-xxxxxxxxxxx.dll` to the location where main exe exist.

## How does it work?

Actually, `xpsupport_sys::init();` do nothing. The main idea is that `DllMain` in `xpsupport.dll` running before `fn main`, and will hook `GetProcAddress`. When rust runtime library call `GetProcAddress` to get funtions like `AcquireSRWLockShared`, it will return the implemented code for XP and vista.

All implemented functions all list as below:

* AcquireSRWLockShared
* ReleaseSRWLockExclusive
* ReleaseSRWLockShared
* TryAcquireSRWLockExclusive
* TryAcquireSRWLockShared
* SleepConditionVariableSRW
* WakeAllConditionVariable
* WakeConditionVariable

## Testing Result
Only `mpsc::stress_recv_timeout_shared` seems deadlock, other tests from libstd/sync all passed! [Test code](https://github.com/lynnux/xpsupport-sys/tree/master/test)
