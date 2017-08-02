#![feature(lang_items, no_core)]
#![no_std]
#![no_core]

// 在没有实际代码里，msvc版本编译最NB，连dll import都没有。而gnu则把没有调用的minhook代码都加进来了

// extern crate libc; // 好坑啊，默认使用std，必须如下：
//libc = { version = "0.2", default-features = false }
// extern {fn dllmain();}
// #[start]                        // 
// fn main()
// {
//     unsafe{dllmain();}
// }

#[lang = "eh_personality"] extern fn eh_personality() {}
#[lang = "panic_fmt"] fn panic_fmt() -> ! { loop {} }

#[lang = "freeze"]
pub trait Freeze {
    // Empty.
}

#[no_mangle]
pub extern fn __mulodi4(){}
#[no_mangle]
pub extern fn __muloti4(){}
#[no_mangle]
pub extern fn __multi3(){}
#[no_mangle]
pub extern fn __udivti3(){}
#[no_mangle]
pub extern fn __umodti3(){}

// copy from rust-master\src\libcore\marker.rs
#[lang = "sized"]
pub trait Sized {
    // Empty.
}

#[lang = "copy"]
pub trait Copy {
    // Empty.
}

#[lang = "send"]
pub unsafe trait Send {
    // empty.
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

