// For one, you can build a #![no_std] library on stable, but not a binary. so disable no std on stable channel

#![cfg_attr(is_nightly, feature(lang_items, no_core))]
#![cfg_attr(is_nightly, no_std)] // dylib会跟主程序冲突
#![cfg_attr(is_nightly, no_core)]

#![allow(private_no_mangle_fns)]
#![allow(unused_macros)]
#![allow(unused_attributes)]
#![allow(dead_code)]

// https://github.com/rust-lang/rfcs/blob/master/text/1510-cdylib.md
#[no_mangle] pub extern "C" fn xpinit() {}

// 在没有实际代码里，msvc版本编译最NB，连dll import都没有。而gnu则把没有调用的minhook代码都加进来了

#[cfg_attr(is_nightly, lang = "eh_personality")]
extern fn eh_personality() {}

#[cfg_attr(is_nightly, no_mangle)]
extern fn __mulodi4(){}
#[cfg_attr(is_nightly, no_mangle)]
extern fn __muloti4(){}
#[cfg_attr(is_nightly, no_mangle)]
extern fn __multi3(){}
#[cfg_attr(is_nightly, no_mangle)]
extern fn __udivti3(){}
#[cfg_attr(is_nightly, no_mangle)]
extern fn __umodti3(){}

// copy from rust-master\src\libcore\marker.rs
#[cfg_attr(is_nightly, lang = "sized")]
trait Sized {
    // Empty.
}

#[cfg_attr(is_nightly, lang = "copy")]
trait Copy {
    // Empty.
}

#[cfg_attr(is_nightly, lang = "freeze")]
trait Freeze {
    // Empty.
}

macro_rules! if_gnu {
    ($($i:item)*) => ($(
        #[cfg(target_env = "gnu")]
        $i
    )*)
}
macro_rules! if_msvc {
    ($($i:item)*) => ($(
        #[cfg(target_env = "msvc")]
        $i
    )*)
}

if_gnu!{
    #[cfg_attr(is_nightly, lang = "eh_unwind_resume")]
    fn eh_unwind_resume(){}
    
    #[cfg_attr(is_nightly, no_mangle)]
    pub extern fn rust_eh_register_frames(){}

    #[cfg_attr(is_nightly, no_mangle)]
    pub extern fn rust_eh_unregister_frames(){}
}


