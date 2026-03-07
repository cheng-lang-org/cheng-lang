use std::any::Any;
use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

use rustc_codegen_ssa::traits::CodegenBackend;
use rustc_codegen_ssa::{CodegenResults, CompiledModule, CrateInfo, ModuleKind, TargetConfig};
use rustc_data_structures::fx::FxIndexMap;
use rustc_middle::dep_graph::{WorkProduct, WorkProductId};
use rustc_middle::ty::TyCtxt;
use rustc_session::config::{CrateType, OutputFilenames, PrintRequest};
use rustc_session::Session;
use rustc_span::{sym, Symbol};
use rustc_target::spec::Arch;

use crate::ffi::{compile_manifest_with_inputs, validate_manifest_with_inputs};
use crate::manifest::{CompileOptionsV1, CompileReportV1, SemanticModuleV1};
use crate::mir_lowering::lower_crate_to_semantic;

const STRICT_MODE_REASON: &str =
    "rustc_codegen_uir is running in strict UIR-only mode; LLVM fallback is disabled";

pub struct UirCodegenBackend;

struct UirCodegenOutcome {
    codegen_results: CodegenResults,
}

struct UirObservation {
    started_at: Instant,
    semantic_module: SemanticModuleV1,
    cgu_lines: String,
    mono_item_lines: String,
    symbol_list_text: String,
}

impl CodegenBackend for UirCodegenBackend {
    fn locale_resource(&self) -> &'static str {
        ""
    }

    fn name(&self) -> &'static str {
        "uir"
    }

    fn init(&self, sess: &Session) {
        sess.dcx().warn(
            "rustc_codegen_uir: LLVM fallback is disabled; only direct UIR compilation is allowed",
        );
    }

    fn print(&self, _req: &PrintRequest, _out: &mut String, _sess: &Session) {}

    fn target_config(&self, sess: &Session) -> TargetConfig {
        target_config_for_session(sess)
    }

    fn codegen_crate<'tcx>(&self, tcx: TyCtxt<'tcx>) -> Box<dyn Any> {
        let mut observation = collect_observation(tcx);
        let outputs = tcx.output_filenames(());
        let artifact_paths =
            UirArtifactPaths::new(outputs, &observation.semantic_module.source_tag);
        observation.semantic_module.crate_type = primary_crate_type_text(tcx.crate_types());
        observation.semantic_module.c_main_target_symbol =
            c_main_target_symbol(tcx, &observation.semantic_module.crate_type).unwrap_or_default();
        observation.semantic_module.semantic_tables_path =
            artifact_paths.semantic_tables_path.display().to_string();
        observation.semantic_module.symbol_list_path =
            artifact_paths.symbol_list_path.display().to_string();
        if needs_c_main_symbol(&observation.semantic_module.crate_type)
            && !has_c_main_candidate(&observation.symbol_list_text)
        {
            observation
                .symbol_list_text
                .push_str("mono_item_candidate\tcgu=synthetic.entry\tkind=fn\tunsafe=0\tuser_defined=0\tsymbol=_main\n");
        }
        let semantic_tables_text = observation.semantic_module.to_semantic_tables_text();
        let diagnostic_manifest_text = observation.semantic_module.to_manifest();

        let mut compile_options = CompileOptionsV1::default();
        compile_options.allow_rust_llvm_fallback = false;
        compile_options.output_path = artifact_paths.object_path.display().to_string();
        let compile_result = compile_manifest_with_inputs(
            &diagnostic_manifest_text,
            &semantic_tables_text,
            &observation.symbol_list_text,
            &compile_options,
        );
        let validate_result = if compile_result.is_ok() {
            None
        } else {
            Some(validate_manifest_with_inputs(
                &diagnostic_manifest_text,
                &semantic_tables_text,
                &observation.symbol_list_text,
            ))
        };
        let bridge_ok = bridge_compile_succeeded(&compile_result, &artifact_paths.object_path);
        let write_input_sidecars = should_write_input_sidecars(bridge_ok);
        if bridge_ok {
            observation.semantic_module.rust_report.mono_items_uir =
                observation.semantic_module.rust_report.mono_items_total;
            observation.semantic_module.high_uir_checked = true;
            observation.semantic_module.low_uir_lowered = true;
            observation.semantic_module.phase_tag = 2;
        }

        observation.semantic_module.rust_report.elapsed_ms =
            elapsed_ms_u32(observation.started_at.elapsed().as_millis());
        let report_text = render_report(
            &observation,
            &artifact_paths,
            write_input_sidecars,
            validate_result
                .as_ref()
                .and_then(|result| result.as_ref().ok()),
            compile_result.as_ref().ok(),
            validate_result
                .as_ref()
                .and_then(|result| result.as_ref().err()),
            compile_result.as_ref().err(),
        );
        write_output_artifacts(
            tcx.sess,
            &artifact_paths,
            &diagnostic_manifest_text,
            &semantic_tables_text,
            &observation.symbol_list_text,
            &report_text,
            write_input_sidecars,
        );

        if bridge_ok {
            let codegen_results = build_codegen_results(tcx, &artifact_paths.object_path);
            return Box::new(UirCodegenOutcome { codegen_results });
        }

        let fatal_message = strict_failure_message(
            &artifact_paths.report_path,
            compile_result.as_ref().ok(),
            validate_result
                .as_ref()
                .and_then(|result| result.as_ref().err()),
            compile_result.as_ref().err(),
        );
        tcx.sess.dcx().fatal(fatal_message);
    }

    fn join_codegen(
        &self,
        ongoing_codegen: Box<dyn Any>,
        _sess: &Session,
        _outputs: &OutputFilenames,
    ) -> (CodegenResults, FxIndexMap<WorkProductId, WorkProduct>) {
        let ongoing = ongoing_codegen
            .downcast::<UirCodegenOutcome>()
            .expect("Expected UirCodegenOutcome, found Box<Any>");
        let UirCodegenOutcome { codegen_results } = *ongoing;
        (codegen_results, FxIndexMap::default())
    }
}

struct UirArtifactPaths {
    manifest_path: PathBuf,
    report_path: PathBuf,
    semantic_tables_path: PathBuf,
    symbol_list_path: PathBuf,
    object_path: PathBuf,
}

impl UirArtifactPaths {
    fn new(outputs: &OutputFilenames, source_tag: &str) -> Self {
        let stem = sanitize_stem(source_tag);
        Self {
            manifest_path: outputs
                .temp_path_for_diagnostic(&format!("{stem}.rust-uir-semantic.txt")),
            report_path: outputs.temp_path_for_diagnostic(&format!("{stem}.rust-uir-report.txt")),
            semantic_tables_path: outputs
                .temp_path_for_diagnostic(&format!("{stem}.rust-uir-semantic-tables.txt")),
            symbol_list_path: outputs
                .temp_path_for_diagnostic(&format!("{stem}.rust-uir-symbols.txt")),
            object_path: outputs.temp_path_for_diagnostic(&format!("{stem}.rust-uir-object.o")),
        }
    }
}

fn collect_observation<'tcx>(tcx: TyCtxt<'tcx>) -> UirObservation {
    let lowered = lower_crate_to_semantic(tcx, STRICT_MODE_REASON);

    UirObservation {
        started_at: Instant::now(),
        semantic_module: lowered.semantic_module,
        cgu_lines: lowered.cgu_lines,
        mono_item_lines: lowered.mono_item_lines,
        symbol_list_text: lowered.symbol_list_text,
    }
}

fn render_report(
    observation: &UirObservation,
    paths: &UirArtifactPaths,
    write_input_sidecars: bool,
    validate_text: Option<&String>,
    compile_report: Option<&CompileReportV1>,
    validate_error: Option<&crate::ffi::UirBridgeError>,
    compile_error: Option<&crate::ffi::UirBridgeError>,
) -> String {
    let rust_report = &observation.semantic_module.rust_report;
    let bridge_ok = compile_report.is_some_and(|report| report.get("status") == Some("ok"))
        && observation.semantic_module.low_uir_lowered
        && rust_report.mono_items_uir == rust_report.mono_items_total;
    let mut out = String::new();
    let _ = writeln!(
        out,
        "status={}",
        if bridge_ok { "ok" } else { "strict_failure" }
    );
    let _ = writeln!(out, "frontend={}", observation.semantic_module.frontend);
    let _ = writeln!(out, "target={}", observation.semantic_module.target);
    let _ = writeln!(out, "source_tag={}", observation.semantic_module.source_tag);
    let _ = writeln!(out, "crate_type={}", observation.semantic_module.crate_type);
    let _ = writeln!(out, "phase_tag={}", observation.semantic_module.phase_tag);
    let _ = writeln!(
        out,
        "high_uir_checked={}",
        bool_manifest(observation.semantic_module.high_uir_checked),
    );
    let _ = writeln!(
        out,
        "low_uir_lowered={}",
        bool_manifest(observation.semantic_module.low_uir_lowered),
    );
    let _ = writeln!(out, "strict_mode=1");
    let _ = writeln!(out, "llvm_fallback=0");
    let _ = writeln!(
        out,
        "fallback_reason={}",
        observation.semantic_module.fallback_reason
    );
    let _ = writeln!(
        out,
        "semantic_manifest_path={}",
        paths.manifest_path.display()
    );
    let _ = writeln!(
        out,
        "semantic_tables_path={}",
        paths.semantic_tables_path.display()
    );
    let _ = writeln!(
        out,
        "semantic_tables_mode={}",
        if write_input_sidecars {
            "file"
        } else {
            "inline"
        }
    );
    let _ = writeln!(out, "report_path={}", paths.report_path.display());
    let _ = writeln!(out, "symbol_list_path={}", paths.symbol_list_path.display());
    let _ = writeln!(
        out,
        "symbol_list_mode={}",
        if write_input_sidecars {
            "file"
        } else {
            "inline"
        }
    );
    let _ = writeln!(out, "uir_object_path={}", paths.object_path.display());
    let _ = writeln!(
        out,
        "uir_func_count={}",
        observation.semantic_module.func_names.len()
    );
    let _ = writeln!(
        out,
        "uir_local_count={}",
        observation.semantic_module.local_names.len()
    );
    let _ = writeln!(
        out,
        "uir_block_count={}",
        observation.semantic_module.block_first_stmt.len()
    );
    let _ = writeln!(
        out,
        "uir_expr_count={}",
        observation.semantic_module.expr_kind.len()
    );
    let _ = writeln!(
        out,
        "uir_stmt_count={}",
        observation.semantic_module.stmt_kind.len()
    );
    let _ = writeln!(
        out,
        "rust_uir_report\tmono_items_total={}\tmono_items_uir={}\tmono_items_llvm_fallback={}\tunsafe_regions={}\tborrow_proof_items={}\tms={}",
        rust_report.mono_items_total,
        rust_report.mono_items_uir,
        rust_report.mono_items_llvm_fallback,
        rust_report.unsafe_regions,
        rust_report.borrow_proof_items,
        rust_report.elapsed_ms,
    );
    let _ = writeln!(
        out,
        "generics_report\trustc_mono_items={}",
        rust_report.mono_items_total,
    );
    match validate_text {
        Some(text) => {
            let _ = writeln!(out, "bridge_validate_status=ok");
            for line in text.lines() {
                let line = line.trim();
                if line.is_empty() {
                    continue;
                }
                let _ = writeln!(out, "bridge_validate.{line}");
            }
        }
        None => {
            if let Some(err) = validate_error {
                let _ = writeln!(out, "bridge_validate_status=error");
                let _ = writeln!(out, "bridge_validate_error={err}");
            } else {
                let _ = writeln!(out, "bridge_validate_status=skipped");
            }
        }
    }
    match compile_report {
        Some(report) => {
            let _ = writeln!(out, "bridge_compile_status=ok");
            for (key, value) in &report.fields {
                let _ = writeln!(out, "bridge_compile.{key}={value}");
            }
        }
        None => {
            let _ = writeln!(out, "bridge_compile_status=error");
            if let Some(err) = compile_error {
                let _ = writeln!(out, "bridge_compile_error={err}");
            }
        }
    }
    out.push_str(&observation.cgu_lines);
    out.push_str(&observation.mono_item_lines);
    out
}

fn write_output_artifacts(
    sess: &Session,
    paths: &UirArtifactPaths,
    manifest_text: &str,
    semantic_tables_text: &str,
    symbol_list_text: &str,
    report_text: &str,
    write_input_sidecars: bool,
) {
    if let Err(err) = fs::write(&paths.manifest_path, manifest_text) {
        sess.dcx().warn(format!(
            "rustc_codegen_uir: failed to write semantic manifest to {}: {err}",
            paths.manifest_path.display()
        ));
    }
    if write_input_sidecars {
        if let Err(err) = fs::write(&paths.semantic_tables_path, semantic_tables_text) {
            sess.dcx().warn(format!(
                "rustc_codegen_uir: failed to write semantic tables to {}: {err}",
                paths.semantic_tables_path.display()
            ));
        }
        if let Err(err) = fs::write(&paths.symbol_list_path, symbol_list_text) {
            sess.dcx().warn(format!(
                "rustc_codegen_uir: failed to write symbol list to {}: {err}",
                paths.symbol_list_path.display()
            ));
        }
    } else {
        let _ = fs::remove_file(&paths.semantic_tables_path);
        let _ = fs::remove_file(&paths.symbol_list_path);
    }
    if let Err(err) = fs::write(&paths.report_path, report_text) {
        sess.dcx().warn(format!(
            "rustc_codegen_uir: failed to write strict report to {}: {err}",
            paths.report_path.display()
        ));
    }
}

fn should_write_input_sidecars(bridge_ok: bool) -> bool {
    !bridge_ok
        || env::var("CHENG_UIR_WRITE_INPUT_SIDECARS")
            .map(|value| value == "1" || value.eq_ignore_ascii_case("true"))
            .unwrap_or(false)
}

fn strict_failure_message(
    report_path: &Path,
    compile_report: Option<&CompileReportV1>,
    validate_error: Option<&crate::ffi::UirBridgeError>,
    compile_error: Option<&crate::ffi::UirBridgeError>,
) -> String {
    if let Some(err) = compile_error {
        return format!(
            "rustc_codegen_uir: strict UIR-only mode forbids LLVM fallback; Cheng bridge compile failed: {err}. See {}",
            report_path.display()
        );
    }
    if let Some(err) = validate_error {
        return format!(
            "rustc_codegen_uir: strict UIR-only mode forbids LLVM fallback; Cheng bridge validation failed: {err}. See {}",
            report_path.display()
        );
    }
    if let Some(report) = compile_report {
        if report.get("status") != Some("ok") {
            let status = report.get("status").unwrap_or("unknown");
            let reason = report
                .get("fallback_reason")
                .or_else(|| report.get("error"))
                .unwrap_or("bridge returned non-ok status");
            return format!(
                "rustc_codegen_uir: strict UIR-only mode forbids LLVM fallback; Cheng bridge returned status={status}: {reason}. See {}",
                report_path.display()
            );
        }
        return format!(
            "rustc_codegen_uir: Cheng bridge returned status=ok, but no linkable UIR object was produced at {}. See {}",
            report
                .get("output_path")
                .unwrap_or("the requested object path"),
            report_path.display()
        );
    }
    format!(
        "rustc_codegen_uir: strict UIR-only mode cannot continue, and LLVM fallback is disabled. See {}",
        report_path.display()
    )
}

fn bridge_compile_succeeded(
    compile_result: &Result<CompileReportV1, crate::ffi::UirBridgeError>,
    object_path: &Path,
) -> bool {
    matches!(
        compile_result,
        Ok(report) if report.get("status") == Some("ok") && object_path.exists()
    )
}

fn build_codegen_results<'tcx>(tcx: TyCtxt<'tcx>, object_path: &Path) -> CodegenResults {
    let target_cpu = tcx.sess.target.cpu.to_string();
    let crate_info = CrateInfo::new(tcx, target_cpu);
    let module = CompiledModule {
        name: "rustc_codegen_uir".to_string(),
        kind: ModuleKind::Regular,
        object: Some(object_path.to_path_buf()),
        dwarf_object: None,
        bytecode: None,
        assembly: None,
        llvm_ir: None,
        links_from_incr_cache: Vec::new(),
    };
    CodegenResults {
        modules: vec![module],
        allocator_module: None,
        crate_info,
    }
}

fn sanitize_stem(text: &str) -> String {
    let mut out = String::new();
    for ch in text.chars() {
        if ch.is_ascii_alphanumeric() || ch == '-' || ch == '_' || ch == '.' {
            out.push(ch);
        } else {
            out.push('_');
        }
    }
    if out.is_empty() {
        "rust".to_string()
    } else {
        out
    }
}

fn bool_manifest(value: bool) -> &'static str {
    if value {
        "1"
    } else {
        "0"
    }
}

fn elapsed_ms_u32(value: u128) -> u32 {
    value.min(u32::MAX as u128) as u32
}

fn primary_crate_type_text(crate_types: &[CrateType]) -> String {
    let primary = crate_types
        .first()
        .copied()
        .unwrap_or(CrateType::Executable);
    match primary {
        CrateType::Executable => "bin",
        CrateType::Dylib => "dylib",
        CrateType::Rlib => "rlib",
        CrateType::Staticlib => "staticlib",
        CrateType::Cdylib => "cdylib",
        CrateType::Sdylib => "sdylib",
        CrateType::ProcMacro => "proc-macro",
    }
    .to_string()
}

fn needs_c_main_symbol(crate_type: &str) -> bool {
    crate_type == "bin"
}

fn has_c_main_candidate(symbol_list_text: &str) -> bool {
    symbol_list_text.contains("\tsymbol=_main\n") || symbol_list_text.contains("\tsymbol=main\n")
}

fn c_main_target_symbol<'tcx>(tcx: TyCtxt<'tcx>, crate_type: &str) -> Option<String> {
    if crate_type != "bin" {
        return None;
    }
    let (def_id, _entry_kind) = tcx.entry_fn(())?;
    let instance = rustc_middle::ty::Instance::mono(tcx, def_id);
    Some(tcx.symbol_name(instance).name.to_string())
}

fn target_config_for_session(sess: &Session) -> TargetConfig {
    let target_features = match sess.target.arch {
        Arch::X86_64 if sess.target.os != "none" => {
            vec![sym::fxsr, sym::sse, sym::sse2, Symbol::intern("x87")]
        }
        Arch::AArch64 => match &*sess.target.os {
            "none" => vec![],
            "macos" => vec![sym::neon, sym::aes, sym::sha2, sym::sha3],
            _ => vec![sym::neon],
        },
        _ => vec![],
    };
    let unstable_target_features = target_features.clone();
    let has_reliable_f16_f128 = !(sess.target.arch == Arch::X86_64
        && sess.target.os == "windows"
        && sess.target.env == "gnu"
        && sess.target.abi != "llvm");

    TargetConfig {
        target_features,
        unstable_target_features,
        has_reliable_f16: has_reliable_f16_f128,
        has_reliable_f16_math: has_reliable_f16_f128,
        has_reliable_f128: has_reliable_f16_f128,
        has_reliable_f128_math: has_reliable_f16_f128,
    }
}

#[no_mangle]
pub fn __rustc_codegen_backend() -> Box<dyn CodegenBackend> {
    Box::new(UirCodegenBackend)
}
