##Usage

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
after `cargo build`, you will found `xpsupport-xxxxxxxxxxx.dll` in `target\debug\deps`, please copy it to the main exe directory and rename it to `xpsupport.dll` , then you can run main exe with `xpsupport.dll` in XP!

##How does it work?

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

