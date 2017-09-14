extern crate gcc;
extern crate rustc_version;

use std::env;
use std::path::Path;
use gcc::Config;
use rustc_version::Channel;

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
        .file(src_dir2.join("sync.c"))
        .compile("libminhook.a");

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=src/minhook/src/");
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rustc-link-lib=kernel32");

    let version = rustc_version::version_meta().unwrap();
    if version.channel == Channel::Nightly {
        println!("cargo:rustc-cfg=is_nightly");
    }
}
