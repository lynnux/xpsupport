## Usage

add this to your Cargo.toml:
```
[dependencies]
xpsupport = "0.2"
```
add this to your crate root:
```
extern crate xpsupport;
```
add `xpsupport::init();` to `fn main` like:
```
fn main()
{
    xpsupport::init();
	//...
}	
```
for MSVC toolchain, build with command: `cargo rustc -- -C link-args="/subsystem:console,5.01"` or `cargo rustc --release -- -C link-args="/subsystem:console,5.01"`.

## How does it work?

Idea of version `0.2` inspired by [ctor](https://github.com/mmastrac/rust-ctor). The `xpsupport::init()` runs before any initialization of Rust's stdlib (due to an issue of ctor [issue27](https://github.com/mmastrac/rust-ctor/issues/27), `xpsupport::init()` in `fn main` is just a place holder). It hooks `GetProcAddress`, and return the below functions when on XP or Vista (most code are from `wine` project):

* AcquireSRWLockShared
* ReleaseSRWLockExclusive
* ReleaseSRWLockShared
* TryAcquireSRWLockExclusive
* TryAcquireSRWLockShared
* SleepConditionVariableSRW
* WakeAllConditionVariable
* WakeConditionVariable

## Testing Result
[Testing code](https://github.com/lynnux/xpsupport-sys/tree/master/test) are from libstd/sync, only `mpsc::stress_recv_timeout_shared` seems deadlock, all other are passed through!
You may consider [parking_lot](https://github.com/Amanieu/parking_lot) crate as the sync library or [spin](https://github.com/mvdnes/spin-rs), they both support XP.

