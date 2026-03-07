#![cfg_attr(feature = "rustc-backend", feature(rustc_private))]

mod ffi;
mod manifest;

#[cfg(feature = "rustc-backend")]
extern crate rustc_abi;
#[cfg(feature = "rustc-backend")]
extern crate rustc_ast;
#[cfg(feature = "rustc-backend")]
extern crate rustc_codegen_ssa;
#[cfg(feature = "rustc-backend")]
extern crate rustc_data_structures;
#[cfg(feature = "rustc-backend")]
#[allow(unused_extern_crates)]
extern crate rustc_driver;
#[cfg(feature = "rustc-backend")]
extern crate rustc_hir;
#[cfg(feature = "rustc-backend")]
extern crate rustc_index;
#[cfg(feature = "rustc-backend")]
extern crate rustc_metadata;
#[cfg(feature = "rustc-backend")]
extern crate rustc_middle;
#[cfg(feature = "rustc-backend")]
extern crate rustc_session;
#[cfg(feature = "rustc-backend")]
extern crate rustc_span;
#[cfg(feature = "rustc-backend")]
extern crate rustc_symbol_mangling;
#[cfg(feature = "rustc-backend")]
extern crate rustc_target;

pub use ffi::{compile_v1_manifest, validate_v1_manifest, UirBridgeError};
pub use manifest::{
    AggregateFieldV1, AggregateLayoutV1, AliasDomainV1, CallAbiDescV1, CompileOptionsV1,
    CompileReportV1, ConstValueV1, DropScopeV1, FatPtrMetaV1, FunctionAbiV1, LayoutDescV1,
    PanicEdgeV1, ReadonlyObjectV1, ReadonlyRelocV1, RustReportV1, SafetyRegionV1, SemanticModuleV1,
    VtableDescV1,
};

#[cfg(feature = "rustc-backend")]
mod mir_lowering;

#[cfg(feature = "rustc-backend")]
mod rustc_backend;

#[cfg(feature = "rustc-backend")]
pub use rustc_backend::__rustc_codegen_backend;
