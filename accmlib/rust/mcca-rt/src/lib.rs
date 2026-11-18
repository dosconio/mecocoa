// ASCII Rust TAB4 LF
// Attribute: CPU(x86) Env(Mecocoa-User)
// ModuTitle: Mecocoa Rust Runtime
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#![no_std]
#![feature(lang_items)]

extern crate alloc;

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

pub mod process {
	use core::ptr;
	use core::sync::atomic::{AtomicIsize, AtomicPtr, Ordering};

	unsafe extern "C" {
		fn getpid() -> i32;
		#[link_name = "exit"]
		fn c_exit(code: i32) -> !;
	}

	static ARGUMENT_COUNT: AtomicIsize = AtomicIsize::new(0);
	static ARGUMENT_VECTOR: AtomicPtr<*const u8> = AtomicPtr::new(ptr::null_mut());

	#[derive(Clone, Copy, Debug, Eq, PartialEq)]
	pub struct ExitCode(i32);

	impl ExitCode {
		pub const SUCCESS: Self = Self(0);
		pub const FAILURE: Self = Self(1);

		pub const fn from_raw(code: i32) -> Self {
			Self(code)
		}

		pub const fn as_raw(self) -> i32 {
			self.0
		}
	}

	pub fn id() -> u32 {
		let process_id = unsafe { getpid() };
		if process_id > 0 { process_id as u32 } else { 0 }
	}

	pub fn exit(code: ExitCode) -> ! {
		unsafe { c_exit(code.as_raw()) }
	}

	#[lang = "termination"]
	pub trait Termination {
		fn report(self) -> ExitCode;
	}

	impl Termination for () {
		fn report(self) -> ExitCode {
			ExitCode::SUCCESS
		}
	}

	impl Termination for ExitCode {
		fn report(self) -> ExitCode {
			self
		}
	}

	pub(crate) fn initialize(
		argument_count: isize,
		argument_vector: *const *const u8,
	) {
		ARGUMENT_VECTOR.store(argument_vector.cast_mut(), Ordering::Relaxed);
		ARGUMENT_COUNT.store(argument_count, Ordering::Release);
	}

	pub(crate) fn arguments() -> (usize, *const *const u8) {
		let argument_count = ARGUMENT_COUNT.load(Ordering::Acquire);
		if argument_count <= 0 {
			return (0, ptr::null());
		}
		let argument_vector = ARGUMENT_VECTOR.load(Ordering::Relaxed).cast_const();
		(argument_count as usize, argument_vector)
	}
}

#[lang = "start"]
fn lang_start<T: process::Termination + 'static>(
	main: fn() -> T,
	argument_count: isize,
	argument_vector: *const *const u8,
	_sigpipe: u8,
) -> isize {
	process::initialize(argument_count, argument_vector);
	main().report().as_raw() as isize
}

pub mod env {
	use alloc::string::String;
	use core::ffi::{c_char, CStr};
	use core::fmt;

	unsafe extern "C" {
		fn getenv(name: *const c_char) -> *mut c_char;
	}

	#[derive(Clone, Copy, Debug, Eq, PartialEq)]
	pub enum VarError {
		NotPresent,
		NotUnicode,
	}

	impl fmt::Display for VarError {
		fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
			match self {
				Self::NotPresent => formatter.write_str("environment variable not found"),
				Self::NotUnicode => formatter.write_str("environment variable is not Unicode"),
			}
		}
	}

	pub struct Args {
		index: usize,
		count: usize,
		vector: *const *const u8,
	}

	impl Iterator for Args {
		type Item = &'static CStr;

		fn next(&mut self) -> Option<Self::Item> {
			if self.index >= self.count || self.vector.is_null() {
				return None;
			}
			let argument = unsafe { *self.vector.add(self.index) };
			self.index += 1;
			if argument.is_null() {
				self.index = self.count;
				return None;
			}
			Some(unsafe { CStr::from_ptr(argument.cast()) })
		}

		fn size_hint(&self) -> (usize, Option<usize>) {
			let remaining = self.count.saturating_sub(self.index);
			(remaining, Some(remaining))
		}
	}

	pub fn args() -> Args {
		let (count, vector) = crate::process::arguments();
		Args { index: 0, count, vector }
	}

	pub fn var(name: &CStr) -> Result<String, VarError> {
		let value = unsafe { getenv(name.as_ptr()) };
		if value.is_null() {
			return Err(VarError::NotPresent);
		}
		let value = unsafe { CStr::from_ptr(value) };
		let value = value.to_str().map_err(|_| VarError::NotUnicode)?;
		Ok(String::from(value))
	}
}

pub mod io {
	use core::fmt::{self, Write};

	pub(crate) const EINTR: i32 = 4;
	pub(crate) const EIO: i32 = 5;
	const STDIN_FILENO: i32 = 0;
	const STDOUT_FILENO: i32 = 1;
	const STDERR_FILENO: i32 = 2;

	unsafe extern "C" {
		fn _errno() -> *mut i32;
		fn read(fd: i32, buffer: *mut u8, length: usize) -> isize;
		fn write(fd: i32, buffer: *const u8, length: usize) -> isize;
	}

	#[derive(Clone, Copy, Debug, Eq, PartialEq)]
	pub struct Error {
		pub errno: i32,
	}

	impl Error {
		pub const fn from_errno(errno: i32) -> Self {
			Self { errno }
		}

		pub fn last_os_error() -> Self {
			let errno = unsafe { *_errno() };
			Self::from_errno(errno)
		}

		pub const fn is_interrupted(self) -> bool {
			self.errno == EINTR
		}
	}

	impl fmt::Display for Error {
		fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
			write!(formatter, "Mecocoa OS error {}", self.errno)
		}
	}

	pub type Result<T> = core::result::Result<T, Error>;

	pub(crate) fn prepare_os_call() {
		unsafe { *_errno() = 0; }
	}

	pub(crate) fn os_call_error() -> Error {
		let error = Error::last_os_error();
		if error.errno == 0 {
			Error::from_errno(EIO)
		}
		else {
			error
		}
	}

	#[derive(Clone, Copy, Debug, Eq, PartialEq)]
	pub struct Fd {
		raw: i32,
	}

	impl Fd {
		pub const fn from_raw(raw: i32) -> Self {
			Self { raw }
		}

		pub const fn as_raw(self) -> i32 {
			self.raw
		}

		pub fn read(self, buffer: &mut [u8]) -> Result<usize> {
			prepare_os_call();
			let read_count = unsafe {
				read(self.raw, buffer.as_mut_ptr(), buffer.len())
			};
			if read_count < 0 {
				return Err(os_call_error());
			}
			let read_count = read_count as usize;
			if read_count > buffer.len() {
				return Err(Error::from_errno(EIO));
			}
			Ok(read_count)
		}

		pub fn read_exact(self, mut buffer: &mut [u8]) -> Result<()> {
			while !buffer.is_empty() {
				match self.read(buffer) {
					Ok(0) => return Err(Error::from_errno(EIO)),
					Ok(read_count) => buffer = &mut buffer[read_count..],
					Err(error) if error.is_interrupted() => {}
					Err(error) => return Err(error),
				}
			}
			Ok(())
		}

		pub fn write(self, buffer: &[u8]) -> Result<usize> {
			prepare_os_call();
			let written = unsafe {
				write(self.raw, buffer.as_ptr(), buffer.len())
			};
			if written < 0 {
				return Err(os_call_error());
			}
			let written = written as usize;
			if written > buffer.len() {
				return Err(Error::from_errno(EIO));
			}
			Ok(written)
		}

		pub fn write_all(self, mut buffer: &[u8]) -> Result<()> {
			while !buffer.is_empty() {
				match self.write(buffer) {
					Ok(0) => return Err(Error::from_errno(EIO)),
					Ok(written) => buffer = &buffer[written..],
					Err(error) if error.is_interrupted() => {}
					Err(error) => return Err(error),
				}
			}
			Ok(())
		}
	}

	#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
	pub struct Stdin;

	impl Stdin {
		pub const fn new() -> Self {
			Self
		}

		pub const fn fd(self) -> Fd {
			Fd::from_raw(STDIN_FILENO)
		}

		pub fn read(self, buffer: &mut [u8]) -> Result<usize> {
			self.fd().read(buffer)
		}

		pub fn read_exact(self, buffer: &mut [u8]) -> Result<()> {
			self.fd().read_exact(buffer)
		}
	}

	#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
	pub struct Stdout;

	impl Stdout {
		pub const fn new() -> Self {
			Self
		}

		pub const fn fd(self) -> Fd {
			Fd::from_raw(STDOUT_FILENO)
		}

		pub fn write(self, buffer: &[u8]) -> Result<usize> {
			self.fd().write(buffer)
		}

		pub fn write_all(self, buffer: &[u8]) -> Result<()> {
			self.fd().write_all(buffer)
		}
	}

	impl Write for Stdout {
		fn write_str(&mut self, text: &str) -> fmt::Result {
			self.write_all(text.as_bytes()).map_err(|_| fmt::Error)
		}
	}

	#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
	pub struct Stderr;

	impl Stderr {
		pub const fn new() -> Self {
			Self
		}

		pub const fn fd(self) -> Fd {
			Fd::from_raw(STDERR_FILENO)
		}

		pub fn write(self, buffer: &[u8]) -> Result<usize> {
			self.fd().write(buffer)
		}

		pub fn write_all(self, buffer: &[u8]) -> Result<()> {
			self.fd().write_all(buffer)
		}
	}

	impl Write for Stderr {
		fn write_str(&mut self, text: &str) -> fmt::Result {
			self.write_all(text.as_bytes()).map_err(|_| fmt::Error)
		}
	}

	pub const fn stdin() -> Stdin {
		Stdin::new()
	}

	pub const fn stdout() -> Stdout {
		Stdout::new()
	}

	pub const fn stderr() -> Stderr {
		Stderr::new()
	}

	#[doc(hidden)]
	pub fn _print(arguments: fmt::Arguments<'_>) {
		let _ = stdout().write_fmt(arguments);
	}

	#[doc(hidden)]
	pub fn _eprint(arguments: fmt::Arguments<'_>) {
		let _ = stderr().write_fmt(arguments);
	}
}

pub mod fs {
	use core::ffi::{c_char, CStr};

	use crate::io::{self, Error, Fd, Result};

	const EBADF: i32 = 9;
	const EINVAL: i32 = 22;
	const O_RDONLY: i32 = 0;
	const O_RDWR: i32 = 2;
	const O_CREAT: i32 = 0x40;
	const O_EXCL: i32 = 0x80;
	const O_TRUNC: i32 = 0x200;
	const SEEK_SET: i32 = 0;
	const SEEK_CUR: i32 = 1;
	const SEEK_END: i32 = 2;

	unsafe extern "C" {
		fn open(path: *const c_char, flags: i32, ...) -> i32;
		fn close(fd: i32) -> i32;
		fn lseek(fd: i32, offset: isize, whence: i32) -> isize;
		fn unlink(path: *const c_char) -> i32;
	}

	#[derive(Clone, Copy, Debug, Eq, PartialEq)]
	pub enum SeekFrom {
		Start(usize),
		End(isize),
		Current(isize),
	}

	#[derive(Debug)]
	pub struct File {
		fd: Option<Fd>,
	}

	impl File {
		fn open_with_flags(path: &CStr, flags: i32) -> Result<Self> {
			io::prepare_os_call();
			let raw = unsafe { open(path.as_ptr(), flags, 0o666usize) };
			if raw < 0 {
				return Err(io::os_call_error());
			}
			Ok(Self { fd: Some(Fd::from_raw(raw)) })
		}

		pub fn open(path: &CStr) -> Result<Self> {
			Self::open_with_flags(path, O_RDONLY)
		}

		pub fn create(path: &CStr) -> Result<Self> {
			Self::open_with_flags(path, O_RDWR | O_CREAT | O_TRUNC)
		}

		pub fn create_new(path: &CStr) -> Result<Self> {
			Self::open_with_flags(path, O_RDWR | O_CREAT | O_EXCL)
		}

		pub fn fd(&self) -> Result<Fd> {
			self.fd.ok_or(Error::from_errno(EBADF))
		}

		pub fn read(&self, buffer: &mut [u8]) -> Result<usize> {
			self.fd()?.read(buffer)
		}

		pub fn read_exact(&self, buffer: &mut [u8]) -> Result<()> {
			self.fd()?.read_exact(buffer)
		}

		pub fn write(&self, buffer: &[u8]) -> Result<usize> {
			self.fd()?.write(buffer)
		}

		pub fn write_all(&self, buffer: &[u8]) -> Result<()> {
			self.fd()?.write_all(buffer)
		}

		pub fn seek(&self, position: SeekFrom) -> Result<usize> {
			let (offset, whence) = match position {
				SeekFrom::Start(offset) => {
					if offset > isize::MAX as usize {
						return Err(Error::from_errno(EINVAL));
					}
					(offset as isize, SEEK_SET)
				}
				SeekFrom::End(offset) => (offset, SEEK_END),
				SeekFrom::Current(offset) => (offset, SEEK_CUR),
			};
			io::prepare_os_call();
			let result = unsafe { lseek(self.fd()?.as_raw(), offset, whence) };
			if result < 0 {
				return Err(io::os_call_error());
			}
			Ok(result as usize)
		}

		fn close_inner(&mut self) -> Result<()> {
			let Some(fd) = self.fd.take() else {
				return Ok(());
			};
			io::prepare_os_call();
			let result = unsafe { close(fd.as_raw()) };
			if result != 0 {
				return Err(io::os_call_error());
			}
			Ok(())
		}

		pub fn close(mut self) -> Result<()> {
			self.close_inner()
		}
	}

	impl Drop for File {
		fn drop(&mut self) {
			let _ = self.close_inner();
		}
	}

	pub fn remove(path: &CStr) -> Result<()> {
		io::prepare_os_call();
		let result = unsafe { unlink(path.as_ptr()) };
		if result != 0 {
			return Err(io::os_call_error());
		}
		Ok(())
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

#[macro_export]
macro_rules! eprint {
	($($argument:tt)*) => {{
		$crate::io::_eprint(core::format_args!($($argument)*));
	}};
}

#[macro_export]
macro_rules! eprintln {
	() => {{
		$crate::eprint!("\n");
	}};
	($($argument:tt)*) => {{
		$crate::eprint!($($argument)*);
		$crate::eprint!("\n");
	}};
}
