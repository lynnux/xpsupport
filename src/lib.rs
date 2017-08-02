#![feature(lang_items)]
#![no_std]

#[lang = "eh_personality"] extern fn eh_personality() {}
#[lang = "panic_fmt"] fn panic_fmt() -> ! { loop {} }

#[cfg(target_env = "gnu")]
mod gnu
{
    
}

#[cfg(target_env = "msvc")]
pub mod msvc
{
    #[no_mangle] // ensure that this symbol is called `main` in the output
    pub extern "stdcall" fn  __DllMainCRTStartup(argc: i32, argv: *const *const u8) -> i32 {
        0
    }
    
    // #[lang = "panic_fmt"] fn panic_fmt() -> ! { loop {} }

}
