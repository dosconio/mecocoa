// ASCII Rust SPA4 LF
// Attribute: 
// LastCheck: 20240505
// AllAuthor: @dosconio
// ModuTitle: Shell Flap-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#![no_std]
#![no_main]

#![feature(lang_items)]

#[lang = "eh_personality"]
extern "C" fn eh_personality() {}

use core::panic::PanicInfo;

//#[link(name = "mccausr-x86", kind = "static")]
extern "C" {
    fn sysouts(str: *const u8);
}

#[allow(unused_variables)]  
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    loop { }
}

#[no_mangle]
pub extern "C" fn main() {
	unsafe {
		sysouts("(App by Rust)\n\r\0".as_ptr());
	}
}
