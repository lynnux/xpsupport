extern crate gcc;

use std::env;
use std::path::Path;

use gcc::Config;

fn main() {
    let root_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let target = env::var("TARGET").unwrap();

    let parts = target.splitn(4, '-').collect::<Vec<_>>();
    let arch = parts[0];
    let sys  = parts[2];

    if sys != "windows" {
        panic!("Platform '{}' not supported.", sys);
    }

    let hde = match arch {
        "i686"   => "HDE/hde32.c",
        "x86_64" => "HDE/hde64.c",
        _        => panic!("Architecture '{}' not supported.", arch)
    };

    let src_dir = Path::new(&root_dir).join("src/minhook/src");
    let src_dir2 = Path::new(&root_dir).join("src");


    Config::new()
        .file(src_dir.join("buffer.c"))
        .file(src_dir.join("hook.c"))
        .file(src_dir.join("trampoline.c"))
        .file(src_dir.join(hde))
        .file(src_dir2.join("main.c"))
    // .file(src_dir2.join("sync.c"))
        .file(src_dir2.join("sync_v2.c"))
        .compile("libminhook.a");

    println!("cargo:rerun-if-changed=src/minhook/src/");
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rustc-link-lib=kernel32");
    println!("cargo:rustc-link-lib=User32");

}
