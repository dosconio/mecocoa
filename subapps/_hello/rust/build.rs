use std::{env, path::Path};

fn main() {
	let uobjpath = env::var("uobjpath").expect("envar uobjpath wanted");
	let native_dir = Path::new(&uobjpath).join("accm-x86");
	let native_lib = native_dir.join("libx86.a");
	let crt0 = Path::new(env!("CARGO_MANIFEST_DIR"))
		.join("../../../accmlib/sysroot/usr/lib/i686-mcca/crt0.o");

	println!("cargo:rerun-if-env-changed=uobjpath");
	println!("cargo:rerun-if-changed={}", crt0.display());
	println!("cargo:rerun-if-changed={}", native_lib.display());
	println!("cargo:rustc-link-arg={}", crt0.display());
	println!("cargo:rustc-link-search=native={}", native_dir.display());
	println!("cargo:rustc-link-lib=static=x86");
}
