## Usage
Make sure use gnu toolchain (help needed for msvc, see below)

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

for msvc: in `deps`, find `xpsupport-xxxxxxxxxxx.dll.lib`, copy and rename to `xpsupport.lib`, then rebuild should be OK, it will run with `xpsupport-xxxxxxxxxxx.dll`. But actually it can't run in xp, see [34407](https://github.com/rust-lang/rust/issues/34407), although `rustc main.rs -C link-args="/subsystem:console,5.01"` maybe work, I don't know how to use this in cargo, so the easy solution is to use gnu toolchain.

## How does it work?

Actually, `xpsupport_sys::init();` do nothing. The main idea is that `DllMain` in `xpsupport.dll` running before `fn main`, and will hook `GetProcAddress`. When rust runtime library call `GetProcAddress` to get funtions like `AcquireSRWLockShared`, it will return the implemented code for XP and vista.

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
so you may consider [parking_lot](https://github.com/Amanieu/parking_lot) crate as the sync library, it's support XP.

## Tips
- for rust stable toolchain: the `xpsupport.dll` will bigger than nightly build, cause it can't build with no std.

