// ASCII Rust TAB4 LF
// Attribute: CPU(x86) Env(Mecocoa-User)
// ModuTitle: Mecocoa Rust Runtime
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#![no_std]

use core::alloc::{GlobalAlloc, Layout};

unsafe extern "C" {
	fn malloc_align(size: usize, alignment: usize) -> *mut u8;
	fn free(ptr: *mut u8);
}

pub struct MccaAllocator;

unsafe impl GlobalAlloc for MccaAllocator {
	unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
		unsafe { malloc_align(layout.size(), layout.align()) }
	}

	unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
		unsafe { free(ptr); }
	}
}

#[global_allocator]
pub static MCCA_ALLOCATOR: MccaAllocator = MccaAllocator;

pub mod io {
	use core::fmt::{self, Write};

	const STDOUT_FILENO: i32 = 1;

	unsafe extern "C" {
		fn write(fd: i32, buffer: *const u8, length: usize) -> isize;
	}

	pub struct Stdout;

	impl Stdout {
		pub const fn new() -> Self {
			Self
		}

		pub fn write_all(&mut self, mut bytes: &[u8]) -> fmt::Result {
			while !bytes.is_empty() {
				let written = unsafe {
					write(STDOUT_FILENO, bytes.as_ptr(), bytes.len())
				};
				if written <= 0 || written as usize > bytes.len() {
					return Err(fmt::Error);
				}
				bytes = &bytes[written as usize..];
			}
			Ok(())
		}
	}

	impl Default for Stdout {
		fn default() -> Self {
			Self::new()
		}
	}

	impl Write for Stdout {
		fn write_str(&mut self, text: &str) -> fmt::Result {
			self.write_all(text.as_bytes())
		}
	}

	pub const fn stdout() -> Stdout {
		Stdout::new()
	}

	#[doc(hidden)]
	pub fn _print(arguments: fmt::Arguments<'_>) {
		let _ = stdout().write_fmt(arguments);
	}
}

#[macro_export]
macro_rules! print {
	($($argument:tt)*) => {{
		$crate::io::_print(core::format_args!($($argument)*));
	}};
}

#[macro_export]
macro_rules! println {
	() => {{
		$crate::print!("\n");
	}};
	($($argument:tt)*) => {{
		$crate::print!($($argument)*);
		$crate::print!("\n");
	}};
}
