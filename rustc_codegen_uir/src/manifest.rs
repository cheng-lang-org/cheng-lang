use std::collections::BTreeMap;
use std::fmt::Write as _;

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RustReportV1 {
    pub mono_items_total: u32,
    pub mono_items_uir: u32,
    pub mono_items_llvm_fallback: u32,
    pub unsafe_regions: u32,
    pub borrow_proof_items: u32,
    pub elapsed_ms: u32,
    pub fallback_reason: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct LayoutDescV1 {
    pub name: String,
    pub size_bytes: u64,
    pub align_bytes: u64,
    pub abi_class: String,
    pub niche_bits: u64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct AliasDomainV1 {
    pub name: String,
    pub is_mutable: bool,
    pub is_exclusive: bool,
    pub proof_id: String,
    pub fallback_reason: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SafetyRegionV1 {
    pub name: String,
    pub kind: String,
    pub is_opaque: bool,
    pub entry_block: i32,
    pub exit_block: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct DropScopeV1 {
    pub name: String,
    pub begin_block: i32,
    pub end_block: i32,
    pub cleanup_block: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct PanicEdgeV1 {
    pub from_block: i32,
    pub to_block: i32,
    pub reason: String,
    pub unwind: bool,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct FatPtrMetaV1 {
    pub name: String,
    pub meta_kind: String,
    pub payload_type: String,
    pub metadata_type: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct VtableDescV1 {
    pub label: String,
    pub entries: Vec<String>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ConstValueV1 {
    pub type_idx: i32,
    pub kind: String,
    pub atom_a_kind: String,
    pub atom_a: String,
    pub atom_a_addend: i64,
    pub atom_b_kind: String,
    pub atom_b: String,
    pub atom_b_addend: i64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ReadonlyObjectV1 {
    pub label: String,
    pub type_idx: i32,
    pub size_bytes: u64,
    pub align_bytes: u64,
    pub bytes_hex: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ReadonlyRelocV1 {
    pub object_idx: i32,
    pub offset_bytes: u64,
    pub target_kind: String,
    pub target: String,
    pub addend: i64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct DataSymbolV1 {
    pub symbol: String,
    pub object_idx: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct AggregateLayoutV1 {
    pub type_idx: i32,
    pub size_bytes: u64,
    pub align_bytes: u64,
    pub field_start: i32,
    pub field_len: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct AggregateFieldV1 {
    pub aggregate_idx: i32,
    pub field_index: i32,
    pub offset_bytes: u64,
    pub type_idx: i32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct FunctionAbiV1 {
    pub func_name: String,
    pub ret_mode: String,
    pub arg_modes: Vec<String>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct CallAbiDescV1 {
    pub stmt_idx: i32,
    pub callee: String,
    pub abi: String,
    pub call_kind: String,
    pub vtable_slot: i32,
    pub can_unwind: bool,
    pub ret_mode: String,
    pub arg_modes: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SemanticModuleV1 {
    pub frontend: String,
    pub target: String,
    pub source_tag: String,
    pub crate_type: String,
    pub entry_path: String,
    pub c_main_target_symbol: String,
    pub semantic_tables_path: String,
    pub symbol_list_path: String,
    pub stage1_root_id: i32,
    pub phase_tag: i32,
    pub high_uir_checked: bool,
    pub low_uir_lowered: bool,
    pub fallback_reason: String,
    pub string_table: Vec<String>,
    pub type_names: Vec<String>,
    pub layout_descs: Vec<LayoutDescV1>,
    pub obj_type_names: Vec<String>,
    pub func_names: Vec<String>,
    pub func_first_block: Vec<i32>,
    pub func_block_len: Vec<i32>,
    pub func_first_local: Vec<i32>,
    pub func_local_len: Vec<i32>,
    pub local_names: Vec<String>,
    pub local_type: Vec<i32>,
    pub local_mutability: Vec<bool>,
    pub block_first_stmt: Vec<i32>,
    pub block_stmt_len: Vec<i32>,
    pub block_term_kind: Vec<String>,
    pub expr_kind: Vec<String>,
    pub expr_type: Vec<i32>,
    pub expr_a: Vec<i32>,
    pub expr_b: Vec<i32>,
    pub expr_c: Vec<i32>,
    pub stmt_kind: Vec<String>,
    pub stmt_a: Vec<i32>,
    pub stmt_b: Vec<i32>,
    pub stmt_c: Vec<i32>,
    pub alias_domains: Vec<AliasDomainV1>,
    pub safety_regions: Vec<SafetyRegionV1>,
    pub drop_scopes: Vec<DropScopeV1>,
    pub panic_edges: Vec<PanicEdgeV1>,
    pub fat_ptr_meta: Vec<FatPtrMetaV1>,
    pub vtable_descs: Vec<VtableDescV1>,
    pub const_values: Vec<ConstValueV1>,
    pub readonly_objects: Vec<ReadonlyObjectV1>,
    pub readonly_relocs: Vec<ReadonlyRelocV1>,
    pub data_symbols: Vec<DataSymbolV1>,
    pub aggregate_layouts: Vec<AggregateLayoutV1>,
    pub aggregate_fields: Vec<AggregateFieldV1>,
    pub func_abi: Vec<FunctionAbiV1>,
    pub call_abi: Vec<CallAbiDescV1>,
    pub atomic_exprs: Vec<i32>,
    pub volatile_exprs: Vec<i32>,
    pub rust_report: RustReportV1,
}

impl Default for SemanticModuleV1 {
    fn default() -> Self {
        Self {
            frontend: "semantic".to_string(),
            target: String::new(),
            source_tag: String::new(),
            crate_type: String::new(),
            entry_path: String::new(),
            c_main_target_symbol: String::new(),
            semantic_tables_path: String::new(),
            symbol_list_path: String::new(),
            stage1_root_id: 0,
            phase_tag: 0,
            high_uir_checked: false,
            low_uir_lowered: false,
            fallback_reason: String::new(),
            string_table: Vec::new(),
            type_names: Vec::new(),
            layout_descs: Vec::new(),
            obj_type_names: Vec::new(),
            func_names: Vec::new(),
            func_first_block: Vec::new(),
            func_block_len: Vec::new(),
            func_first_local: Vec::new(),
            func_local_len: Vec::new(),
            local_names: Vec::new(),
            local_type: Vec::new(),
            local_mutability: Vec::new(),
            block_first_stmt: Vec::new(),
            block_stmt_len: Vec::new(),
            block_term_kind: Vec::new(),
            expr_kind: Vec::new(),
            expr_type: Vec::new(),
            expr_a: Vec::new(),
            expr_b: Vec::new(),
            expr_c: Vec::new(),
            stmt_kind: Vec::new(),
            stmt_a: Vec::new(),
            stmt_b: Vec::new(),
            stmt_c: Vec::new(),
            alias_domains: Vec::new(),
            safety_regions: Vec::new(),
            drop_scopes: Vec::new(),
            panic_edges: Vec::new(),
            fat_ptr_meta: Vec::new(),
            vtable_descs: Vec::new(),
            const_values: Vec::new(),
            readonly_objects: Vec::new(),
            readonly_relocs: Vec::new(),
            data_symbols: Vec::new(),
            aggregate_layouts: Vec::new(),
            aggregate_fields: Vec::new(),
            func_abi: Vec::new(),
            call_abi: Vec::new(),
            atomic_exprs: Vec::new(),
            volatile_exprs: Vec::new(),
            rust_report: RustReportV1::default(),
        }
    }
}

impl SemanticModuleV1 {
    pub fn stage1_file(path: impl Into<String>, target: impl Into<String>) -> Self {
        let entry_path = path.into();
        let mut out = Self::default();
        out.frontend = "stage1".to_string();
        out.target = target.into();
        out.source_tag = entry_path.clone();
        out.entry_path = entry_path;
        out
    }

    pub fn stage1_root(
        root_id: i32,
        source_tag: impl Into<String>,
        target: impl Into<String>,
    ) -> Self {
        let mut out = Self::default();
        out.frontend = "stage1".to_string();
        out.target = target.into();
        out.source_tag = source_tag.into();
        out.stage1_root_id = root_id;
        out
    }

    pub fn rust_frontend(target: impl Into<String>) -> Self {
        let mut out = Self::default();
        out.frontend = "rust".to_string();
        out.target = target.into();
        out
    }

    pub fn to_manifest(&self) -> String {
        self.to_manifest_with_inline_inputs(None, None)
    }

    pub fn to_manifest_with_inline_inputs(
        &self,
        semantic_tables_inline: Option<&str>,
        symbol_list_inline: Option<&str>,
    ) -> String {
        let mut fields = BTreeMap::new();
        fields.insert("entry_path", self.entry_path.clone());
        fields.insert("c_main_target_symbol", self.c_main_target_symbol.clone());
        fields.insert("fallback_reason", self.fallback_reason.clone());
        fields.insert("frontend", self.frontend.clone());
        fields.insert("crate_type", self.crate_type.clone());
        fields.insert(
            "high_uir_checked",
            bool_manifest(self.high_uir_checked).to_string(),
        );
        fields.insert(
            "low_uir_lowered",
            bool_manifest(self.low_uir_lowered).to_string(),
        );
        fields.insert(
            "mono_items_llvm_fallback",
            self.rust_report.mono_items_llvm_fallback.to_string(),
        );
        fields.insert(
            "mono_items_total",
            self.rust_report.mono_items_total.to_string(),
        );
        fields.insert(
            "mono_items_uir",
            self.rust_report.mono_items_uir.to_string(),
        );
        fields.insert(
            "borrow_proof_items",
            self.rust_report.borrow_proof_items.to_string(),
        );
        fields.insert("ms", self.rust_report.elapsed_ms.to_string());
        fields.insert("phase_tag", self.phase_tag.to_string());
        fields.insert("semantic_tables_path", self.semantic_tables_path.clone());
        fields.insert(
            "semantic_tables_inline",
            semantic_tables_inline.unwrap_or_default().to_string(),
        );
        fields.insert("source_tag", self.source_tag.clone());
        fields.insert("symbol_list_path", self.symbol_list_path.clone());
        fields.insert(
            "symbol_list_inline",
            symbol_list_inline.unwrap_or_default().to_string(),
        );
        fields.insert("stage1_root_id", self.stage1_root_id.to_string());
        fields.insert("target", self.target.clone());
        fields.insert(
            "unsafe_regions",
            self.rust_report.unsafe_regions.to_string(),
        );
        fields.insert("string_table_len", self.string_table.len().to_string());
        fields.insert("type_names_len", self.type_names.len().to_string());
        fields.insert("layout_descs_len", self.layout_descs.len().to_string());
        fields.insert("obj_type_names_len", self.obj_type_names.len().to_string());
        fields.insert("func_names_len", self.func_names.len().to_string());
        fields.insert("local_names_len", self.local_names.len().to_string());
        fields.insert("block_len", self.block_first_stmt.len().to_string());
        fields.insert("expr_len", self.expr_kind.len().to_string());
        fields.insert("stmt_len", self.stmt_kind.len().to_string());
        fields.insert("alias_domains_len", self.alias_domains.len().to_string());
        fields.insert("safety_regions_len", self.safety_regions.len().to_string());
        fields.insert("drop_scopes_len", self.drop_scopes.len().to_string());
        fields.insert("panic_edges_len", self.panic_edges.len().to_string());
        fields.insert("fat_ptr_meta_len", self.fat_ptr_meta.len().to_string());
        fields.insert("vtable_descs_len", self.vtable_descs.len().to_string());
        fields.insert("const_values_len", self.const_values.len().to_string());
        fields.insert(
            "readonly_objects_len",
            self.readonly_objects.len().to_string(),
        );
        fields.insert(
            "readonly_relocs_len",
            self.readonly_relocs.len().to_string(),
        );
        fields.insert("data_symbols_len", self.data_symbols.len().to_string());
        fields.insert(
            "aggregate_layouts_len",
            self.aggregate_layouts.len().to_string(),
        );
        fields.insert(
            "aggregate_fields_len",
            self.aggregate_fields.len().to_string(),
        );
        fields.insert("func_abi_len", self.func_abi.len().to_string());
        fields.insert("call_abi_len", self.call_abi.len().to_string());
        fields.insert("atomic_exprs_len", self.atomic_exprs.len().to_string());
        fields.insert("volatile_exprs_len", self.volatile_exprs.len().to_string());

        serialize_manifest(fields)
    }

    pub fn to_semantic_tables_text(&self) -> String {
        let mut out = String::new();
        let _ = writeln!(out, "version=1");
        let _ = writeln!(out, "frontend={}", escape_table_field(&self.frontend));
        let _ = writeln!(out, "target={}", escape_table_field(&self.target));
        let _ = writeln!(out, "source_tag={}", escape_table_field(&self.source_tag));
        let _ = writeln!(out, "crate_type={}", escape_table_field(&self.crate_type));
        let _ = writeln!(out, "string_table_len={}", self.string_table.len());
        let _ = writeln!(out, "type_names_len={}", self.type_names.len());
        let _ = writeln!(out, "layout_descs_len={}", self.layout_descs.len());
        let _ = writeln!(out, "obj_type_names_len={}", self.obj_type_names.len());
        let _ = writeln!(out, "func_names_len={}", self.func_names.len());
        let _ = writeln!(out, "local_names_len={}", self.local_names.len());
        let _ = writeln!(out, "block_len={}", self.block_first_stmt.len());
        let _ = writeln!(out, "expr_len={}", self.expr_kind.len());
        let _ = writeln!(out, "stmt_len={}", self.stmt_kind.len());
        let _ = writeln!(out, "fat_ptr_meta_len={}", self.fat_ptr_meta.len());
        let _ = writeln!(out, "vtable_descs_len={}", self.vtable_descs.len());
        let _ = writeln!(out, "const_values_len={}", self.const_values.len());
        let _ = writeln!(out, "readonly_objects_len={}", self.readonly_objects.len());
        let _ = writeln!(out, "readonly_relocs_len={}", self.readonly_relocs.len());
        let _ = writeln!(out, "data_symbols_len={}", self.data_symbols.len());
        let _ = writeln!(
            out,
            "aggregate_layouts_len={}",
            self.aggregate_layouts.len()
        );
        let _ = writeln!(out, "aggregate_fields_len={}", self.aggregate_fields.len());
        let _ = writeln!(out, "func_abi_len={}", self.func_abi.len());
        let _ = writeln!(out, "call_abi_len={}", self.call_abi.len());

        for (idx, value) in self.string_table.iter().enumerate() {
            let _ = writeln!(
                out,
                "string\tidx={idx}\tvalue={}",
                escape_table_field(value)
            );
        }
        for (idx, value) in self.type_names.iter().enumerate() {
            let _ = writeln!(out, "type\tidx={idx}\tname={}", escape_table_field(value));
        }
        for (idx, layout) in self.layout_descs.iter().enumerate() {
            let _ = writeln!(
                out,
                "layout\tidx={idx}\tname={}\tsize={}\talign={}\tabi={}\tniche_bits={}",
                escape_table_field(&layout.name),
                layout.size_bytes,
                layout.align_bytes,
                escape_table_field(&layout.abi_class),
                layout.niche_bits,
            );
        }
        for (idx, value) in self.obj_type_names.iter().enumerate() {
            let _ = writeln!(
                out,
                "obj_type\tidx={idx}\tname={}",
                escape_table_field(value)
            );
        }
        for (idx, value) in self.func_names.iter().enumerate() {
            let _ = writeln!(
                out,
                "func\tidx={idx}\tname={}\tfirst_block={}\tblock_len={}\tfirst_local={}\tlocal_len={}",
                escape_table_field(value),
                self.func_first_block.get(idx).copied().unwrap_or_default(),
                self.func_block_len.get(idx).copied().unwrap_or_default(),
                self.func_first_local.get(idx).copied().unwrap_or_default(),
                self.func_local_len.get(idx).copied().unwrap_or_default(),
            );
        }
        for (idx, value) in self.local_names.iter().enumerate() {
            let _ = writeln!(
                out,
                "local\tidx={idx}\tname={}\ttype={}\tmutable={}",
                escape_table_field(value),
                self.local_type.get(idx).copied().unwrap_or(-1),
                bool_manifest(self.local_mutability.get(idx).copied().unwrap_or(false)),
            );
        }
        for idx in 0..self.block_first_stmt.len() {
            let _ = writeln!(
                out,
                "block\tidx={idx}\tfirst_stmt={}\tstmt_len={}\tterm_kind={}",
                self.block_first_stmt[idx],
                self.block_stmt_len.get(idx).copied().unwrap_or_default(),
                escape_table_field(
                    self.block_term_kind
                        .get(idx)
                        .map(String::as_str)
                        .unwrap_or("")
                ),
            );
        }
        for idx in 0..self.expr_kind.len() {
            let _ = writeln!(
                out,
                "expr\tidx={idx}\tkind={}\ttype={}\ta={}\tb={}\tc={}",
                escape_table_field(&self.expr_kind[idx]),
                self.expr_type.get(idx).copied().unwrap_or(-1),
                self.expr_a.get(idx).copied().unwrap_or(-1),
                self.expr_b.get(idx).copied().unwrap_or(-1),
                self.expr_c.get(idx).copied().unwrap_or(-1),
            );
        }
        for idx in 0..self.stmt_kind.len() {
            let _ = writeln!(
                out,
                "stmt\tidx={idx}\tkind={}\ta={}\tb={}\tc={}",
                escape_table_field(&self.stmt_kind[idx]),
                self.stmt_a.get(idx).copied().unwrap_or(-1),
                self.stmt_b.get(idx).copied().unwrap_or(-1),
                self.stmt_c.get(idx).copied().unwrap_or(-1),
            );
        }
        for (idx, value) in self.alias_domains.iter().enumerate() {
            let _ = writeln!(
                out,
                "alias_domain\tidx={idx}\tname={}\tmutable={}\texclusive={}\tproof_id={}\tfallback_reason={}",
                escape_table_field(&value.name),
                bool_manifest(value.is_mutable),
                bool_manifest(value.is_exclusive),
                escape_table_field(&value.proof_id),
                escape_table_field(&value.fallback_reason),
            );
        }
        for (idx, value) in self.safety_regions.iter().enumerate() {
            let _ = writeln!(
                out,
                "safety_region\tidx={idx}\tname={}\tkind={}\topaque={}\tentry_block={}\texit_block={}",
                escape_table_field(&value.name),
                escape_table_field(&value.kind),
                bool_manifest(value.is_opaque),
                value.entry_block,
                value.exit_block,
            );
        }
        for (idx, value) in self.drop_scopes.iter().enumerate() {
            let _ = writeln!(
                out,
                "drop_scope\tidx={idx}\tname={}\tbegin_block={}\tend_block={}\tcleanup_block={}",
                escape_table_field(&value.name),
                value.begin_block,
                value.end_block,
                value.cleanup_block,
            );
        }
        for (idx, value) in self.panic_edges.iter().enumerate() {
            let _ = writeln!(
                out,
                "panic_edge\tidx={idx}\tfrom_block={}\tto_block={}\treason={}\tunwind={}",
                value.from_block,
                value.to_block,
                escape_table_field(&value.reason),
                bool_manifest(value.unwind),
            );
        }
        for (idx, value) in self.fat_ptr_meta.iter().enumerate() {
            let _ = writeln!(
                out,
                "fat_ptr\tidx={idx}\tname={}\tmeta_kind={}\tpayload_type={}\tmetadata_type={}",
                escape_table_field(&value.name),
                escape_table_field(&value.meta_kind),
                escape_table_field(&value.payload_type),
                escape_table_field(&value.metadata_type),
            );
        }
        for (idx, value) in self.vtable_descs.iter().enumerate() {
            let _ = writeln!(
                out,
                "vtable\tidx={idx}\tlabel={}\tentries={}",
                escape_table_field(&value.label),
                escape_table_field(&value.entries.join(",")),
            );
        }
        for (idx, value) in self.const_values.iter().enumerate() {
            let _ = writeln!(
                out,
                "const\tidx={idx}\ttype={}\tkind={}\tatom_a_kind={}\tatom_a={}\tatom_a_addend={}\tatom_b_kind={}\tatom_b={}\tatom_b_addend={}",
                value.type_idx,
                escape_table_field(&value.kind),
                escape_table_field(&value.atom_a_kind),
                escape_table_field(&value.atom_a),
                value.atom_a_addend,
                escape_table_field(&value.atom_b_kind),
                escape_table_field(&value.atom_b),
                value.atom_b_addend,
            );
        }
        for (idx, value) in self.readonly_objects.iter().enumerate() {
            let _ = writeln!(
                out,
                "readonly_object\tidx={idx}\tlabel={}\ttype={}\tsize={}\talign={}\tbytes={}",
                escape_table_field(&value.label),
                value.type_idx,
                value.size_bytes,
                value.align_bytes,
                escape_table_field(&value.bytes_hex),
            );
        }
        for (idx, value) in self.readonly_relocs.iter().enumerate() {
            let _ = writeln!(
                out,
                "readonly_reloc\tidx={idx}\tobject={}\toffset={}\ttarget_kind={}\ttarget={}\taddend={}",
                value.object_idx,
                value.offset_bytes,
                escape_table_field(&value.target_kind),
                escape_table_field(&value.target),
                value.addend,
            );
        }
        for (idx, value) in self.data_symbols.iter().enumerate() {
            let _ = writeln!(
                out,
                "data_symbol\tidx={idx}\tsymbol={}\tobject={}",
                escape_table_field(&value.symbol),
                value.object_idx,
            );
        }
        for (idx, value) in self.aggregate_layouts.iter().enumerate() {
            let _ = writeln!(
                out,
                "aggregate\tidx={idx}\ttype={}\tsize={}\talign={}\tfield_start={}\tfield_len={}",
                value.type_idx,
                value.size_bytes,
                value.align_bytes,
                value.field_start,
                value.field_len,
            );
        }
        for (idx, value) in self.aggregate_fields.iter().enumerate() {
            let _ = writeln!(
                out,
                "aggregate_field\tidx={idx}\taggregate={}\tfield_index={}\toffset={}\ttype={}",
                value.aggregate_idx, value.field_index, value.offset_bytes, value.type_idx,
            );
        }
        for (idx, value) in self.func_abi.iter().enumerate() {
            let _ = writeln!(
                out,
                "func_abi\tidx={idx}\tfunc={}\tret_mode={}\targ_modes={}",
                escape_table_field(&value.func_name),
                escape_table_field(&value.ret_mode),
                escape_table_field(&value.arg_modes.join(",")),
            );
        }
        for (idx, value) in self.call_abi.iter().enumerate() {
            let _ = writeln!(
                out,
                "call_abi\tidx={idx}\tstmt_idx={}\tcallee={}\tabi={}\tcall_kind={}\tvtable_slot={}\tcan_unwind={}\tret_mode={}\targ_modes={}",
                value.stmt_idx,
                escape_table_field(&value.callee),
                escape_table_field(&value.abi),
                escape_table_field(&value.call_kind),
                value.vtable_slot,
                bool_manifest(value.can_unwind),
                escape_table_field(&value.ret_mode),
                escape_table_field(&value.arg_modes.join(",")),
            );
        }
        for (idx, value) in self.atomic_exprs.iter().enumerate() {
            let _ = writeln!(out, "atomic_expr\tidx={idx}\texpr={value}");
        }
        for (idx, value) in self.volatile_exprs.iter().enumerate() {
            let _ = writeln!(out, "volatile_expr\tidx={idx}\texpr={value}");
        }
        out
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CompileOptionsV1 {
    pub emit: String,
    pub output_path: String,
    pub obj_writer: String,
    pub opt_level: i32,
    pub validate: bool,
    pub uir_simd: bool,
    pub uir_simd_max_width: i32,
    pub uir_simd_policy: String,
    pub allow_rust_llvm_fallback: bool,
}

impl Default for CompileOptionsV1 {
    fn default() -> Self {
        Self {
            emit: "obj".to_string(),
            output_path: String::new(),
            obj_writer: String::new(),
            opt_level: 2,
            validate: true,
            uir_simd: false,
            uir_simd_max_width: 0,
            uir_simd_policy: "autovec".to_string(),
            allow_rust_llvm_fallback: true,
        }
    }
}

impl CompileOptionsV1 {
    pub fn to_manifest(&self) -> String {
        let mut fields = BTreeMap::new();
        fields.insert(
            "allow_rust_llvm_fallback",
            bool_manifest(self.allow_rust_llvm_fallback).to_string(),
        );
        fields.insert("emit", self.emit.clone());
        fields.insert("obj_writer", self.obj_writer.clone());
        fields.insert("opt_level", self.opt_level.to_string());
        fields.insert("output_path", self.output_path.clone());
        fields.insert("uir_simd", bool_manifest(self.uir_simd).to_string());
        fields.insert("uir_simd_max_width", self.uir_simd_max_width.to_string());
        fields.insert("uir_simd_policy", self.uir_simd_policy.clone());
        fields.insert("validate", bool_manifest(self.validate).to_string());
        serialize_manifest(fields)
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct CompileReportV1 {
    pub fields: BTreeMap<String, String>,
}

impl CompileReportV1 {
    pub fn parse(text: &str) -> Self {
        let mut fields = BTreeMap::new();
        for line in text.lines() {
            if let Some((label, rest)) = line.split_once('\t') {
                fields.insert(label.trim().to_string(), rest.trim().to_string());
                for token in rest.split('\t') {
                    if let Some((key, value)) = token.split_once('=') {
                        fields.insert(
                            format!("{}.{}", label.trim(), key.trim()),
                            value.trim().to_string(),
                        );
                    }
                }
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                fields.insert(key.trim().to_string(), value.trim().to_string());
            }
        }
        Self { fields }
    }

    pub fn get(&self, key: &str) -> Option<&str> {
        self.fields.get(key).map(String::as_str)
    }
}

fn bool_manifest(value: bool) -> &'static str {
    if value {
        "1"
    } else {
        "0"
    }
}

fn serialize_manifest(fields: BTreeMap<&str, String>) -> String {
    let mut out = String::new();
    for (key, value) in fields {
        if value.is_empty() {
            continue;
        }
        out.push_str(key);
        out.push('=');
        out.push_str(&escape_manifest_value(&value));
        out.push('\n');
    }
    out
}

fn escape_manifest_value(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
}

fn escape_table_field(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('\t', "\\t")
        .replace('\n', "\\n")
}

#[cfg(test)]
mod tests {
    use super::{CompileOptionsV1, CompileReportV1, SemanticModuleV1};

    #[test]
    fn stage1_manifest_keeps_target_and_source() {
        let module = SemanticModuleV1::stage1_file(
            "tests/cheng/backend/fixtures/return_add.cheng",
            "arm64-apple-darwin",
        );
        let manifest = module.to_manifest();
        assert!(manifest.contains("frontend=stage1\n"));
        assert!(manifest.contains("entry_path=tests/cheng/backend/fixtures/return_add.cheng\n"));
        assert!(manifest.contains("source_tag=tests/cheng/backend/fixtures/return_add.cheng\n"));
        assert!(manifest.contains("target=arm64-apple-darwin\n"));
    }

    #[test]
    fn options_manifest_uses_obj_bootstrap_defaults() {
        let options = CompileOptionsV1::default();
        let manifest = options.to_manifest();
        assert!(manifest.contains("emit=obj\n"));
        assert!(manifest.contains("opt_level=2\n"));
        assert!(manifest.contains("validate=1\n"));
        assert!(manifest.contains("allow_rust_llvm_fallback=1\n"));
    }

    #[test]
    fn compile_report_parser_keeps_known_keys() {
        let report = CompileReportV1::parse(
            "status=ok\nfrontend=stage1\nobj_bytes=128\nrust_uir_report\tmono_items_total=0\n",
        );
        assert_eq!(report.get("status"), Some("ok"));
        assert_eq!(report.get("frontend"), Some("stage1"));
        assert_eq!(report.get("obj_bytes"), Some("128"));
        assert_eq!(report.get("rust_uir_report.mono_items_total"), Some("0"));
    }

    #[test]
    fn manifest_can_inline_multiline_inputs() {
        let mut module = SemanticModuleV1::rust_frontend("aarch64-apple-darwin");
        module.crate_type = "bin".to_string();
        let manifest = module.to_manifest_with_inline_inputs(
            Some("version=1\nfunc\tidx=0\tname=demo\n"),
            Some("mono_item_candidate\tkind=fn\tsymbol=demo\n"),
        );
        assert!(manifest.contains("semantic_tables_inline=version=1\\nfunc\tidx=0\tname=demo\\n\n"));
        assert!(
            manifest.contains("symbol_list_inline=mono_item_candidate\tkind=fn\tsymbol=demo\\n\n")
        );
    }
}
