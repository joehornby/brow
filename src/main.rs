use std::{
    cell::RefCell,
    ffi::{CStr, CString, c_char},
};
use url::Url;

#[derive(Default)]
struct Browser {
    tabs: Vec<u64>,
    active: usize,
    next_id: u64,
}

thread_local! { static BROWSER: RefCell<Browser> = RefCell::default(); }

unsafe extern "C" {
    fn host_run();
    fn host_create(id: u64, chromium: bool) -> bool;
    fn host_present(id: u64, previous: u64, direction: i32);
    fn host_dispose(id: u64);
    fn host_close();
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_count() -> usize {
    BROWSER.with_borrow(|b| b.tabs.len())
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_id(index: usize) -> u64 {
    BROWSER.with_borrow(|b| b.tabs.get(index).copied().unwrap_or_default())
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_current() -> u64 {
    BROWSER.with_borrow(|b| b.tabs.get(b.active).copied().unwrap_or_default())
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_select(index: usize) {
    let old = brow_current();
    let direction = BROWSER.with_borrow_mut(|b| {
        if index >= b.tabs.len() || index == b.active {
            return 0;
        }
        let forward = index > b.active;
        b.active = index;
        if forward { 1 } else { -1 }
    });
    if direction != 0 {
        unsafe { host_present(brow_current(), old, direction) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_new(chromium: bool, replace: bool) -> bool {
    let old = brow_current();
    let id = BROWSER.with_borrow_mut(|b| {
        b.next_id += 1;
        b.next_id
    });
    if !unsafe { host_create(id, chromium) } {
        return false;
    }
    BROWSER.with_borrow_mut(|b| {
        if replace && old != 0 {
            let i = b.active;
            b.tabs[i] = id;
        } else {
            b.tabs.push(id);
            b.active = b.tabs.len() - 1;
        }
    });
    unsafe {
        host_present(id, old, 0);
        if replace && old != 0 {
            host_dispose(old);
        }
    }
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn brow_close_tab() {
    if brow_count() <= 1 {
        unsafe { host_close() };
        return;
    }
    let old = brow_current();
    BROWSER.with_borrow_mut(|b| {
        b.tabs.remove(b.active);
        b.active = b.active.min(b.tabs.len() - 1);
    });
    unsafe {
        host_present(brow_current(), old, 0);
        host_dispose(old);
    }
}

fn destination(input: &str) -> Option<Url> {
    let text = input.trim();
    if text.is_empty() {
        return None;
    }
    if !text.chars().any(char::is_whitespace) {
        let addr = if text.contains("://") {
            text.to_owned()
        } else if text.starts_with("localhost")
            || text.starts_with("127.")
            || text.starts_with("[::1]")
        {
            format!("http://{text}")
        } else {
            format!("https://{text}")
        };
        if let Ok(url) = Url::parse(&addr)
            && matches!(url.scheme(), "http" | "https")
            && url.host_str().is_some_and(|host| {
                text.contains("://") || host == "localhost" || host.contains(['.', ':'])
            })
        {
            return Some(url);
        }
    }
    Url::parse_with_params("https://duckduckgo.com/", &[("q", text)]).ok()
}

#[unsafe(no_mangle)]
unsafe extern "C" fn brow_destination(text: *const c_char) -> *mut c_char {
    let input = unsafe { CStr::from_ptr(text) }.to_string_lossy();
    destination(&input)
        .map(|url| CString::new(url.as_str()).unwrap().into_raw())
        .unwrap_or_default()
}

#[unsafe(no_mangle)]
unsafe extern "C" fn brow_free(text: *mut c_char) {
    drop(unsafe { CString::from_raw(text) });
}

fn main() {
    unsafe { host_run() }
}
