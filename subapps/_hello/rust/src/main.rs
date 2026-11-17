// ASCII Rust SPA4 LF
// Attribute: 
// LastCheck: 20240505
// AllAuthor: @dosconio
// ModuTitle: Mecocoa Rust Allocation Test
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;
extern crate mcca_rt;

use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::alloc::Layout;
use core::panic::PanicInfo;
use mcca_rt::{env, process};
use mcca_rt::fs::{self, File, SeekFrom};
use mcca_rt::process::ExitCode;
use unisym::__add;

fn report_test(name: &str, passed: bool) -> bool {
	if passed {
		mcca_rt::println!("{}: PASS \u{2705}", name);
	}
	else {
		mcca_rt::println!("{}: FAIL \u{274c}", name);
	}
	passed
}

fn test_core() -> bool {
	let values = [1_u32, 2, 3, 4];
	values.iter().copied().sum::<u32>() == 10 &&
		values.first() == Some(&1) && values.last() == Some(&4)
}

fn test_alloc() -> bool {
	let boxed = Box::new(1234_u32);
	let mut vector = Vec::new();
	vector.push(1_u32);
	vector.push(2_u32);
	let string = String::from("Mecocoa");

	#[repr(align(64))]
	struct Aligned([u8; 64]);

	let aligned = Box::new(Aligned([0xA5; 64]));
	let aligned_address = (&*aligned as *const Aligned) as usize;
	*boxed == 1234 && vector.as_slice() == [1, 2] &&
		string.as_str() == "Mecocoa" && (aligned_address & 63) == 0 &&
		aligned.0[0] == 0xA5 && aligned.0[63] == 0xA5
}

fn test_unisym() -> bool {
	__add(20, 22) == 42
}

fn test_process() -> bool {
	process::id() != 0
}

fn print_arguments() {
	let mut found = false;
	for (index, argument) in env::args().enumerate() {
		found = true;
		match argument.to_str() {
			Ok(text) => mcca_rt::println!("Rust argv[{}]: {}", index, text),
			Err(_) => mcca_rt::println!(
				"Rust argv[{}]: {:?}",
				index,
				argument.to_bytes(),
			),
		}
	}
	if !found {
		mcca_rt::println!("Rust argv: (none)");
	}
}

fn test_environment() -> bool {
	matches!(
		env::var(c"__MCCA_RUST_ENV_TEST_MISSING__"),
		Err(env::VarError::NotPresent),
	)
}

fn test_file_io() -> bool {
	const CONTENT: &[u8] = b"Mecocoa Rust file I/O";
	let path = c"/md0/rustio.tmp";
	let file = match File::create_new(path) {
		Ok(file) => file,
		Err(error) => {
			mcca_rt::eprintln!("Rust file create failed: {}", error);
			return false;
		}
	};
	let result = (|| -> mcca_rt::io::Result<bool> {
		file.write_all(CONTENT)?;
		if file.seek(SeekFrom::Start(0))? != 0 {
			return Ok(false);
		}
		let mut buffer = [0u8; CONTENT.len()];
		file.read_exact(&mut buffer)?;
		let first_end = file.read(&mut [0u8; 1])?;
		file.close()?;

		let reopened = File::open(path)?;
		let mut reopened_buffer = [0u8; CONTENT.len()];
		reopened.read_exact(&mut reopened_buffer)?;
		let reopened_end = reopened.read(&mut [0u8; 1])?;
		reopened.close()?;
		Ok(buffer == CONTENT && first_end == 0 &&
			reopened_buffer == CONTENT && reopened_end == 0)
	})();
	let removed = fs::remove(path);
	match (result, removed) {
		(Ok(passed), Ok(())) => passed,
		(Err(error), _) | (_, Err(error)) => {
			mcca_rt::eprintln!("Rust file I/O failed: {}", error);
			false
		}
	}
}

#[alloc_error_handler]
fn allocation_error(layout: Layout) -> ! {
	mcca_rt::eprintln!(
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
	mcca_rt::eprintln!("Rust panic: {}", info);
	loop {
		core::hint::spin_loop();
	}
}

fn main() -> ExitCode {
	let mut passed = true;
	print_arguments();
	if !report_test("Unisym", test_unisym()) {
		passed = false;
	}
	if !report_test("Rust core", test_core()) {
		passed = false;
	}
	if !report_test("Rust alloc", test_alloc()) {
		passed = false;
	}
	if !report_test("Rust file I/O", test_file_io()) {
		passed = false;
	}
	if !report_test("Rust process", test_process()) {
		passed = false;
	}
	if !report_test("Rust environment", test_environment()) {
		passed = false;
	}

	if passed { ExitCode::SUCCESS } else { ExitCode::FAILURE }
}
