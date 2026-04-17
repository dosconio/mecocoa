use std::env;

fn main() {
    let uobjpath = env::var("uobjpath").expect("envar uobjpath wanted");
    println!("cargo:rustc-link-search=native={}/accm-atx-x86-flap32", uobjpath);
    println!("cargo:rustc-link-lib=static=atx-x86-flap32");
}