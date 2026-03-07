use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write as _;

use rustc_abi::Size;
use rustc_ast::expand::allocator::{global_fn_name, ALLOCATOR_METHODS, NO_ALLOC_SHIM_IS_UNSTABLE};
use rustc_ast::Mutability;
use rustc_hir::def_id::LOCAL_CRATE;
use rustc_middle::mir::interpret::{
    alloc_range, AllocError, AllocId, GlobalAlloc, Scalar as InterpScalar,
};
use rustc_middle::mir::mono::MonoItem;
use rustc_middle::mir::{
    self, BasicBlock, Body, Operand, Place, ProjectionElem, Rvalue, StatementKind, TerminatorKind,
    UnwindAction,
};
use rustc_middle::ty::{self, EarlyBinder, Instance, Ty, TyCtxt, TypingEnv};
use rustc_session::config::OomStrategy;
use rustc_symbol_mangling::mangle_internal_symbol;
use rustc_target::callconv::{ArgAbi, PassMode};

use crate::manifest::{
    AggregateFieldV1, AggregateLayoutV1, AliasDomainV1, CallAbiDescV1, ConstValueV1, DataSymbolV1,
    DropScopeV1, FatPtrMetaV1, FunctionAbiV1, LayoutDescV1, PanicEdgeV1, ReadonlyObjectV1,
    ReadonlyRelocV1, RustReportV1, SafetyRegionV1, SemanticModuleV1, VtableDescV1,
};

pub struct MirLoweringOutput {
    pub semantic_module: SemanticModuleV1,
    pub cgu_lines: String,
    pub mono_item_lines: String,
    pub symbol_list_text: String,
}

#[derive(Clone, Debug, Default, Eq, Ord, PartialEq, PartialOrd)]
struct ConstAtom {
    kind: String,
    value: String,
    addend: i64,
}

pub fn lower_crate_to_semantic<'tcx>(
    tcx: TyCtxt<'tcx>,
    strict_mode_reason: &str,
) -> MirLoweringOutput {
    let mono_partitions = tcx.collect_and_partition_mono_items(());
    let mut unique_symbols = BTreeSet::new();
    let mut function_symbols = BTreeSet::new();
    let mut unsafe_symbols = BTreeSet::new();
    let mut cgu_lines = String::new();
    let mut mono_item_lines = String::new();
    let mut symbol_list_text = String::new();

    let mut semantic_module =
        SemanticModuleV1::rust_frontend(tcx.sess.opts.target_triple.to_string());
    semantic_module.source_tag = tcx.crate_name(LOCAL_CRATE).to_string();
    semantic_module.fallback_reason = strict_mode_reason.to_string();

    let mut lowering = MirLoweringCx::new(tcx, &mut semantic_module);

    for cgu in mono_partitions.codegen_units.iter() {
        let cgu_name = cgu.name().to_string();
        let mut cgu_symbols = BTreeSet::new();
        let mut cgu_fn_items = 0usize;
        let mut cgu_unsafe_items = 0usize;

        for mono_item in cgu.items().keys() {
            let symbol = mono_item.symbol_name(tcx).to_string();
            let is_function = matches!(mono_item, MonoItem::Fn(_));
            let is_unsafe = mono_item_is_unsafe(tcx, mono_item);
            let is_user_defined = mono_item.is_user_defined();

            cgu_symbols.insert(symbol.clone());
            unique_symbols.insert(symbol.clone());
            if is_function {
                function_symbols.insert(symbol.clone());
                cgu_fn_items += 1;
            }
            if is_unsafe {
                unsafe_symbols.insert(symbol.clone());
                cgu_unsafe_items += 1;
            }

            let _ = writeln!(
                mono_item_lines,
                "mono_item_candidate\tcgu={cgu_name}\tkind={}\tunsafe={}\tuser_defined={}\tsymbol={symbol}",
                mono_item_kind(mono_item),
                bool_manifest(is_unsafe),
                bool_manifest(is_user_defined),
            );
            let _ = writeln!(
                symbol_list_text,
                "mono_item_candidate\tcgu={cgu_name}\tkind={}\tunsafe={}\tuser_defined={}\tsymbol={symbol}",
                mono_item_kind(mono_item),
                bool_manifest(is_unsafe),
                bool_manifest(is_user_defined),
            );

            lowering.record_mono_item(mono_item, &symbol, is_unsafe);
        }

        let _ = writeln!(
            cgu_lines,
            "cgu_candidate\tname={cgu_name}\tmono_items={}\tfn_items={cgu_fn_items}\tunsafe_items={cgu_unsafe_items}",
            cgu_symbols.len(),
        );
    }

    for symbol in allocator_shim_symbol_names(tcx) {
        if unique_symbols.insert(symbol.clone()) {
            let _ = writeln!(
                symbol_list_text,
                "mono_item_candidate\tcgu=allocator.shim\tkind=fn\tunsafe=0\tuser_defined=0\tsymbol={symbol}",
            );
        }
    }

    let mut rust_report = RustReportV1::default();
    rust_report.mono_items_total = saturating_u32(unique_symbols.len());
    rust_report.mono_items_uir = saturating_u32(unique_symbols.len());
    rust_report.mono_items_llvm_fallback = 0;
    rust_report.unsafe_regions = saturating_u32(unsafe_symbols.len());
    rust_report.borrow_proof_items = saturating_u32(function_symbols.len());
    rust_report.fallback_reason = strict_mode_reason.to_string();
    semantic_module.rust_report = rust_report;

    MirLoweringOutput {
        semantic_module,
        cgu_lines,
        mono_item_lines,
        symbol_list_text,
    }
}

struct MirLoweringCx<'tcx, 'a> {
    tcx: TyCtxt<'tcx>,
    semantic_module: &'a mut SemanticModuleV1,
    string_ids: BTreeMap<String, i32>,
    type_ids: BTreeMap<String, i32>,
    object_type_names: BTreeSet<String>,
    alias_domain_names: BTreeSet<String>,
    fat_ptr_names: BTreeSet<String>,
    vtable_desc_ids: BTreeMap<String, i32>,
    readonly_object_ids: BTreeMap<AllocId, i32>,
    aggregate_layout_ids: BTreeMap<i32, i32>,
    func_abi_names: BTreeSet<String>,
    data_symbol_names: BTreeSet<String>,
}

impl<'tcx, 'a> MirLoweringCx<'tcx, 'a> {
    fn new(tcx: TyCtxt<'tcx>, semantic_module: &'a mut SemanticModuleV1) -> Self {
        Self {
            tcx,
            semantic_module,
            string_ids: BTreeMap::new(),
            type_ids: BTreeMap::new(),
            object_type_names: BTreeSet::new(),
            alias_domain_names: BTreeSet::new(),
            fat_ptr_names: BTreeSet::new(),
            vtable_desc_ids: BTreeMap::new(),
            readonly_object_ids: BTreeMap::new(),
            aggregate_layout_ids: BTreeMap::new(),
            func_abi_names: BTreeSet::new(),
            data_symbol_names: BTreeSet::new(),
        }
    }

    fn record_mono_item(&mut self, mono_item: &MonoItem<'tcx>, symbol: &str, is_unsafe: bool) {
        self.record_obj_type(symbol);
        match mono_item {
            MonoItem::Fn(instance) => self.lower_instance(*instance, symbol, is_unsafe),
            MonoItem::Static(def_id) => self.record_static(*def_id, symbol),
            MonoItem::GlobalAsm(item_id) => {
                let name = self.tcx.item_name(item_id.owner_id.to_def_id()).to_string();
                self.record_static_like(symbol, &format!("global_asm::{name}"));
            }
        }
    }

    fn lower_instance(&mut self, instance: Instance<'tcx>, symbol: &str, is_unsafe: bool) {
        let raw_body = self.tcx.instance_mir(instance.def);
        let body = instance.instantiate_mir_and_normalize_erasing_regions(
            self.tcx,
            TypingEnv::fully_monomorphized(),
            EarlyBinder::bind(raw_body.clone()),
        );

        let fn_name = symbol.to_string();
        let func_idx = self.semantic_module.func_names.len();
        let first_block = self.semantic_module.block_first_stmt.len();
        let first_local = self.semantic_module.local_names.len();
        self.semantic_module.func_names.push(fn_name.clone());
        self.semantic_module
            .func_first_block
            .push(saturating_i32(first_block));
        self.semantic_module.func_block_len.push(0);
        self.semantic_module
            .func_first_local
            .push(saturating_i32(first_local));
        self.semantic_module.func_local_len.push(0);

        let fn_ty = instance.ty(self.tcx, TypingEnv::fully_monomorphized());
        let fn_type_idx = self.intern_type(fn_ty);
        self.record_function_abi(&fn_name, instance);
        let entry_block = first_block as i32;
        let exit_block = entry_block + saturating_i32(body.basic_blocks.len()).saturating_sub(1);
        self.semantic_module.safety_regions.push(SafetyRegionV1 {
            name: fn_name.clone(),
            kind: if is_unsafe { "unsafe_fn" } else { "safe_fn" }.to_string(),
            is_opaque: false,
            entry_block,
            exit_block,
        });
        self.record_alias_domain_for_ty(
            &format!("{fn_name}::ret"),
            body.local_decls[mir::RETURN_PLACE].ty,
        );
        self.record_fat_ptr_meta_for_ty(&fn_name, fn_ty);

        for (local, local_decl) in body.local_decls.iter_enumerated() {
            let local_name = local_name(local, body.arg_count);
            let type_idx = self.intern_type(local_decl.ty);
            self.semantic_module.local_names.push(local_name.clone());
            self.semantic_module.local_type.push(type_idx);
            self.semantic_module
                .local_mutability
                .push(matches!(local_decl.mutability, Mutability::Mut));
            self.record_alias_domain_for_ty(&format!("{fn_name}::{local_name}"), local_decl.ty);
            self.record_fat_ptr_meta_for_ty(&format!("{fn_name}::{local_name}"), local_decl.ty);
        }

        for (bb, data) in body.basic_blocks.iter_enumerated() {
            let first_stmt = self.semantic_module.stmt_kind.len();
            for statement in &data.statements {
                self.lower_statement(&body, bb, statement.kind.clone());
            }
            let term_kind =
                self.lower_terminator(&body, bb, data.terminator().kind.clone(), fn_type_idx);
            self.semantic_module
                .block_first_stmt
                .push(saturating_i32(first_stmt));
            self.semantic_module.block_stmt_len.push(saturating_i32(
                self.semantic_module
                    .stmt_kind
                    .len()
                    .saturating_sub(first_stmt),
            ));
            self.semantic_module.block_term_kind.push(term_kind);
        }

        self.semantic_module.func_block_len[func_idx] = saturating_i32(
            self.semantic_module
                .block_first_stmt
                .len()
                .saturating_sub(first_block),
        );
        self.semantic_module.func_local_len[func_idx] = saturating_i32(
            self.semantic_module
                .local_names
                .len()
                .saturating_sub(first_local),
        );
    }

    fn record_static(&mut self, def_id: rustc_hir::def_id::DefId, symbol: &str) {
        let ty = self.tcx.type_of(def_id).instantiate_identity();
        self.record_static_like(symbol, &ty.to_string());
        self.intern_type(ty);
        self.record_alias_domain_for_ty(symbol, ty);
        self.record_fat_ptr_meta_for_ty(symbol, ty);
        self.record_static_data_symbol(def_id, symbol, ty);
    }

    fn record_static_like(&mut self, symbol: &str, type_name: &str) {
        self.record_obj_type(symbol);
        self.semantic_module.func_names.push(symbol.to_string());
        self.semantic_module
            .func_first_block
            .push(saturating_i32(self.semantic_module.block_first_stmt.len()));
        self.semantic_module.func_block_len.push(0);
        self.semantic_module
            .func_first_local
            .push(saturating_i32(self.semantic_module.local_names.len()));
        self.semantic_module.func_local_len.push(0);
        self.intern_string(type_name);
    }

    fn lower_statement(
        &mut self,
        body: &Body<'tcx>,
        _bb: BasicBlock,
        statement: StatementKind<'tcx>,
    ) {
        match statement {
            StatementKind::Assign(assign) => {
                let (place, rvalue) = *assign;
                let lhs = self.lower_place(body, &place);
                let rhs = self.lower_rvalue(body, &rvalue);
                self.push_stmt("assign", lhs, rhs, -1);
            }
            StatementKind::FakeRead(cause_place) => {
                let (cause, place) = *cause_place;
                let place_expr = self.lower_place(body, &place);
                let cause_idx = self.intern_string(&format!("{cause:?}"));
                self.push_stmt("fake_read", place_expr, cause_idx, -1);
            }
            StatementKind::SetDiscriminant {
                place,
                variant_index,
            } => {
                let place_expr = self.lower_place(body, &place);
                self.push_stmt(
                    "set_discriminant",
                    place_expr,
                    saturating_i32(variant_index.index()),
                    -1,
                );
            }
            StatementKind::StorageLive(local) => {
                self.push_stmt("storage_live", saturating_i32(local.index()), -1, -1);
            }
            StatementKind::StorageDead(local) => {
                self.push_stmt("storage_dead", saturating_i32(local.index()), -1, -1);
            }
            StatementKind::Retag(kind, place) => {
                let place_expr = self.lower_place(body, &place);
                let kind_idx = self.intern_string(&format!("{kind:?}"));
                self.push_stmt("retag", place_expr, kind_idx, -1);
            }
            StatementKind::PlaceMention(place) => {
                let place_expr = self.lower_place(body, &place);
                self.push_stmt("place_mention", place_expr, -1, -1);
            }
            StatementKind::AscribeUserType(place_projection, variance) => {
                let (place, user_ty) = *place_projection;
                let place_expr = self.lower_place(body, &place);
                let variance_idx = self.intern_string(&format!("{variance:?}"));
                let user_ty_idx = self.intern_string(&format!("{user_ty:?}"));
                self.push_stmt("ascribe_user_type", place_expr, variance_idx, user_ty_idx);
            }
            StatementKind::Coverage(coverage) => {
                let coverage_idx = self.intern_string(&format!("{coverage:?}"));
                self.push_stmt("coverage", coverage_idx, -1, -1);
            }
            StatementKind::Intrinsic(intrinsic) => {
                self.lower_intrinsic(body, *intrinsic);
            }
            StatementKind::ConstEvalCounter => {
                self.push_stmt("const_eval_counter", -1, -1, -1);
            }
            StatementKind::Nop => {
                self.push_stmt("nop", -1, -1, -1);
            }
            StatementKind::BackwardIncompatibleDropHint { place, reason } => {
                let place_expr = self.lower_place(body, &place);
                let reason_idx = self.intern_string(&format!("{reason:?}"));
                self.push_stmt(
                    "backward_incompatible_drop_hint",
                    place_expr,
                    reason_idx,
                    -1,
                );
            }
        }
    }

    fn lower_intrinsic(&mut self, body: &Body<'tcx>, intrinsic: mir::NonDivergingIntrinsic<'tcx>) {
        match intrinsic {
            mir::NonDivergingIntrinsic::Assume(operand) => {
                let expr = self.lower_operand(body, &operand);
                self.push_stmt("intrinsic.assume", expr, -1, -1);
            }
            mir::NonDivergingIntrinsic::CopyNonOverlapping(copy) => {
                let src = self.lower_operand(body, &copy.src);
                let dst = self.lower_operand(body, &copy.dst);
                let count = self.lower_operand(body, &copy.count);
                self.push_stmt("intrinsic.copy_nonoverlapping", src, dst, count);
            }
        }
    }

    fn lower_terminator(
        &mut self,
        body: &Body<'tcx>,
        block: BasicBlock,
        terminator: TerminatorKind<'tcx>,
        fn_type_idx: i32,
    ) -> String {
        match terminator {
            TerminatorKind::Goto { target } => {
                self.push_stmt("term.goto", saturating_i32(target.index()), -1, -1);
                "goto".to_string()
            }
            TerminatorKind::SwitchInt { discr, targets } => {
                let discr_expr = self.lower_operand(body, &discr);
                let targets_idx = self.intern_string(&switch_targets_text(&targets));
                self.push_stmt(
                    "term.switch_int",
                    discr_expr,
                    saturating_i32(targets.otherwise().index()),
                    targets_idx,
                );
                "switch_int".to_string()
            }
            TerminatorKind::UnwindResume => {
                self.push_stmt("term.unwind_resume", -1, -1, -1);
                "unwind_resume".to_string()
            }
            TerminatorKind::UnwindTerminate(reason) => {
                let reason_idx = self.intern_string(&format!("{reason:?}"));
                self.push_stmt("term.unwind_terminate", reason_idx, -1, -1);
                "unwind_terminate".to_string()
            }
            TerminatorKind::Return => {
                let ret_expr = self.lower_place(body, &Place::return_place());
                self.push_stmt("term.return", ret_expr, fn_type_idx, -1);
                "return".to_string()
            }
            TerminatorKind::Unreachable => {
                self.push_stmt("term.unreachable", -1, -1, -1);
                "unreachable".to_string()
            }
            TerminatorKind::Drop {
                place,
                target,
                unwind,
                replace,
                drop,
                async_fut,
            } => {
                let place_expr = self.lower_place(body, &place);
                let info_idx = self.intern_string(&format!(
                    "replace={replace},drop={:?},async_fut={:?}",
                    drop.map(|bb| bb.index()),
                    async_fut.map(|local| local.index())
                ));
                self.push_stmt(
                    "term.drop",
                    place_expr,
                    saturating_i32(target.index()),
                    info_idx,
                );
                self.record_unwind_edge(block, unwind, "drop");
                self.semantic_module.drop_scopes.push(DropScopeV1 {
                    name: format!("bb{}_drop", block.index()),
                    begin_block: saturating_i32(block.index()),
                    end_block: saturating_i32(target.index()),
                    cleanup_block: cleanup_block_or_neg1(unwind),
                });
                "drop".to_string()
            }
            TerminatorKind::Call {
                func,
                args,
                destination,
                target,
                unwind,
                call_source,
                ..
            } => {
                let func_expr = self.lower_operand(body, &func);
                if let Some(symbol) = self.resolve_direct_call_symbol(&func) {
                    let symbol_idx = self.intern_string(&symbol);
                    self.push_stmt("term.call_symbol", symbol_idx, -1, -1);
                }
                let arg_exprs = args
                    .iter()
                    .map(|arg| self.lower_operand(body, &arg.node))
                    .collect::<Vec<_>>();
                let args_expr = self.record_operand_list(arg_exprs);
                let destination_expr = self.lower_place(body, &destination);
                let stmt_idx = self.push_stmt("term.call", func_expr, args_expr, destination_expr);
                self.record_call_abi(body, &func, unwind, call_source, stmt_idx);
                self.record_atomic_or_volatile(body, &func, func_expr);
                self.record_unwind_edge(block, unwind, "call");
                if let Some(target) = target {
                    self.push_stmt("term.call_target", saturating_i32(target.index()), -1, -1);
                }
                "call".to_string()
            }
            TerminatorKind::TailCall { func, args, .. } => {
                let func_expr = self.lower_operand(body, &func);
                if let Some(symbol) = self.resolve_direct_call_symbol(&func) {
                    let symbol_idx = self.intern_string(&symbol);
                    self.push_stmt("term.tail_call_symbol", symbol_idx, -1, -1);
                }
                let arg_exprs = args
                    .iter()
                    .map(|arg| self.lower_operand(body, &arg.node))
                    .collect::<Vec<_>>();
                let args_expr = self.record_operand_list(arg_exprs);
                let stmt_idx = self.push_stmt("term.tail_call", func_expr, args_expr, -1);
                self.record_call_abi(
                    body,
                    &func,
                    UnwindAction::Continue,
                    mir::CallSource::Misc,
                    stmt_idx,
                );
                self.record_atomic_or_volatile(body, &func, func_expr);
                "tail_call".to_string()
            }
            TerminatorKind::Assert {
                cond,
                expected,
                msg,
                target,
                unwind,
            } => {
                let cond_expr = self.lower_operand(body, &cond);
                let msg_idx = self.intern_string(&format!("{msg:?},expected={expected}"));
                self.push_stmt(
                    "term.assert",
                    cond_expr,
                    saturating_i32(target.index()),
                    msg_idx,
                );
                self.record_unwind_edge(block, unwind, "assert");
                "assert".to_string()
            }
            TerminatorKind::Yield {
                value,
                resume,
                resume_arg,
                drop,
            } => {
                let value_expr = self.lower_operand(body, &value);
                let resume_arg_expr = self.lower_place(body, &resume_arg);
                let drop_idx = drop
                    .map(|target| saturating_i32(target.index()))
                    .unwrap_or(-1);
                self.push_stmt(
                    "term.yield",
                    value_expr,
                    saturating_i32(resume.index()),
                    if drop_idx >= 0 {
                        drop_idx
                    } else {
                        resume_arg_expr
                    },
                );
                "yield".to_string()
            }
            TerminatorKind::CoroutineDrop => {
                self.push_stmt("term.coroutine_drop", -1, -1, -1);
                "coroutine_drop".to_string()
            }
            TerminatorKind::FalseEdge {
                real_target,
                imaginary_target,
            } => {
                let info_idx =
                    self.intern_string(&format!("imaginary={}", imaginary_target.index()));
                self.push_stmt(
                    "term.false_edge",
                    saturating_i32(real_target.index()),
                    info_idx,
                    -1,
                );
                "false_edge".to_string()
            }
            TerminatorKind::FalseUnwind {
                real_target,
                unwind,
            } => {
                self.push_stmt(
                    "term.false_unwind",
                    saturating_i32(real_target.index()),
                    cleanup_block_or_neg1(unwind),
                    -1,
                );
                self.record_unwind_edge(block, unwind, "false_unwind");
                "false_unwind".to_string()
            }
            TerminatorKind::InlineAsm {
                template,
                operands,
                options,
                line_spans,
                targets,
                unwind,
                asm_macro,
            } => {
                let asm_idx = self.intern_string(&format!(
                    "template={template:?},operands={operands:?},options={options:?},line_spans={line_spans:?},asm_macro={asm_macro:?}"
                ));
                let targets_idx = self.intern_string(
                    &targets
                        .iter()
                        .map(|target| target.index().to_string())
                        .collect::<Vec<_>>()
                        .join(","),
                );
                self.push_stmt(
                    "term.inline_asm",
                    asm_idx,
                    targets_idx,
                    cleanup_block_or_neg1(unwind),
                );
                self.record_unwind_edge(block, unwind, "inline_asm");
                "inline_asm".to_string()
            }
        }
    }

    fn resolve_direct_call_symbol(&self, func: &Operand<'tcx>) -> Option<String> {
        let (def_id, args) = func.const_fn_def()?;
        let instance =
            ty::Instance::try_resolve(self.tcx, TypingEnv::fully_monomorphized(), def_id, args)
                .ok()
                .flatten()?;
        Some(self.tcx.symbol_name(instance).name.to_string())
    }

    fn record_function_abi(&mut self, fn_name: &str, instance: Instance<'tcx>) {
        if !self.func_abi_names.insert(fn_name.to_string()) {
            return;
        }
        let typing_env = TypingEnv::fully_monomorphized();
        let Ok(fn_abi) = self
            .tcx
            .fn_abi_of_instance(typing_env.as_query_input((instance, ty::List::empty())))
        else {
            return;
        };
        self.semantic_module.func_abi.push(FunctionAbiV1 {
            func_name: fn_name.to_string(),
            ret_mode: ret_mode_from_arg_abi(&fn_abi.ret).to_string(),
            arg_modes: fn_abi
                .args
                .iter()
                .map(arg_mode_from_arg_abi)
                .map(str::to_string)
                .collect(),
        });
    }

    fn record_call_abi(
        &mut self,
        body: &Body<'tcx>,
        func: &Operand<'tcx>,
        unwind: UnwindAction,
        call_source: mir::CallSource,
        stmt_idx: i32,
    ) {
        let callee_ty = func.ty(&body.local_decls, self.tcx);
        let typing_env = TypingEnv::fully_monomorphized();
        let mut ret_mode = "void".to_string();
        let mut arg_modes = Vec::new();
        let (callee_name, call_kind, vtable_slot) =
            if let Some((def_id, args)) = func.const_fn_def() {
                let kind = if self.tcx.is_foreign_item(def_id) {
                    "extern"
                } else {
                    "direct"
                };
                if let Ok(Some(instance)) =
                    ty::Instance::try_resolve(self.tcx, typing_env, def_id, args)
                {
                    if let Ok(fn_abi) = self.tcx.fn_abi_of_instance(
                        typing_env.as_query_input((instance, ty::List::empty())),
                    ) {
                        ret_mode = ret_mode_from_arg_abi(&fn_abi.ret).to_string();
                        arg_modes = fn_abi
                            .args
                            .iter()
                            .map(arg_mode_from_arg_abi)
                            .map(str::to_string)
                            .collect();
                    }
                }
                (self.tcx.def_path_str(def_id), kind.to_string(), -1)
            } else if let ty::FnPtr(sig_tys, header) = callee_ty.kind() {
                let sig = sig_tys.with(*header);
                if let Ok(fn_abi) = self
                    .tcx
                    .fn_abi_of_fn_ptr(typing_env.as_query_input((sig, ty::List::empty())))
                {
                    ret_mode = ret_mode_from_arg_abi(&fn_abi.ret).to_string();
                    arg_modes = fn_abi
                        .args
                        .iter()
                        .map(arg_mode_from_arg_abi)
                        .map(str::to_string)
                        .collect();
                }
                ("fn_ptr".to_string(), "indirect".to_string(), -1)
            } else {
                (format!("{callee_ty}"), format!("misc::{call_source:?}"), -1)
            };
        let abi = format!("{:?}", callee_ty.fn_sig(self.tcx).skip_binder().abi);
        self.semantic_module.call_abi.push(CallAbiDescV1 {
            stmt_idx,
            callee: callee_name,
            abi,
            call_kind,
            vtable_slot,
            can_unwind: matches!(unwind, UnwindAction::Cleanup(_) | UnwindAction::Continue),
            ret_mode,
            arg_modes,
        });
    }

    fn record_atomic_or_volatile(
        &mut self,
        body: &Body<'tcx>,
        func: &Operand<'tcx>,
        func_expr: i32,
    ) {
        let name = if let Some((def_id, _)) = func.const_fn_def() {
            self.tcx.def_path_str(def_id)
        } else {
            func.ty(&body.local_decls, self.tcx).to_string()
        };
        if name.contains("atomic") {
            self.semantic_module.atomic_exprs.push(func_expr);
        }
        if name.contains("volatile") {
            self.semantic_module.volatile_exprs.push(func_expr);
        }
    }

    fn record_unwind_edge(&mut self, block: BasicBlock, unwind: UnwindAction, reason: &str) {
        if let UnwindAction::Cleanup(target) = unwind {
            self.semantic_module.panic_edges.push(PanicEdgeV1 {
                from_block: saturating_i32(block.index()),
                to_block: saturating_i32(target.index()),
                reason: reason.to_string(),
                unwind: true,
            });
        }
    }

    fn lower_place(&mut self, body: &Body<'tcx>, place: &Place<'tcx>) -> i32 {
        let local_ty = body.local_decls[place.local].ty;
        let local_type_idx = self.intern_type(local_ty);
        let mut current = self.push_expr(
            "place.local",
            local_type_idx,
            saturating_i32(place.local.index()),
            -1,
            -1,
        );
        for (projection_idx, projection) in place.projection.iter().enumerate() {
            let projection_kind = projection_kind_name(&projection);
            let projected_ty = mir::PlaceRef {
                local: place.local,
                projection: &place.projection[..=projection_idx],
            }
            .ty(&body.local_decls, self.tcx)
            .ty;
            let projection_type_idx = self.intern_type(projected_ty);
            let (a, b, c) = match projection {
                ProjectionElem::Deref => (current, -1, -1),
                ProjectionElem::Field(field, field_ty) => {
                    let field_type_idx = self.intern_type(field_ty);
                    (current, saturating_i32(field.index()), field_type_idx)
                }
                ProjectionElem::Index(local) => (current, saturating_i32(local.index()), -1),
                ProjectionElem::ConstantIndex {
                    offset,
                    min_length,
                    from_end,
                } => (
                    current,
                    saturating_i32_u64(offset),
                    if from_end {
                        -saturating_i32_u64(min_length)
                    } else {
                        saturating_i32_u64(min_length)
                    },
                ),
                ProjectionElem::Subslice { from, to, from_end } => (
                    current,
                    saturating_i32_u64(from),
                    if from_end {
                        -saturating_i32_u64(to)
                    } else {
                        saturating_i32_u64(to)
                    },
                ),
                ProjectionElem::Downcast(symbol, variant) => {
                    let symbol_idx =
                        self.intern_string(&symbol.map_or_else(String::new, |sym| sym.to_string()));
                    (current, saturating_i32(variant.index()), symbol_idx)
                }
                ProjectionElem::OpaqueCast(ty) | ProjectionElem::UnwrapUnsafeBinder(ty) => {
                    (current, self.intern_type(ty), -1)
                }
            };
            current = self.push_expr(projection_kind, projection_type_idx, a, b, c);
        }
        current
    }

    fn lower_operand(&mut self, body: &Body<'tcx>, operand: &Operand<'tcx>) -> i32 {
        match operand {
            Operand::Copy(place) => {
                let place_expr = self.lower_place(body, place);
                let type_idx = self.intern_type(operand.ty(&body.local_decls, self.tcx));
                self.push_expr("operand.copy", type_idx, place_expr, -1, -1)
            }
            Operand::Move(place) => {
                let place_expr = self.lower_place(body, place);
                let type_idx = self.intern_type(operand.ty(&body.local_decls, self.tcx));
                self.push_expr("operand.move", type_idx, place_expr, -1, -1)
            }
            Operand::Constant(constant) => {
                let value_idx =
                    self.record_const_value(constant.const_, constant.const_.ty(), constant.span);
                let type_idx = self.intern_type(constant.const_.ty());
                self.push_expr("operand.const", type_idx, value_idx, -1, -1)
            }
        }
    }

    fn record_operand_list(&mut self, expr_ids: Vec<i32>) -> i32 {
        let summary_idx = self.intern_string(&join_i32_list(&expr_ids));
        self.push_expr(
            "operand.list",
            -1,
            summary_idx,
            saturating_i32(expr_ids.len()),
            -1,
        )
    }

    fn lower_rvalue(&mut self, body: &Body<'tcx>, rvalue: &Rvalue<'tcx>) -> i32 {
        let type_idx = self.intern_type(rvalue.ty(&body.local_decls, self.tcx));
        match rvalue {
            Rvalue::Use(operand) => {
                let operand_expr = self.lower_operand(body, operand);
                self.push_expr("rvalue.use", type_idx, operand_expr, -1, -1)
            }
            Rvalue::Repeat(operand, count) => {
                let operand_expr = self.lower_operand(body, operand);
                let count_idx = self.intern_string(&format!("{count:?}"));
                self.push_expr("rvalue.repeat", type_idx, operand_expr, count_idx, -1)
            }
            Rvalue::Ref(_, borrow_kind, place) => {
                let place_expr = self.lower_place(body, place);
                let borrow_idx = self.intern_string(&format!("{borrow_kind:?}"));
                self.push_expr("rvalue.ref", type_idx, place_expr, borrow_idx, -1)
            }
            Rvalue::ThreadLocalRef(def_id) => {
                let name_idx = self.intern_string(&self.tcx.def_path_str(*def_id));
                self.push_expr("rvalue.thread_local_ref", type_idx, name_idx, -1, -1)
            }
            Rvalue::RawPtr(kind, place) => {
                let place_expr = self.lower_place(body, place);
                let kind_idx = self.intern_string(&format!("{kind:?}"));
                self.push_expr("rvalue.raw_ptr", type_idx, place_expr, kind_idx, -1)
            }
            Rvalue::Cast(kind, operand, cast_ty) => {
                let operand_expr = self.lower_operand(body, operand);
                let kind_idx = self.intern_string(&format!("{kind:?}"));
                let cast_type_idx = self.intern_type(*cast_ty);
                let cast_meta = self
                    .record_vtable_desc_for_unsize_cast(
                        operand.ty(&body.local_decls, self.tcx),
                        *cast_ty,
                    )
                    .unwrap_or(type_idx);
                self.push_expr(
                    "rvalue.cast",
                    cast_type_idx,
                    operand_expr,
                    kind_idx,
                    cast_meta,
                )
            }
            Rvalue::BinaryOp(op, operands) => {
                let lhs = self.lower_operand(body, &operands.0);
                let rhs = self.lower_operand(body, &operands.1);
                let op_idx = self.intern_string(&format!("{op:?}"));
                self.push_expr("rvalue.binary", type_idx, lhs, rhs, op_idx)
            }
            Rvalue::NullaryOp(op, _) => {
                let op_idx = self.intern_string(&format!("{op:?}"));
                self.push_expr("rvalue.nullary", type_idx, op_idx, -1, -1)
            }
            Rvalue::UnaryOp(op, operand) => {
                let operand_expr = self.lower_operand(body, operand);
                let op_idx = self.intern_string(&format!("{op:?}"));
                self.push_expr("rvalue.unary", type_idx, operand_expr, op_idx, -1)
            }
            Rvalue::Discriminant(place) => {
                let place_expr = self.lower_place(body, place);
                self.push_expr("rvalue.discriminant", type_idx, place_expr, -1, -1)
            }
            Rvalue::Aggregate(kind, operands) => {
                let descriptor_idx = self.intern_string(&format!("{kind:?}"));
                let field_exprs = operands
                    .iter()
                    .map(|operand| self.lower_operand(body, operand))
                    .collect::<Vec<_>>();
                let fields_idx = self.intern_string(&join_i32_list(&field_exprs));
                self.push_expr(
                    "rvalue.aggregate",
                    type_idx,
                    descriptor_idx,
                    fields_idx,
                    saturating_i32(field_exprs.len()),
                )
            }
            Rvalue::ShallowInitBox(operand, boxed_ty) => {
                let operand_expr = self.lower_operand(body, operand);
                let boxed_type_idx = self.intern_type(*boxed_ty);
                self.push_expr(
                    "rvalue.shallow_init_box",
                    boxed_type_idx,
                    operand_expr,
                    -1,
                    -1,
                )
            }
            Rvalue::CopyForDeref(place) => {
                let place_expr = self.lower_place(body, place);
                self.push_expr("rvalue.copy_for_deref", type_idx, place_expr, -1, -1)
            }
            Rvalue::WrapUnsafeBinder(operand, binder_ty) => {
                let operand_expr = self.lower_operand(body, operand);
                let binder_idx = self.intern_type(*binder_ty);
                self.push_expr(
                    "rvalue.wrap_unsafe_binder",
                    binder_idx,
                    operand_expr,
                    -1,
                    -1,
                )
            }
        }
    }

    fn record_obj_type(&mut self, name: &str) {
        if self.object_type_names.insert(name.to_string()) {
            self.semantic_module.obj_type_names.push(name.to_string());
        }
    }

    fn record_alias_domain_for_ty(&mut self, name: &str, ty: Ty<'tcx>) {
        let key = format!("{name}:{ty}");
        if !self.alias_domain_names.insert(key) {
            return;
        }
        let (is_mutable, is_exclusive, fallback_reason) = match ty.kind() {
            ty::Ref(_, _, Mutability::Mut) => (true, true, String::new()),
            ty::Ref(_, _, Mutability::Not) => (false, false, String::new()),
            ty::RawPtr(_, _) => (true, false, "unsafe_or_opaque".to_string()),
            ty::Adt(adt, _) if adt.is_box() => (true, true, String::new()),
            _ => (false, false, String::new()),
        };
        if !is_mutable && !is_exclusive && fallback_reason.is_empty() {
            return;
        }
        self.semantic_module.alias_domains.push(AliasDomainV1 {
            name: name.to_string(),
            is_mutable,
            is_exclusive,
            proof_id: format!("borrowck::{name}"),
            fallback_reason,
        });
    }

    fn record_fat_ptr_meta_for_ty(&mut self, name: &str, ty: Ty<'tcx>) {
        let Some((payload_type, metadata_type, meta_kind)) = fat_ptr_meta_for_ty(ty) else {
            return;
        };
        if !self
            .fat_ptr_names
            .insert(format!("{name}:{payload_type}:{metadata_type}:{meta_kind}"))
        {
            return;
        }
        self.semantic_module.fat_ptr_meta.push(FatPtrMetaV1 {
            name: name.to_string(),
            meta_kind: meta_kind.to_string(),
            payload_type,
            metadata_type,
        });
    }

    fn record_vtable_desc_for_unsize_cast(
        &mut self,
        source_ty: Ty<'tcx>,
        cast_ty: Ty<'tcx>,
    ) -> Option<i32> {
        let source_inner = match source_ty.kind() {
            ty::Ref(_, inner, _) | ty::RawPtr(inner, _) => *inner,
            _ => return None,
        };
        let cast_inner = match cast_ty.kind() {
            ty::Ref(_, inner, _) | ty::RawPtr(inner, _) => *inner,
            _ => return None,
        };
        let ty::Dynamic(predicates, _) = cast_inner.kind() else {
            return None;
        };
        let principal = predicates.principal()?.skip_binder();
        let trait_ref = principal.with_self_ty(self.tcx, source_inner);
        let trait_ref_key = format!("{source_inner}::{principal}");
        if let Some(&id) = self.vtable_desc_ids.get(&trait_ref_key) {
            return Some(id);
        }

        let layout = match self
            .tcx
            .layout_of(TypingEnv::fully_monomorphized().as_query_input(source_inner))
        {
            Ok(layout) => layout,
            Err(_) => return None,
        };
        let drop_symbol = if source_inner.needs_drop(self.tcx, TypingEnv::fully_monomorphized()) {
            let instance = Instance::resolve_drop_in_place(self.tcx, source_inner);
            Some(self.tcx.symbol_name(instance).name.to_string())
        } else {
            None
        };
        if let Some(symbol) = &drop_symbol {
            self.record_obj_type(symbol);
        }

        let label = format!("__cheng_vtable_{}", self.semantic_module.vtable_descs.len());
        let mut entries = Vec::new();
        for entry in self.tcx.vtable_entries(trait_ref) {
            match entry {
                ty::VtblEntry::MetadataDropInPlace => {
                    entries.push(
                        drop_symbol
                            .clone()
                            .map_or_else(|| "null".to_string(), |symbol| format!("fn:{symbol}")),
                    );
                }
                ty::VtblEntry::MetadataSize => {
                    entries.push(format!("int:{}", layout.size.bytes()));
                }
                ty::VtblEntry::MetadataAlign => {
                    entries.push(format!("int:{}", layout.align.bytes()));
                }
                ty::VtblEntry::Method(instance) => {
                    let symbol = self.tcx.symbol_name(*instance).name.to_string();
                    self.record_obj_type(&symbol);
                    entries.push(format!("fn:{symbol}"));
                }
                ty::VtblEntry::TraitVPtr(super_trait_ref) => {
                    let super_existential =
                        ty::ExistentialTraitRef::erase_self_ty(self.tcx, *super_trait_ref);
                    let super_key = format!("{source_inner}::{super_existential}");
                    let super_id = if let Some(&id) = self.vtable_desc_ids.get(&super_key) {
                        id
                    } else {
                        let super_label =
                            self.record_vtable_desc_for_trait(source_inner, super_existential)?;
                        super_label
                    };
                    let super_label = self
                        .semantic_module
                        .vtable_descs
                        .get(usize::try_from(super_id).ok()?)?
                        .label
                        .clone();
                    entries.push(format!("vtable:{super_label}"));
                }
                ty::VtblEntry::Vacant => entries.push("null".to_string()),
            }
        }

        let id = saturating_i32(self.semantic_module.vtable_descs.len());
        self.semantic_module.vtable_descs.push(VtableDescV1 {
            label: label.clone(),
            entries,
        });
        self.vtable_desc_ids.insert(trait_ref_key, id);
        Some(id)
    }

    fn record_vtable_desc_for_trait(
        &mut self,
        source_inner: Ty<'tcx>,
        existential_trait: ty::ExistentialTraitRef<'tcx>,
    ) -> Option<i32> {
        let key = format!("{source_inner}::{existential_trait}");
        if let Some(&id) = self.vtable_desc_ids.get(&key) {
            return Some(id);
        }
        let trait_ref = existential_trait.with_self_ty(self.tcx, source_inner);
        let layout = self
            .tcx
            .layout_of(TypingEnv::fully_monomorphized().as_query_input(source_inner))
            .ok()?;
        let drop_symbol = if source_inner.needs_drop(self.tcx, TypingEnv::fully_monomorphized()) {
            let instance = Instance::resolve_drop_in_place(self.tcx, source_inner);
            let symbol = self.tcx.symbol_name(instance).name.to_string();
            self.record_obj_type(&symbol);
            Some(symbol)
        } else {
            None
        };
        let label = format!("__cheng_vtable_{}", self.semantic_module.vtable_descs.len());
        let mut entries = Vec::new();
        for entry in self.tcx.vtable_entries(trait_ref) {
            match entry {
                ty::VtblEntry::MetadataDropInPlace => {
                    entries.push(
                        drop_symbol
                            .clone()
                            .map_or_else(|| "null".to_string(), |symbol| format!("fn:{symbol}")),
                    );
                }
                ty::VtblEntry::MetadataSize => entries.push(format!("int:{}", layout.size.bytes())),
                ty::VtblEntry::MetadataAlign => {
                    entries.push(format!("int:{}", layout.align.bytes()))
                }
                ty::VtblEntry::Method(instance) => {
                    let symbol = self.tcx.symbol_name(*instance).name.to_string();
                    self.record_obj_type(&symbol);
                    entries.push(format!("fn:{symbol}"));
                }
                ty::VtblEntry::TraitVPtr(super_trait_ref) => {
                    let super_existential =
                        ty::ExistentialTraitRef::erase_self_ty(self.tcx, *super_trait_ref);
                    let super_id =
                        self.record_vtable_desc_for_trait(source_inner, super_existential)?;
                    let super_label = self
                        .semantic_module
                        .vtable_descs
                        .get(usize::try_from(super_id).ok()?)?
                        .label
                        .clone();
                    entries.push(format!("vtable:{super_label}"));
                }
                ty::VtblEntry::Vacant => entries.push("null".to_string()),
            }
        }
        let id = saturating_i32(self.semantic_module.vtable_descs.len());
        self.semantic_module.vtable_descs.push(VtableDescV1 {
            label: label.clone(),
            entries,
        });
        self.vtable_desc_ids.insert(key, id);
        Some(id)
    }

    fn record_const_value(
        &mut self,
        constant: mir::Const<'tcx>,
        ty: Ty<'tcx>,
        span: rustc_span::Span,
    ) -> i32 {
        let type_idx = self.intern_type(ty);
        let value = constant
            .eval(self.tcx, TypingEnv::fully_monomorphized(), span)
            .unwrap_or_else(|err| panic!("failed to eval constant {constant:?}: {err:?}"));
        let const_value = match value {
            mir::ConstValue::ZeroSized => ConstValueV1 {
                type_idx,
                kind: "zero_sized".to_string(),
                atom_a_kind: "none".to_string(),
                atom_a: String::new(),
                atom_a_addend: 0,
                atom_b_kind: "none".to_string(),
                atom_b: String::new(),
                atom_b_addend: 0,
            },
            mir::ConstValue::Scalar(scalar) => {
                let atom = self.const_atom_from_scalar(scalar).unwrap_or_else(|err| {
                    panic!("failed to lower scalar const {constant:?}: {err}")
                });
                ConstValueV1 {
                    type_idx,
                    kind: "scalar".to_string(),
                    atom_a_kind: atom.kind,
                    atom_a: atom.value,
                    atom_a_addend: atom.addend,
                    atom_b_kind: "none".to_string(),
                    atom_b: String::new(),
                    atom_b_addend: 0,
                }
            }
            mir::ConstValue::Slice { alloc_id, meta } => {
                let atom = self
                    .const_atom_from_readonly_alloc(alloc_id, 0)
                    .unwrap_or_else(|err| {
                        panic!("failed to lower slice const {constant:?}: {err}")
                    });
                ConstValueV1 {
                    type_idx,
                    kind: "scalar_pair".to_string(),
                    atom_a_kind: atom.kind,
                    atom_a: atom.value,
                    atom_a_addend: atom.addend,
                    atom_b_kind: "imm".to_string(),
                    atom_b: meta.to_string(),
                    atom_b_addend: 0,
                }
            }
            mir::ConstValue::Indirect { alloc_id, offset } => self
                .const_value_from_indirect(alloc_id, offset, ty, type_idx)
                .unwrap_or_else(|err| panic!("failed to lower indirect const {constant:?}: {err}")),
        };
        let id = saturating_i32(self.semantic_module.const_values.len());
        self.semantic_module.const_values.push(const_value);
        id
    }

    fn const_value_from_indirect(
        &mut self,
        alloc_id: AllocId,
        offset: Size,
        ty: Ty<'tcx>,
        type_idx: i32,
    ) -> Result<ConstValueV1, String> {
        let layout = self
            .tcx
            .layout_of(TypingEnv::fully_monomorphized().as_query_input(ty))
            .map_err(|err| format!("layout error for indirect const {ty}: {err:?}"))?;
        let alloc = self.load_const_alloc(alloc_id)?;
        match layout.backend_repr {
            rustc_abi::BackendRepr::Scalar(scalar) => {
                let scalar = alloc
                    .read_scalar(&self.tcx, alloc_range(offset, scalar.size(&self.tcx)), true)
                    .map_err(|err| format!("read scalar const failed: {err:?}"))?;
                let atom = self.const_atom_from_scalar(scalar)?;
                Ok(ConstValueV1 {
                    type_idx,
                    kind: "scalar".to_string(),
                    atom_a_kind: atom.kind,
                    atom_a: atom.value,
                    atom_a_addend: atom.addend,
                    atom_b_kind: "none".to_string(),
                    atom_b: String::new(),
                    atom_b_addend: 0,
                })
            }
            rustc_abi::BackendRepr::ScalarPair(first, second) => {
                let field_count = layout.fields.count();
                let first_offset = offset
                    + if field_count > 0 {
                        layout.fields.offset(0)
                    } else {
                        Size::ZERO
                    };
                let second_offset = offset
                    + if field_count > 1 {
                        layout.fields.offset(1)
                    } else {
                        first.size(&self.tcx).align_to(second.align(&self.tcx).abi)
                    };
                let first_scalar = alloc
                    .read_scalar(
                        &self.tcx,
                        alloc_range(first_offset, first.size(&self.tcx)),
                        true,
                    )
                    .map_err(|err| format!("read first scalar-pair const failed: {err:?}"))?;
                let second_scalar = match alloc.read_scalar(
                    &self.tcx,
                    alloc_range(second_offset, second.size(&self.tcx)),
                    true,
                ) {
                    Ok(value) => Some(value),
                    Err(err) if alloc_err_is_invalid_uninit(&err) => None,
                    Err(err) => {
                        return Err(format!("read second scalar-pair const failed: {err:?}"));
                    }
                };
                let first_atom = self.const_atom_from_scalar(first_scalar)?;
                let second_atom = if let Some(second_scalar) = second_scalar {
                    self.const_atom_from_scalar(second_scalar)?
                } else {
                    ConstAtom {
                        kind: "none".to_string(),
                        value: String::new(),
                        addend: 0,
                    }
                };
                Ok(ConstValueV1 {
                    type_idx,
                    kind: "scalar_pair".to_string(),
                    atom_a_kind: first_atom.kind,
                    atom_a: first_atom.value,
                    atom_a_addend: first_atom.addend,
                    atom_b_kind: second_atom.kind,
                    atom_b: second_atom.value,
                    atom_b_addend: second_atom.addend,
                })
            }
            rustc_abi::BackendRepr::Memory { .. } => Err(format!(
                "memory const lowering is unsupported for type {ty}"
            )),
            rustc_abi::BackendRepr::SimdVector { .. } => {
                Err(format!("simd const lowering is unsupported for type {ty}"))
            }
        }
    }

    fn const_atom_from_scalar(&mut self, scalar: InterpScalar) -> Result<ConstAtom, String> {
        match scalar {
            InterpScalar::Int(int) => Ok(ConstAtom {
                kind: "imm".to_string(),
                value: int.to_bits(int.size()).to_string(),
                addend: 0,
            }),
            InterpScalar::Ptr(pointer, _) => {
                let (prov, offset) = pointer.prov_and_relative_offset();
                self.const_atom_from_global_alloc(prov.alloc_id(), offset.bytes() as i64)
            }
        }
    }

    fn const_atom_from_readonly_alloc(
        &mut self,
        alloc_id: AllocId,
        addend: i64,
    ) -> Result<ConstAtom, String> {
        let object_idx = self.intern_readonly_alloc(alloc_id)?;
        let label = self
            .semantic_module
            .readonly_objects
            .get(
                usize::try_from(object_idx)
                    .map_err(|_| format!("invalid readonly object id {object_idx}"))?,
            )
            .map(|value| value.label.clone())
            .ok_or_else(|| format!("readonly object out of range {object_idx}"))?;
        Ok(ConstAtom {
            kind: "readonly".to_string(),
            value: label,
            addend,
        })
    }

    fn const_atom_from_global_alloc(
        &mut self,
        alloc_id: AllocId,
        addend: i64,
    ) -> Result<ConstAtom, String> {
        match self.tcx.global_alloc(alloc_id) {
            GlobalAlloc::Function { instance } => {
                let symbol = self.tcx.symbol_name(instance).name.to_string();
                self.record_obj_type(&symbol);
                Ok(ConstAtom {
                    kind: "symbol".to_string(),
                    value: symbol,
                    addend,
                })
            }
            GlobalAlloc::VTable(ty, dyn_ty) => {
                let Some(principal) = dyn_ty.principal() else {
                    return Err(format!(
                        "vtable const for {ty} does not have a principal trait"
                    ));
                };
                let vtable_id = self
                    .record_vtable_desc_for_trait(ty, principal.skip_binder())
                    .ok_or_else(|| format!("failed to materialize vtable const for {ty}"))?;
                let label = self
                    .semantic_module
                    .vtable_descs
                    .get(
                        usize::try_from(vtable_id)
                            .map_err(|_| format!("invalid vtable id {vtable_id}"))?,
                    )
                    .map(|value| value.label.clone())
                    .ok_or_else(|| format!("vtable out of range {vtable_id}"))?;
                Ok(ConstAtom {
                    kind: "vtable".to_string(),
                    value: label,
                    addend,
                })
            }
            GlobalAlloc::Memory(_) | GlobalAlloc::Static(_) => {
                self.const_atom_from_readonly_alloc(alloc_id, addend)
            }
            GlobalAlloc::TypeId { ty } => {
                Err(format!("type-id const lowering is unsupported for {ty}"))
            }
        }
    }

    fn load_const_alloc(
        &self,
        alloc_id: AllocId,
    ) -> Result<&'tcx rustc_middle::mir::interpret::Allocation, String> {
        match self.tcx.global_alloc(alloc_id) {
            GlobalAlloc::Memory(mem) => Ok(mem.inner()),
            GlobalAlloc::Static(def_id) => self
                .tcx
                .eval_static_initializer(def_id)
                .map(|alloc| alloc.inner())
                .map_err(|err| format!("failed to eval static initializer {def_id:?}: {err:?}")),
            GlobalAlloc::Function { instance } => Err(format!(
                "expected memory allocation, found function {}",
                self.tcx.symbol_name(instance).name
            )),
            GlobalAlloc::VTable(ty, _) => {
                Err(format!("expected memory allocation, found vtable for {ty}"))
            }
            GlobalAlloc::TypeId { ty } => Err(format!(
                "expected memory allocation, found type id for {ty}"
            )),
        }
    }

    fn intern_readonly_alloc(&mut self, alloc_id: AllocId) -> Result<i32, String> {
        if let Some(&id) = self.readonly_object_ids.get(&alloc_id) {
            return Ok(id);
        }
        let label = format!(
            "__cheng_rodata_{}",
            self.semantic_module.readonly_objects.len()
        );
        let id = saturating_i32(self.semantic_module.readonly_objects.len());
        self.readonly_object_ids.insert(alloc_id, id);
        self.semantic_module
            .readonly_objects
            .push(ReadonlyObjectV1 {
                label: label.clone(),
                type_idx: -1,
                size_bytes: 0,
                align_bytes: 1,
                bytes_hex: String::new(),
            });

        let alloc = self.load_const_alloc(alloc_id)?;
        let mut relocs = Vec::new();
        for &(offset, _prov) in alloc.provenance().ptrs().iter() {
            let scalar = alloc
                .read_scalar(
                    &self.tcx,
                    alloc_range(offset, self.tcx.data_layout.pointer_size()),
                    true,
                )
                .map_err(|err| format!("failed to read relocation at {offset:?}: {err:?}"))?;
            let atom = self.const_atom_from_scalar(scalar)?;
            relocs.push(ReadonlyRelocV1 {
                object_idx: id,
                offset_bytes: offset.bytes(),
                target_kind: atom.kind,
                target: atom.value,
                addend: atom.addend,
            });
        }

        let object_index =
            usize::try_from(id).map_err(|_| format!("invalid readonly object index {id}"))?;
        self.semantic_module.readonly_objects[object_index] = ReadonlyObjectV1 {
            label,
            type_idx: -1,
            size_bytes: alloc.len() as u64,
            align_bytes: alloc.align.bytes(),
            bytes_hex: encode_hex(
                alloc.inspect_with_uninit_and_ptr_outside_interpreter(0..alloc.len()),
            ),
        };
        self.semantic_module.readonly_relocs.extend(relocs);
        Ok(id)
    }

    fn record_static_data_symbol(
        &mut self,
        def_id: rustc_hir::def_id::DefId,
        symbol: &str,
        ty: Ty<'tcx>,
    ) {
        if !self.data_symbol_names.insert(symbol.to_string()) {
            return;
        }
        let Ok(allocation) = self.tcx.eval_static_initializer(def_id) else {
            return;
        };
        let alloc = allocation.inner();
        let object_idx = saturating_i32(self.semantic_module.readonly_objects.len());
        let type_idx = self.intern_type(ty);
        self.semantic_module
            .readonly_objects
            .push(ReadonlyObjectV1 {
                label: symbol.to_string(),
                type_idx,
                size_bytes: alloc.len() as u64,
                align_bytes: alloc.align.bytes(),
                bytes_hex: encode_hex(
                    alloc.inspect_with_uninit_and_ptr_outside_interpreter(0..alloc.len()),
                ),
            });

        let mut relocs = Vec::new();
        for &(offset, _prov) in alloc.provenance().ptrs().iter() {
            let scalar = match alloc.read_scalar(
                &self.tcx,
                alloc_range(offset, self.tcx.data_layout.pointer_size()),
                true,
            ) {
                Ok(value) => value,
                Err(_) => return,
            };
            let atom = match self.const_atom_from_scalar(scalar) {
                Ok(atom) => atom,
                Err(_) => return,
            };
            relocs.push(ReadonlyRelocV1 {
                object_idx,
                offset_bytes: offset.bytes(),
                target_kind: atom.kind,
                target: atom.value,
                addend: atom.addend,
            });
        }

        self.semantic_module.readonly_relocs.extend(relocs);
        self.semantic_module.data_symbols.push(DataSymbolV1 {
            symbol: symbol.to_string(),
            object_idx,
        });
    }

    fn record_aggregate_layout_for_type(&mut self, type_idx: i32, ty: Ty<'tcx>) {
        if type_idx < 0 || self.aggregate_layout_ids.contains_key(&type_idx) {
            return;
        }
        let Ok(layout) = self
            .tcx
            .layout_of(TypingEnv::fully_monomorphized().as_query_input(ty))
        else {
            return;
        };
        let Some(field_tys) = aggregate_field_types(self.tcx, ty) else {
            return;
        };
        if field_tys.is_empty() {
            return;
        }
        let aggregate_idx = saturating_i32(self.semantic_module.aggregate_layouts.len());
        self.aggregate_layout_ids.insert(type_idx, aggregate_idx);
        let field_start = saturating_i32(self.semantic_module.aggregate_fields.len());
        let field_count = field_tys.len().min(layout.fields.count());
        for (index, field_ty) in field_tys.into_iter().take(field_count).enumerate() {
            let field_type_idx = self.intern_type(field_ty);
            self.semantic_module
                .aggregate_fields
                .push(AggregateFieldV1 {
                    aggregate_idx,
                    field_index: saturating_i32(index),
                    offset_bytes: layout.fields.offset(index).bytes(),
                    type_idx: field_type_idx,
                });
        }
        self.semantic_module
            .aggregate_layouts
            .push(AggregateLayoutV1 {
                type_idx,
                size_bytes: layout.size.bytes(),
                align_bytes: layout.align.bytes(),
                field_start,
                field_len: saturating_i32(field_count),
            });
    }

    fn intern_string(&mut self, text: &str) -> i32 {
        if let Some(&id) = self.string_ids.get(text) {
            return id;
        }
        let id = saturating_i32(self.semantic_module.string_table.len());
        self.semantic_module.string_table.push(text.to_string());
        self.string_ids.insert(text.to_string(), id);
        id
    }

    fn intern_type(&mut self, ty: Ty<'tcx>) -> i32 {
        let name = ty.to_string();
        if let Some(&id) = self.type_ids.get(&name) {
            return id;
        }
        let id = saturating_i32(self.semantic_module.type_names.len());
        self.semantic_module.type_names.push(name.clone());
        let layout_desc = layout_desc(self.tcx, ty, &name);
        self.semantic_module.layout_descs.push(layout_desc);
        self.type_ids.insert(name, id);
        self.record_aggregate_layout_for_type(id, ty);
        id
    }

    fn push_expr(&mut self, kind: &str, type_idx: i32, a: i32, b: i32, c: i32) -> i32 {
        let id = saturating_i32(self.semantic_module.expr_kind.len());
        self.semantic_module.expr_kind.push(kind.to_string());
        self.semantic_module.expr_type.push(type_idx);
        self.semantic_module.expr_a.push(a);
        self.semantic_module.expr_b.push(b);
        self.semantic_module.expr_c.push(c);
        id
    }

    fn push_stmt(&mut self, kind: &str, a: i32, b: i32, c: i32) -> i32 {
        let id = saturating_i32(self.semantic_module.stmt_kind.len());
        self.semantic_module.stmt_kind.push(kind.to_string());
        self.semantic_module.stmt_a.push(a);
        self.semantic_module.stmt_b.push(b);
        self.semantic_module.stmt_c.push(c);
        id
    }
}

fn mono_item_kind(mono_item: &MonoItem<'_>) -> &'static str {
    match mono_item {
        MonoItem::Fn(_) => "fn",
        MonoItem::Static(_) => "static",
        MonoItem::GlobalAsm(_) => "global_asm",
    }
}

fn mono_item_is_unsafe<'tcx>(tcx: TyCtxt<'tcx>, mono_item: &MonoItem<'tcx>) -> bool {
    match mono_item {
        MonoItem::Fn(instance) => tcx
            .hir_get_if_local(instance.def_id())
            .and_then(|node| node.fn_sig())
            .is_some_and(|sig| sig.header.is_unsafe()),
        MonoItem::Static(_) => false,
        MonoItem::GlobalAsm(_) => true,
    }
}

fn projection_kind_name(projection: &ProjectionElem<mir::Local, Ty<'_>>) -> &'static str {
    match projection {
        ProjectionElem::Deref => "place.deref",
        ProjectionElem::Field(..) => "place.field",
        ProjectionElem::Index(..) => "place.index",
        ProjectionElem::ConstantIndex { .. } => "place.constant_index",
        ProjectionElem::Subslice { .. } => "place.subslice",
        ProjectionElem::Downcast(..) => "place.downcast",
        ProjectionElem::OpaqueCast(..) => "place.opaque_cast",
        ProjectionElem::UnwrapUnsafeBinder(..) => "place.unwrap_unsafe_binder",
    }
}

fn layout_desc<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>, name: &str) -> LayoutDescV1 {
    let layout = tcx.layout_of(TypingEnv::fully_monomorphized().as_query_input(ty));
    match layout {
        Ok(layout) => LayoutDescV1 {
            name: name.to_string(),
            size_bytes: layout.size.bytes(),
            align_bytes: layout.align.bytes(),
            abi_class: format!("{:?}", layout.backend_repr),
            niche_bits: 0,
        },
        Err(_) => LayoutDescV1 {
            name: name.to_string(),
            size_bytes: 0,
            align_bytes: 0,
            abi_class: "unknown".to_string(),
            niche_bits: 0,
        },
    }
}

fn ret_mode_from_arg_abi(arg: &ArgAbi<'_, Ty<'_>>) -> &'static str {
    match arg.mode {
        PassMode::Ignore => "void",
        PassMode::Direct(_) | PassMode::Cast { .. } => {
            direct_mode_from_backend_repr(arg.layout.backend_repr)
        }
        PassMode::Pair(..) => "scalar_pair",
        PassMode::Indirect { .. } => "sret_x8",
    }
}

fn arg_mode_from_arg_abi(arg: &ArgAbi<'_, Ty<'_>>) -> &'static str {
    match arg.mode {
        PassMode::Ignore => "void",
        PassMode::Direct(_) | PassMode::Cast { .. } => {
            direct_mode_from_backend_repr(arg.layout.backend_repr)
        }
        PassMode::Pair(..) => "scalar_pair",
        PassMode::Indirect { on_stack: true, .. } => "by_stack",
        PassMode::Indirect {
            on_stack: false, ..
        } => "indirect",
    }
}

fn direct_mode_from_backend_repr(repr: rustc_abi::BackendRepr) -> &'static str {
    match repr {
        rustc_abi::BackendRepr::Scalar(_) => "scalar",
        rustc_abi::BackendRepr::ScalarPair(..) => "scalar_pair",
        rustc_abi::BackendRepr::Memory { .. } | rustc_abi::BackendRepr::SimdVector { .. } => {
            "scalar"
        }
    }
}

fn aggregate_field_types<'tcx>(tcx: TyCtxt<'tcx>, ty: Ty<'tcx>) -> Option<Vec<Ty<'tcx>>> {
    match ty.kind() {
        ty::Tuple(fields) => Some(fields.iter().collect()),
        ty::Adt(adt, args) => Some(
            adt.variants()
                .iter()
                .next()?
                .fields
                .iter()
                .map(|field| field.ty(tcx, args))
                .collect(),
        ),
        _ => None,
    }
}

fn encode_hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        let _ = write!(out, "{byte:02x}");
    }
    out
}

fn fat_ptr_meta_for_ty(ty: Ty<'_>) -> Option<(String, String, &'static str)> {
    match ty.kind() {
        ty::Ref(_, inner, _) | ty::RawPtr(inner, _) => match inner.kind() {
            ty::Slice(_) => Some((inner.to_string(), "usize".to_string(), "slice_len")),
            ty::Str => Some((inner.to_string(), "usize".to_string(), "str_len")),
            ty::Dynamic(..) => Some((inner.to_string(), "vtable".to_string(), "dyn_vtable")),
            _ => None,
        },
        _ => None,
    }
}

fn allocator_shim_symbol_names(tcx: TyCtxt<'_>) -> Vec<String> {
    if tcx.allocator_kind(()).is_none() {
        return Vec::new();
    }
    ALLOCATOR_METHODS
        .iter()
        .map(|method| mangle_internal_symbol(tcx, global_fn_name(method.name).as_str()))
        .chain([
            mangle_internal_symbol(tcx, OomStrategy::SYMBOL),
            mangle_internal_symbol(tcx, NO_ALLOC_SHIM_IS_UNSTABLE),
        ])
        .collect()
}

fn alloc_err_is_invalid_uninit(err: &AllocError) -> bool {
    matches!(err, AllocError::InvalidUninitBytes(_))
}

fn local_name(local: mir::Local, arg_count: usize) -> String {
    let idx = local.index();
    if idx == 0 {
        "_return".to_string()
    } else if idx <= arg_count {
        format!("arg{}", idx - 1)
    } else {
        format!("_{}", idx)
    }
}

fn switch_targets_text(targets: &mir::SwitchTargets) -> String {
    let mut parts = targets
        .iter()
        .map(|(value, target)| format!("{}=>bb{}", value, target.index()))
        .collect::<Vec<_>>();
    parts.push(format!("otherwise=>bb{}", targets.otherwise().index()));
    parts.join(",")
}

fn cleanup_block_or_neg1(unwind: UnwindAction) -> i32 {
    match unwind {
        UnwindAction::Cleanup(target) => saturating_i32(target.index()),
        _ => -1,
    }
}

fn join_i32_list(values: &[i32]) -> String {
    values
        .iter()
        .map(i32::to_string)
        .collect::<Vec<_>>()
        .join(",")
}

fn bool_manifest(value: bool) -> &'static str {
    if value {
        "1"
    } else {
        "0"
    }
}

fn saturating_u32(value: usize) -> u32 {
    value.min(u32::MAX as usize) as u32
}

fn saturating_i32(value: usize) -> i32 {
    value.min(i32::MAX as usize) as i32
}

fn saturating_i32_u64(value: u64) -> i32 {
    value.min(i32::MAX as u64) as i32
}
