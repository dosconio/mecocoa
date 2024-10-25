use std::env;

fn main() {
    let ubinpath = env::var("ubinpath").expect("envar ubinpath wanted");
    println!("cargo:rustc-link-search=native={}/mecocoa", ubinpath);
    println!("cargo:rustc-link-lib=static=mccausr-x86");
}