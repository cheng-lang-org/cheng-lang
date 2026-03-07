use std::env;
use std::fmt;
use std::os::raw::{c_int, c_void};
use std::path::PathBuf;
use std::sync::OnceLock;

use libloading::Library;

use crate::manifest::{CompileOptionsV1, CompileReportV1, SemanticModuleV1};

type ValidateFn = unsafe extern "C" fn(*const u8, c_int, *mut c_int) -> *mut c_void;
type CompileFn =
    unsafe extern "C" fn(*const u8, c_int, *const u8, c_int, *mut c_int) -> *mut c_void;
type ValidateV2Fn = unsafe extern "C" fn(
    *const u8,
    c_int,
    *const u8,
    c_int,
    *const u8,
    c_int,
    *mut c_int,
) -> *mut c_void;
type CompileV2Fn = unsafe extern "C" fn(
    *const u8,
    c_int,
    *const u8,
    c_int,
    *const u8,
    c_int,
    *const u8,
    c_int,
    *mut c_int,
) -> *mut c_void;
type FreeFn = unsafe extern "C" fn(*mut c_void);

static BRIDGE_FNS: OnceLock<Result<BridgeFns, String>> = OnceLock::new();

#[derive(Clone, Copy)]
#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
struct BridgeFns {
    validate: ValidateFn,
    compile: CompileFn,
    validate_v2: Option<ValidateV2Fn>,
    compile_v2: Option<CompileV2Fn>,
    free: FreeFn,
}

#[derive(Debug)]
pub enum UirBridgeError {
    BridgeUnavailable(String),
    NullResult,
    InvalidUtf8(std::string::FromUtf8Error),
}

impl fmt::Display for UirBridgeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::BridgeUnavailable(reason) => write!(f, "cheng_uir bridge unavailable: {reason}"),
            Self::NullResult => f.write_str("cheng_uir ABI returned a null result buffer"),
            Self::InvalidUtf8(err) => write!(f, "cheng_uir ABI returned invalid UTF-8: {err}"),
        }
    }
}

impl std::error::Error for UirBridgeError {}

pub fn validate_v1_manifest(module: &SemanticModuleV1) -> Result<String, UirBridgeError> {
    validate_v1_manifest_text(&module.to_manifest())
}

pub fn compile_v1_manifest(
    module: &SemanticModuleV1,
    options: &CompileOptionsV1,
) -> Result<CompileReportV1, UirBridgeError> {
    compile_v1_manifest_text(&module.to_manifest(), options)
}

pub fn validate_v1_manifest_text(manifest: &str) -> Result<String, UirBridgeError> {
    unsafe { call_text_result_v1(manifest.as_bytes(), &[], false) }
}

pub fn compile_v1_manifest_text(
    manifest: &str,
    options: &CompileOptionsV1,
) -> Result<CompileReportV1, UirBridgeError> {
    let option_manifest = options.to_manifest();
    let report =
        unsafe { call_text_result_v1(manifest.as_bytes(), option_manifest.as_bytes(), true)? };
    Ok(CompileReportV1::parse(&report))
}

#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
pub fn validate_manifest_with_inputs(
    manifest: &str,
    semantic_tables: &str,
    symbol_list: &str,
) -> Result<String, UirBridgeError> {
    let bridge = resolve_bridge_fns()?;
    if bridge.validate_v2.is_some() {
        let report = unsafe {
            call_text_result_v2(
                manifest.as_bytes(),
                semantic_tables.as_bytes(),
                symbol_list.as_bytes(),
                &[],
                false,
            )?
        };
        return Ok(report);
    }
    let fallback_manifest = manifest_with_inline_inputs(manifest, semantic_tables, symbol_list);
    unsafe { call_text_result_v1(fallback_manifest.as_bytes(), &[], false) }
}

#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
pub fn compile_manifest_with_inputs(
    manifest: &str,
    semantic_tables: &str,
    symbol_list: &str,
    options: &CompileOptionsV1,
) -> Result<CompileReportV1, UirBridgeError> {
    let option_manifest = options.to_manifest();
    let bridge = resolve_bridge_fns()?;
    let report = if bridge.compile_v2.is_some() {
        unsafe {
            call_text_result_v2(
                manifest.as_bytes(),
                semantic_tables.as_bytes(),
                symbol_list.as_bytes(),
                option_manifest.as_bytes(),
                true,
            )?
        }
    } else {
        let fallback_manifest = manifest_with_inline_inputs(manifest, semantic_tables, symbol_list);
        unsafe {
            call_text_result_v1(
                fallback_manifest.as_bytes(),
                option_manifest.as_bytes(),
                true,
            )?
        }
    };
    Ok(CompileReportV1::parse(&report))
}

unsafe fn call_text_result_v1(
    input: &[u8],
    options: &[u8],
    compile: bool,
) -> Result<String, UirBridgeError> {
    let bridge = resolve_bridge_fns()?;
    let mut out_len: c_int = 0;
    let ptr = if compile {
        (bridge.compile)(
            input.as_ptr(),
            input.len() as c_int,
            options.as_ptr(),
            options.len() as c_int,
            &mut out_len,
        )
    } else {
        (bridge.validate)(input.as_ptr(), input.len() as c_int, &mut out_len)
    };
    if ptr.is_null() {
        return Err(UirBridgeError::NullResult);
    }

    let bytes = std::slice::from_raw_parts(ptr.cast::<u8>(), out_len as usize).to_vec();
    (bridge.free)(ptr);
    String::from_utf8(bytes).map_err(UirBridgeError::InvalidUtf8)
}

#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
unsafe fn call_text_result_v2(
    input: &[u8],
    semantic_tables: &[u8],
    symbol_list: &[u8],
    options: &[u8],
    compile: bool,
) -> Result<String, UirBridgeError> {
    let bridge = resolve_bridge_fns()?;
    let mut out_len: c_int = 0;
    let ptr = if compile {
        let compile_v2 = bridge.compile_v2.ok_or_else(|| {
            UirBridgeError::BridgeUnavailable("cheng_uir_compile_v2 missing".to_string())
        })?;
        compile_v2(
            input.as_ptr(),
            input.len() as c_int,
            semantic_tables.as_ptr(),
            semantic_tables.len() as c_int,
            symbol_list.as_ptr(),
            symbol_list.len() as c_int,
            options.as_ptr(),
            options.len() as c_int,
            &mut out_len,
        )
    } else {
        let validate_v2 = bridge.validate_v2.ok_or_else(|| {
            UirBridgeError::BridgeUnavailable("cheng_uir_validate_v2 missing".to_string())
        })?;
        validate_v2(
            input.as_ptr(),
            input.len() as c_int,
            semantic_tables.as_ptr(),
            semantic_tables.len() as c_int,
            symbol_list.as_ptr(),
            symbol_list.len() as c_int,
            &mut out_len,
        )
    };
    if ptr.is_null() {
        return Err(UirBridgeError::NullResult);
    }
    let bytes = std::slice::from_raw_parts(ptr.cast::<u8>(), out_len as usize).to_vec();
    (bridge.free)(ptr);
    String::from_utf8(bytes).map_err(UirBridgeError::InvalidUtf8)
}

fn resolve_bridge_fns() -> Result<&'static BridgeFns, UirBridgeError> {
    BRIDGE_FNS
        .get_or_init(load_bridge_fns)
        .as_ref()
        .map_err(|reason| UirBridgeError::BridgeUnavailable(reason.clone()))
}

fn load_bridge_fns() -> Result<BridgeFns, String> {
    let dylib_path = resolve_bridge_path()?;

    unsafe {
        let library = Box::new(
            Library::new(&dylib_path)
                .map_err(|err| format!("failed to load {dylib_path}: {err}"))?,
        );
        let validate = *library
            .get::<ValidateFn>(b"cheng_uir_validate_v1\0")
            .map_err(|err| format!("missing symbol cheng_uir_validate_v1: {err}"))?;
        let compile = *library
            .get::<CompileFn>(b"cheng_uir_compile_v1\0")
            .map_err(|err| format!("missing symbol cheng_uir_compile_v1: {err}"))?;
        let validate_v2 = library
            .get::<ValidateV2Fn>(b"cheng_uir_validate_v2\0")
            .ok()
            .map(|symbol| *symbol);
        let compile_v2 = library
            .get::<CompileV2Fn>(b"cheng_uir_compile_v2\0")
            .ok()
            .map(|symbol| *symbol);
        let free = *library
            .get::<FreeFn>(b"cheng_uir_free_result_v1\0")
            .map_err(|err| format!("missing symbol cheng_uir_free_result_v1: {err}"))?;

        let _leaked_library = Box::leak(library);
        Ok(BridgeFns {
            validate,
            compile,
            validate_v2,
            compile_v2,
            free,
        })
    }
}

#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
fn manifest_with_inline_inputs(manifest: &str, semantic_tables: &str, symbol_list: &str) -> String {
    let mut out =
        String::with_capacity(manifest.len() + semantic_tables.len() + symbol_list.len() + 64);
    out.push_str(manifest);
    if !out.ends_with('\n') {
        out.push('\n');
    }
    out.push_str("semantic_tables_inline=");
    out.push_str(&escape_manifest_value(semantic_tables));
    out.push('\n');
    out.push_str("symbol_list_inline=");
    out.push_str(&escape_manifest_value(symbol_list));
    out.push('\n');
    out
}

#[cfg_attr(not(feature = "rustc-backend"), allow(dead_code))]
fn escape_manifest_value(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
}

fn resolve_bridge_path() -> Result<String, String> {
    if let Ok(path) = env::var("CHENG_UIR_BRIDGE_DYLIB") {
        return Ok(path);
    }

    for candidate in native_bridge_candidates() {
        if candidate.exists() {
            return Ok(candidate.display().to_string());
        }
    }

    for candidate in bootstrap_bridge_candidates() {
        if candidate.exists() {
            return Ok(candidate.display().to_string());
        }
    }

    Err(
        "set CHENG_UIR_BRIDGE_DYLIB to a Cheng UIR bridge dylib path, or build a native Cheng UIR dylib / cheng_uir_bridge_bootstrap in this workspace".to_string(),
    )
}

fn native_bridge_candidates() -> Vec<PathBuf> {
    let workspace_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    let dylib_name = if cfg!(target_os = "macos") {
        "libcheng_uir_native.dylib"
    } else if cfg!(target_os = "windows") {
        "cheng_uir_native.dll"
    } else {
        "libcheng_uir_native.so"
    };
    vec![
        workspace_root
            .join("artifacts")
            .join("cheng_uir_native")
            .join(dylib_name),
        workspace_root.join(dylib_name),
    ]
}

fn bootstrap_bridge_candidates() -> Vec<PathBuf> {
    let workspace_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    let bridge_root = workspace_root
        .join("cheng_uir_bridge_bootstrap")
        .join("target");
    let dylib_name = if cfg!(target_os = "macos") {
        "libcheng_uir_bridge_bootstrap.dylib"
    } else if cfg!(target_os = "windows") {
        "cheng_uir_bridge_bootstrap.dll"
    } else {
        "libcheng_uir_bridge_bootstrap.so"
    };
    vec![
        bridge_root.join("debug").join(dylib_name),
        bridge_root.join("release").join(dylib_name),
    ]
}
