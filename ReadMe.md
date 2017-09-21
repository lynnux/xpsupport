## Usage

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
for GNU toolchain: run `cargo build`, you will get error like: `ld: cannot find -lxpsupport`, goto `target\debug\deps` directory, find `xpsupport-xxxxxxxxxxx.dll`, copy and rename to `xpsupport.dll` in the same directory, rebuild, this should be OK now, copy `xpsupport-xxxxxxxxxxx.dll` to the location where main exe exist.

for MSVC toolchain: build with command: `cargo rustc -- -C link-args="/subsystem:console,5.01"` or `cargo rustc --release -- -C link-args="/subsystem:console,5.01"`, goto directory `deps`, find `xpsupport-xxxxxxxxxxx.dll.lib`, copy and rename to `xpsupport.lib`, then rebuild

## How does it work?

Actually, `xpsupport_sys::init();` do nothing. The main idea is that `DllMain` in `xpsupport.dll` running before `fn main`, and will hook `GetProcAddress`. When rust runtime library call `GetProcAddress` to get funtions like `AcquireSRWLockShared`, it will return the implemented code for XP and vista (mainly code are from wine project, I ported to support mingw/msvc and x64 platform)

All implemented functions are below:

* AcquireSRWLockShared
* ReleaseSRWLockExclusive
* ReleaseSRWLockShared
* TryAcquireSRWLockExclusive
* TryAcquireSRWLockShared
* SleepConditionVariableSRW
* WakeAllConditionVariable
* WakeConditionVariable

## Testing Result
Only `mpsc::stress_recv_timeout_shared` seems deadlock, other tests from libstd/sync all passed! [Test code](https://github.com/lynnux/xpsupport-sys/tree/master/test),
so you may consider [parking_lot](https://github.com/Amanieu/parking_lot) crate as the sync library or [spin](https://github.com/mvdnes/spin-rs)(only works on nightly), they both support XP.

## Tips
- for rust stable toolchain: the `xpsupport.dll` will bigger than nightly build, cause it can't build with no std.

