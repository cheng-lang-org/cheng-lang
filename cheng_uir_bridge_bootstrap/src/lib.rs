use std::collections::{BTreeMap, BTreeSet};
use std::ffi::CString;
use std::fmt::Write as _;
use std::fs;
use std::os::raw::{c_int, c_void};
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use object::write::{Object, StandardSection, Symbol, SymbolSection};
use object::{Architecture, BinaryFormat, Endianness, SymbolFlags, SymbolKind, SymbolScope};

#[derive(Clone, Debug, Default)]
struct Manifest {
    fields: BTreeMap<String, String>,
}

#[derive(Clone, Debug, Default)]
struct SemanticTableSummary {
    func_count: usize,
    local_count: usize,
    block_count: usize,
    expr_count: usize,
    stmt_count: usize,
}

#[derive(Clone, Debug, Default)]
struct SemanticModuleData {
    frontend: String,
    target: String,
    crate_type: String,
    strings: Vec<String>,
    types: Vec<String>,
    layouts: Vec<SemanticLayout>,
    vtable_descs: Vec<SemanticVtableDesc>,
    const_values: Vec<SemanticConstValue>,
    readonly_objects: Vec<SemanticReadonlyObject>,
    readonly_relocs: Vec<SemanticReadonlyReloc>,
    data_symbols: Vec<SemanticDataSymbol>,
    aggregate_layouts: Vec<SemanticAggregateLayout>,
    aggregate_fields: Vec<SemanticAggregateField>,
    func_abi: Vec<SemanticFuncAbi>,
    call_abi: Vec<SemanticCallAbi>,
    funcs: Vec<SemanticFunc>,
    locals: Vec<SemanticLocal>,
    blocks: Vec<SemanticBlock>,
    exprs: Vec<SemanticExpr>,
    stmts: Vec<SemanticStmt>,
}

#[derive(Clone, Debug, Default)]
struct SemanticFunc {
    name: String,
    first_block: usize,
    block_len: usize,
    first_local: usize,
    local_len: usize,
}

#[derive(Clone, Debug, Default)]
struct SemanticLocal {
    name: String,
    type_idx: i32,
}

#[derive(Clone, Debug, Default)]
struct SemanticBlock {
    first_stmt: usize,
    stmt_len: usize,
    term_kind: String,
}

#[derive(Clone, Debug, Default)]
struct SemanticExpr {
    kind: String,
    type_idx: i32,
    a: i32,
    b: i32,
    c: i32,
}

#[derive(Clone, Debug, Default)]
struct SemanticStmt {
    kind: String,
    a: i32,
    b: i32,
    c: i32,
}

#[derive(Clone, Debug, Default)]
struct SemanticLayout {
    name: String,
    size_bytes: u64,
    align_bytes: u64,
    abi_class: String,
}

#[derive(Clone, Debug, Default)]
struct SemanticVtableDesc {
    label: String,
    entries: Vec<String>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticConstValue {
    type_idx: i32,
    kind: String,
    atom_a_kind: String,
    atom_a: String,
    atom_a_addend: i64,
    atom_b_kind: String,
    atom_b: String,
    atom_b_addend: i64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticReadonlyObject {
    label: String,
    type_idx: i32,
    size_bytes: u64,
    align_bytes: u64,
    bytes: Vec<u8>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticReadonlyReloc {
    object_idx: usize,
    offset_bytes: u64,
    target_kind: String,
    target: String,
    addend: i64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticDataSymbol {
    symbol: String,
    object_idx: usize,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticAggregateLayout {
    type_idx: i32,
    size_bytes: u64,
    align_bytes: u64,
    field_start: usize,
    field_len: usize,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticAggregateField {
    aggregate_idx: usize,
    field_index: usize,
    offset_bytes: u64,
    type_idx: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticFuncAbi {
    func_name: String,
    ret_mode: String,
    arg_modes: Vec<String>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SemanticCallAbi {
    stmt_idx: usize,
    callee: String,
    abi: String,
    call_kind: String,
    vtable_slot: i32,
    can_unwind: bool,
    ret_mode: String,
    arg_modes: Vec<String>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct SubsetBuildStats {
    real_fn_symbols: usize,
    stub_fn_symbols: usize,
    readonly_object_count: usize,
    readonly_reloc_count: usize,
    memory_abi_calls: usize,
    real_symbols: Vec<String>,
    stub_symbols: Vec<String>,
    stub_reasons: Vec<(String, String)>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ScalarWidth {
    W32,
    X64,
    Void,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PlaceBase {
    Stack,
    IndirectStack,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LocalStorage {
    Void,
    Direct { offset: i32 },
    Indirect { pointer_offset: i32 },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct FrameLocal {
    storage: LocalStorage,
    type_idx: i32,
}

#[derive(Clone, Debug, Default)]
struct FramePlan {
    locals: Vec<FrameLocal>,
    frame_size: i32,
    frame_record_offset: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PlaceLocation {
    base: PlaceBase,
    slot: Option<usize>,
    base_offset: i32,
    projection_offset: i32,
    type_idx: i32,
}

impl SemanticModuleData {
    fn summary(&self) -> SemanticTableSummary {
        SemanticTableSummary {
            func_count: self.funcs.len(),
            local_count: self.locals.len(),
            block_count: self.blocks.len(),
            expr_count: self.exprs.len(),
            stmt_count: self.stmts.len(),
        }
    }
}

impl Manifest {
    fn parse(text: &str) -> Self {
        let mut fields = BTreeMap::new();
        for line in text.lines() {
            if let Some((key, value)) = line.split_once('=') {
                fields.insert(
                    key.trim().to_string(),
                    unescape_manifest_value(value.trim()),
                );
            }
        }
        Self { fields }
    }

    fn get(&self, key: &str) -> &str {
        self.fields.get(key).map(String::as_str).unwrap_or("")
    }
}

fn unescape_manifest_value(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    let mut chars = value.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            out.push(ch);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('r') => out.push('\r'),
            Some('\\') => out.push('\\'),
            Some(other) => {
                out.push('\\');
                out.push(other);
            }
            None => out.push('\\'),
        }
    }
    out
}

fn load_manifest_text_input(
    manifest: &Manifest,
    inline_key: &str,
    path_key: &str,
    missing_label: &str,
) -> Result<String, String> {
    let inline = manifest.get(inline_key);
    if !inline.is_empty() {
        return Ok(inline.to_string());
    }
    let path = manifest.get(path_key);
    if path.is_empty() {
        return Err(format!("missing {missing_label}"));
    }
    fs::read_to_string(path).map_err(|err| format!("failed to read {missing_label} {path}: {err}"))
}

#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd)]
struct SymbolRecord {
    kind: SymbolRecordKind,
    name: String,
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
enum SymbolRecordKind {
    Function,
    Data,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TargetSpec {
    format: BinaryFormat,
    arch: Architecture,
    endian: Endianness,
}

impl TargetSpec {
    fn parse(target: &str) -> Result<Self, String> {
        let mut parts = target.split('-');
        let arch = match parts.next().unwrap_or("") {
            "aarch64" => Architecture::Aarch64,
            "x86_64" => Architecture::X86_64,
            other => return Err(format!("unsupported target architecture: {other}")),
        };

        if target.contains("apple-darwin") {
            return Ok(Self {
                format: BinaryFormat::MachO,
                arch,
                endian: Endianness::Little,
            });
        }
        if target.contains("linux-gnu") || target.contains("linux-musl") {
            return Ok(Self {
                format: BinaryFormat::Elf,
                arch,
                endian: Endianness::Little,
            });
        }
        Err(format!("unsupported target triple: {target}"))
    }
}

#[no_mangle]
pub unsafe extern "C" fn cheng_uir_validate_v1(
    input_ptr: *const u8,
    input_len: c_int,
    out_len: *mut c_int,
) -> *mut c_void {
    let report = validate_entry_v1(input_ptr, input_len).unwrap_or_else(|err| error_report(&err));
    into_abi_text(report, out_len)
}

#[no_mangle]
pub unsafe extern "C" fn cheng_uir_compile_v1(
    input_ptr: *const u8,
    input_len: c_int,
    options_ptr: *const u8,
    options_len: c_int,
    out_len: *mut c_int,
) -> *mut c_void {
    let report = compile_entry(input_ptr, input_len, options_ptr, options_len)
        .unwrap_or_else(|err| error_report(&err));
    into_abi_text(report, out_len)
}

#[no_mangle]
pub unsafe extern "C" fn cheng_uir_validate_v2(
    input_ptr: *const u8,
    input_len: c_int,
    semantic_ptr: *const u8,
    semantic_len: c_int,
    symbol_ptr: *const u8,
    symbol_len: c_int,
    out_len: *mut c_int,
) -> *mut c_void {
    let report = validate_entry_v2(
        input_ptr,
        input_len,
        semantic_ptr,
        semantic_len,
        symbol_ptr,
        symbol_len,
    )
    .unwrap_or_else(|err| error_report(&err));
    into_abi_text(report, out_len)
}

#[no_mangle]
pub unsafe extern "C" fn cheng_uir_compile_v2(
    input_ptr: *const u8,
    input_len: c_int,
    semantic_ptr: *const u8,
    semantic_len: c_int,
    symbol_ptr: *const u8,
    symbol_len: c_int,
    options_ptr: *const u8,
    options_len: c_int,
    out_len: *mut c_int,
) -> *mut c_void {
    let report = compile_entry_v2(
        input_ptr,
        input_len,
        semantic_ptr,
        semantic_len,
        symbol_ptr,
        symbol_len,
        options_ptr,
        options_len,
    )
    .unwrap_or_else(|err| error_report(&err));
    into_abi_text(report, out_len)
}

#[no_mangle]
pub unsafe extern "C" fn cheng_uir_free_result_v1(ptr: *mut c_void) {
    if !ptr.is_null() {
        let _ = CString::from_raw(ptr.cast::<i8>());
    }
}

unsafe fn validate_entry_v1(input_ptr: *const u8, input_len: c_int) -> Result<String, String> {
    let input_text = read_utf8_input(input_ptr, input_len, "manifest")?;
    let manifest = Manifest::parse(&input_text);
    validate_common(&manifest, None, None)
}

unsafe fn validate_entry_v2(
    input_ptr: *const u8,
    input_len: c_int,
    semantic_ptr: *const u8,
    semantic_len: c_int,
    symbol_ptr: *const u8,
    symbol_len: c_int,
) -> Result<String, String> {
    let input_text = read_utf8_input(input_ptr, input_len, "manifest")?;
    let semantic_text = read_utf8_input(semantic_ptr, semantic_len, "semantic tables")?;
    let symbol_text = read_utf8_input(symbol_ptr, symbol_len, "symbol list")?;
    let manifest = Manifest::parse(&input_text);
    validate_common(&manifest, Some(&semantic_text), Some(&symbol_text))
}

unsafe fn compile_entry(
    input_ptr: *const u8,
    input_len: c_int,
    options_ptr: *const u8,
    options_len: c_int,
) -> Result<String, String> {
    let input_text = read_utf8_input(input_ptr, input_len, "manifest")?;
    let options_text = read_utf8_input(options_ptr, options_len, "options manifest")?;
    let manifest = Manifest::parse(&input_text);
    let options = Manifest::parse(&options_text);
    compile_common(&manifest, &options, None, None)
}

unsafe fn compile_entry_v2(
    input_ptr: *const u8,
    input_len: c_int,
    semantic_ptr: *const u8,
    semantic_len: c_int,
    symbol_ptr: *const u8,
    symbol_len: c_int,
    options_ptr: *const u8,
    options_len: c_int,
) -> Result<String, String> {
    let input_text = read_utf8_input(input_ptr, input_len, "manifest")?;
    let semantic_text = read_utf8_input(semantic_ptr, semantic_len, "semantic tables")?;
    let symbol_text = read_utf8_input(symbol_ptr, symbol_len, "symbol list")?;
    let options_text = read_utf8_input(options_ptr, options_len, "options manifest")?;
    let manifest = Manifest::parse(&input_text);
    let options = Manifest::parse(&options_text);
    compile_common(
        &manifest,
        &options,
        Some(&semantic_text),
        Some(&symbol_text),
    )
}

fn validate_common(
    manifest: &Manifest,
    semantic_text: Option<&str>,
    symbol_text: Option<&str>,
) -> Result<String, String> {
    validate_manifest(
        manifest,
        semantic_text.is_some() || !manifest.get("semantic_tables_inline").is_empty(),
        symbol_text.is_some() || !manifest.get("symbol_list_inline").is_empty(),
    )?;
    if manifest.get("frontend") == "rust" {
        let _ = load_semantic_module_with_override(manifest, semantic_text)?;
        let _ = load_symbol_records_with_override(manifest, symbol_text)?;
    }
    Ok(format!(
        "status=ok\nfrontend={}\ntarget={}\ncrate_type={}\nbridge=bootstrap_stub\n",
        manifest.get("frontend"),
        manifest.get("target"),
        manifest.get("crate_type"),
    ))
}

fn compile_common(
    manifest: &Manifest,
    options: &Manifest,
    semantic_text: Option<&str>,
    symbol_text: Option<&str>,
) -> Result<String, String> {
    validate_manifest(
        manifest,
        semantic_text.is_some() || !manifest.get("semantic_tables_inline").is_empty(),
        symbol_text.is_some() || !manifest.get("symbol_list_inline").is_empty(),
    )?;

    let output_path = options.get("output_path");
    if output_path.is_empty() {
        return Err("missing output_path in compile options".to_string());
    }
    if options.get("emit").is_empty() || options.get("emit") != "obj" {
        return Err(format!(
            "unsupported emit mode: {}",
            if options.get("emit").is_empty() {
                "<empty>"
            } else {
                options.get("emit")
            }
        ));
    }

    let target = TargetSpec::parse(manifest.get("target"))?;
    let semantic_module = load_semantic_module_with_override(manifest, semantic_text)?;
    let semantic_summary = semantic_module.summary();
    let mut symbols = load_symbol_records_with_override(manifest, symbol_text)?;
    if manifest.get("crate_type") == "bin" && !symbols_have_c_main_candidate(&symbols) {
        symbols.insert(SymbolRecord {
            kind: SymbolRecordKind::Function,
            name: "_main".to_string(),
        });
    }
    if symbols.is_empty() {
        return Err("symbol list did not provide any symbols".to_string());
    }

    let (object_bytes, bridge_kind, subset_stats) =
        build_object_bytes(&manifest, &target, &semantic_module, &symbols)?;
    let output_path = PathBuf::from(output_path);
    if let Some(parent) = output_path.parent() {
        fs::create_dir_all(parent).map_err(|err| {
            format!(
                "failed to create output directory {}: {err}",
                parent.display()
            )
        })?;
    }
    fs::write(&output_path, &object_bytes)
        .map_err(|err| format!("failed to write {}: {err}", output_path.display()))?;

    let fn_symbols = symbols
        .iter()
        .filter(|symbol| symbol.kind == SymbolRecordKind::Function)
        .count();
    let data_symbols = symbols.len().saturating_sub(fn_symbols);
    Ok(format!(
        "status=ok\nfrontend={}\ntarget={}\ncrate_type={}\nbridge={}\noutput_path={}\nobj_bytes={}\nsymbols_total={}\nfn_symbols={}\ndata_symbols={}\nsemantic_func_count={}\nsemantic_local_count={}\nsemantic_block_count={}\nsemantic_expr_count={}\nsemantic_stmt_count={}\nreadonly_object_count={}\nreadonly_reloc_count={}\nmemory_abi_calls={}\nhigh_uir_checked=1\nlow_uir_lowered=1\n",
        manifest.get("frontend"),
        manifest.get("target"),
        manifest.get("crate_type"),
        bridge_kind,
        output_path.display(),
        object_bytes.len(),
        symbols.len(),
        fn_symbols,
        data_symbols,
        semantic_summary.func_count,
        semantic_summary.local_count,
        semantic_summary.block_count,
        semantic_summary.expr_count,
        semantic_summary.stmt_count,
        subset_stats.readonly_object_count,
        subset_stats.readonly_reloc_count,
        subset_stats.memory_abi_calls,
    ) + &format!(
        "real_fn_symbols={}\nstub_fn_symbols={}\n",
        subset_stats.real_fn_symbols,
        subset_stats.stub_fn_symbols
    ) + &subset_stats
        .real_symbols
        .iter()
        .map(|symbol| format!("real_fn_symbol={symbol}\n"))
        .collect::<String>()
        + &subset_stats
            .stub_symbols
            .iter()
            .map(|symbol| format!("stub_fn_symbol={symbol}\n"))
            .collect::<String>()
        + &subset_stats
            .stub_reasons
            .iter()
            .map(|(symbol, reason)| format!("stub_fn_reason\tsymbol={symbol}\treason={reason}\n"))
            .collect::<String>())
}

fn validate_manifest(
    manifest: &Manifest,
    has_semantic_inline: bool,
    has_symbol_inline: bool,
) -> Result<(), String> {
    let frontend = manifest.get("frontend");
    if frontend != "rust" && frontend != "stage1" {
        return Err(format!("unsupported frontend: {frontend}"));
    }
    let target = manifest.get("target");
    if target.is_empty() {
        return Err("missing target".to_string());
    }
    TargetSpec::parse(target)?;
    if frontend == "rust" {
        if manifest.get("semantic_tables_path").is_empty() && !has_semantic_inline {
            return Err(
                "missing semantic_tables_path or semantic_tables_inline for rust frontend"
                    .to_string(),
            );
        }
        if manifest.get("symbol_list_path").is_empty() && !has_symbol_inline {
            return Err(
                "missing symbol_list_path or symbol_list_inline for rust frontend".to_string(),
            );
        }
        if manifest.get("crate_type").is_empty() {
            return Err("missing crate_type for rust frontend".to_string());
        }
    }
    Ok(())
}

#[cfg_attr(not(test), allow(dead_code))]
fn load_semantic_module(manifest: &Manifest) -> Result<SemanticModuleData, String> {
    load_semantic_module_with_override(manifest, None)
}

fn load_semantic_module_with_override(
    manifest: &Manifest,
    semantic_text_override: Option<&str>,
) -> Result<SemanticModuleData, String> {
    let text = if let Some(text) = semantic_text_override {
        text.to_string()
    } else if manifest.get("semantic_tables_inline").is_empty()
        && manifest.get("semantic_tables_path").is_empty()
    {
        return Ok(SemanticModuleData::default());
    } else {
        load_manifest_text_input(
            manifest,
            "semantic_tables_inline",
            "semantic_tables_path",
            "semantic tables",
        )?
    };
    let mut module = SemanticModuleData::default();
    for raw_line in text.lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some((key, value)) = line.split_once('=') {
            if !line.contains('\t') {
                match key.trim() {
                    "frontend" => module.frontend = unescape_table_field(value.trim()),
                    "target" => module.target = unescape_table_field(value.trim()),
                    "crate_type" => module.crate_type = unescape_table_field(value.trim()),
                    _ => {}
                }
                continue;
            }
        }
        let kind = line.split('\t').next().unwrap_or_default();
        let fields = parse_table_fields(line);
        match kind {
            "string" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len(&mut module.strings, idx + 1);
                module.strings[idx] = unescape_table_field(field_str(&fields, "value"));
            }
            "type" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len(&mut module.types, idx + 1);
                module.types[idx] = unescape_table_field(field_str(&fields, "name"));
            }
            "layout" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.layouts, idx + 1);
                module.layouts[idx] = SemanticLayout {
                    name: unescape_table_field(field_str(&fields, "name")),
                    size_bytes: field_str(&fields, "size")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field size: {err}"))?,
                    align_bytes: field_str(&fields, "align")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field align: {err}"))?,
                    abi_class: unescape_table_field(field_str(&fields, "abi")),
                };
            }
            "vtable" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.vtable_descs, idx + 1);
                let entries = unescape_table_field(field_str(&fields, "entries"));
                module.vtable_descs[idx] = SemanticVtableDesc {
                    label: unescape_table_field(field_str(&fields, "label")),
                    entries: if entries.is_empty() {
                        Vec::new()
                    } else {
                        entries.split(',').map(str::to_string).collect()
                    },
                };
            }
            "const" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.const_values, idx + 1);
                module.const_values[idx] = SemanticConstValue {
                    type_idx: parse_i32_field(&fields, "type")?,
                    kind: unescape_table_field(field_str(&fields, "kind")),
                    atom_a_kind: unescape_table_field(field_str(&fields, "atom_a_kind")),
                    atom_a: unescape_table_field(field_str(&fields, "atom_a")),
                    atom_a_addend: parse_i64_field(&fields, "atom_a_addend")?,
                    atom_b_kind: unescape_table_field(field_str(&fields, "atom_b_kind")),
                    atom_b: unescape_table_field(field_str(&fields, "atom_b")),
                    atom_b_addend: parse_i64_field(&fields, "atom_b_addend")?,
                };
            }
            "readonly_object" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.readonly_objects, idx + 1);
                module.readonly_objects[idx] = SemanticReadonlyObject {
                    label: unescape_table_field(field_str(&fields, "label")),
                    type_idx: parse_i32_field(&fields, "type")?,
                    size_bytes: field_str(&fields, "size")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field size: {err}"))?,
                    align_bytes: field_str(&fields, "align")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field align: {err}"))?,
                    bytes: decode_hex(field_str(&fields, "bytes"))?,
                };
            }
            "readonly_reloc" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.readonly_relocs, idx + 1);
                module.readonly_relocs[idx] = SemanticReadonlyReloc {
                    object_idx: parse_usize_field(&fields, "object")?,
                    offset_bytes: field_str(&fields, "offset")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field offset: {err}"))?,
                    target_kind: unescape_table_field(field_str(&fields, "target_kind")),
                    target: unescape_table_field(field_str(&fields, "target")),
                    addend: parse_i64_field(&fields, "addend")?,
                };
            }
            "data_symbol" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.data_symbols, idx + 1);
                module.data_symbols[idx] = SemanticDataSymbol {
                    symbol: unescape_table_field(field_str(&fields, "symbol")),
                    object_idx: parse_usize_field(&fields, "object")?,
                };
            }
            "aggregate" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.aggregate_layouts, idx + 1);
                module.aggregate_layouts[idx] = SemanticAggregateLayout {
                    type_idx: parse_i32_field(&fields, "type")?,
                    size_bytes: field_str(&fields, "size")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field size: {err}"))?,
                    align_bytes: field_str(&fields, "align")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field align: {err}"))?,
                    field_start: parse_usize_field(&fields, "field_start")?,
                    field_len: parse_usize_field(&fields, "field_len")?,
                };
            }
            "aggregate_field" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.aggregate_fields, idx + 1);
                module.aggregate_fields[idx] = SemanticAggregateField {
                    aggregate_idx: parse_usize_field(&fields, "aggregate")?,
                    field_index: parse_usize_field(&fields, "field_index")?,
                    offset_bytes: field_str(&fields, "offset")
                        .parse::<u64>()
                        .map_err(|err| format!("invalid u64 field offset: {err}"))?,
                    type_idx: parse_i32_field(&fields, "type")?,
                };
            }
            "func_abi" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.func_abi, idx + 1);
                let arg_modes = unescape_table_field(field_str(&fields, "arg_modes"));
                module.func_abi[idx] = SemanticFuncAbi {
                    func_name: unescape_table_field(field_str(&fields, "func")),
                    ret_mode: unescape_table_field(field_str(&fields, "ret_mode")),
                    arg_modes: split_csv_field(&arg_modes),
                };
            }
            "func" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.funcs, idx + 1);
                module.funcs[idx] = SemanticFunc {
                    name: unescape_table_field(field_str(&fields, "name")),
                    first_block: parse_usize_field(&fields, "first_block")?,
                    block_len: parse_usize_field(&fields, "block_len")?,
                    first_local: parse_usize_field(&fields, "first_local")?,
                    local_len: parse_usize_field(&fields, "local_len")?,
                };
            }
            "local" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.locals, idx + 1);
                module.locals[idx] = SemanticLocal {
                    name: unescape_table_field(field_str(&fields, "name")),
                    type_idx: parse_i32_field(&fields, "type")?,
                };
            }
            "block" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.blocks, idx + 1);
                module.blocks[idx] = SemanticBlock {
                    first_stmt: parse_usize_field(&fields, "first_stmt")?,
                    stmt_len: parse_usize_field(&fields, "stmt_len")?,
                    term_kind: unescape_table_field(field_str(&fields, "term_kind")),
                };
            }
            "expr" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.exprs, idx + 1);
                module.exprs[idx] = SemanticExpr {
                    kind: unescape_table_field(field_str(&fields, "kind")),
                    type_idx: parse_i32_field(&fields, "type")?,
                    a: parse_i32_field(&fields, "a")?,
                    b: parse_i32_field(&fields, "b")?,
                    c: parse_i32_field(&fields, "c")?,
                };
            }
            "stmt" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.stmts, idx + 1);
                module.stmts[idx] = SemanticStmt {
                    kind: unescape_table_field(field_str(&fields, "kind")),
                    a: parse_i32_field(&fields, "a")?,
                    b: parse_i32_field(&fields, "b")?,
                    c: parse_i32_field(&fields, "c")?,
                };
            }
            "call_abi" => {
                let idx = parse_usize_field(&fields, "idx")?;
                ensure_len_with(&mut module.call_abi, idx + 1);
                let arg_modes = unescape_table_field(field_str(&fields, "arg_modes"));
                module.call_abi[idx] = SemanticCallAbi {
                    stmt_idx: parse_usize_field_opt(&fields, "stmt_idx")?.unwrap_or_default(),
                    callee: unescape_table_field(field_str(&fields, "callee")),
                    abi: unescape_table_field(field_str(&fields, "abi")),
                    call_kind: unescape_table_field(field_str(&fields, "call_kind")),
                    vtable_slot: parse_i32_field_opt(&fields, "vtable_slot")?.unwrap_or(-1),
                    can_unwind: field_str(&fields, "can_unwind") == "1",
                    ret_mode: unescape_table_field(field_str(&fields, "ret_mode")),
                    arg_modes: split_csv_field(&arg_modes),
                };
            }
            _ => {}
        }
    }
    Ok(module)
}

#[cfg_attr(not(test), allow(dead_code))]
fn load_symbol_records(manifest: &Manifest) -> Result<BTreeSet<SymbolRecord>, String> {
    load_symbol_records_with_override(manifest, None)
}

fn load_symbol_records_with_override(
    manifest: &Manifest,
    symbol_text_override: Option<&str>,
) -> Result<BTreeSet<SymbolRecord>, String> {
    let text = if let Some(text) = symbol_text_override {
        text.to_string()
    } else if manifest.get("symbol_list_inline").is_empty()
        && manifest.get("symbol_list_path").is_empty()
    {
        return Ok(BTreeSet::new());
    } else {
        load_manifest_text_input(
            manifest,
            "symbol_list_inline",
            "symbol_list_path",
            "symbol list",
        )?
    };
    let mut out = BTreeSet::new();
    for line in text.lines() {
        let mut kind = None;
        let mut symbol = None;
        for token in line.split('\t') {
            if let Some((key, value)) = token.split_once('=') {
                match key {
                    "kind" => kind = Some(value),
                    "symbol" => symbol = Some(value),
                    _ => {}
                }
            }
        }
        let Some(symbol_name) = symbol else {
            continue;
        };
        if symbol_name.is_empty() {
            continue;
        }
        let record_kind = match kind.unwrap_or("fn") {
            "static" => SymbolRecordKind::Data,
            "global_asm" => SymbolRecordKind::Function,
            _ => SymbolRecordKind::Function,
        };
        out.insert(SymbolRecord {
            kind: record_kind,
            name: symbol_name.to_string(),
        });
    }
    Ok(out)
}

unsafe fn read_utf8_input(ptr: *const u8, len: c_int, label: &str) -> Result<String, String> {
    std::str::from_utf8(input_bytes(ptr, len)?)
        .map(str::to_string)
        .map_err(|err| format!("invalid UTF-8 {label}: {err}"))
}

fn build_object_bytes(
    manifest: &Manifest,
    target: &TargetSpec,
    semantic_module: &SemanticModuleData,
    symbols: &BTreeSet<SymbolRecord>,
) -> Result<(Vec<u8>, &'static str, SubsetBuildStats), String> {
    if let Some((asm, stats)) = build_subset_asm(manifest, target, semantic_module, symbols)? {
        let bytes = assemble_subset_object(manifest.get("target"), &asm)?;
        return Ok((bytes, "bootstrap_subset_asm", stats));
    }
    let bytes = build_stub_object(target, symbols)?;
    Ok((
        bytes,
        "bootstrap_stub",
        SubsetBuildStats {
            real_fn_symbols: 0,
            stub_fn_symbols: symbols.len(),
            readonly_object_count: 0,
            readonly_reloc_count: 0,
            memory_abi_calls: 0,
            real_symbols: Vec::new(),
            stub_symbols: symbols.iter().map(|symbol| symbol.name.clone()).collect(),
            stub_reasons: symbols
                .iter()
                .map(|symbol| (symbol.name.clone(), "subset asm unavailable".to_string()))
                .collect(),
        },
    ))
}

fn build_subset_asm(
    manifest: &Manifest,
    target: &TargetSpec,
    semantic_module: &SemanticModuleData,
    symbols: &BTreeSet<SymbolRecord>,
) -> Result<Option<(String, SubsetBuildStats)>, String> {
    if target.format != BinaryFormat::MachO || target.arch != Architecture::Aarch64 {
        return Ok(None);
    }
    if semantic_module.frontend != "rust" || semantic_module.funcs.is_empty() {
        return Ok(None);
    }
    let exported_data_symbols = semantic_module
        .data_symbols
        .iter()
        .map(|entry| entry.symbol.as_str())
        .collect::<BTreeSet<_>>();
    if symbols
        .iter()
        .filter(|symbol| symbol.kind == SymbolRecordKind::Data)
        .any(|symbol| !exported_data_symbols.contains(symbol.name.as_str()))
    {
        return Ok(None);
    }
    let func_lookup = semantic_module
        .funcs
        .iter()
        .enumerate()
        .map(|(idx, func)| (func.name.as_str(), idx))
        .collect::<BTreeMap<_, _>>();
    let c_main_target = manifest.get("c_main_target_symbol");

    let mut asm = String::new();
    let mut stats = SubsetBuildStats {
        readonly_object_count: semantic_module.readonly_objects.len(),
        readonly_reloc_count: semantic_module.readonly_relocs.len(),
        memory_abi_calls: semantic_module
            .call_abi
            .iter()
            .filter(|call| {
                call.ret_mode == "sret_x8"
                    || call
                        .arg_modes
                        .iter()
                        .any(|mode| mode == "indirect" || mode == "by_stack")
            })
            .count(),
        ..SubsetBuildStats::default()
    };
    emit_readonly_data(&mut asm, semantic_module, &exported_data_symbols);
    asm.push_str(".text\n");
    asm.push_str(".subsections_via_symbols\n");
    let ordered = symbols.iter().collect::<Vec<_>>();
    let mut emitted_globals = BTreeSet::new();
    for pass in [false, true] {
        for symbol in &ordered {
            if (symbol.name == "_main") != pass {
                continue;
            }
            if symbol.kind == SymbolRecordKind::Data {
                continue;
            }
            let global_name = target_symbol_name(target.format, &symbol.name);
            if emitted_globals.contains(&global_name) {
                continue;
            }
            if symbol.name == "_main" && !c_main_target.is_empty() {
                emit_c_main_wrapper(&mut asm, semantic_module, &func_lookup, c_main_target)?;
                emitted_globals.insert(global_name);
                stats.real_fn_symbols += 1;
                stats.real_symbols.push(symbol.name.clone());
                continue;
            }
            if let Some(&func_index) = func_lookup.get(symbol.name.as_str()) {
                let func = &semantic_module.funcs[func_index];
                let mut func_asm = String::new();
                match emit_aarch64_function(&mut func_asm, semantic_module, func_index, func) {
                    Ok(()) => {
                        asm.push_str(&func_asm);
                        emitted_globals.insert(global_name);
                        stats.real_fn_symbols += 1;
                        stats.real_symbols.push(symbol.name.clone());
                        continue;
                    }
                    Err(err) => {
                        emit_stub_function_asm(&mut asm, target.format, &symbol.name);
                        emitted_globals.insert(global_name);
                        stats.stub_fn_symbols += 1;
                        stats.stub_symbols.push(symbol.name.clone());
                        stats.stub_reasons.push((symbol.name.clone(), err));
                        continue;
                    }
                }
            }
            if emit_rust_allocator_shim_asm(&mut asm, target.format, &symbol.name)? {
                emitted_globals.insert(global_name);
                stats.real_fn_symbols += 1;
                stats.real_symbols.push(symbol.name.clone());
                continue;
            }
            emit_stub_function_asm(&mut asm, target.format, &symbol.name);
            emitted_globals.insert(global_name);
            stats.stub_fn_symbols += 1;
            stats.stub_symbols.push(symbol.name.clone());
            stats
                .stub_reasons
                .push((symbol.name.clone(), "missing semantic function".to_string()));
        }
    }
    emit_vtable_data(&mut asm, &semantic_module.vtable_descs);
    let strict_stub_details = stats
        .stub_reasons
        .iter()
        .filter(|(symbol, _)| {
            symbol.contains("core3fmt2rt")
                || symbol.contains("std2io5stdio6_print")
                || symbol.contains("std2rt10lang_start")
        })
        .map(|(symbol, reason)| format!("{symbol}: {reason}"))
        .collect::<Vec<_>>();
    if !strict_stub_details.is_empty() {
        return Err(format!(
            "subset asm: strict std/fmt coverage still contains bootstrap stubs: {}",
            strict_stub_details.join(" | ")
        ));
    }
    Ok(Some((asm, stats)))
}

fn emit_readonly_data(
    asm: &mut String,
    module: &SemanticModuleData,
    exported_symbols: &BTreeSet<&str>,
) {
    if module.readonly_objects.is_empty() {
        return;
    }
    asm.push_str(".section __DATA,__const\n");
    for (index, object) in module.readonly_objects.iter().enumerate() {
        let align = u64_align_pow2(object.align_bytes.max(1));
        let label = target_symbol_name(BinaryFormat::MachO, &object.label);
        if exported_symbols.contains(object.label.as_str()) {
            let _ = writeln!(asm, ".globl {label}");
        }
        let _ = writeln!(asm, ".p2align {}", align.trailing_zeros());
        let _ = writeln!(asm, "{label}:");
        let mut relocs = module
            .readonly_relocs
            .iter()
            .filter(|reloc| reloc.object_idx == index)
            .collect::<Vec<_>>();
        relocs.sort_by_key(|reloc| reloc.offset_bytes);
        let mut cursor = 0usize;
        for reloc in relocs {
            let reloc_offset = reloc.offset_bytes as usize;
            if reloc_offset > cursor {
                emit_byte_span(asm, &object.bytes[cursor..reloc_offset]);
            }
            emit_relocation_quad(asm, reloc);
            cursor = reloc_offset.saturating_add(8);
        }
        if cursor < object.bytes.len() {
            emit_byte_span(asm, &object.bytes[cursor..]);
        }
    }
}

fn emit_byte_span(asm: &mut String, bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    for chunk in bytes.chunks(16) {
        let rendered = chunk
            .iter()
            .map(|byte| format!("0x{byte:02x}"))
            .collect::<Vec<_>>()
            .join(", ");
        let _ = writeln!(asm, "  .byte {rendered}");
    }
}

fn emit_relocation_quad(asm: &mut String, reloc: &SemanticReadonlyReloc) {
    let target = match reloc.target_kind.as_str() {
        "readonly" | "vtable" => target_symbol_name(BinaryFormat::MachO, &reloc.target),
        "symbol" => target_symbol_name(BinaryFormat::MachO, &reloc.target),
        _ => reloc.target.clone(),
    };
    if reloc.addend == 0 {
        let _ = writeln!(asm, "  .quad {target}");
    } else if reloc.addend > 0 {
        let _ = writeln!(asm, "  .quad {target} + {}", reloc.addend);
    } else {
        let _ = writeln!(asm, "  .quad {target} - {}", reloc.addend.unsigned_abs());
    }
}

fn function_abi_for<'a>(
    module: &'a SemanticModuleData,
    func: &SemanticFunc,
) -> Result<SemanticFuncAbi, String> {
    if let Some(found) = module
        .func_abi
        .iter()
        .find(|abi| abi.func_name == func.name)
    {
        return Ok(found.clone());
    }
    let ret_local = module_local(module, func, 0)?;
    let ret_mode = if type_is_scalar_pair(module, ret_local.type_idx)? {
        "scalar_pair".to_string()
    } else {
        match width_for_type(module, ret_local.type_idx) {
            Ok(ScalarWidth::Void) => "void".to_string(),
            Ok(_) => "scalar".to_string(),
            Err(_) => "void".to_string(),
        }
    };
    let mut arg_modes = Vec::new();
    for slot in 1..func.local_len {
        let local = module_local(module, func, slot)?;
        if parse_arg_local_index(&local.name).is_none() {
            continue;
        }
        let mode = if type_is_scalar_pair(module, local.type_idx)? {
            "scalar_pair"
        } else {
            match width_for_type(module, local.type_idx) {
                Ok(ScalarWidth::Void) => "void",
                Ok(_) => "scalar",
                Err(_) => "indirect",
            }
        };
        arg_modes.push(mode.to_string());
    }
    Ok(SemanticFuncAbi {
        func_name: func.name.clone(),
        ret_mode,
        arg_modes,
    })
}

fn call_abi_for_stmt<'a>(
    module: &'a SemanticModuleData,
    stmt_index: usize,
    stmt: &SemanticStmt,
) -> Result<SemanticCallAbi, String> {
    if let Some(found) = module
        .call_abi
        .iter()
        .find(|abi| abi.stmt_idx == stmt_index)
    {
        return Ok(found.clone());
    }
    let destination_type = module_expr(module, stmt.c)?.type_idx;
    let ret_mode = if destination_type < 0 {
        "void".to_string()
    } else if type_is_scalar_pair(module, destination_type)? {
        "scalar_pair".to_string()
    } else {
        match width_for_type(module, destination_type) {
            Ok(ScalarWidth::Void) => "void".to_string(),
            Ok(_) => "scalar".to_string(),
            Err(_) => "indirect".to_string(),
        }
    };
    let arg_modes = operand_list_expr_ids(module, stmt.b)?
        .into_iter()
        .map(|expr_id| {
            let expr = module_expr(module, expr_id)?;
            if type_is_scalar_pair(module, expr.type_idx)? {
                Ok("scalar_pair".to_string())
            } else {
                match width_for_type(module, expr.type_idx) {
                    Ok(ScalarWidth::Void) => Ok("void".to_string()),
                    Ok(_) => Ok("scalar".to_string()),
                    Err(_) => Ok("indirect".to_string()),
                }
            }
        })
        .collect::<Result<Vec<_>, String>>()?;
    Ok(SemanticCallAbi {
        stmt_idx: stmt_index,
        callee: String::new(),
        abi: String::new(),
        call_kind: "fallback".to_string(),
        vtable_slot: -1,
        can_unwind: false,
        ret_mode,
        arg_modes,
    })
}

fn build_frame_plan(
    module: &SemanticModuleData,
    func: &SemanticFunc,
    abi: &SemanticFuncAbi,
) -> Result<FramePlan, String> {
    let mut offset = 0i32;
    let mut locals = Vec::with_capacity(func.local_len);
    for slot in 0..func.local_len {
        let local = module_local(module, func, slot)?;
        let (size, align) = size_align_for_type_idx(module, local.type_idx)?;
        let storage = if size == 0 {
            LocalStorage::Void
        } else if slot == 0 && abi.ret_mode == "sret_x8" {
            offset = align_to(offset, 8);
            let pointer_offset = offset;
            offset += 8;
            LocalStorage::Indirect { pointer_offset }
        } else if let Some(arg_index) = parse_arg_local_index(&local.name) {
            let arg_mode = abi
                .arg_modes
                .get(arg_index)
                .map(String::as_str)
                .unwrap_or("scalar");
            if arg_mode == "indirect" || arg_mode == "by_stack" {
                offset = align_to(offset, 8);
                let pointer_offset = offset;
                offset += 8;
                LocalStorage::Indirect { pointer_offset }
            } else {
                offset = align_to(offset, align);
                let local_offset = offset;
                offset += size;
                LocalStorage::Direct {
                    offset: local_offset,
                }
            }
        } else {
            offset = align_to(offset, align);
            let local_offset = offset;
            offset += size;
            LocalStorage::Direct {
                offset: local_offset,
            }
        };
        locals.push(FrameLocal {
            storage,
            type_idx: local.type_idx,
        });
    }
    let frame_size = align_to(offset + 16, 16);
    let frame_record_offset = frame_size - 16;
    Ok(FramePlan {
        locals,
        frame_size,
        frame_record_offset,
    })
}

fn emit_c_main_wrapper(
    asm: &mut String,
    module: &SemanticModuleData,
    func_lookup: &BTreeMap<&str, usize>,
    target_symbol: &str,
) -> Result<(), String> {
    let Some(&func_index) = func_lookup.get(target_symbol) else {
        emit_stub_function_asm(asm, BinaryFormat::MachO, "_main");
        return Ok(());
    };
    let func = &module.funcs[func_index];
    let ret_width = width_for_local_slot(module, func, 0)?;
    let global_name = target_symbol_name(BinaryFormat::MachO, "_main");
    let callee_name = target_symbol_name(BinaryFormat::MachO, target_symbol);
    let _ = writeln!(asm, ".globl {global_name}");
    let _ = writeln!(asm, ".p2align 2");
    let _ = writeln!(asm, "{global_name}:");
    let _ = writeln!(asm, "  sub sp, sp, #16");
    let _ = writeln!(asm, "  stp x29, x30, [sp, #0]");
    let _ = writeln!(asm, "  mov x29, sp");
    let _ = writeln!(asm, "  bl {callee_name}");
    if ret_width == ScalarWidth::Void {
        let _ = writeln!(asm, "  mov w0, #0");
    }
    let _ = writeln!(asm, "  ldp x29, x30, [sp, #0]");
    let _ = writeln!(asm, "  add sp, sp, #16");
    let _ = writeln!(asm, "  ret");
    Ok(())
}

fn emit_stub_function_asm(asm: &mut String, format: BinaryFormat, symbol_name: &str) {
    let global_name = target_symbol_name(format, symbol_name);
    let _ = writeln!(asm, ".globl {global_name}");
    let _ = writeln!(asm, ".p2align 2");
    let _ = writeln!(asm, "{global_name}:");
    let _ = writeln!(asm, "  mov w0, #0");
    let _ = writeln!(asm, "  ret");
}

fn emit_rust_allocator_shim_asm(
    asm: &mut String,
    format: BinaryFormat,
    symbol_name: &str,
) -> Result<bool, String> {
    let global_name = target_symbol_name(format, symbol_name);
    if let Some(target_name) = rust_allocator_wrapper_target(symbol_name) {
        let target_name = target_symbol_name(format, &target_name);
        let _ = writeln!(asm, ".globl {global_name}");
        let _ = writeln!(asm, ".p2align 2");
        let _ = writeln!(asm, "{global_name}:");
        let _ = writeln!(asm, "  b {target_name}");
        return Ok(true);
    }
    if symbol_name.ends_with("35___rust_no_alloc_shim_is_unstable_v2") {
        let _ = writeln!(asm, ".globl {global_name}");
        let _ = writeln!(asm, ".p2align 2");
        let _ = writeln!(asm, "{global_name}:");
        let _ = writeln!(asm, "  ret");
        return Ok(true);
    }
    if symbol_name.ends_with("42___rust_alloc_error_handler_should_panic_v2") {
        let _ = writeln!(asm, ".globl {global_name}");
        let _ = writeln!(asm, ".p2align 2");
        let _ = writeln!(asm, "{global_name}:");
        let _ = writeln!(asm, "  mov w0, #0");
        let _ = writeln!(asm, "  ret");
        return Ok(true);
    }
    Ok(false)
}

fn rust_allocator_wrapper_target(symbol_name: &str) -> Option<String> {
    replace_symbol_suffix(symbol_name, "12___rust_alloc", "11___rdl_alloc")
        .or_else(|| replace_symbol_suffix(symbol_name, "14___rust_dealloc", "13___rdl_dealloc"))
        .or_else(|| replace_symbol_suffix(symbol_name, "14___rust_realloc", "13___rdl_realloc"))
        .or_else(|| {
            replace_symbol_suffix(
                symbol_name,
                "19___rust_alloc_zeroed",
                "18___rdl_alloc_zeroed",
            )
        })
}

fn replace_symbol_suffix(symbol_name: &str, suffix: &str, replacement: &str) -> Option<String> {
    symbol_name
        .strip_suffix(suffix)
        .map(|prefix| format!("{prefix}{replacement}"))
}

fn emit_aarch64_function(
    asm: &mut String,
    module: &SemanticModuleData,
    func_index: usize,
    func: &SemanticFunc,
) -> Result<(), String> {
    if func.local_len == 0 {
        return Err(format!("subset asm: func {} missing locals", func.name));
    }
    let func_abi = function_abi_for(module, func)?;
    let frame = build_frame_plan(module, func, &func_abi)?;
    let global_name = target_symbol_name(BinaryFormat::MachO, &func.name);
    let _ = writeln!(asm, ".globl {global_name}");
    let _ = writeln!(asm, ".p2align 2");
    let _ = writeln!(asm, "{global_name}:");
    let _ = writeln!(asm, "  sub sp, sp, #{}", frame.frame_size);
    let _ = writeln!(asm, "  stp x29, x30, [sp, #{}]", frame.frame_record_offset);
    let _ = writeln!(asm, "  add x29, sp, #{}", frame.frame_record_offset);
    if func_abi.ret_mode == "sret_x8" {
        store_pointer_local(&frame, 0, 8, asm)?;
    }
    let mut next_arg_reg = 0u8;
    for slot in 0..func.local_len {
        let local = module_local(module, func, slot)?;
        let Some(arg_index) = parse_arg_local_index(&local.name) else {
            continue;
        };
        let arg_mode = func_abi
            .arg_modes
            .get(arg_index)
            .map(String::as_str)
            .unwrap_or("scalar");
        match arg_mode {
            "void" => {}
            "scalar_pair" => {
                if next_arg_reg > 6 {
                    return Err(format!(
                        "subset asm: arg local {} exceeds x7 for scalar-pair ABI",
                        local.name
                    ));
                }
                emit_store_scalar_pair_local(
                    asm,
                    module,
                    func,
                    &frame,
                    slot,
                    next_arg_reg,
                    next_arg_reg + 1,
                )?;
                next_arg_reg += 2;
            }
            "indirect" | "by_stack" => {
                store_pointer_local(&frame, slot, next_arg_reg, asm)?;
                next_arg_reg += 1;
            }
            _ => {
                let width = width_for_type(module, local.type_idx)?;
                if width == ScalarWidth::Void {
                    continue;
                }
                store_direct_local(&frame, slot, width, next_arg_reg, asm)?;
                next_arg_reg += 1;
            }
        }
    }

    for local_block in 0..func.block_len {
        let block = module_block(module, func, local_block)?;
        let _ = writeln!(asm, "{}:", block_label(func_index, local_block));
        let stmt_start = block.first_stmt;
        let stmt_end = block.first_stmt + block.stmt_len;
        let mut term_built = false;
        for stmt_index in stmt_start..stmt_end {
            let stmt = module
                .stmts
                .get(stmt_index)
                .ok_or_else(|| format!("subset asm: stmt index out of range {stmt_index}"))?;
            match stmt.kind.as_str() {
                "assign" => {
                    emit_assign_stmt(asm, module, func, &frame, stmt)?;
                }
                "term.return" => {
                    match func_abi.ret_mode.as_str() {
                        "void" | "sret_x8" => {}
                        "scalar_pair" => {
                            emit_scalar_pair_expr(asm, module, func, &frame, stmt.a, 0, 1)?;
                        }
                        _ => {
                            let ret_width = width_for_local_slot(module, func, 0)?;
                            if ret_width != ScalarWidth::Void {
                                emit_aarch64_expr(asm, module, func, &frame, stmt.a, 0)?;
                            }
                        }
                    }
                    emit_epilogue(asm, frame.frame_size, frame.frame_record_offset);
                    term_built = true;
                }
                "term.goto" => {
                    let _ = writeln!(asm, "  b {}", block_label(func_index, stmt.a as usize));
                    term_built = true;
                }
                "term.call" => {
                    emit_call_stmt(
                        asm, module, func, &frame, &func_abi, func_index, stmt_index, stmt,
                        stmt_start, stmt_end,
                    )?;
                    term_built = true;
                }
                "term.assert" => {
                    emit_assert_stmt(asm, module, func, &frame, func_index, stmt)?;
                    term_built = true;
                }
                "term.drop" => {
                    emit_drop_stmt(asm, module, func, &frame, func_index, stmt)?;
                    term_built = true;
                }
                "term.unwind_resume" | "term.unwind_terminate" | "term.unreachable" => {
                    let _ = writeln!(asm, "  brk #0");
                    term_built = true;
                }
                "term.call_symbol" | "term.call_target" => {}
                "storage_live" | "storage_dead" | "nop" | "fake_read" | "retag"
                | "place_mention" | "coverage" => {}
                other => {
                    return Err(format!(
                        "subset asm: unsupported stmt kind {other} in {}",
                        func.name
                    ));
                }
            }
        }
        if !term_built {
            if block.term_kind == "return" {
                match func_abi.ret_mode.as_str() {
                    "void" | "sret_x8" => {}
                    "scalar_pair" => {
                        emit_scalar_pair_expr(asm, module, func, &frame, 0, 0, 1)?;
                    }
                    _ => {
                        let ret_width = width_for_local_slot(module, func, 0)?;
                        if ret_width != ScalarWidth::Void {
                            emit_load_place(
                                asm,
                                resolve_local_slot_location(func, &frame, 0)?,
                                ret_width,
                                0,
                            );
                        }
                    }
                }
                emit_epilogue(asm, frame.frame_size, frame.frame_record_offset);
            } else {
                return Err(format!(
                    "subset asm: block {} in {} missing supported terminator {}",
                    local_block, func.name, block.term_kind
                ));
            }
        }
    }
    Ok(())
}

fn emit_assign_stmt(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    stmt: &SemanticStmt,
) -> Result<(), String> {
    let value = module_expr(module, stmt.b)?;
    match value.kind.as_str() {
        "rvalue.ref" => {
            emit_ref_assign(asm, module, func, frame, stmt.a, value)?;
            return Ok(());
        }
        "rvalue.aggregate" => {
            emit_aggregate_assign(asm, module, func, frame, stmt.a, value)?;
            return Ok(());
        }
        "rvalue.raw_ptr" => {
            emit_raw_ptr_assign(asm, module, func, frame, stmt.a, value)?;
            return Ok(());
        }
        "rvalue.cast" => {
            if emit_unsize_cast_assign(asm, module, func, frame, stmt.a, value)? {
                return Ok(());
            }
            if emit_cast_assign(asm, module, func, frame, stmt.a, value)? {
                return Ok(());
            }
        }
        _ => {}
    }
    if value.kind == "rvalue.binary" {
        let op_name = module_string(module, value.c)?;
        if op_name == "AddWithOverflow" || op_name == "SubWithOverflow" {
            return emit_checked_overflow_assign(asm, module, func, frame, stmt.a, value, op_name);
        }
    }
    if try_emit_const_blob_assign(asm, module, func, frame, stmt.a, stmt.b)? {
        return Ok(());
    }
    if type_is_scalar_pair(module, value.type_idx)? {
        emit_scalar_pair_expr(asm, module, func, frame, stmt.b, 8, 9)?;
        emit_store_scalar_pair_place(asm, module, func, frame, stmt.a, 8, 9)?;
        return Ok(());
    }
    emit_aarch64_expr(asm, module, func, frame, stmt.b, 8)?;
    emit_store_place(asm, module, func, frame, stmt.a, 8)?;
    Ok(())
}

fn emit_ref_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
) -> Result<(), String> {
    emit_place_address(asm, module, func, frame, value.a, 8)?;
    emit_store_place(asm, module, func, frame, place_expr_id, 8)?;
    Ok(())
}

fn emit_raw_ptr_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
) -> Result<(), String> {
    if type_is_scalar_pair(module, value.type_idx)? {
        return Err("subset asm: unsized raw pointer lowering is unsupported".to_string());
    }
    emit_place_address(asm, module, func, frame, value.a, 8)?;
    emit_store_place(asm, module, func, frame, place_expr_id, 8)?;
    Ok(())
}

fn emit_aggregate_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
) -> Result<(), String> {
    let dest = resolve_place_location(module, func, frame, place_expr_id)?;
    let field_expr_ids = parse_expr_id_list(module, value.b)?;
    if let Some(aggregate) = aggregate_layout_for_type(module, value.type_idx) {
        for (field_index, field_expr_id) in field_expr_ids.into_iter().enumerate() {
            let field = aggregate_field(module, aggregate, field_index)?;
            if type_is_scalar_pair(module, field.type_idx)? {
                emit_scalar_pair_expr(asm, module, func, frame, field_expr_id, 8, 9)?;
                emit_store_scalar_pair_place_at_offset(asm, dest, field.offset_bytes as i32, 8, 9)?;
                continue;
            }
            let width = emit_aarch64_expr(asm, module, func, frame, field_expr_id, 8)?;
            emit_store_place_at_offset(asm, dest, field.offset_bytes as i32, width, 8)?;
        }
        return Ok(());
    }
    let mut field_offset = 0i32;
    for field_expr_id in field_expr_ids {
        let width = emit_aarch64_expr(asm, module, func, frame, field_expr_id, 8)?;
        let (size, align) = size_align_for_width(width);
        field_offset = align_to(field_offset, align);
        emit_store_place_at_offset(asm, dest, field_offset, width, 8)?;
        field_offset += size;
    }
    Ok(())
}

fn try_emit_const_blob_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value_expr_id: i32,
) -> Result<bool, String> {
    let value = module_expr(module, value_expr_id)?;
    if value.kind != "operand.const" {
        return Ok(false);
    }
    let Some(const_value) = module_const(module, value.a) else {
        return Ok(false);
    };
    match const_value.kind.as_str() {
        "zero_sized" => Ok(true),
        "scalar_pair" => {
            emit_scalar_pair_const(asm, module, const_value, 8, 9)?;
            emit_store_scalar_pair_place(asm, module, func, frame, place_expr_id, 8, 9)?;
            Ok(true)
        }
        _ => Ok(false),
    }
}

fn emit_cast_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
) -> Result<bool, String> {
    let cast_kind = module_string(module, value.b)?;
    match cast_kind {
        "IntToInt"
        | "PointerExposeAddress"
        | "PointerCoercion(MutToConstPointer, AsCast)"
        | "PointerCoercion(ArrayToPointer, AsCast)" => {
            emit_aarch64_expr(asm, module, func, frame, value.a, 8)?;
            emit_store_place(asm, module, func, frame, place_expr_id, 8)?;
            Ok(true)
        }
        _ => Ok(false),
    }
}

fn emit_unsize_cast_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
) -> Result<bool, String> {
    let cast_kind = module_string(module, value.b)?;
    if cast_kind != "PointerCoercion(Unsize, Implicit)" {
        return Ok(false);
    }
    if !type_is_scalar_pair(module, value.type_idx)? {
        return Ok(false);
    }
    let dst_type = module_type_name(module, value.type_idx)?;
    let src_width = emit_aarch64_expr(asm, module, func, frame, value.a, 8)?;
    if src_width != ScalarWidth::X64 {
        return Err("subset asm: unsize cast source must lower to x-register width".to_string());
    }
    if is_slice_like_type(dst_type) {
        let src_expr = module_expr(module, value.a)?;
        let src_type = module_type_name(module, src_expr.type_idx)?;
        let slice_len = parse_array_len_from_pointer_type(src_type).ok_or_else(|| {
            format!("subset asm: unsupported slice unsize source type {src_type}")
        })?;
        emit_move_imm(asm, ScalarWidth::X64, 9, slice_len as i64);
    } else {
        let vtable = module_vtable(module, value.c)?;
        emit_label_address(asm, &vtable.label, 9);
    }
    emit_store_scalar_pair_place(asm, module, func, frame, place_expr_id, 8, 9)?;
    Ok(true)
}

fn emit_assert_stmt(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    func_index: usize,
    stmt: &SemanticStmt,
) -> Result<(), String> {
    let cond_width = emit_aarch64_expr(asm, module, func, frame, stmt.a, 8)?;
    if cond_width == ScalarWidth::Void {
        return Err("subset asm: assert condition cannot be void".to_string());
    }
    let expected = assert_expected(module, stmt.c)?;
    let _ = writeln!(
        asm,
        "  cmp {}, #{}",
        aarch64_reg(cond_width, 8),
        if expected { 1 } else { 0 }
    );
    let target_block = usize::try_from(stmt.b)
        .map_err(|_| format!("subset asm: invalid assert target block {}", stmt.b))?;
    let _ = writeln!(asm, "  b.eq {}", block_label(func_index, target_block));
    let _ = writeln!(asm, "  brk #0");
    Ok(())
}

fn emit_drop_stmt(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    func_index: usize,
    stmt: &SemanticStmt,
) -> Result<(), String> {
    let place = resolve_place_location(module, func, frame, stmt.a)?;
    if !type_is_trivially_droppable(module, place.type_idx)? {
        let ty = module_type_name(module, place.type_idx)?;
        return Err(format!(
            "subset asm: non-trivial drop is unsupported for {ty}"
        ));
    }
    let target_block = usize::try_from(stmt.b)
        .map_err(|_| format!("subset asm: invalid drop target block {}", stmt.b))?;
    let _ = writeln!(asm, "  b {}", block_label(func_index, target_block));
    Ok(())
}

fn emit_checked_overflow_assign(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value: &SemanticExpr,
    op_name: &str,
) -> Result<(), String> {
    let place = resolve_place_location(module, func, frame, place_expr_id)?;
    let tuple_type = module_type_name(module, place.type_idx)?;
    let fields = tuple_field_types(tuple_type)
        .ok_or_else(|| format!("subset asm: {op_name} target {tuple_type} is not a tuple"))?;
    if fields.len() != 2 {
        return Err(format!(
            "subset asm: {op_name} target {tuple_type} must be a 2-field tuple"
        ));
    }

    let lhs_width = emit_aarch64_expr(asm, module, func, frame, value.a, 8)?;
    let rhs_width = emit_aarch64_expr(asm, module, func, frame, value.b, 9)?;
    if lhs_width != rhs_width || lhs_width == ScalarWidth::Void {
        return Err(
            "subset asm: AddWithOverflow operands must have the same scalar width".to_string(),
        );
    }

    let lhs = module_expr(module, value.a)?;
    let lhs_type = module_type_name(module, lhs.type_idx)?;
    let overflow_cond = overflow_condition(lhs_type)?;
    let dst = aarch64_reg(lhs_width, 8);
    let rhs = aarch64_reg(rhs_width, 9);
    let op = match op_name {
        "AddWithOverflow" => "adds",
        "SubWithOverflow" => "subs",
        other => return Err(format!("subset asm: unsupported checked binary op {other}")),
    };
    let _ = writeln!(asm, "  {op} {dst}, {dst}, {rhs}");
    let _ = writeln!(asm, "  cset w10, {overflow_cond}");

    let sum_offset = tuple_field_offset(module, place.type_idx, 0)?;
    let flag_offset = tuple_field_offset(module, place.type_idx, 1)?;
    emit_store_place_at_offset(asm, place, sum_offset, lhs_width, 8)?;
    emit_store_place_at_offset(asm, place, flag_offset, ScalarWidth::W32, 10)?;
    Ok(())
}

fn emit_call_stmt(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    func_abi: &SemanticFuncAbi,
    func_index: usize,
    stmt_index: usize,
    stmt: &SemanticStmt,
    stmt_start: usize,
    stmt_end: usize,
) -> Result<(), String> {
    let arg_expr_ids = operand_list_expr_ids(module, stmt.b)?;
    let call_abi = call_abi_for_stmt(module, stmt_index, stmt)?;
    let mut next_arg_reg = 0u8;
    for (arg_index, expr_id) in arg_expr_ids.iter().enumerate() {
        let arg_mode = call_abi
            .arg_modes
            .get(arg_index)
            .map(String::as_str)
            .unwrap_or("scalar");
        match arg_mode {
            "void" => {}
            "scalar_pair" => {
                if next_arg_reg > 6 {
                    return Err(
                        "subset asm: scalar-pair call args past x7 are unsupported".to_string()
                    );
                }
                emit_scalar_pair_expr(
                    asm,
                    module,
                    func,
                    frame,
                    *expr_id,
                    next_arg_reg,
                    next_arg_reg + 1,
                )?;
                next_arg_reg += 2;
            }
            "indirect" | "by_stack" => {
                if next_arg_reg >= 8 {
                    return Err(
                        "subset asm: more than 8 call arg registers are unsupported".to_string()
                    );
                }
                emit_addressable_expr(asm, module, func, frame, *expr_id, next_arg_reg)?;
                next_arg_reg += 1;
            }
            _ => {
                if next_arg_reg >= 8 {
                    return Err(
                        "subset asm: more than 8 call arg registers are unsupported".to_string()
                    );
                }
                let expr = module_expr(module, *expr_id)?;
                let width = if type_is_scalar_pair(module, expr.type_idx)? {
                    emit_scalar_pair_expr(asm, module, func, frame, *expr_id, 14, 15)?;
                    let _ = writeln!(asm, "  mov x{next_arg_reg}, x14");
                    ScalarWidth::X64
                } else {
                    emit_aarch64_expr(asm, module, func, frame, *expr_id, next_arg_reg)?
                };
                if width != ScalarWidth::Void {
                    next_arg_reg += 1;
                }
            }
        }
    }
    if call_abi.ret_mode == "sret_x8" {
        emit_place_address(asm, module, func, frame, stmt.c, 8)?;
    }
    let mut call_symbol = None;
    let mut target_block = None;
    for look in stmt_start..stmt_end {
        if look == stmt_index {
            continue;
        }
        let meta = &module.stmts[look];
        match meta.kind.as_str() {
            "term.call_symbol" => {
                let symbol_idx = meta.a;
                let symbol = module_string(module, symbol_idx)?;
                call_symbol = Some(target_symbol_name(BinaryFormat::MachO, symbol));
            }
            "term.call_target" => target_block = Some(meta.a as usize),
            _ => {}
        }
    }
    if let Some(call_symbol) = call_symbol {
        let _ = writeln!(asm, "  bl {call_symbol}");
    } else {
        let callee_width = emit_aarch64_expr(asm, module, func, frame, stmt.a, 15)?;
        if callee_width != ScalarWidth::X64 {
            return Err(
                "subset asm: indirect call target must lower to x-register width".to_string(),
            );
        }
        let _ = writeln!(asm, "  blr x15");
    }
    if stmt.c >= 0 {
        match call_abi.ret_mode.as_str() {
            "void" | "sret_x8" => {}
            "scalar_pair" => emit_store_scalar_pair_place(asm, module, func, frame, stmt.c, 0, 1)?,
            _ => emit_store_place(asm, module, func, frame, stmt.c, 0)?,
        }
    }
    if let Some(target_block) = target_block {
        let _ = writeln!(asm, "  b {}", block_label(func_index, target_block));
    } else {
        match func_abi.ret_mode.as_str() {
            "void" | "sret_x8" => {}
            "scalar_pair" => emit_scalar_pair_local(asm, module, func, frame, 0, 0, 1)?,
            _ => {
                let ret_width = width_for_local_slot(module, func, 0)?;
                if ret_width != ScalarWidth::Void {
                    emit_load_place(
                        asm,
                        resolve_local_slot_location(func, frame, 0)?,
                        ret_width,
                        0,
                    );
                }
            }
        }
        emit_epilogue(asm, frame.frame_size, frame.frame_record_offset);
    }
    Ok(())
}

fn emit_addressable_expr(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    expr_id: i32,
    dest_reg: u8,
) -> Result<(), String> {
    let expr = module_expr(module, expr_id)?;
    match expr.kind.as_str() {
        "place.local" | "place.field" | "place.deref" => {
            emit_place_address(asm, module, func, frame, expr_id, dest_reg)
        }
        "operand.copy" | "operand.move" | "rvalue.use" => {
            emit_addressable_expr(asm, module, func, frame, expr.a, dest_reg)
        }
        "operand.const" => {
            let const_value = module_const(module, expr.a)
                .ok_or_else(|| format!("subset asm: const index out of range {}", expr.a))?;
            if const_value.kind == "scalar" {
                emit_const_atom_into_reg(
                    asm,
                    ScalarWidth::X64,
                    &const_value.atom_a_kind,
                    &const_value.atom_a,
                    const_value.atom_a_addend,
                    dest_reg,
                )?;
                return Ok(());
            }
            Err(format!(
                "subset asm: operand.const {} is not addressable for indirect ABI",
                const_value.kind
            ))
        }
        other => Err(format!("subset asm: expr kind {other} is not addressable")),
    }
}

fn emit_aarch64_expr(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    expr_id: i32,
    dest_reg: u8,
) -> Result<ScalarWidth, String> {
    let expr = module_expr(module, expr_id)?;
    match expr.kind.as_str() {
        "place.local" | "place.field" | "place.deref" => {
            let place = resolve_place_location(module, func, frame, expr_id)?;
            let width = width_for_type(module, place.type_idx)?;
            if width == ScalarWidth::Void {
                return Ok(ScalarWidth::Void);
            }
            emit_load_place(asm, place, width, dest_reg);
            Ok(width)
        }
        "operand.copy" | "operand.move" | "rvalue.use" => {
            emit_aarch64_expr(asm, module, func, frame, expr.a, dest_reg)
        }
        "operand.const" => {
            if let Some(const_value) = module_const(module, expr.a) {
                return emit_const_value(asm, module, const_value, dest_reg);
            }
            let raw = module_string(module, expr.a)?;
            let width = width_for_expr(module, expr)?;
            if width == ScalarWidth::Void {
                return Ok(ScalarWidth::Void);
            }
            let imm = parse_const_i64(raw)?;
            emit_move_imm(asm, width, dest_reg, imm);
            Ok(width)
        }
        "rvalue.cast" => {
            let cast_kind = module_string(module, expr.b)?;
            let src_type_idx = module_expr(module, expr.a)?.type_idx;
            let src_width = if type_is_scalar_pair(module, src_type_idx)? {
                emit_scalar_pair_expr(asm, module, func, frame, expr.a, dest_reg, 15)?;
                ScalarWidth::X64
            } else {
                emit_aarch64_expr(asm, module, func, frame, expr.a, dest_reg)?
            };
            let dst_width = width_for_expr(module, expr)?;
            if dst_width == ScalarWidth::Void {
                return Ok(src_width);
            }
            match cast_kind {
                "IntToInt" => {
                    if src_width == ScalarWidth::X64 && dst_width == ScalarWidth::W32 {
                        let _ = writeln!(
                            asm,
                            "  mov {}, {}",
                            aarch64_reg(dst_width, dest_reg),
                            aarch64_reg(src_width, dest_reg)
                        );
                    }
                    Ok(dst_width)
                }
                "PtrToPtr"
                | "PointerExposeAddress"
                | "PointerCoercion(MutToConstPointer, AsCast)"
                | "PointerCoercion(ArrayToPointer, AsCast)" => Ok(dst_width),
                _ if cast_kind.starts_with("PointerCoercion(") => Ok(src_width),
                _ => Err(format!("subset asm: unsupported cast kind {cast_kind}")),
            }
        }
        "rvalue.unary" => {
            let op_name = module_string(module, expr.b)?;
            match op_name {
                "PtrMetadata" => {
                    if !type_is_scalar_pair(module, module_expr(module, expr.a)?.type_idx)? {
                        return Err(
                            "subset asm: PtrMetadata expects scalar-pair operand".to_string()
                        );
                    }
                    emit_scalar_pair_expr(asm, module, func, frame, expr.a, 14, 15)?;
                    let _ = writeln!(asm, "  mov x{dest_reg}, x15");
                    Ok(ScalarWidth::X64)
                }
                other => Err(format!("subset asm: unsupported unary op {other}")),
            }
        }
        "rvalue.binary" => {
            let lhs_width = emit_aarch64_expr(asm, module, func, frame, expr.a, dest_reg)?;
            let rhs_width = emit_aarch64_expr(asm, module, func, frame, expr.b, dest_reg + 1)?;
            let width = width_for_expr(module, expr)?;
            if lhs_width == ScalarWidth::Void
                || rhs_width == ScalarWidth::Void
                || width == ScalarWidth::Void
            {
                return Err("subset asm: void binary operands are unsupported".to_string());
            }
            let op_name = module_string(module, expr.c)?;
            let dst = aarch64_reg(width, dest_reg);
            let lhs = aarch64_reg(width, dest_reg);
            let rhs = aarch64_reg(width, dest_reg + 1);
            match op_name {
                "Add" => {
                    let _ = writeln!(asm, "  add {dst}, {lhs}, {rhs}");
                }
                "Sub" => {
                    let _ = writeln!(asm, "  sub {dst}, {lhs}, {rhs}");
                }
                "BitAnd" => {
                    let _ = writeln!(asm, "  and {dst}, {lhs}, {rhs}");
                }
                "BitOr" => {
                    let _ = writeln!(asm, "  orr {dst}, {lhs}, {rhs}");
                }
                "BitXor" => {
                    let _ = writeln!(asm, "  eor {dst}, {lhs}, {rhs}");
                }
                other => {
                    return Err(format!("subset asm: unsupported binary op {other}"));
                }
            }
            Ok(width)
        }
        other => Err(format!("subset asm: unsupported expr kind {other}")),
    }
}

fn emit_store_place(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    value_reg: u8,
) -> Result<(), String> {
    let place = resolve_place_location(module, func, frame, place_expr_id)?;
    let width = width_for_type(module, place.type_idx)?;
    if width == ScalarWidth::Void {
        return Ok(());
    }
    emit_store_place_at_offset(asm, place, 0, width, value_reg)
}

fn emit_load_local(asm: &mut String, offset: i32, width: ScalarWidth, reg: u8) {
    let _ = writeln!(asm, "  ldr {}, [sp, #{}]", aarch64_reg(width, reg), offset);
}

fn emit_move_imm(asm: &mut String, width: ScalarWidth, reg: u8, value: i64) {
    if width == ScalarWidth::Void {
        return;
    }
    let _ = writeln!(asm, "  mov {}, #{}", aarch64_reg(width, reg), value);
}

fn emit_adjust_reg_i64(asm: &mut String, reg: u8, addend: i64) -> Result<(), String> {
    if addend == 0 {
        return Ok(());
    }
    if (1..=4095).contains(&addend) {
        let _ = writeln!(asm, "  add x{reg}, x{reg}, #{addend}");
        return Ok(());
    }
    if (-4095..=-1).contains(&addend) {
        let _ = writeln!(asm, "  sub x{reg}, x{reg}, #{}", addend.unsigned_abs());
        return Ok(());
    }
    Err(format!(
        "subset asm: const addend {addend} exceeds bootstrap immediate range"
    ))
}

fn emit_const_atom_into_reg(
    asm: &mut String,
    width: ScalarWidth,
    atom_kind: &str,
    atom: &str,
    addend: i64,
    dest_reg: u8,
) -> Result<(), String> {
    match atom_kind {
        "none" => emit_move_imm(asm, width, dest_reg, 0),
        "imm" => {
            let value = parse_const_i64(atom)? + addend;
            emit_move_imm(asm, width, dest_reg, value);
        }
        "readonly" | "vtable" => {
            if width != ScalarWidth::X64 {
                return Err(format!(
                    "subset asm: address-like const {atom_kind} requires x-register width"
                ));
            }
            let label = target_symbol_name(BinaryFormat::MachO, atom);
            emit_label_address(asm, &label, dest_reg);
            emit_adjust_reg_i64(asm, dest_reg, addend)?;
        }
        "symbol" => {
            if width != ScalarWidth::X64 {
                return Err("subset asm: symbol const requires x-register width".to_string());
            }
            let name = target_symbol_name(BinaryFormat::MachO, atom);
            emit_label_address(asm, &name, dest_reg);
            emit_adjust_reg_i64(asm, dest_reg, addend)?;
        }
        other => {
            return Err(format!("subset asm: unsupported const atom kind {other}"));
        }
    }
    Ok(())
}

fn emit_const_value(
    asm: &mut String,
    module: &SemanticModuleData,
    const_value: &SemanticConstValue,
    dest_reg: u8,
) -> Result<ScalarWidth, String> {
    match const_value.kind.as_str() {
        "zero_sized" => Ok(ScalarWidth::Void),
        "scalar" => {
            let width = width_for_type(module, const_value.type_idx)?;
            emit_const_atom_into_reg(
                asm,
                width,
                &const_value.atom_a_kind,
                &const_value.atom_a,
                const_value.atom_a_addend,
                dest_reg,
            )?;
            Ok(width)
        }
        "scalar_pair" => {
            Err("subset asm: scalar-pair const requires pair lowering path".to_string())
        }
        other => Err(format!("subset asm: unsupported const kind {other}")),
    }
}

fn emit_scalar_pair_const(
    asm: &mut String,
    _module: &SemanticModuleData,
    const_value: &SemanticConstValue,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    if const_value.kind != "scalar_pair" {
        return Err(format!(
            "subset asm: const kind {} is not scalar_pair",
            const_value.kind
        ));
    }
    emit_const_atom_into_reg(
        asm,
        ScalarWidth::X64,
        &const_value.atom_a_kind,
        &const_value.atom_a,
        const_value.atom_a_addend,
        first_reg,
    )?;
    emit_const_atom_into_reg(
        asm,
        ScalarWidth::X64,
        &const_value.atom_b_kind,
        &const_value.atom_b,
        const_value.atom_b_addend,
        second_reg,
    )?;
    Ok(())
}

fn emit_load_place_at_offset(
    asm: &mut String,
    place: PlaceLocation,
    extra_offset: i32,
    width: ScalarWidth,
    reg: u8,
) {
    match place.base {
        PlaceBase::Stack => emit_load_local(
            asm,
            place_stack_offset(place.base_offset + place.projection_offset + extra_offset),
            width,
            reg,
        ),
        PlaceBase::IndirectStack => {
            emit_load_local(
                asm,
                place_stack_offset(place.base_offset),
                ScalarWidth::X64,
                15,
            );
            let _ = writeln!(
                asm,
                "  ldr {}, [x15, #{}]",
                aarch64_reg(width, reg),
                place.projection_offset + extra_offset
            );
        }
    }
}

fn emit_load_place(asm: &mut String, place: PlaceLocation, width: ScalarWidth, reg: u8) {
    match place.base {
        PlaceBase::Stack => emit_load_local(
            asm,
            place_stack_offset(place.base_offset + place.projection_offset),
            width,
            reg,
        ),
        PlaceBase::IndirectStack => {
            emit_load_local(
                asm,
                place_stack_offset(place.base_offset),
                ScalarWidth::X64,
                15,
            );
            let _ = writeln!(
                asm,
                "  ldr {}, [x15, #{}]",
                aarch64_reg(width, reg),
                place.projection_offset
            );
        }
    }
}

fn emit_store_place_at_offset(
    asm: &mut String,
    place: PlaceLocation,
    extra_offset: i32,
    width: ScalarWidth,
    value_reg: u8,
) -> Result<(), String> {
    if width == ScalarWidth::Void {
        return Ok(());
    }
    match place.base {
        PlaceBase::Stack => {
            let _ = writeln!(
                asm,
                "  str {}, [sp, #{}]",
                aarch64_reg(width, value_reg),
                place_stack_offset(place.base_offset + place.projection_offset + extra_offset),
            );
        }
        PlaceBase::IndirectStack => {
            emit_load_local(
                asm,
                place_stack_offset(place.base_offset),
                ScalarWidth::X64,
                15,
            );
            let _ = writeln!(
                asm,
                "  str {}, [x15, #{}]",
                aarch64_reg(width, value_reg),
                place.projection_offset + extra_offset
            );
        }
    }
    Ok(())
}

fn emit_place_address(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    dest_reg: u8,
) -> Result<(), String> {
    let place = resolve_place_location(module, func, frame, place_expr_id)?;
    match place.base {
        PlaceBase::Stack => {
            let offset = place_stack_offset(place.base_offset + place.projection_offset);
            let _ = writeln!(asm, "  add x{}, sp, #{}", dest_reg, offset);
        }
        PlaceBase::IndirectStack => {
            emit_load_local(
                asm,
                place_stack_offset(place.base_offset),
                ScalarWidth::X64,
                dest_reg,
            );
            if place.projection_offset != 0 {
                let _ = writeln!(
                    asm,
                    "  add x{}, x{}, #{}",
                    dest_reg, dest_reg, place.projection_offset
                );
            }
        }
    }
    Ok(())
}

fn emit_label_address(asm: &mut String, label: &str, dest_reg: u8) {
    let _ = writeln!(asm, "  adrp x{dest_reg}, {label}@PAGE");
    let _ = writeln!(asm, "  add x{dest_reg}, x{dest_reg}, {label}@PAGEOFF");
}

fn emit_epilogue(asm: &mut String, frame_size: i32, frame_record_offset: i32) {
    let _ = writeln!(asm, "  ldp x29, x30, [sp, #{}]", frame_record_offset);
    let _ = writeln!(asm, "  add sp, sp, #{frame_size}");
    let _ = writeln!(asm, "  ret");
}

fn assemble_subset_object(target: &str, asm: &str) -> Result<Vec<u8>, String> {
    let clang = find_clang()?;
    let nonce = unique_nonce();
    let asm_path = std::env::temp_dir().join(format!("cheng_uir_subset_{nonce}.s"));
    let obj_path = std::env::temp_dir().join(format!("cheng_uir_subset_{nonce}.o"));
    fs::write(&asm_path, asm)
        .map_err(|err| format!("failed to write subset asm {}: {err}", asm_path.display()))?;
    let output = Command::new(&clang)
        .arg("-target")
        .arg(target)
        .arg("-c")
        .arg(&asm_path)
        .arg("-o")
        .arg(&obj_path)
        .output()
        .map_err(|err| format!("failed to run {clang} for subset asm: {err}"))?;
    let _ = fs::remove_file(&asm_path);
    if !output.status.success() {
        let _ = fs::remove_file(&obj_path);
        return Err(format!(
            "subset asm clang failed: {}",
            String::from_utf8_lossy(&output.stderr)
        ));
    }
    let bytes = fs::read(&obj_path)
        .map_err(|err| format!("failed to read subset object {}: {err}", obj_path.display()))?;
    let _ = fs::remove_file(&obj_path);
    Ok(bytes)
}

fn find_clang() -> Result<String, String> {
    for candidate in ["clang", "/usr/bin/clang"] {
        let status = Command::new(candidate).arg("--version").output();
        if let Ok(output) = status {
            if output.status.success() {
                return Ok(candidate.to_string());
            }
        }
    }
    Err("subset asm codegen requires clang".to_string())
}

fn unique_nonce() -> String {
    static NONCE_COUNTER: AtomicU64 = AtomicU64::new(0);
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    let counter = NONCE_COUNTER.fetch_add(1, Ordering::Relaxed);
    format!("{}_{}_{}", std::process::id(), nanos, counter)
}

fn parse_table_fields(line: &str) -> BTreeMap<String, String> {
    let mut fields = BTreeMap::new();
    for token in line.split('\t').skip(1) {
        if let Some((key, value)) = token.split_once('=') {
            fields.insert(key.trim().to_string(), value.trim().to_string());
        }
    }
    fields
}

fn field_str<'a>(fields: &'a BTreeMap<String, String>, key: &str) -> &'a str {
    fields.get(key).map(String::as_str).unwrap_or("")
}

fn parse_usize_field(fields: &BTreeMap<String, String>, key: &str) -> Result<usize, String> {
    field_str(fields, key)
        .parse::<usize>()
        .map_err(|err| format!("invalid usize field {key}: {err}"))
}

fn parse_i32_field(fields: &BTreeMap<String, String>, key: &str) -> Result<i32, String> {
    field_str(fields, key)
        .parse::<i32>()
        .map_err(|err| format!("invalid i32 field {key}: {err}"))
}

fn parse_i64_field(fields: &BTreeMap<String, String>, key: &str) -> Result<i64, String> {
    field_str(fields, key)
        .parse::<i64>()
        .map_err(|err| format!("invalid i64 field {key}: {err}"))
}

fn parse_usize_field_opt(
    fields: &BTreeMap<String, String>,
    key: &str,
) -> Result<Option<usize>, String> {
    let raw = field_str(fields, key);
    if raw.is_empty() {
        return Ok(None);
    }
    raw.parse::<usize>()
        .map(Some)
        .map_err(|err| format!("invalid optional usize field {key}: {err}"))
}

fn parse_i32_field_opt(
    fields: &BTreeMap<String, String>,
    key: &str,
) -> Result<Option<i32>, String> {
    let raw = field_str(fields, key);
    if raw.is_empty() {
        return Ok(None);
    }
    raw.parse::<i32>()
        .map(Some)
        .map_err(|err| format!("invalid optional i32 field {key}: {err}"))
}

fn split_csv_field(raw: &str) -> Vec<String> {
    if raw.trim().is_empty() {
        return Vec::new();
    }
    raw.split(',')
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string)
        .collect()
}

fn decode_hex(raw: &str) -> Result<Vec<u8>, String> {
    if raw.is_empty() {
        return Ok(Vec::new());
    }
    if raw.len() % 2 != 0 {
        return Err(format!("invalid hex payload length {}", raw.len()));
    }
    let mut bytes = Vec::with_capacity(raw.len() / 2);
    let mut chars = raw.as_bytes().chunks_exact(2);
    for pair in &mut chars {
        let text = std::str::from_utf8(pair).map_err(|err| format!("invalid hex utf8: {err}"))?;
        let byte = u8::from_str_radix(text, 16)
            .map_err(|err| format!("invalid hex byte {text}: {err}"))?;
        bytes.push(byte);
    }
    Ok(bytes)
}

fn unescape_table_field(raw: &str) -> String {
    let mut out = String::new();
    let mut chars = raw.chars();
    while let Some(ch) = chars.next() {
        if ch == '\\' {
            match chars.next() {
                Some('t') => out.push('\t'),
                Some('n') => out.push('\n'),
                Some('\\') => out.push('\\'),
                Some(other) => {
                    out.push('\\');
                    out.push(other);
                }
                None => out.push('\\'),
            }
        } else {
            out.push(ch);
        }
    }
    out
}

fn ensure_len<T: Default + Clone>(items: &mut Vec<T>, len: usize) {
    if items.len() < len {
        items.resize(len, T::default());
    }
}

fn ensure_len_with<T: Default + Clone>(items: &mut Vec<T>, len: usize) {
    ensure_len(items, len)
}

fn align_to(value: i32, align: i32) -> i32 {
    ((value + align - 1) / align) * align
}

fn u64_align_pow2(align: u64) -> u64 {
    if align <= 1 {
        1
    } else {
        align.next_power_of_two()
    }
}

fn target_symbol_name(format: BinaryFormat, name: &str) -> String {
    if format == BinaryFormat::MachO {
        if name.starts_with("__") {
            return name.to_string();
        }
        if name.starts_with("_ZN") || name.starts_with("_R") {
            return format!("_{name}");
        }
        if !name.starts_with('_') {
            return format!("_{name}");
        }
    }
    name.to_string()
}

fn block_label(func_index: usize, block_index: usize) -> String {
    format!("Luir_func{func_index}_bb{block_index}")
}

fn place_stack_offset(byte_offset: i32) -> i32 {
    byte_offset
}

fn parse_arg_local_index(name: &str) -> Option<usize> {
    name.strip_prefix("arg")?.parse::<usize>().ok()
}

fn module_local<'a>(
    module: &'a SemanticModuleData,
    func: &SemanticFunc,
    slot: usize,
) -> Result<&'a SemanticLocal, String> {
    let index = func.first_local + slot;
    module
        .locals
        .get(index)
        .ok_or_else(|| format!("subset asm: local slot out of range {index}"))
}

fn module_block<'a>(
    module: &'a SemanticModuleData,
    func: &SemanticFunc,
    block_index: usize,
) -> Result<&'a SemanticBlock, String> {
    let index = func.first_block + block_index;
    module
        .blocks
        .get(index)
        .ok_or_else(|| format!("subset asm: block index out of range {index}"))
}

fn module_expr(module: &SemanticModuleData, expr_id: i32) -> Result<&SemanticExpr, String> {
    let index =
        usize::try_from(expr_id).map_err(|_| format!("subset asm: negative expr id {expr_id}"))?;
    module
        .exprs
        .get(index)
        .ok_or_else(|| format!("subset asm: expr out of range {index}"))
}

fn module_string(module: &SemanticModuleData, string_id: i32) -> Result<&str, String> {
    let index = usize::try_from(string_id)
        .map_err(|_| format!("subset asm: negative string id {string_id}"))?;
    module
        .strings
        .get(index)
        .map(String::as_str)
        .ok_or_else(|| format!("subset asm: string out of range {index}"))
}

fn module_type_name(module: &SemanticModuleData, type_idx: i32) -> Result<&str, String> {
    let index = usize::try_from(type_idx)
        .map_err(|_| format!("subset asm: negative type idx {type_idx}"))?;
    module
        .types
        .get(index)
        .map(String::as_str)
        .ok_or_else(|| format!("subset asm: type out of range {index}"))
}

fn module_vtable(
    module: &SemanticModuleData,
    vtable_idx: i32,
) -> Result<&SemanticVtableDesc, String> {
    let index = usize::try_from(vtable_idx)
        .map_err(|_| format!("subset asm: negative vtable idx {vtable_idx}"))?;
    module
        .vtable_descs
        .get(index)
        .ok_or_else(|| format!("subset asm: vtable out of range {index}"))
}

fn module_layout(module: &SemanticModuleData, type_idx: i32) -> Option<&SemanticLayout> {
    usize::try_from(type_idx)
        .ok()
        .and_then(|index| module.layouts.get(index))
}

fn module_const(module: &SemanticModuleData, const_idx: i32) -> Option<&SemanticConstValue> {
    usize::try_from(const_idx)
        .ok()
        .and_then(|index| module.const_values.get(index))
}

fn width_for_local_slot(
    module: &SemanticModuleData,
    func: &SemanticFunc,
    slot: usize,
) -> Result<ScalarWidth, String> {
    let local = module_local(module, func, slot)?;
    width_for_type(module, local.type_idx)
}

fn width_for_expr(module: &SemanticModuleData, expr: &SemanticExpr) -> Result<ScalarWidth, String> {
    width_for_type(module, expr.type_idx)
}

fn size_align_for_type_idx(
    module: &SemanticModuleData,
    type_idx: i32,
) -> Result<(i32, i32), String> {
    if type_idx < 0 {
        return Ok((0, 1));
    }
    if let Some(layout) = module_layout(module, type_idx) {
        return Ok((
            i32::try_from(layout.size_bytes)
                .map_err(|_| format!("subset asm: size does not fit i32 for {}", layout.name))?,
            i32::try_from(layout.align_bytes.max(1))
                .map_err(|_| format!("subset asm: align does not fit i32 for {}", layout.name))?,
        ));
    }
    let type_name = module_type_name(module, type_idx)?;
    size_align_for_type_name_in_module(module, type_name)
}

fn width_for_type(module: &SemanticModuleData, type_idx: i32) -> Result<ScalarWidth, String> {
    if type_idx < 0 {
        return Ok(ScalarWidth::Void);
    }
    let type_name = module_type_name(module, type_idx)?;
    if let Some(layout) = module_layout(module, type_idx) {
        if layout.size_bytes == 0 {
            return Ok(ScalarWidth::Void);
        }
        if layout.abi_class.starts_with("Scalar(") {
            return match layout.size_bytes {
                0 => Ok(ScalarWidth::Void),
                1..=4 => Ok(ScalarWidth::W32),
                5..=8 => Ok(ScalarWidth::X64),
                other => Err(format!(
                    "subset asm: unsupported scalar width {other} bytes for {type_name}"
                )),
            };
        }
        if layout.abi_class.starts_with("ScalarPair(") {
            return Err(format!(
                "subset asm: scalar-pair ABI is not yet supported for {type_name}"
            ));
        }
    }
    match type_name.trim() {
        "()" => Ok(ScalarWidth::Void),
        "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "bool" | "char" => Ok(ScalarWidth::W32),
        "i64" | "u64" | "isize" | "usize" => Ok(ScalarWidth::X64),
        other if is_pointer_like_type(other) => Ok(ScalarWidth::X64),
        other => Err(format!("subset asm: unsupported scalar type {other}")),
    }
}

fn type_is_scalar_pair(module: &SemanticModuleData, type_idx: i32) -> Result<bool, String> {
    if type_idx < 0 {
        return Ok(false);
    }
    let type_name = module_type_name(module, type_idx)?;
    let Some(layout) = module_layout(module, type_idx) else {
        return Ok(false);
    };
    if layout.abi_class.starts_with("ScalarPair(") && layout.size_bytes <= 16 {
        return Ok(true);
    }
    if layout.abi_class.starts_with("ScalarPair(") {
        return Err(format!(
            "subset asm: unsupported scalar-pair width {} bytes for {type_name}",
            layout.size_bytes
        ));
    }
    Ok(false)
}

fn aggregate_layout_for_type(
    module: &SemanticModuleData,
    type_idx: i32,
) -> Option<&SemanticAggregateLayout> {
    module
        .aggregate_layouts
        .iter()
        .find(|layout| layout.type_idx == type_idx)
}

fn aggregate_field<'a>(
    module: &'a SemanticModuleData,
    aggregate: &SemanticAggregateLayout,
    field_index: usize,
) -> Result<&'a SemanticAggregateField, String> {
    if field_index >= aggregate.field_len {
        return Err(format!(
            "subset asm: aggregate field {} out of bounds for type {}",
            field_index, aggregate.type_idx
        ));
    }
    let index = aggregate.field_start + field_index;
    let field = module
        .aggregate_fields
        .get(index)
        .ok_or_else(|| format!("subset asm: aggregate field out of range {index}"))?;
    if field.field_index != field_index {
        return Err(format!(
            "subset asm: aggregate field index mismatch for type {}: expected {}, found {}",
            aggregate.type_idx, field_index, field.field_index
        ));
    }
    Ok(field)
}

fn resolve_local_slot_location(
    func: &SemanticFunc,
    frame: &FramePlan,
    slot: usize,
) -> Result<PlaceLocation, String> {
    let local = frame.locals.get(slot).ok_or_else(|| {
        format!(
            "subset asm: local slot out of frame range {} in {}",
            slot, func.name
        )
    })?;
    match local.storage {
        LocalStorage::Void => Ok(PlaceLocation {
            base: PlaceBase::Stack,
            slot: Some(slot),
            base_offset: 0,
            projection_offset: 0,
            type_idx: local.type_idx,
        }),
        LocalStorage::Direct { offset } => Ok(PlaceLocation {
            base: PlaceBase::Stack,
            slot: Some(slot),
            base_offset: offset,
            projection_offset: 0,
            type_idx: local.type_idx,
        }),
        LocalStorage::Indirect { pointer_offset } => Ok(PlaceLocation {
            base: PlaceBase::IndirectStack,
            slot: Some(slot),
            base_offset: pointer_offset,
            projection_offset: 0,
            type_idx: local.type_idx,
        }),
    }
}

fn store_pointer_local(
    frame: &FramePlan,
    slot: usize,
    src_reg: u8,
    asm: &mut String,
) -> Result<(), String> {
    let local = frame
        .locals
        .get(slot)
        .ok_or_else(|| format!("subset asm: local slot out of frame range {slot}"))?;
    match local.storage {
        LocalStorage::Void => Ok(()),
        LocalStorage::Direct { offset } => {
            let _ = writeln!(
                asm,
                "  str x{src_reg}, [sp, #{}]",
                place_stack_offset(offset)
            );
            Ok(())
        }
        LocalStorage::Indirect { pointer_offset } => {
            let _ = writeln!(
                asm,
                "  str x{src_reg}, [sp, #{}]",
                place_stack_offset(pointer_offset)
            );
            Ok(())
        }
    }
}

fn store_direct_local(
    frame: &FramePlan,
    slot: usize,
    width: ScalarWidth,
    src_reg: u8,
    asm: &mut String,
) -> Result<(), String> {
    let local = frame
        .locals
        .get(slot)
        .ok_or_else(|| format!("subset asm: local slot out of frame range {slot}"))?;
    match local.storage {
        LocalStorage::Void => Ok(()),
        LocalStorage::Direct { offset } => {
            let _ = writeln!(
                asm,
                "  str {}, [sp, #{}]",
                aarch64_reg(width, src_reg),
                place_stack_offset(offset)
            );
            Ok(())
        }
        LocalStorage::Indirect { .. } => Err(format!(
            "subset asm: local slot {slot} requires indirect storage, not direct scalar write"
        )),
    }
}

fn resolve_place_location(
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    expr_id: i32,
) -> Result<PlaceLocation, String> {
    let expr = module_expr(module, expr_id)?;
    match expr.kind.as_str() {
        "place.local" => {
            let slot = usize::try_from(expr.a)
                .map_err(|_| format!("subset asm: invalid local slot {}", expr.a))?;
            resolve_local_slot_location(func, frame, slot)
        }
        "place.deref" => {
            let base = resolve_place_location(module, func, frame, expr.a)?;
            if base.base == PlaceBase::IndirectStack {
                return Err("subset asm: double-indirect deref is unsupported".to_string());
            }
            Ok(PlaceLocation {
                base: PlaceBase::IndirectStack,
                slot: None,
                base_offset: base.base_offset + base.projection_offset,
                projection_offset: 0,
                type_idx: expr.type_idx,
            })
        }
        "place.field" => {
            let base = resolve_place_location(module, func, frame, expr.a)?;
            let field_idx = usize::try_from(expr.b)
                .map_err(|_| format!("subset asm: invalid field index {}", expr.b))?;
            let field_type = if expr.c >= 0 { expr.c } else { expr.type_idx };
            Ok(PlaceLocation {
                base: base.base,
                slot: base.slot,
                base_offset: base.base_offset,
                projection_offset: base.projection_offset
                    + tuple_field_offset(module, base.type_idx, field_idx)?,
                type_idx: field_type,
            })
        }
        other => Err(format!("subset asm: unsupported place kind {other}")),
    }
}

fn tuple_field_offset(
    module: &SemanticModuleData,
    tuple_type_idx: i32,
    target_field: usize,
) -> Result<i32, String> {
    if let Some(aggregate) = aggregate_layout_for_type(module, tuple_type_idx) {
        let field = aggregate_field(module, aggregate, target_field)?;
        return i32::try_from(field.offset_bytes).map_err(|_| {
            format!(
                "subset asm: aggregate field offset too large {}",
                field.offset_bytes
            )
        });
    }
    let tuple_name = module_type_name(module, tuple_type_idx)?;
    if let Some(fields) = tuple_field_types(tuple_name) {
        if target_field >= fields.len() {
            return Err(format!(
                "subset asm: tuple field {} out of bounds for {tuple_name}",
                target_field
            ));
        }
        let mut offset = 0i32;
        for (index, field_name) in fields.iter().enumerate() {
            let (_, align) = size_align_for_type_name_in_module(module, field_name)?;
            offset = align_to(offset, align);
            if index == target_field {
                return Ok(offset);
            }
            let (size, _) = size_align_for_type_name_in_module(module, field_name)?;
            offset += size;
        }
        return Err(format!(
            "subset asm: tuple field {} missing in {tuple_name}",
            target_field
        ));
    }
    if target_field == 0 {
        return Ok(0);
    }
    Err(format!(
        "subset asm: type {tuple_name} does not support field projection {}",
        target_field
    ))
}

fn tuple_field_types(type_name: &str) -> Option<Vec<String>> {
    let inner = type_name.trim().strip_prefix('(')?.strip_suffix(')')?;
    if inner.trim().is_empty() {
        return Some(Vec::new());
    }
    let mut fields = Vec::new();
    let mut current = String::new();
    let mut depth = 0i32;
    for ch in inner.chars() {
        match ch {
            '(' | '[' | '{' | '<' => {
                depth += 1;
                current.push(ch);
            }
            ')' | ']' | '}' | '>' => {
                depth -= 1;
                current.push(ch);
            }
            ',' if depth == 0 => {
                fields.push(current.trim().to_string());
                current.clear();
            }
            _ => current.push(ch),
        }
    }
    if !current.trim().is_empty() {
        fields.push(current.trim().to_string());
    }
    Some(fields)
}

fn size_align_for_type_name_in_module(
    module: &SemanticModuleData,
    type_name: &str,
) -> Result<(i32, i32), String> {
    if let Some(layout) = module
        .layouts
        .iter()
        .find(|layout| layout.name == type_name.trim())
    {
        return Ok((
            i32::try_from(layout.size_bytes)
                .map_err(|_| format!("subset asm: size does not fit i32 for {}", layout.name))?,
            i32::try_from(layout.align_bytes.max(1))
                .map_err(|_| format!("subset asm: align does not fit i32 for {}", layout.name))?,
        ));
    }
    match type_name.trim() {
        "()" => Ok((0, 1)),
        "bool" | "i8" | "u8" => Ok((1, 1)),
        "i16" | "u16" => Ok((2, 2)),
        "i32" | "u32" | "char" => Ok((4, 4)),
        "i64" | "u64" | "isize" | "usize" => Ok((8, 8)),
        other if is_pointer_like_type(other) => Ok((8, 8)),
        other if other.starts_with('(') && other.ends_with(')') => {
            let fields = tuple_field_types(other)
                .ok_or_else(|| format!("subset asm: malformed tuple type {other}"))?;
            if fields.is_empty() {
                return Ok((0, 1));
            }
            let mut offset = 0i32;
            let mut max_align = 1i32;
            for field in &fields {
                let (size, align) = size_align_for_type_name_in_module(module, field)?;
                offset = align_to(offset, align);
                offset += size;
                max_align = max_align.max(align);
            }
            Ok((align_to(offset, max_align), max_align))
        }
        other if other.starts_with("{closure@") || other.starts_with('{') => Ok((8, 8)),
        other => Err(format!("subset asm: unsupported sized type {other}")),
    }
}

fn overflow_condition(type_name: &str) -> Result<&'static str, String> {
    match type_name.trim() {
        "i8" | "i16" | "i32" | "i64" | "isize" => Ok("vs"),
        "u8" | "u16" | "u32" | "u64" | "usize" | "bool" | "char" => Ok("cs"),
        other => Err(format!(
            "subset asm: overflow flag is unsupported for {other}"
        )),
    }
}

fn assert_expected(module: &SemanticModuleData, msg_idx: i32) -> Result<bool, String> {
    let msg = module_string(module, msg_idx)?;
    if msg.contains("expected=true") {
        return Ok(true);
    }
    if msg.contains("expected=false") {
        return Ok(false);
    }
    Err(format!(
        "subset asm: assert payload missing expected= marker: {msg}"
    ))
}

fn aarch64_reg(width: ScalarWidth, reg: u8) -> String {
    match width {
        ScalarWidth::W32 => format!("w{reg}"),
        ScalarWidth::X64 => format!("x{reg}"),
        ScalarWidth::Void => "xzr".to_string(),
    }
}

fn operand_list_expr_ids(module: &SemanticModuleData, expr_id: i32) -> Result<Vec<i32>, String> {
    let expr = module_expr(module, expr_id)?;
    if expr.kind != "operand.list" {
        return Err("subset asm: call arg payload is not operand.list".to_string());
    }
    let raw = module_string(module, expr.a)?;
    if raw.is_empty() {
        return Ok(Vec::new());
    }
    raw.split(',')
        .map(|part| {
            part.trim()
                .parse::<i32>()
                .map_err(|err| format!("subset asm: invalid operand.list item {part}: {err}"))
        })
        .collect()
}

fn parse_expr_id_list(
    module: &SemanticModuleData,
    string_expr_id: i32,
) -> Result<Vec<i32>, String> {
    if string_expr_id < 0 {
        return Ok(Vec::new());
    }
    let raw = module_string(module, string_expr_id)?;
    if raw.is_empty() {
        return Ok(Vec::new());
    }
    raw.split(',')
        .map(|part| {
            part.trim()
                .parse::<i32>()
                .map_err(|err| format!("subset asm: invalid expr id {part}: {err}"))
        })
        .collect()
}

fn size_align_for_width(width: ScalarWidth) -> (i32, i32) {
    match width {
        ScalarWidth::W32 => (4, 4),
        ScalarWidth::X64 => (8, 8),
        ScalarWidth::Void => (0, 1),
    }
}

fn emit_scalar_pair_expr(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    expr_id: i32,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    let expr = module_expr(module, expr_id)?;
    match expr.kind.as_str() {
        "place.local" | "place.field" | "place.deref" => {
            let place = resolve_place_location(module, func, frame, expr_id)?;
            emit_load_place_at_offset(asm, place, 0, ScalarWidth::X64, first_reg);
            emit_load_place_at_offset(asm, place, 8, ScalarWidth::X64, second_reg);
            Ok(())
        }
        "operand.copy" | "operand.move" | "rvalue.use" => {
            emit_scalar_pair_expr(asm, module, func, frame, expr.a, first_reg, second_reg)
        }
        "operand.const" => {
            let const_value = module_const(module, expr.a)
                .ok_or_else(|| format!("subset asm: const index out of range {}", expr.a))?;
            emit_scalar_pair_const(asm, module, const_value, first_reg, second_reg)
        }
        other => Err(format!(
            "subset asm: unsupported scalar-pair expr kind {other}"
        )),
    }
}

fn emit_store_scalar_pair_place_at_offset(
    asm: &mut String,
    place: PlaceLocation,
    extra_offset: i32,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    emit_store_place_at_offset(asm, place, extra_offset, ScalarWidth::X64, first_reg)?;
    emit_store_place_at_offset(asm, place, extra_offset + 8, ScalarWidth::X64, second_reg)
}

fn emit_store_scalar_pair_place(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    place_expr_id: i32,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    let place = resolve_place_location(module, func, frame, place_expr_id)?;
    emit_store_scalar_pair_place_at_offset(asm, place, 0, first_reg, second_reg)
}

fn emit_store_scalar_pair_local(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    slot: usize,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    let local = module_local(module, func, slot)?;
    if !type_is_scalar_pair(module, local.type_idx)? {
        return Err(format!(
            "subset asm: local {} is not scalar-pair typed",
            local.name
        ));
    }
    let place = resolve_local_slot_location(func, frame, slot)?;
    emit_store_scalar_pair_place_at_offset(asm, place, 0, first_reg, second_reg)
}

fn emit_scalar_pair_local(
    asm: &mut String,
    module: &SemanticModuleData,
    func: &SemanticFunc,
    frame: &FramePlan,
    slot: usize,
    first_reg: u8,
    second_reg: u8,
) -> Result<(), String> {
    let local = module_local(module, func, slot)?;
    if !type_is_scalar_pair(module, local.type_idx)? {
        return Err(format!(
            "subset asm: local {} is not scalar-pair typed",
            local.name
        ));
    }
    let place = resolve_local_slot_location(func, frame, slot)?;
    emit_load_place_at_offset(asm, place, 0, ScalarWidth::X64, first_reg);
    emit_load_place_at_offset(asm, place, 8, ScalarWidth::X64, second_reg);
    Ok(())
}

fn emit_vtable_data(asm: &mut String, vtables: &[SemanticVtableDesc]) {
    if vtables.is_empty() {
        return;
    }
    asm.push_str(".data\n");
    for vtable in vtables {
        let _ = writeln!(asm, ".p2align 3");
        let _ = writeln!(asm, "{}:", vtable.label);
        for entry in &vtable.entries {
            if let Some(symbol) = entry.strip_prefix("fn:") {
                let _ = writeln!(
                    asm,
                    "  .quad {}",
                    target_symbol_name(BinaryFormat::MachO, symbol)
                );
            } else if let Some(other) = entry.strip_prefix("vtable:") {
                let _ = writeln!(asm, "  .quad {other}");
            } else if let Some(value) = entry.strip_prefix("int:") {
                let _ = writeln!(asm, "  .quad {value}");
            } else {
                let _ = writeln!(asm, "  .quad 0");
            }
        }
    }
    asm.push_str(".text\n");
}

fn is_pointer_like_type(type_name: &str) -> bool {
    let ty = type_name.trim();
    ty.starts_with('&')
        || ty.starts_with("*const ")
        || ty.starts_with("*mut ")
        || ty.starts_with("fn(")
        || ty.starts_with("extern ")
        || ty.starts_with("for<")
}

fn is_slice_like_type(type_name: &str) -> bool {
    let ty = type_name.trim();
    ty == "&str"
        || ty.starts_with("&[")
        || ty.starts_with("&mut [")
        || ty.starts_with("*const [")
        || ty.starts_with("*mut [")
}

fn parse_array_len_from_pointer_type(type_name: &str) -> Option<u64> {
    let ty = type_name.trim();
    let array_part = if let Some(rest) = ty.strip_prefix("&mut ") {
        rest
    } else if let Some(rest) = ty.strip_prefix("&") {
        rest
    } else if let Some(rest) = ty.strip_prefix("*const ") {
        rest
    } else if let Some(rest) = ty.strip_prefix("*mut ") {
        rest
    } else {
        return None;
    };
    parse_array_len(array_part)
}

fn parse_array_len(type_name: &str) -> Option<u64> {
    let inner = type_name.trim().strip_prefix('[')?.strip_suffix(']')?;
    let (_, len_text) = inner.rsplit_once(';')?;
    len_text.trim().parse::<u64>().ok()
}

fn type_is_trivially_droppable(module: &SemanticModuleData, type_idx: i32) -> Result<bool, String> {
    if type_idx < 0 {
        return Ok(true);
    }
    let type_name = module_type_name(module, type_idx)?;
    if let Some(layout) = module_layout(module, type_idx) {
        if layout.size_bytes == 0
            || layout.abi_class.starts_with("Scalar(")
            || layout.abi_class.starts_with("ScalarPair(")
        {
            return Ok(true);
        }
    }
    if matches!(
        type_name.trim(),
        "()" | "bool"
            | "char"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
    ) || is_pointer_like_type(type_name)
    {
        return Ok(true);
    }
    if let Some(fields) = tuple_field_types(type_name) {
        for field in fields {
            if !type_is_trivially_droppable_name(&field) {
                return Ok(false);
            }
        }
        return Ok(true);
    }
    Ok(type_is_trivially_droppable_name(type_name))
}

fn type_is_trivially_droppable_name(type_name: &str) -> bool {
    let ty = type_name.trim();
    matches!(
        ty,
        "()" | "bool"
            | "char"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
    ) || is_pointer_like_type(ty)
}

fn parse_const_i64(raw: &str) -> Result<i64, String> {
    let text = raw.trim();
    if text.is_empty() || text == "false" || text == "Val(ZeroSized, ())" {
        return Ok(0);
    }
    if text == "true" {
        return Ok(1);
    }
    if let Ok(value) = text.parse::<i64>() {
        return Ok(value);
    }
    if let Some(hex) = text.strip_prefix("0x") {
        return i64::from_str_radix(hex, 16)
            .map_err(|err| format!("subset asm: invalid hex const {raw}: {err}"));
    }
    if let Some(inner) = text
        .strip_prefix("Val(Scalar(")
        .and_then(|rest| rest.split_once("),"))
        .map(|(scalar, _)| scalar.trim())
    {
        if let Some(hex) = inner.strip_prefix("0x") {
            let bits = u64::from_str_radix(hex, 16)
                .map_err(|err| format!("subset asm: invalid scalar const {raw}: {err}"))?;
            return Ok(bits as i64);
        }
    }
    Err(format!("subset asm: unsupported const payload {raw}"))
}

fn symbols_have_c_main_candidate(symbols: &BTreeSet<SymbolRecord>) -> bool {
    symbols.iter().any(|symbol| {
        symbol.kind == SymbolRecordKind::Function
            && (symbol.name == "main" || symbol.name == "_main")
    })
}

fn build_stub_object(
    target: &TargetSpec,
    symbols: &BTreeSet<SymbolRecord>,
) -> Result<Vec<u8>, String> {
    let mut object = Object::new(target.format, target.arch, target.endian);
    object.add_file_symbol(b"cheng_uir_bridge_bootstrap".to_vec());

    let text = object.section_id(StandardSection::Text);
    let data = object.section_id(StandardSection::Data);

    for symbol in symbols {
        let symbol_name = encoded_symbol_name(target.format, &symbol.name);
        match symbol.kind {
            SymbolRecordKind::Function => {
                let body = function_stub_bytes(target.arch)?;
                let offset =
                    object.append_section_data(text, body, function_alignment(target.arch));
                object.add_symbol(Symbol {
                    name: symbol_name,
                    value: offset,
                    size: body.len() as u64,
                    kind: SymbolKind::Text,
                    scope: SymbolScope::Linkage,
                    weak: false,
                    section: SymbolSection::Section(text),
                    flags: SymbolFlags::None,
                });
            }
            SymbolRecordKind::Data => {
                let bytes = [0u8; 8];
                let offset = object.append_section_data(data, &bytes, 8);
                object.add_symbol(Symbol {
                    name: symbol_name,
                    value: offset,
                    size: bytes.len() as u64,
                    kind: SymbolKind::Data,
                    scope: SymbolScope::Linkage,
                    weak: false,
                    section: SymbolSection::Section(data),
                    flags: SymbolFlags::None,
                });
            }
        }
    }

    object
        .write()
        .map_err(|err| format!("failed to serialize object: {err}"))
}

fn encoded_symbol_name(format: BinaryFormat, symbol_name: &str) -> Vec<u8> {
    if format == BinaryFormat::MachO {
        return symbol_name
            .strip_prefix('_')
            .unwrap_or(symbol_name)
            .as_bytes()
            .to_vec();
    }
    symbol_name.as_bytes().to_vec()
}

fn function_stub_bytes(arch: Architecture) -> Result<&'static [u8], String> {
    match arch {
        Architecture::Aarch64 => Ok(&[
            0x00, 0x00, 0x80, 0x52, // mov w0, #0
            0xc0, 0x03, 0x5f, 0xd6, // ret
        ]),
        Architecture::X86_64 => Ok(&[
            0x31, 0xc0, // xor eax, eax
            0xc3, // ret
        ]),
        other => Err(format!("unsupported object stub architecture: {other:?}")),
    }
}

fn function_alignment(arch: Architecture) -> u64 {
    match arch {
        Architecture::Aarch64 => 4,
        _ => 1,
    }
}

unsafe fn input_bytes<'a>(ptr: *const u8, len: c_int) -> Result<&'a [u8], String> {
    if len < 0 {
        return Err("negative ABI buffer length".to_string());
    }
    if len == 0 {
        return Ok(&[]);
    }
    if ptr.is_null() {
        return Err("null ABI buffer pointer".to_string());
    }
    Ok(std::slice::from_raw_parts(ptr, len as usize))
}

fn into_abi_text(text: String, out_len: *mut c_int) -> *mut c_void {
    let sanitized = text.replace('\0', " ");
    let bytes_len = sanitized.len();
    if !out_len.is_null() {
        unsafe {
            *out_len = bytes_len.min(c_int::MAX as usize) as c_int;
        }
    }
    let c_string = CString::new(sanitized).expect("sanitized ABI text must not contain NUL");
    c_string.into_raw().cast::<c_void>()
}

fn error_report(message: &str) -> String {
    format!("status=error\nerror={message}\n")
}

#[cfg(test)]
mod tests {
    use super::{
        build_stub_object, build_subset_asm, compile_entry, compile_entry_v2, load_semantic_module,
        load_symbol_records, Manifest, SymbolRecord, SymbolRecordKind, TargetSpec,
    };
    use object::{Architecture, BinaryFormat, Object as _, ObjectSymbol};
    use std::fs;
    use std::os::raw::c_int;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn symbol_list_parser_keeps_function_and_static_symbols() {
        let temp_dir = temp_dir("symbol_list");
        let symbol_path = temp_dir.join("symbols.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=_ZN4test4main17h123E\nmono_item_candidate\tkind=static\tsymbol=_ZN4test3FOO17h456E\n",
        )
        .unwrap();
        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let symbols = load_symbol_records(&manifest).unwrap();
        assert!(symbols.contains(&SymbolRecord {
            kind: SymbolRecordKind::Function,
            name: "_ZN4test4main17h123E".to_string(),
        }));
        assert!(symbols.contains(&SymbolRecord {
            kind: SymbolRecordKind::Data,
            name: "_ZN4test3FOO17h456E".to_string(),
        }));
    }

    #[test]
    fn inline_manifest_inputs_are_parsed_without_sidecar_files() {
        let mut manifest = Manifest::default();
        manifest
            .fields
            .insert("semantic_tables_inline".to_string(), "version=1\nfunc_names_len=1\nfunc\tidx=0\tname=demo\tfirst_block=0\tblock_len=0\tfirst_local=0\tlocal_len=0\n".to_string());
        manifest.fields.insert(
            "symbol_list_inline".to_string(),
            "mono_item_candidate\tkind=fn\tsymbol=demo\n".to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        assert_eq!(module.funcs.len(), 1);
        assert_eq!(module.funcs[0].name, "demo");
        assert!(symbols.contains(&SymbolRecord {
            kind: SymbolRecordKind::Function,
            name: "demo".to_string(),
        }));
    }

    #[test]
    fn mach_o_stub_contains_requested_symbols() {
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let symbols = std::collections::BTreeSet::from([
            SymbolRecord {
                kind: SymbolRecordKind::Function,
                name: "_main".to_string(),
            },
            SymbolRecord {
                kind: SymbolRecordKind::Function,
                name: "_ZN4test4main17h123E".to_string(),
            },
        ]);
        let bytes = build_stub_object(&target, &symbols).unwrap();
        let file = object::File::parse(&*bytes).unwrap();
        assert_eq!(file.format(), BinaryFormat::MachO);
        assert_eq!(file.architecture(), Architecture::Aarch64);
        let exported = file
            .symbols()
            .filter_map(|symbol| symbol.name().ok().map(str::to_string))
            .collect::<Vec<_>>();
        assert!(exported.iter().any(|symbol| symbol == "_main"));
        assert!(exported
            .iter()
            .any(|symbol| symbol.contains("ZN4test4main17h123E")));
    }

    #[test]
    fn compile_entry_writes_bootstrap_object() {
        let temp_dir = temp_dir("compile_entry");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        let object_path = temp_dir.join("stub.o");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=_ZN4test4main17h123E\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "func_names_len=1\nlocal_names_len=1\nblock_len=1\nexpr_len=2\nstmt_len=1\n",
        )
        .unwrap();

        let manifest = format!(
            "frontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nsemantic_tables_path={}\nsymbol_list_path={}\n",
            semantic_path.display(),
            symbol_path.display(),
        );
        let options = format!("emit=obj\noutput_path={}\n", object_path.display());
        let report = unsafe {
            compile_entry(
                manifest.as_ptr(),
                manifest.len() as c_int,
                options.as_ptr(),
                options.len() as c_int,
            )
        }
        .unwrap();

        assert!(report.contains("status=ok\n"));
        assert!(object_path.exists());
        let object = fs::read(&object_path).unwrap();
        let file = object::File::parse(&*object).unwrap();
        let exported = file
            .symbols()
            .filter_map(|symbol| symbol.name().ok().map(str::to_string))
            .collect::<Vec<_>>();
        assert!(exported.iter().any(|symbol| symbol == "_main"));
        assert!(exported
            .iter()
            .any(|symbol| symbol.contains("ZN4test4main17h123E")));
    }

    #[test]
    fn compile_entry_v2_writes_bootstrap_object() {
        let temp_dir = temp_dir("compile_entry_v2");
        let object_path = temp_dir.join("stub_v2.o");
        let manifest = "frontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\n";
        let semantic = "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nfunc_names_len=1\nlocal_names_len=1\nblock_len=1\nexpr_len=2\nstmt_len=1\nfunc\tidx=0\tname=_ZN4test4main17h123E\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=1\nlocal\tidx=0\tname=_return\ttype=-1\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=1\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=-1\ta=0\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=-1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=term.return\ta=1\tb=0\tc=-1\n";
        let symbols = "mono_item_candidate\tkind=fn\tsymbol=_ZN4test4main17h123E\n";
        let options = format!("emit=obj\noutput_path={}\n", object_path.display());
        let report = unsafe {
            compile_entry_v2(
                manifest.as_ptr(),
                manifest.len() as c_int,
                semantic.as_ptr(),
                semantic.len() as c_int,
                symbols.as_ptr(),
                symbols.len() as c_int,
                options.as_ptr(),
                options.len() as c_int,
            )
        }
        .unwrap();

        assert!(report.contains("status=ok\n"));
        assert!(object_path.exists());
        let object = fs::read(&object_path).unwrap();
        let file = object::File::parse(&*object).unwrap();
        let exported = file
            .symbols()
            .filter_map(|symbol| symbol.name().ok().map(str::to_string))
            .collect::<Vec<_>>();
        assert!(exported.iter().any(|symbol| symbol == "_main"));
        assert!(exported
            .iter()
            .any(|symbol| symbol.contains("ZN4test4main17h123E")));
    }

    #[test]
    fn subset_asm_recognizes_wrap_add_semantics() {
        let temp_dir = temp_dir("subset_wrap_add");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=wrap_add\nmono_item_candidate\tkind=fn\tsymbol=_ZN4core3num21_$LT$impl$u20$i32$GT$12wrapping_add17ha2f2b804e34dadd3E\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=4\ntype_names_len=3\nfunc_names_len=2\nlocal_names_len=6\nblock_len=3\nexpr_len=15\nstmt_len=6\nstring\tidx=0\tvalue=Val(ZeroSized, FnDef(DefId(2:25031 ~ core[2869]::num::{impl#2}::wrapping_add), []))\nstring\tidx=1\tvalue=_ZN4core3num21_$LT$impl$u20$i32$GT$12wrapping_add17ha2f2b804e34dadd3E\nstring\tidx=2\tvalue=2,4\nstring\tidx=3\tvalue=Add\ntype\tidx=0\tname=extern \"C\" fn(i32, i32) -> i32 {wrap_add}\ntype\tidx=1\tname=i32\ntype\tidx=2\tname=fn(i32, i32) -> i32 {core::num::<impl i32>::wrapping_add}\nfunc\tidx=0\tname=wrap_add\tfirst_block=0\tblock_len=2\tfirst_local=0\tlocal_len=3\nfunc\tidx=1\tname=_ZN4core3num21_$LT$impl$u20$i32$GT$12wrapping_add17ha2f2b804e34dadd3E\tfirst_block=2\tblock_len=1\tfirst_local=3\tlocal_len=3\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nlocal\tidx=2\tname=arg1\ttype=1\tmutable=0\nlocal\tidx=3\tname=_return\ttype=1\tmutable=1\nlocal\tidx=4\tname=arg0\ttype=1\tmutable=0\nlocal\tidx=5\tname=arg1\ttype=1\tmutable=0\nblock\tidx=0\tfirst_stmt=0\tstmt_len=3\tterm_kind=call\nblock\tidx=1\tfirst_stmt=3\tstmt_len=1\tterm_kind=return\nblock\tidx=2\tfirst_stmt=4\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=operand.const\ttype=2\ta=0\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=2\tkind=operand.copy\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=1\ta=2\tb=-1\tc=-1\nexpr\tidx=4\tkind=operand.copy\ttype=1\ta=3\tb=-1\tc=-1\nexpr\tidx=5\tkind=operand.list\ttype=-1\ta=2\tb=2\tc=-1\nexpr\tidx=6\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=7\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=8\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=9\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=10\tkind=operand.copy\ttype=1\ta=9\tb=-1\tc=-1\nexpr\tidx=11\tkind=place.local\ttype=1\ta=2\tb=-1\tc=-1\nexpr\tidx=12\tkind=operand.copy\ttype=1\ta=11\tb=-1\tc=-1\nexpr\tidx=13\tkind=rvalue.binary\ttype=1\ta=10\tb=12\tc=3\nexpr\tidx=14\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=term.call_symbol\ta=1\tb=-1\tc=-1\nstmt\tidx=1\tkind=term.call\ta=0\tb=5\tc=6\nstmt\tidx=2\tkind=term.call_target\ta=1\tb=-1\tc=-1\nstmt\tidx=3\tkind=term.return\ta=7\tb=0\tc=-1\nstmt\tidx=4\tkind=assign\ta=8\tb=13\tc=-1\nstmt\tidx=5\tkind=term.return\ta=14\tb=2\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let manifest = Manifest::default();
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("wrap_add semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 2);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_wrap_add:"));
        assert!(asm
            .contains("bl __ZN4core3num21_$LT$impl$u20$i32$GT$12wrapping_add17ha2f2b804e34dadd3E"));
        assert!(asm.contains("add w8, w8, w9") || asm.contains("add w0, w0, w1"));
    }

    #[test]
    fn subset_asm_recognizes_checked_add_semantics() {
        let temp_dir = temp_dir("subset_checked_add");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(&symbol_path, "mono_item_candidate\tkind=fn\tsymbol=add\n").unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=2\ntype_names_len=4\nfunc_names_len=1\nlocal_names_len=4\nblock_len=2\nexpr_len=15\nstmt_len=4\nstring\tidx=0\tvalue=AddWithOverflow\nstring\tidx=1\tvalue=Overflow(Add, copy _1, copy _2),expected=false\ntype\tidx=0\tname=extern \"C\" fn(i32, i32) -> i32 {add}\ntype\tidx=1\tname=i32\ntype\tidx=2\tname=(i32, bool)\ntype\tidx=3\tname=bool\nfunc\tidx=0\tname=add\tfirst_block=0\tblock_len=2\tfirst_local=0\tlocal_len=4\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nlocal\tidx=2\tname=arg1\ttype=1\tmutable=0\nlocal\tidx=3\tname=_3\ttype=2\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=2\tterm_kind=assert\nblock\tidx=1\tfirst_stmt=2\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=2\ta=3\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=2\tkind=operand.copy\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=1\ta=2\tb=-1\tc=-1\nexpr\tidx=4\tkind=operand.copy\ttype=1\ta=3\tb=-1\tc=-1\nexpr\tidx=5\tkind=rvalue.binary\ttype=2\ta=2\tb=4\tc=0\nexpr\tidx=6\tkind=place.local\ttype=3\ta=3\tb=-1\tc=-1\nexpr\tidx=7\tkind=place.field\ttype=3\ta=6\tb=1\tc=3\nexpr\tidx=8\tkind=operand.move\ttype=3\ta=7\tb=-1\tc=-1\nexpr\tidx=9\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=10\tkind=place.local\ttype=1\ta=3\tb=-1\tc=-1\nexpr\tidx=11\tkind=place.field\ttype=1\ta=10\tb=0\tc=1\nexpr\tidx=12\tkind=operand.move\ttype=1\ta=11\tb=-1\tc=-1\nexpr\tidx=13\tkind=rvalue.use\ttype=1\ta=12\tb=-1\tc=-1\nexpr\tidx=14\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=5\tc=-1\nstmt\tidx=1\tkind=term.assert\ta=8\tb=1\tc=1\nstmt\tidx=2\tkind=assign\ta=9\tb=13\tc=-1\nstmt\tidx=3\tkind=term.return\ta=14\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let manifest = Manifest::default();
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("checked add semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_add:"));
        assert!(asm.contains("adds w8, w8, w9"));
        assert!(asm.contains("cset w10, vs"));
        assert!(asm.contains("b.eq Luir_func0_bb1"));
        assert!(asm.contains("str w8, [sp, #12]"));
        assert!(asm.contains("str w10, [sp, #16]"));
    }

    #[test]
    fn compile_entry_bin_main_avoids_duplicate_c_main() {
        let temp_dir = temp_dir("compile_bin_main");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        let object_path = temp_dir.join("main_bin.o");
        fs::write(&symbol_path, "mono_item_candidate\tkind=fn\tsymbol=main\n").unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nstring_table_len=1\ntype_names_len=2\nfunc_names_len=1\nlocal_names_len=1\nblock_len=1\nexpr_len=4\nstmt_len=2\nstring\tidx=0\tvalue=Val(Scalar(0x00000000), i32)\ntype\tidx=0\tname=extern \"C\" fn() -> i32 {main}\ntype\tidx=1\tname=i32\nfunc\tidx=0\tname=main\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=1\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=1\tkind=operand.const\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=2\tkind=rvalue.use\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=2\tc=-1\nstmt\tidx=1\tkind=term.return\ta=3\tb=0\tc=-1\n",
        )
        .unwrap();

        let manifest = format!(
            "frontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nsemantic_tables_path={}\nsymbol_list_path={}\n",
            semantic_path.display(),
            symbol_path.display(),
        );
        let options = format!("emit=obj\noutput_path={}\n", object_path.display());
        let report = unsafe {
            compile_entry(
                manifest.as_ptr(),
                manifest.len() as c_int,
                options.as_ptr(),
                options.len() as c_int,
            )
        }
        .unwrap();

        assert!(report.contains("bridge=bootstrap_subset_asm\n"));
        assert!(report.contains("fn_symbols=1\n"));
        assert!(report.contains("real_fn_symbols=1\n"));
        assert!(report.contains("stub_fn_symbols=0\n"));
        let object = fs::read(&object_path).unwrap();
        let file = object::File::parse(&*object).unwrap();
        let exported = file
            .symbols()
            .filter_map(|symbol| symbol.name().ok().map(str::to_string))
            .collect::<Vec<_>>();
        let main_count = exported
            .iter()
            .filter(|name| name.as_str() == "_main")
            .count();
        assert_eq!(main_count, 1);
    }

    #[test]
    fn compile_entry_bin_wrapper_uses_explicit_entry_symbol() {
        let temp_dir = temp_dir("compile_bin_wrapper");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        let object_path = temp_dir.join("wrapper_bin.o");
        let entry_symbol = "_ZN15strict_std_main4main17hd22d3c39d6f5503eE";
        fs::write(
            &symbol_path,
            format!(
                "mono_item_candidate\tkind=fn\tsymbol={entry_symbol}\nmono_item_candidate\tkind=fn\tsymbol=_main\n"
            ),
        )
        .unwrap();
        fs::write(
            &semantic_path,
            format!(
                "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nstring_table_len=0\ntype_names_len=2\nfunc_names_len=1\nlocal_names_len=1\nblock_len=1\nexpr_len=1\nstmt_len=1\ntype\tidx=0\tname=fn() {{main}}\ntype\tidx=1\tname=()\nfunc\tidx=0\tname={entry_symbol}\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=1\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=1\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=term.return\ta=0\tb=0\tc=-1\n"
            ),
        )
        .unwrap();

        let manifest = format!(
            "frontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=bin\nc_main_target_symbol={entry_symbol}\nsemantic_tables_path={}\nsymbol_list_path={}\n",
            semantic_path.display(),
            symbol_path.display(),
        );
        let options = format!("emit=obj\noutput_path={}\n", object_path.display());
        let report = unsafe {
            compile_entry(
                manifest.as_ptr(),
                manifest.len() as c_int,
                options.as_ptr(),
                options.len() as c_int,
            )
        }
        .unwrap();

        assert!(report.contains("bridge=bootstrap_subset_asm\n"));
        assert!(report.contains("fn_symbols=2\n"));
        assert!(report.contains("real_fn_symbols=2\n"));
        assert!(report.contains("stub_fn_symbols=0\n"));
        let object = fs::read(&object_path).unwrap();
        let file = object::File::parse(&*object).unwrap();
        let exported = file
            .symbols()
            .filter_map(|symbol| symbol.name().ok().map(str::to_string))
            .collect::<Vec<_>>();
        assert!(exported.iter().any(|name| name == "_main"));
        assert!(exported
            .iter()
            .any(|name| name == &format!("_{entry_symbol}")));
    }

    #[test]
    fn subset_asm_recognizes_ref_and_deref_semantics() {
        let temp_dir = temp_dir("subset_ref_deref");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=load_ref\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=1\ntype_names_len=3\nfunc_names_len=1\nlocal_names_len=3\nblock_len=1\nexpr_len=8\nstmt_len=3\nstring\tidx=0\tvalue=Shared\ntype\tidx=0\tname=extern \"C\" fn(i32) -> i32 {load_ref}\ntype\tidx=1\tname=i32\ntype\tidx=2\tname=&i32\nfunc\tidx=0\tname=load_ref\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=3\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nlocal\tidx=2\tname=_2\ttype=2\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=3\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=2\ta=2\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=2\tkind=rvalue.ref\ttype=2\ta=1\tb=0\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=4\tkind=place.local\ttype=2\ta=2\tb=-1\tc=-1\nexpr\tidx=5\tkind=place.deref\ttype=1\ta=4\tb=-1\tc=-1\nexpr\tidx=6\tkind=operand.move\ttype=1\ta=5\tb=-1\tc=-1\nexpr\tidx=7\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=2\tc=-1\nstmt\tidx=1\tkind=assign\ta=3\tb=6\tc=-1\nstmt\tidx=2\tkind=term.return\ta=7\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("ref/deref semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_load_ref:"));
        assert!(asm.contains("add x8, sp, #4"));
        assert!(asm.contains("ldr x15, [sp, #8]"));
        assert!(asm.contains("ldr w8, [x15, #0]"));
    }

    #[test]
    fn subset_asm_recognizes_unknown_aggregate_field_semantics() {
        let temp_dir = temp_dir("subset_unknown_aggregate");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=closure_field\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=2\ntype_names_len=3\nfunc_names_len=1\nlocal_names_len=3\nblock_len=1\nexpr_len=10\nstmt_len=3\nstring\tidx=0\tvalue=Closure(fake)\nstring\tidx=1\tvalue=2\ntype\tidx=0\tname=extern \"C\" fn(i32) -> i32 {closure_field}\ntype\tidx=1\tname=i32\ntype\tidx=2\tname={closure@fake}\nfunc\tidx=0\tname=closure_field\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=3\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nlocal\tidx=2\tname=_2\ttype=2\tmutable=1\nblock\tidx=0\tfirst_stmt=0\tstmt_len=3\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=2\ta=2\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=2\tkind=operand.copy\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=rvalue.aggregate\ttype=2\ta=0\tb=1\tc=1\nexpr\tidx=4\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=5\tkind=place.local\ttype=2\ta=2\tb=-1\tc=-1\nexpr\tidx=6\tkind=place.field\ttype=1\ta=5\tb=0\tc=1\nexpr\tidx=7\tkind=operand.move\ttype=1\ta=6\tb=-1\tc=-1\nexpr\tidx=8\tkind=rvalue.use\ttype=1\ta=7\tb=-1\tc=-1\nexpr\tidx=9\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=3\tc=-1\nstmt\tidx=1\tkind=assign\ta=4\tb=8\tc=-1\nstmt\tidx=2\tkind=term.return\ta=9\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) =
            asm.expect("aggregate field semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_closure_field:"));
        assert!(asm.contains("str w8, [sp, #8]"));
        assert!(asm.contains("ldr w8, [sp, #8]"));
    }

    #[test]
    fn subset_asm_uses_layout_scalar_for_newtype_return() {
        let temp_dir = temp_dir("subset_layout_newtype");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=report\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=1\ntype_names_len=2\nlayout_descs_len=2\nfunc_names_len=1\nlocal_names_len=2\nblock_len=1\nexpr_len=4\nstmt_len=2\nstring\tidx=0\tvalue=Val(Scalar(0x00), std::process::ExitCode)\ntype\tidx=0\tname=fn(()) -> ExitCode {report}\ntype\tidx=1\tname=ExitCode\nlayout\tidx=0\tname=fn(()) -> ExitCode {report}\tsize=0\talign=1\tabi=Memory { sized: true }\tniche_bits=0\nlayout\tidx=1\tname=ExitCode\tsize=1\talign=1\tabi=Scalar(Initialized { value: Int(I8, false), valid_range: 0..=255 })\tniche_bits=0\nfunc\tidx=0\tname=report\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=2\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=-1\tmutable=0\nblock\tidx=0\tfirst_stmt=0\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=1\tkind=operand.const\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=2\tkind=rvalue.use\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=2\tc=-1\nstmt\tidx=1\tkind=term.return\ta=3\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("layout-backed newtype should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_report:"));
        assert!(asm.contains("mov w8, #0"));
    }

    #[test]
    fn subset_asm_recognizes_indirect_fn_pointer_call() {
        let temp_dir = temp_dir("subset_indirect_call");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=invoke\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=1\ntype_names_len=3\nlayout_descs_len=3\nfunc_names_len=1\nlocal_names_len=2\nblock_len=2\nexpr_len=5\nstmt_len=3\nstring\tidx=0\tvalue=\ntype\tidx=0\tname=fn(fn()) {invoke}\ntype\tidx=1\tname=fn()\ntype\tidx=2\tname=()\nlayout\tidx=0\tname=fn(fn()) {invoke}\tsize=0\talign=1\tabi=Memory { sized: true }\tniche_bits=0\nlayout\tidx=1\tname=fn()\tsize=8\talign=8\tabi=Scalar(Initialized { value: Pointer(AddressSpace(0)), valid_range: 1..=18446744073709551615 })\tniche_bits=0\nlayout\tidx=2\tname=()\tsize=0\talign=1\tabi=Memory { sized: true }\tniche_bits=0\nfunc\tidx=0\tname=invoke\tfirst_block=0\tblock_len=2\tfirst_local=0\tlocal_len=2\nlocal\tidx=0\tname=_return\ttype=2\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nblock\tidx=0\tfirst_stmt=0\tstmt_len=2\tterm_kind=call\nblock\tidx=1\tfirst_stmt=2\tstmt_len=1\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=1\tkind=operand.move\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=2\tkind=operand.list\ttype=-1\ta=0\tb=0\tc=-1\nexpr\tidx=3\tkind=place.local\ttype=2\ta=0\tb=-1\tc=-1\nexpr\tidx=4\tkind=place.local\ttype=2\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=term.call\ta=1\tb=2\tc=3\nstmt\tidx=1\tkind=term.call_target\ta=1\tb=-1\tc=-1\nstmt\tidx=2\tkind=term.return\ta=4\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("indirect fn pointer call should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_invoke:"));
        assert!(asm.contains("blr x15"));
    }

    #[test]
    fn subset_asm_recognizes_slice_len_metadata() {
        let temp_dir = temp_dir("subset_slice_len");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=slice_len\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=staticlib\nstring_table_len=1\ntype_names_len=3\nlayout_descs_len=3\nfunc_names_len=1\nlocal_names_len=2\nblock_len=1\nexpr_len=5\nstmt_len=2\nfat_ptr_meta_len=1\nvtable_descs_len=0\ncall_abi_len=0\nstring\tidx=0\tvalue=PtrMetadata\ntype\tidx=0\tname=for<'a> extern \"C\" fn(&'a [i32]) -> usize {slice_len}\ntype\tidx=1\tname=usize\ntype\tidx=2\tname=&[i32]\nlayout\tidx=0\tname=for<'a> extern \"C\" fn(&'a [i32]) -> usize {slice_len}\tsize=0\talign=1\tabi=Memory { sized: true }\tniche_bits=0\nlayout\tidx=1\tname=usize\tsize=8\talign=8\tabi=Scalar(Initialized { value: Int(I64, false), valid_range: 0..=18446744073709551615 })\tniche_bits=0\nlayout\tidx=2\tname=&[i32]\tsize=16\talign=8\tabi=ScalarPair(Initialized { value: Pointer(AddressSpace(0)), valid_range: 1..=18446744073709551615 }, Initialized { value: Int(I64, false), valid_range: 0..=18446744073709551615 })\tniche_bits=0\nfunc\tidx=0\tname=slice_len\tfirst_block=0\tblock_len=1\tfirst_local=0\tlocal_len=2\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=2\tmutable=0\nblock\tidx=0\tfirst_stmt=0\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=2\ta=1\tb=-1\tc=-1\nexpr\tidx=2\tkind=operand.copy\ttype=2\ta=1\tb=-1\tc=-1\nexpr\tidx=3\tkind=rvalue.unary\ttype=1\ta=2\tb=0\tc=-1\nexpr\tidx=4\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=assign\ta=0\tb=3\tc=-1\nstmt\tidx=1\tkind=term.return\ta=4\tb=0\tc=-1\nfat_ptr\tidx=0\tname=slice_len::arg0\tmeta_kind=slice_len\tpayload_type=[i32]\tmetadata_type=usize\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("slice len semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_slice_len:"));
        assert!(asm.contains("str x0, [sp, #8]"));
        assert!(asm.contains("str x1, [sp, #16]"));
        assert!(asm.contains("mov x8, x15"));
    }

    #[test]
    fn subset_asm_recognizes_trivial_drop_terminator() {
        let temp_dir = temp_dir("subset_drop");
        let symbol_path = temp_dir.join("symbols.txt");
        let semantic_path = temp_dir.join("semantic.txt");
        fs::write(
            &symbol_path,
            "mono_item_candidate\tkind=fn\tsymbol=drop_then_return\n",
        )
        .unwrap();
        fs::write(
            &semantic_path,
            "version=1\nfrontend=rust\ntarget=aarch64-apple-darwin\ncrate_type=rlib\nstring_table_len=0\ntype_names_len=2\nfunc_names_len=1\nlocal_names_len=2\nblock_len=2\nexpr_len=3\nstmt_len=3\ntype\tidx=0\tname=extern \"C\" fn(i32) -> i32 {drop_then_return}\ntype\tidx=1\tname=i32\nfunc\tidx=0\tname=drop_then_return\tfirst_block=0\tblock_len=2\tfirst_local=0\tlocal_len=2\nlocal\tidx=0\tname=_return\ttype=1\tmutable=1\nlocal\tidx=1\tname=arg0\ttype=1\tmutable=0\nblock\tidx=0\tfirst_stmt=0\tstmt_len=1\tterm_kind=drop\nblock\tidx=1\tfirst_stmt=1\tstmt_len=2\tterm_kind=return\nexpr\tidx=0\tkind=place.local\ttype=1\ta=1\tb=-1\tc=-1\nexpr\tidx=1\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nexpr\tidx=2\tkind=place.local\ttype=1\ta=0\tb=-1\tc=-1\nstmt\tidx=0\tkind=term.drop\ta=0\tb=1\tc=-1\nstmt\tidx=1\tkind=assign\ta=1\tb=0\tc=-1\nstmt\tidx=2\tkind=term.return\ta=2\tb=0\tc=-1\n",
        )
        .unwrap();

        let mut manifest = Manifest::default();
        manifest.fields.insert(
            "semantic_tables_path".to_string(),
            semantic_path.display().to_string(),
        );
        manifest.fields.insert(
            "symbol_list_path".to_string(),
            symbol_path.display().to_string(),
        );
        let module = load_semantic_module(&manifest).unwrap();
        let symbols = load_symbol_records(&manifest).unwrap();
        let target = TargetSpec {
            format: BinaryFormat::MachO,
            arch: Architecture::Aarch64,
            endian: object::Endianness::Little,
        };
        let asm = build_subset_asm(&manifest, &target, &module, &symbols).unwrap();
        let (asm, stats) = asm.expect("trivial drop semantic tables should lower into subset asm");
        assert_eq!(stats.real_fn_symbols, 1);
        assert_eq!(stats.stub_fn_symbols, 0);
        assert!(asm.contains("_drop_then_return:"));
        assert!(asm.contains("b Luir_func0_bb1"));
    }

    fn temp_dir(label: &str) -> std::path::PathBuf {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path =
            std::env::temp_dir().join(format!("cheng_uir_bridge_bootstrap_{label}_{unique}"));
        fs::create_dir_all(&path).unwrap();
        path
    }
}
