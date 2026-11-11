// [CHOSEN, not recovered] Owner-requested setup C ABI. Inputs remain borrowed;
// output ownership crosses the boundary only as a boxed slice with a matching free.
use std::{cell::RefCell, ffi::{CString, c_char}, panic::{catch_unwind, AssertUnwindSafe}};

thread_local! { static ERROR: RefCell<CString> = RefCell::new(CString::new("").unwrap()); }
fn error(text: impl ToString) { ERROR.with(|e| *e.borrow_mut() = CString::new(text.to_string().replace('\0', "?")).unwrap()); }
fn guard<T>(failure: T, f: impl FnOnce() -> Result<T, String>) -> T {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(v)) => v,
        Ok(Err(e)) => { error(e); failure },
        Err(_) => { error("rtptool panic; setup incomplete"); failure },
    }
}
#[repr(C)]
pub struct RtpBytes { data: *mut u8, size: usize }
#[unsafe(no_mangle)]
pub extern "C" fn obf2_rtp_error() -> *const c_char { ERROR.with(|e| e.borrow().as_ptr()) }
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_open(data: *const u8, size: usize) -> *mut rtptool::RtpPatch {
    guard(std::ptr::null_mut(), || {
        if data.is_null() || size == 0 { return Err("empty RTP input".into()); }
        let p = rtptool::parse(unsafe { std::slice::from_raw_parts(data, size) }.to_vec()).map_err(|e| e.to_string())?;
        if p.header.version != 0x0320 { return Err("setup requires RTPatch 0x0320".into()); }
        Ok(Box::into_raw(Box::new(p)))
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_close(p: *mut rtptool::RtpPatch) {
    if !p.is_null() { drop(unsafe { Box::from_raw(p) }); }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_count(p: *const rtptool::RtpPatch) -> usize {
    if p.is_null() { 0 } else { unsafe { &*p }.records.len() }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_name(p: *const rtptool::RtpPatch, index: usize, name: *mut c_char, capacity: usize) -> i32 {
    guard(-1, || {
        let p = unsafe { p.as_ref() }.ok_or("null patch")?;
        let r = p.records.get(index).ok_or("invalid record")?;
        if name.is_null() || capacity <= r.filename.len() { return Err("pathname buffer too small".into()); }
        unsafe {
            std::ptr::copy_nonoverlapping(r.filename.as_ptr(), name.cast(), r.filename.len());
            *name.add(r.filename.len()) = 0;
        }
        Ok(0)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_apply(p: *const rtptool::RtpPatch, index: usize, source: *const u8, size: usize, present: i32, output: *mut RtpBytes) -> i32 {
    guard(-1, || {
        let p = unsafe { p.as_ref() }.ok_or("null patch")?;
        let r = p.records.get(index).ok_or("invalid record")?;
        let out = unsafe { output.as_mut() }.ok_or("null output")?;
        *out = RtpBytes { data: std::ptr::null_mut(), size: 0 };
        let input = if present == 0 { None } else if size == 0 { Some(&[][..]) }
            else if source.is_null() { return Err("null source".into()); }
            else { Some(unsafe { std::slice::from_raw_parts(source, size) }) };
        match rtptool::apply_record(p, r, input).map_err(|e| e.to_string())? {
            None => Ok(1),
            Some(bytes) => {
                let mut bytes = bytes.into_boxed_slice();
                *out = RtpBytes { data: bytes.as_mut_ptr(), size: bytes.len() };
                std::mem::forget(bytes);
                Ok(0)
            }
        }
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_free(bytes: RtpBytes) {
    if !bytes.data.is_null() {
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(bytes.data, bytes.size)) });
    }
}
// Final operation metadata, used by the base-game policy; no application checks change.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_terminal(p: *const rtptool::RtpPatch, index: usize) -> i32 {
    guard(-1, || {
        let p = unsafe { p.as_ref() }.ok_or("null patch")?;
        let r = p.records.get(index).ok_or("invalid record")?;
        let last = if r.rec_type == rtptool::RecordType::History { r.children.last().ok_or("empty history")? } else { r };
        Ok(match last.rec_type { rtptool::RecordType::Delete => 1, rtptool::RecordType::Add => 2, rtptool::RecordType::Modify => 3, _ => 0 })
    })
}

// Borrow a reader, fill one Rust-owned buffer, then transfer it into the parser.
// No second complete RTP copy is needed across the language boundary.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_rtp_open_reader(size: usize, read: Option<unsafe extern "C" fn(*mut std::ffi::c_void, usize, *mut u8, usize) -> i32>, context: *mut std::ffi::c_void) -> *mut rtptool::RtpPatch {
    guard(std::ptr::null_mut(), || {
        let read = read.ok_or("null RTP reader")?;
        if size == 0 || size > 1024*1024*1024 { return Err("RTP input exceeds 1 GiB policy".into()); }
        let mut bytes = Vec::new();bytes.try_reserve_exact(size).map_err(|_| "Cannot allocate RTP input")?;bytes.resize(size,0);
        for offset in (0..size).step_by(1024*1024) {
            let n = (size-offset).min(1024*1024);
            if unsafe { read(context,offset,bytes.as_mut_ptr().add(offset),n) } != 0 {return Err("RTP input read cancelled or failed".into());}
        }
        let patch=rtptool::parse(bytes).map_err(|e|e.to_string())?;
        if patch.header.version != 0x0320 {return Err("setup requires RTPatch 0x0320".into());}
        Ok(Box::into_raw(Box::new(patch)))
    })
}
