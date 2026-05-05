use std::env;

fn main() {
    let uobjpath = env::var("uobjpath").expect("envar uobjpath wanted");
    println!("cargo:rustc-link-search=native={}/accm-x86", uobjpath);
    println!("cargo:rustc-link-lib=static=x86");
}