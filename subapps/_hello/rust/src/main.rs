// ASCII Rust SPA4 LF
// Attribute: 
// LastCheck: 20240505
// AllAuthor: @dosconio
// ModuTitle: Mecocoa Rust Allocation Test
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#![no_std]
#![no_main]
#![feature(alloc_error_handler)]

extern crate alloc;
extern crate mcca_rt;

use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::alloc::Layout;
use core::panic::PanicInfo;
use unisym::__add;

#[alloc_error_handler]
fn allocation_error(layout: Layout) -> ! {
	mcca_rt::println!(
		"Rust allocation failed: size={}, alignment={}",
		layout.size(),
		layout.align(),
	);
	loop {
		core::hint::spin_loop();
	}
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
	mcca_rt::println!("Rust panic: {}", info);
	loop {
		core::hint::spin_loop();
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn main(
	_argc: isize,
	_argv: *const *const u8,
	_envp: *const *const u8,
) -> isize {
	let boxed = Box::new(1234_u32);
	let mut vector = Vec::new();
	vector.push(1_u32);
	vector.push(2_u32);
	let string = String::from("Mecocoa");

	#[repr(align(64))]
	struct Aligned([u8; 64]);

	let aligned = Box::new(Aligned([0xA5; 64]));
	let aligned_address = (&*aligned as *const Aligned) as usize;
	let unisym_result = __add(20, 22);
	let passed = *boxed == 1234 && vector.as_slice() == [1, 2] &&
		string.as_str() == "Mecocoa" && unisym_result == 42 &&
		(aligned_address & 63) == 0 &&
		aligned.0[0] == 0xA5 && aligned.0[63] == 0xA5;

	if passed {
		mcca_rt::println!(
			"Rust core + alloc + Unisym passed: __add={}",
			unisym_result,
		);
	}
	else {
		mcca_rt::println!("Rust core + alloc + Unisym failed.");
	}

	if passed { 0 } else { 1 }
}
