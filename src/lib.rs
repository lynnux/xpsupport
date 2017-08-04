#![feature(lang_items, no_core)]
#![no_std]                      // dylib会跟主程序冲突
#![no_core]

// https://github.com/rust-lang/rfcs/blob/master/text/1510-cdylib.md
#[no_mangle] pub extern "C" fn xpinit() {}

// 在没有实际代码里，msvc版本编译最NB，连dll import都没有。而gnu则把没有调用的minhook代码都加进来了

#[lang = "eh_personality"] extern fn eh_personality() {}
#[lang = "panic_fmt"] fn panic_fmt() -> ! { loop {} }

#[no_mangle]
extern fn __mulodi4(){}
#[no_mangle]
extern fn __muloti4(){}
#[no_mangle]
extern fn __multi3(){}
#[no_mangle]
extern fn __udivti3(){}
#[no_mangle]
extern fn __umodti3(){}

// copy from rust-master\src\libcore\marker.rs
#[lang = "sized"]
trait Sized {
    // Empty.
}

#[lang = "copy"]
trait Copy {
    // Empty.
}

#[lang = "send"]
unsafe trait Send {
    // empty.
}

#[lang = "freeze"]
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
    #[lang = "eh_unwind_resume"]
    fn eh_unwind_resume(){}
    #[no_mangle]
    pub extern fn rust_eh_register_frames(){}
    #[no_mangle]
    pub extern fn rust_eh_unregister_frames(){}
}


