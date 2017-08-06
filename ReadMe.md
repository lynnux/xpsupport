## Usage
Make sure use gnu toolchain (it's OK with msvc, but need more step not described here).

Add this to your Cargo.toml:
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
then `cargo build`, and this will fail you will see error like: `ld: cannot find -lxpsupport`, this due to a cargo bug [3674](https://github.com/rust-lang/cargo/issues/3674)

you need found `xpsupport-xxxxxxxxxxx.dll` in `target\debug\deps`, please copy and rename it to `xpsupport.dll` in the same directory, 

then rebuild, this should be OK, finally you can run main exe with `xpsupport-xxxxxxxxxxx.dll` in XP!

## How does it work?

Actually, `xpsupport_sys::init();` do nothing at all. The main idea it that `DllMain` in `xpsupport.dll` running before `fn main`, the `DllMain` will hook `GetProcAddress`, and when rust runtime library call `GetProcAddress` to get like `AcquireSRWLockShared`, it will return a funtions which not implemented on XP.

All implemented functions all list as below:

* AcquireSRWLockShared
* ReleaseSRWLockExclusive
* ReleaseSRWLockShared
* TryAcquireSRWLockExclusive
* TryAcquireSRWLockShared
* SleepConditionVariableSRW
* WakeAllConditionVariable
* WakeConditionVariable

## Drawbacks

Althrough it can work, but has some performance issues. So if you want to support XP, please avoid use RWLock, Condvar, Barrier, and mpsc::* like channel/sync_channel, you can try another replacement library like [parking_lot](https://github.com/Amanieu/parking_lot), which implemeted Mutex/Condvar/RwLock etc.
