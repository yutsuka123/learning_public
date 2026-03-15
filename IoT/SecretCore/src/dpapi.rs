use std::ptr;
use winapi::um::dpapi::{CryptProtectData, CryptUnprotectData};
use winapi::um::wincrypt::CRYPTOAPI_BLOB;
use winapi::um::winbase::LocalFree;

pub fn protect_data(data: &[u8]) -> Result<Vec<u8>, String> {
    let mut data_in = CRYPTOAPI_BLOB {
        cbData: data.len() as u32,
        pbData: data.as_ptr() as *mut u8,
    };
    let mut data_out = CRYPTOAPI_BLOB {
        cbData: 0,
        pbData: ptr::null_mut(),
    };

    let success = unsafe {
        CryptProtectData(
            &mut data_in,
            ptr::null_mut(), // description (optional)
            ptr::null_mut(), // entropy (optional)
            ptr::null_mut(), // reserved
            ptr::null_mut(), // prompt struct
            0,               // flags (0 = tie to current user)
            &mut data_out,
        )
    };

    if success == 0 {
        return Err("CryptProtectData failed".to_string());
    }

    let out_slice = unsafe { std::slice::from_raw_parts(data_out.pbData, data_out.cbData as usize) };
    let result = out_slice.to_vec();
    unsafe { LocalFree(data_out.pbData as *mut _) };

    Ok(result)
}

pub fn unprotect_data(encrypted_data: &[u8]) -> Result<Vec<u8>, String> {
    let mut data_in = CRYPTOAPI_BLOB {
        cbData: encrypted_data.len() as u32,
        pbData: encrypted_data.as_ptr() as *mut u8,
    };
    let mut data_out = CRYPTOAPI_BLOB {
        cbData: 0,
        pbData: ptr::null_mut(),
    };

    let success = unsafe {
        CryptUnprotectData(
            &mut data_in,
            ptr::null_mut(), // description out
            ptr::null_mut(), // entropy
            ptr::null_mut(), // reserved
            ptr::null_mut(), // prompt struct
            0,               // flags
            &mut data_out,
        )
    };

    if success == 0 {
        return Err("CryptUnprotectData failed".to_string());
    }

    let out_slice = unsafe { std::slice::from_raw_parts(data_out.pbData, data_out.cbData as usize) };
    let result = out_slice.to_vec();
    unsafe { LocalFree(data_out.pbData as *mut _) };

    Ok(result)
}
