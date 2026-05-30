#![feature(rustc_private)]

extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_public;

use rustc_public::crate_def::CrateDef;
use rustc_public::mir::{
    AggregateKind, Operand, Place, ProjectionElem, Rvalue, Statement, StatementKind, TerminatorKind,
};
use rustc_public::target::{Endian, MachineInfo};
use rustc_public::ty::{ConstantKind, RigidTy, Ty, TyKind};
use rustc_public::CrateItem;
use std::env;
use std::ffi::{CStr, CString};
use std::fs;
use std::io;
use std::os::raw::{c_char, c_int, c_void};
use std::path::{Path, PathBuf};
use std::process::{self, Command};
use std::time::{SystemTime, UNIX_EPOCH};

const RTLD_NOW: c_int = 0x2;
const RTLD_LOCAL: c_int = 0x0;
const RTLD_DEEPBIND: c_int = 0x8;

unsafe extern "C" {
    fn dlopen(filename: *const c_char, flags: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    fn dlerror() -> *const c_char;
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirStringRef {
    data: *const c_char,
    length: isize,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirContext {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirLocation {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirModule {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirOperation {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirBlock {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirType {
    ptr: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct MlirAttribute {
    ptr: *mut c_void,
}

type RustMlirContextCreate = unsafe extern "C" fn() -> MlirContext;
type RustMlirContextDestroy = unsafe extern "C" fn(MlirContext);
type RustMlirLocationUnknownGet = unsafe extern "C" fn(MlirContext) -> MlirLocation;
type RustMlirLocationFromRustSpan =
    unsafe extern "C" fn(MlirContext, MlirStringRef) -> MlirLocation;
type RustMlirModuleCreate = unsafe extern "C" fn(MlirLocation) -> MlirModule;
type RustMlirModuleDestroy = unsafe extern "C" fn(MlirModule);
type RustMlirModuleGetOperation = unsafe extern "C" fn(MlirModule) -> MlirOperation;
type RustMlirModuleGetBody = unsafe extern "C" fn(MlirModule) -> MlirBlock;
type RustMlirBlockAppendOwnedOperation = unsafe extern "C" fn(MlirBlock, MlirOperation);
type RustMlirOperationVerify = unsafe extern "C" fn(MlirOperation) -> bool;
type RustMlirWriteBytecodeToFile = unsafe extern "C" fn(MlirOperation, MlirStringRef) -> bool;
type RustMlirWriteTextToFile = unsafe extern "C" fn(MlirOperation, MlirStringRef) -> bool;
type RustMlirMergeTextModulesToFile =
    unsafe extern "C" fn(isize, *const MlirStringRef, MlirStringRef, bool) -> bool;
type RustMirModuleSetTarget = unsafe extern "C" fn(MlirModule, i64, MlirStringRef);
type RustMirTypeFromRustcPublicString =
    unsafe extern "C" fn(MlirContext, MlirStringRef) -> MlirType;
type RustTypedTupleTypeGet = unsafe extern "C" fn(MlirContext, isize, *const MlirType) -> MlirType;
type RustMirProjectionAttrGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, MlirStringRef) -> MlirAttribute;
type RustMirProjectionFieldAttrGet =
    unsafe extern "C" fn(MlirContext, i64, MlirStringRef) -> MlirAttribute;
type RustMirPlaceAttrGet =
    unsafe extern "C" fn(MlirContext, i64, isize, *const MlirAttribute) -> MlirAttribute;
type RustMirOperandCopyAttrGet = unsafe extern "C" fn(MlirContext, MlirAttribute) -> MlirAttribute;
type RustMirOperandMoveAttrGet = unsafe extern "C" fn(MlirContext, MlirAttribute) -> MlirAttribute;
type RustMirOperandConstantI64AttrGet =
    unsafe extern "C" fn(MlirContext, i64, MlirStringRef, MlirStringRef) -> MlirAttribute;
type RustMirOperandDebugAttrGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, MlirStringRef) -> MlirAttribute;
type RustMirRvalueBinaryOpAttrGet = unsafe extern "C" fn(
    MlirContext,
    MlirStringRef,
    MlirStringRef,
    MlirAttribute,
    MlirAttribute,
) -> MlirAttribute;
type RustMirRvalueUnaryOpAttrGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, MlirStringRef, MlirAttribute) -> MlirAttribute;
type RustMirRvalueAggregateAttrGet = unsafe extern "C" fn(
    MlirContext,
    MlirStringRef,
    MlirStringRef,
    isize,
    *const MlirAttribute,
) -> MlirAttribute;
type RustMirRvalueUseAttrGet = unsafe extern "C" fn(MlirContext, MlirAttribute) -> MlirAttribute;
type RustMirRvalueDebugAttrGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, MlirStringRef) -> MlirAttribute;
type RustMirAssignPayloadAttrGet =
    unsafe extern "C" fn(MlirContext, MlirAttribute, MlirAttribute) -> MlirAttribute;
type RustMirTargetPayloadAttrGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, i64, MlirStringRef) -> MlirAttribute;
type RustMirSwitchTargetsAttrGet =
    unsafe extern "C" fn(MlirContext, i64, isize, *const i64, *const i64) -> MlirAttribute;
type RustMirSwitchIntPayloadAttrGet =
    unsafe extern "C" fn(MlirContext, MlirAttribute, MlirAttribute, MlirStringRef) -> MlirAttribute;
type RustMirAssertPayloadAttrGet =
    unsafe extern "C" fn(MlirContext, MlirAttribute, bool, i64, MlirStringRef) -> MlirAttribute;
type RustMirFuncCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    i64,
    MlirStringRef,
) -> MlirOperation;
type RustMirBlockCreate = unsafe extern "C" fn(MlirLocation, i64) -> MlirOperation;
type RustMirOperationGetBodyBlock = unsafe extern "C" fn(MlirOperation) -> MlirBlock;
type RustMirLocalCreate = unsafe extern "C" fn(
    MlirLocation,
    i64,
    MlirStringRef,
    MlirStringRef,
    MlirType,
    MlirStringRef,
) -> MlirOperation;
type RustMirAssignCreate = unsafe extern "C" fn(MlirLocation, i64, MlirAttribute) -> MlirOperation;
type RustMirReturnCreate = unsafe extern "C" fn(MlirLocation) -> MlirOperation;
type RustMirPayloadOpCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirAttribute,
) -> MlirOperation;

struct MlirApi {
    _library_handle: *mut c_void,
    context_create: RustMlirContextCreate,
    context_destroy: RustMlirContextDestroy,
    location_unknown_get: RustMlirLocationUnknownGet,
    location_from_rust_span: RustMlirLocationFromRustSpan,
    module_create: RustMlirModuleCreate,
    module_destroy: RustMlirModuleDestroy,
    module_get_operation: RustMlirModuleGetOperation,
    module_get_body: RustMlirModuleGetBody,
    block_append_owned_operation: RustMlirBlockAppendOwnedOperation,
    operation_verify: RustMlirOperationVerify,
    write_bytecode_to_file: RustMlirWriteBytecodeToFile,
    write_text_to_file: RustMlirWriteTextToFile,
    merge_text_modules_to_file: RustMlirMergeTextModulesToFile,
    module_set_target: RustMirModuleSetTarget,
    type_from_rustc_public_string: RustMirTypeFromRustcPublicString,
    typed_tuple_type_get: RustTypedTupleTypeGet,
    projection_attr_get: RustMirProjectionAttrGet,
    projection_field_attr_get: RustMirProjectionFieldAttrGet,
    place_attr_get: RustMirPlaceAttrGet,
    operand_copy_attr_get: RustMirOperandCopyAttrGet,
    operand_move_attr_get: RustMirOperandMoveAttrGet,
    operand_constant_i64_attr_get: RustMirOperandConstantI64AttrGet,
    operand_debug_attr_get: RustMirOperandDebugAttrGet,
    rvalue_binary_op_attr_get: RustMirRvalueBinaryOpAttrGet,
    rvalue_unary_op_attr_get: RustMirRvalueUnaryOpAttrGet,
    rvalue_aggregate_attr_get: RustMirRvalueAggregateAttrGet,
    rvalue_use_attr_get: RustMirRvalueUseAttrGet,
    rvalue_debug_attr_get: RustMirRvalueDebugAttrGet,
    assign_payload_attr_get: RustMirAssignPayloadAttrGet,
    target_payload_attr_get: RustMirTargetPayloadAttrGet,
    switch_targets_attr_get: RustMirSwitchTargetsAttrGet,
    switch_int_payload_attr_get: RustMirSwitchIntPayloadAttrGet,
    assert_payload_attr_get: RustMirAssertPayloadAttrGet,
    func_create: RustMirFuncCreate,
    block_create: RustMirBlockCreate,
    operation_get_body_block: RustMirOperationGetBodyBlock,
    local_create: RustMirLocalCreate,
    assign_create: RustMirAssignCreate,
    return_create: RustMirReturnCreate,
    payload_op_create: RustMirPayloadOpCreate,
}

impl MlirApi {
    fn load() -> io::Result<Self> {
        let path = mlir_capi_library_path();
        let c_path = CString::new(path.clone()).map_err(invalid_input)?;
        let handle = unsafe { dlopen(c_path.as_ptr(), RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND) };
        if handle.is_null() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("failed to load {path}: {}", dl_error_string()),
            ));
        }

        unsafe {
            Ok(Self {
                _library_handle: handle,
                context_create: load_symbol(handle, "rustMlirContextCreate")?,
                context_destroy: load_symbol(handle, "rustMlirContextDestroy")?,
                location_unknown_get: load_symbol(handle, "rustMlirLocationUnknownGet")?,
                location_from_rust_span: load_symbol(handle, "rustMlirLocationFromRustSpan")?,
                module_create: load_symbol(handle, "rustMlirModuleCreate")?,
                module_destroy: load_symbol(handle, "rustMlirModuleDestroy")?,
                module_get_operation: load_symbol(handle, "rustMlirModuleGetOperation")?,
                module_get_body: load_symbol(handle, "rustMlirModuleGetBody")?,
                block_append_owned_operation: load_symbol(
                    handle,
                    "rustMlirBlockAppendOwnedOperation",
                )?,
                operation_verify: load_symbol(handle, "rustMlirOperationVerify")?,
                write_bytecode_to_file: load_symbol(handle, "rustMlirWriteBytecodeToFile")?,
                write_text_to_file: load_symbol(handle, "rustMlirWriteTextToFile")?,
                merge_text_modules_to_file: load_symbol(handle, "rustMlirMergeTextModulesToFile")?,
                module_set_target: load_symbol(handle, "rustMirModuleSetTarget")?,
                type_from_rustc_public_string: load_symbol(
                    handle,
                    "rustMirTypeFromRustcPublicString",
                )?,
                typed_tuple_type_get: load_symbol(handle, "rustTypedTupleTypeGet")?,
                projection_attr_get: load_symbol(handle, "rustMirProjectionAttrGet")?,
                projection_field_attr_get: load_symbol(handle, "rustMirProjectionFieldAttrGet")?,
                place_attr_get: load_symbol(handle, "rustMirPlaceAttrGet")?,
                operand_copy_attr_get: load_symbol(handle, "rustMirOperandCopyAttrGet")?,
                operand_move_attr_get: load_symbol(handle, "rustMirOperandMoveAttrGet")?,
                operand_constant_i64_attr_get: load_symbol(
                    handle,
                    "rustMirOperandConstantI64AttrGet",
                )?,
                operand_debug_attr_get: load_symbol(handle, "rustMirOperandDebugAttrGet")?,
                rvalue_binary_op_attr_get: load_symbol(handle, "rustMirRvalueBinaryOpAttrGet")?,
                rvalue_unary_op_attr_get: load_symbol(handle, "rustMirRvalueUnaryOpAttrGet")?,
                rvalue_aggregate_attr_get: load_symbol(handle, "rustMirRvalueAggregateAttrGet")?,
                rvalue_use_attr_get: load_symbol(handle, "rustMirRvalueUseAttrGet")?,
                rvalue_debug_attr_get: load_symbol(handle, "rustMirRvalueDebugAttrGet")?,
                assign_payload_attr_get: load_symbol(handle, "rustMirAssignPayloadAttrGet")?,
                target_payload_attr_get: load_symbol(handle, "rustMirTargetPayloadAttrGet")?,
                switch_targets_attr_get: load_symbol(handle, "rustMirSwitchTargetsAttrGet")?,
                switch_int_payload_attr_get: load_symbol(handle, "rustMirSwitchIntPayloadAttrGet")?,
                assert_payload_attr_get: load_symbol(handle, "rustMirAssertPayloadAttrGet")?,
                func_create: load_symbol(handle, "rustMirFuncCreate")?,
                block_create: load_symbol(handle, "rustMirBlockCreate")?,
                operation_get_body_block: load_symbol(handle, "rustMirOperationGetBodyBlock")?,
                local_create: load_symbol(handle, "rustMirLocalCreate")?,
                assign_create: load_symbol(handle, "rustMirAssignCreate")?,
                return_create: load_symbol(handle, "rustMirReturnCreate")?,
                payload_op_create: load_symbol(handle, "rustMirPayloadOpCreate")?,
            })
        }
    }
}

unsafe fn load_symbol<T: Copy>(handle: *mut c_void, name: &str) -> io::Result<T> {
    let c_name = CString::new(name).map_err(invalid_input)?;
    let symbol = dlsym(handle, c_name.as_ptr());
    if symbol.is_null() {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            format!("failed to load symbol {name}: {}", dl_error_string()),
        ));
    }
    Ok(std::mem::transmute_copy::<*mut c_void, T>(&symbol))
}

fn mlir_capi_library_path() -> String {
    env::var("RUST_TO_LLVM_CAPI_LIBRARY")
        .ok()
        .or_else(|| option_env!("RUST_TO_LLVM_CAPI_LIBRARY").map(str::to_string))
        .unwrap_or_else(|| "libRustToLLVMRustCAPI.so".to_string())
}

fn dl_error_string() -> String {
    let error = unsafe { dlerror() };
    if error.is_null() {
        return "unknown dynamic loader error".to_string();
    }
    unsafe { CStr::from_ptr(error) }
        .to_string_lossy()
        .into_owned()
}

fn invalid_input(err: impl std::fmt::Display) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, err.to_string())
}

fn mlir_string(text: &str) -> MlirStringRef {
    MlirStringRef {
        data: text.as_ptr() as *const c_char,
        length: text.len() as isize,
    }
}

#[derive(Clone, Copy)]
enum OutputFormat {
    Bytecode,
    Text,
}

struct Options {
    crate_root: Option<String>,
    cargo_dir: Option<String>,
    output: Option<String>,
    format: OutputFormat,
    passthrough: Vec<String>,
}

struct RustcOutputDir {
    path: PathBuf,
}

impl RustcOutputDir {
    fn create() -> io::Result<Self> {
        let root = env::temp_dir();
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(invalid_input)?
            .as_nanos();
        for attempt in 0..1024 {
            let path = root.join(format!(
                "rust-mir-extract-{}-{nonce}-{attempt}",
                process::id()
            ));
            match std::fs::create_dir(&path) {
                Ok(()) => return Ok(Self { path }),
                Err(err) if err.kind() == io::ErrorKind::AlreadyExists => continue,
                Err(err) => return Err(err),
            }
        }
        Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            "failed to create a unique rustc output directory",
        ))
    }

    fn path(&self) -> &Path {
        &self.path
    }
}

impl Drop for RustcOutputDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

impl Default for Options {
    fn default() -> Self {
        Self {
            crate_root: None,
            cargo_dir: None,
            output: None,
            format: OutputFormat::Bytecode,
            passthrough: Vec::new(),
        }
    }
}

struct MirProgram {
    target: TargetInfo,
    functions: Vec<MirFunction>,
}

struct TargetInfo {
    pointer_width: i64,
    endian: String,
}

struct MirFunction {
    name: String,
    signature: String,
    item_kind: String,
    span: String,
    arg_count: usize,
    locals: Vec<MirLocal>,
    blocks: Vec<MirBlockData>,
}

struct MirLocal {
    index: usize,
    name: String,
    role: String,
    ty: MirType,
    mutability: String,
    span: String,
}

struct MirBlockData {
    index: usize,
    span: String,
    statements: Vec<MirStatement>,
    terminator: MirTerminator,
}

enum MirStatement {
    Assign {
        index: usize,
        span: String,
        place: MirPlace,
        rvalue: MirRvalue,
    },
    Unsupported {
        span: String,
        kind: String,
        debug: String,
        op_name: &'static str,
    },
}

enum MirTerminator {
    Return {
        span: String,
    },
    Goto {
        span: String,
        target: usize,
    },
    SwitchInt {
        span: String,
        discr: MirOperand,
        targets: MirSwitchTargets,
        debug: String,
    },
    Assert {
        span: String,
        cond: MirOperand,
        expected: bool,
        target: usize,
        debug: String,
    },
    Target {
        span: String,
        kind: String,
        target: usize,
        debug: String,
        op_name: &'static str,
    },
    Unsupported {
        span: String,
        kind: String,
        debug: String,
        op_name: &'static str,
    },
}

struct MirSwitchTargets {
    branches: Vec<(i64, usize)>,
    otherwise: usize,
}

struct MirPlace {
    local: usize,
    projection: Vec<MirProjection>,
}

enum MirProjection {
    Field { index: usize, ty: String },
    Unsupported { kind: String, debug: String },
}

#[derive(Clone)]
enum MirType {
    Debug(String),
    Tuple(Vec<MirType>),
}

impl MirType {
    fn from_public(ty: Ty) -> Self {
        match ty.kind() {
            TyKind::RigidTy(RigidTy::Tuple(elements)) if elements.is_empty() => {
                Self::Debug("()".to_string())
            }
            TyKind::RigidTy(RigidTy::Tuple(elements)) => Self::Tuple(
                elements
                    .iter()
                    .copied()
                    .map(Self::from_public)
                    .collect::<Vec<_>>(),
            ),
            _ => Self::Debug(format!("{ty:?}")),
        }
    }
}

enum MirOperand {
    Copy(MirPlace),
    Move(MirPlace),
    Constant(MirConstant),
    RuntimeChecks { debug: String },
}

struct MirConstant {
    ty: String,
    debug: String,
    value: Option<i64>,
}

enum MirRvalue {
    BinaryOp {
        kind: String,
        op: String,
        lhs: Box<MirOperand>,
        rhs: Box<MirOperand>,
    },
    UnaryOp {
        kind: String,
        op: String,
        operand: Box<MirOperand>,
    },
    Aggregate {
        kind: String,
        aggregate_kind: String,
        operands: Vec<MirOperand>,
    },
    Use {
        operand: Box<MirOperand>,
    },
    Unsupported {
        kind: String,
        debug: String,
    },
}

impl MirProgram {
    fn collect() -> Self {
        let target = TargetInfo::collect();
        let mut functions = Vec::new();
        for item in rustc_public::all_local_items() {
            if !item.has_body() {
                continue;
            }
            functions.push(MirFunction::collect(item));
        }

        Self { target, functions }
    }
}

impl TargetInfo {
    fn collect() -> Self {
        let target = MachineInfo::target();
        let endian = match target.endian {
            Endian::Little => "little",
            Endian::Big => "big",
        };
        Self {
            pointer_width: target.pointer_width.bits() as i64,
            endian: endian.to_string(),
        }
    }
}

impl MirFunction {
    fn collect(item: CrateItem) -> Self {
        let body = item.expect_body();
        let mut locals = Vec::new();
        let ret_local = body.ret_local();
        locals.push(MirLocal {
            index: 0,
            name: "_0".to_string(),
            role: "return".to_string(),
            ty: MirType::from_public(ret_local.ty),
            mutability: format!("{:?}", ret_local.mutability),
            span: span_string(&ret_local.span),
        });

        for (arg_index, local) in body.arg_locals().iter().enumerate() {
            let local_index = arg_index + 1;
            locals.push(MirLocal {
                index: local_index,
                name: format!("_{local_index}"),
                role: "arg".to_string(),
                ty: MirType::from_public(local.ty),
                mutability: format!("{:?}", local.mutability),
                span: span_string(&local.span),
            });
        }

        for (inner_index, local) in body.inner_locals().iter().enumerate() {
            let local_index = inner_index + body.arg_locals().len() + 1;
            locals.push(MirLocal {
                index: local_index,
                name: format!("_{local_index}"),
                role: "inner".to_string(),
                ty: MirType::from_public(local.ty),
                mutability: format!("{:?}", local.mutability),
                span: span_string(&local.span),
            });
        }

        let blocks = body
            .blocks
            .iter()
            .enumerate()
            .map(|(block_index, block)| {
                let span = span_string(&block.terminator.span);
                let statements = block
                    .statements
                    .iter()
                    .enumerate()
                    .map(|(statement_index, statement)| {
                        MirStatement::from_public(statement, statement_index)
                    })
                    .collect();
                let terminator = MirTerminator::from_public(&block.terminator.kind, span.clone());
                MirBlockData {
                    index: block_index,
                    span,
                    statements,
                    terminator,
                }
            })
            .collect();

        Self {
            name: item.name().to_string(),
            signature: format!("{:?}", item.ty()),
            item_kind: format!("{:?}", item.kind()),
            span: span_string(&body.span),
            arg_count: body.arg_locals().len(),
            locals,
            blocks,
        }
    }
}

impl MirStatement {
    fn from_public(statement: &Statement, index: usize) -> Self {
        let span = span_string(&statement.span);
        match &statement.kind {
            StatementKind::Assign(place, rvalue) => Self::Assign {
                index,
                span,
                place: MirPlace::from_public(place),
                rvalue: MirRvalue::from_public(rvalue),
            },
            _ => {
                let debug = format!("{:?}", statement.kind);
                let kind = variant_name(&debug).to_string();
                Self::Unsupported {
                    span,
                    op_name: statement_op_name(&kind),
                    kind,
                    debug,
                }
            }
        }
    }
}

impl MirTerminator {
    fn from_public(terminator: &TerminatorKind, span: String) -> Self {
        match terminator {
            TerminatorKind::Return => Self::Return { span },
            TerminatorKind::Goto { target } => Self::Goto {
                span,
                target: *target,
            },
            TerminatorKind::SwitchInt { discr, targets } => Self::SwitchInt {
                span,
                discr: MirOperand::from_public(discr),
                targets: MirSwitchTargets {
                    branches: targets
                        .branches()
                        .filter_map(|(value, target)| {
                            i64::try_from(value).ok().map(|value| (value, target))
                        })
                        .collect(),
                    otherwise: targets.otherwise(),
                },
                debug: format!("{terminator:?}"),
            },
            TerminatorKind::Assert {
                cond,
                expected,
                target,
                ..
            } => Self::Assert {
                span,
                cond: MirOperand::from_public(cond),
                expected: *expected,
                target: *target,
                debug: format!("{terminator:?}"),
            },
            TerminatorKind::Drop { target, .. } => {
                let debug = format!("{terminator:?}");
                Self::Target {
                    span,
                    kind: "Drop".to_string(),
                    target: *target,
                    debug,
                    op_name: "rust.mir.drop",
                }
            }
            TerminatorKind::Call {
                target: Some(target),
                ..
            } => {
                let debug = format!("{terminator:?}");
                Self::Target {
                    span,
                    kind: "Call".to_string(),
                    target: *target,
                    debug,
                    op_name: "rust.mir.call",
                }
            }
            TerminatorKind::InlineAsm {
                destination: Some(target),
                ..
            } => {
                let debug = format!("{terminator:?}");
                Self::Target {
                    span,
                    kind: "InlineAsm".to_string(),
                    target: *target,
                    debug,
                    op_name: "rust.mir.inline_asm",
                }
            }
            _ => {
                let debug = format!("{terminator:?}");
                let kind = variant_name(&debug).to_string();
                Self::Unsupported {
                    span,
                    op_name: terminator_op_name(&kind),
                    kind,
                    debug,
                }
            }
        }
    }
}

impl MirPlace {
    fn from_public(place: &Place) -> Self {
        Self {
            local: place.local,
            projection: place
                .projection
                .iter()
                .map(MirProjection::from_public)
                .collect(),
        }
    }
}

impl MirProjection {
    fn from_public(elem: &ProjectionElem) -> Self {
        match elem {
            ProjectionElem::Field(index, ty) => Self::Field {
                index: *index,
                ty: format!("{ty:?}"),
            },
            _ => {
                let debug = format!("{elem:?}");
                let kind = variant_name(&debug).to_string();
                Self::Unsupported { kind, debug }
            }
        }
    }
}

impl MirOperand {
    fn from_public(operand: &Operand) -> Self {
        match operand {
            Operand::Copy(place) => Self::Copy(MirPlace::from_public(place)),
            Operand::Move(place) => Self::Move(MirPlace::from_public(place)),
            Operand::Constant(constant) => Self::Constant(MirConstant {
                ty: format!("{:?}", constant.const_.ty()),
                debug: constant_scalar_text(constant)
                    .unwrap_or_else(|| format!("{:?}", constant.const_)),
                value: constant_scalar_text(constant).and_then(|text| text.parse::<i64>().ok()),
            }),
            Operand::RuntimeChecks(checks) => Self::RuntimeChecks {
                debug: format!("{checks:?}"),
            },
        }
    }
}

impl MirRvalue {
    fn from_public(rvalue: &Rvalue) -> Self {
        match rvalue {
            Rvalue::BinaryOp(op, lhs, rhs) | Rvalue::CheckedBinaryOp(op, lhs, rhs) => {
                let kind = match rvalue {
                    Rvalue::BinaryOp(_, _, _) => "BinaryOp",
                    Rvalue::CheckedBinaryOp(_, _, _) => "CheckedBinaryOp",
                    _ => unreachable!(),
                };
                let op_debug = format!("{op:?}");
                Self::BinaryOp {
                    kind: kind.to_string(),
                    op: variant_name(&op_debug).to_string(),
                    lhs: Box::new(MirOperand::from_public(lhs)),
                    rhs: Box::new(MirOperand::from_public(rhs)),
                }
            }
            Rvalue::UnaryOp(op, operand) => {
                let op_debug = format!("{op:?}");
                Self::UnaryOp {
                    kind: "UnaryOp".to_string(),
                    op: variant_name(&op_debug).to_string(),
                    operand: Box::new(MirOperand::from_public(operand)),
                }
            }
            Rvalue::Aggregate(kind, operands) => Self::Aggregate {
                kind: "Aggregate".to_string(),
                aggregate_kind: aggregate_kind_name(kind).to_string(),
                operands: operands.iter().map(MirOperand::from_public).collect(),
            },
            Rvalue::Use(operand) => Self::Use {
                operand: Box::new(MirOperand::from_public(operand)),
            },
            _ => {
                let debug = format!("{rvalue:?}");
                let kind = variant_name(&debug).to_string();
                Self::Unsupported { kind, debug }
            }
        }
    }
}

struct MlirEmitter {
    api: MlirApi,
    context: MlirContext,
    module: MlirModule,
}

impl MlirEmitter {
    fn new() -> io::Result<Self> {
        let api = MlirApi::load()?;
        let context = unsafe { (api.context_create)() };
        let unknown_loc = unsafe { (api.location_unknown_get)(context) };
        let module = unsafe { (api.module_create)(unknown_loc) };
        Ok(Self {
            api,
            context,
            module,
        })
    }

    fn emit_program(&self, program: &MirProgram) -> io::Result<()> {
        unsafe {
            (self.api.module_set_target)(
                self.module,
                program.target.pointer_width,
                mlir_string(&program.target.endian),
            );
        }

        let module_body = unsafe { (self.api.module_get_body)(self.module) };
        self.require_block(module_body, "module")?;
        for function in &program.functions {
            let op = self.create_function(function)?;
            unsafe {
                (self.api.block_append_owned_operation)(module_body, op);
            }
        }

        let module_op = unsafe { (self.api.module_get_operation)(self.module) };
        if unsafe { !(self.api.operation_verify)(module_op) } {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "generated MLIR failed verification",
            ));
        }
        Ok(())
    }

    fn write(&self, output: &str, format: OutputFormat) -> io::Result<()> {
        let module_op = unsafe { (self.api.module_get_operation)(self.module) };
        let ok = unsafe {
            match format {
                OutputFormat::Bytecode => {
                    (self.api.write_bytecode_to_file)(module_op, mlir_string(output))
                }
                OutputFormat::Text => (self.api.write_text_to_file)(module_op, mlir_string(output)),
            }
        };
        if !ok {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("failed to write MLIR to {output}"),
            ));
        }
        Ok(())
    }

    fn create_function(&self, function: &MirFunction) -> io::Result<MlirOperation> {
        let loc = self.location(&function.span);
        let op = unsafe {
            (self.api.func_create)(
                loc,
                mlir_string(&function.name),
                mlir_string(&function.name),
                mlir_string(&function.signature),
                function.arg_count as i64,
                mlir_string(&function.item_kind),
            )
        };
        let body = unsafe { (self.api.operation_get_body_block)(op) };
        self.require_block(body, "rust.mir.func")?;

        for local in &function.locals {
            let local_op = self.create_local(local);
            unsafe {
                (self.api.block_append_owned_operation)(body, local_op);
            }
        }

        for block in &function.blocks {
            let block_op = self.create_block(block)?;
            unsafe {
                (self.api.block_append_owned_operation)(body, block_op);
            }
        }

        Ok(op)
    }

    fn create_local(&self, local: &MirLocal) -> MlirOperation {
        let loc = self.location(&local.span);
        let rust_type = self.type_from_mir(&local.ty);
        unsafe {
            (self.api.local_create)(
                loc,
                local.index as i64,
                mlir_string(&local.name),
                mlir_string(&local.role),
                rust_type,
                mlir_string(&local.mutability),
            )
        }
    }

    fn create_block(&self, block: &MirBlockData) -> io::Result<MlirOperation> {
        let loc = self.location(&block.span);
        let op = unsafe { (self.api.block_create)(loc, block.index as i64) };
        let body = unsafe { (self.api.operation_get_body_block)(op) };
        self.require_block(body, "rust.mir.block")?;

        for statement in &block.statements {
            let statement_op = self.create_statement(statement);
            unsafe {
                (self.api.block_append_owned_operation)(body, statement_op);
            }
        }

        let terminator_op = self.create_terminator(&block.terminator);
        unsafe {
            (self.api.block_append_owned_operation)(body, terminator_op);
        }
        Ok(op)
    }

    fn create_statement(&self, statement: &MirStatement) -> MlirOperation {
        match statement {
            MirStatement::Assign {
                index,
                span,
                place,
                rvalue,
            } => {
                let place_attr = self.place_attr(place);
                let rvalue_attr = self.rvalue_attr(rvalue);
                let payload = unsafe {
                    (self.api.assign_payload_attr_get)(self.context, place_attr, rvalue_attr)
                };
                unsafe { (self.api.assign_create)(self.location(span), *index as i64, payload) }
            }
            MirStatement::Unsupported {
                span,
                kind,
                debug,
                op_name,
            } => self.payload_op(span, op_name, kind, self.debug_payload(kind, debug)),
        }
    }

    fn create_terminator(&self, terminator: &MirTerminator) -> MlirOperation {
        match terminator {
            MirTerminator::Return { span } => unsafe {
                (self.api.return_create)(self.location(span))
            },
            MirTerminator::Goto { span, target } => {
                let payload = unsafe {
                    (self.api.target_payload_attr_get)(
                        self.context,
                        mlir_string("Goto"),
                        *target as i64,
                        mlir_string(""),
                    )
                };
                self.payload_op(span, "rust.mir.goto", "Goto", payload)
            }
            MirTerminator::SwitchInt {
                span,
                discr,
                targets,
                debug,
            } => {
                let discr = self.operand_attr(discr);
                let values = targets
                    .branches
                    .iter()
                    .map(|(value, _)| *value)
                    .collect::<Vec<_>>();
                let branch_targets = targets
                    .branches
                    .iter()
                    .map(|(_, target)| *target as i64)
                    .collect::<Vec<_>>();
                let targets_attr = unsafe {
                    (self.api.switch_targets_attr_get)(
                        self.context,
                        targets.otherwise as i64,
                        values.len() as isize,
                        values.as_ptr(),
                        branch_targets.as_ptr(),
                    )
                };
                let payload = unsafe {
                    (self.api.switch_int_payload_attr_get)(
                        self.context,
                        discr,
                        targets_attr,
                        mlir_string(debug),
                    )
                };
                self.payload_op(span, "rust.mir.switch_int", "SwitchInt", payload)
            }
            MirTerminator::Assert {
                span,
                cond,
                expected,
                target,
                debug,
            } => {
                let cond = self.operand_attr(cond);
                let payload = unsafe {
                    (self.api.assert_payload_attr_get)(
                        self.context,
                        cond,
                        *expected,
                        *target as i64,
                        mlir_string(debug),
                    )
                };
                self.payload_op(span, "rust.mir.assert", "Assert", payload)
            }
            MirTerminator::Target {
                span,
                kind,
                target,
                debug,
                op_name,
            } => {
                let payload = unsafe {
                    (self.api.target_payload_attr_get)(
                        self.context,
                        mlir_string(kind),
                        *target as i64,
                        mlir_string(debug),
                    )
                };
                self.payload_op(span, op_name, kind, payload)
            }
            MirTerminator::Unsupported {
                span,
                kind,
                debug,
                op_name,
            } => self.payload_op(span, op_name, kind, self.debug_payload(kind, debug)),
        }
    }

    fn payload_op(
        &self,
        span: &str,
        op_name: &str,
        mir_kind: &str,
        payload: MlirAttribute,
    ) -> MlirOperation {
        unsafe {
            (self.api.payload_op_create)(
                self.location(span),
                mlir_string(op_name),
                mlir_string(mir_kind),
                payload,
            )
        }
    }

    fn location(&self, span: &str) -> MlirLocation {
        unsafe { (self.api.location_from_rust_span)(self.context, mlir_string(span)) }
    }

    fn type_from_debug(&self, ty: &str) -> MlirType {
        unsafe { (self.api.type_from_rustc_public_string)(self.context, mlir_string(ty)) }
    }

    fn type_from_mir(&self, ty: &MirType) -> MlirType {
        match ty {
            MirType::Debug(spelling) => self.type_from_debug(spelling),
            MirType::Tuple(elements) => {
                let element_types = elements
                    .iter()
                    .map(|element| self.type_from_mir(element))
                    .collect::<Vec<_>>();
                unsafe {
                    (self.api.typed_tuple_type_get)(
                        self.context,
                        element_types.len() as isize,
                        element_types.as_ptr(),
                    )
                }
            }
        }
    }

    fn projection_attr(&self, projection: &MirProjection) -> MlirAttribute {
        match projection {
            MirProjection::Field { index, ty } => unsafe {
                (self.api.projection_field_attr_get)(self.context, *index as i64, mlir_string(ty))
            },
            MirProjection::Unsupported { kind, debug } => unsafe {
                (self.api.projection_attr_get)(self.context, mlir_string(kind), mlir_string(debug))
            },
        }
    }

    fn place_attr(&self, place: &MirPlace) -> MlirAttribute {
        let projections = place
            .projection
            .iter()
            .map(|projection| self.projection_attr(projection))
            .collect::<Vec<_>>();
        unsafe {
            (self.api.place_attr_get)(
                self.context,
                place.local as i64,
                projections.len() as isize,
                projections.as_ptr(),
            )
        }
    }

    fn operand_attr(&self, operand: &MirOperand) -> MlirAttribute {
        match operand {
            MirOperand::Copy(place) => {
                let place = self.place_attr(place);
                unsafe { (self.api.operand_copy_attr_get)(self.context, place) }
            }
            MirOperand::Move(place) => {
                let place = self.place_attr(place);
                unsafe { (self.api.operand_move_attr_get)(self.context, place) }
            }
            MirOperand::Constant(constant) => {
                if let Some(value) = constant.value {
                    unsafe {
                        (self.api.operand_constant_i64_attr_get)(
                            self.context,
                            value,
                            mlir_string(&constant.debug),
                            mlir_string(&constant.ty),
                        )
                    }
                } else {
                    unsafe {
                        (self.api.operand_debug_attr_get)(
                            self.context,
                            mlir_string("Constant"),
                            mlir_string(&constant.debug),
                        )
                    }
                }
            }
            MirOperand::RuntimeChecks { debug } => unsafe {
                (self.api.operand_debug_attr_get)(
                    self.context,
                    mlir_string("RuntimeChecks"),
                    mlir_string(debug),
                )
            },
        }
    }

    fn rvalue_attr(&self, rvalue: &MirRvalue) -> MlirAttribute {
        match rvalue {
            MirRvalue::BinaryOp { kind, op, lhs, rhs } => {
                let lhs = self.operand_attr(lhs);
                let rhs = self.operand_attr(rhs);
                unsafe {
                    (self.api.rvalue_binary_op_attr_get)(
                        self.context,
                        mlir_string(kind),
                        mlir_string(op),
                        lhs,
                        rhs,
                    )
                }
            }
            MirRvalue::UnaryOp { kind, op, operand } => {
                let operand = self.operand_attr(operand);
                unsafe {
                    (self.api.rvalue_unary_op_attr_get)(
                        self.context,
                        mlir_string(kind),
                        mlir_string(op),
                        operand,
                    )
                }
            }
            MirRvalue::Aggregate {
                kind,
                aggregate_kind,
                operands,
            } => {
                let operands = operands
                    .iter()
                    .map(|operand| self.operand_attr(operand))
                    .collect::<Vec<_>>();
                unsafe {
                    (self.api.rvalue_aggregate_attr_get)(
                        self.context,
                        mlir_string(kind),
                        mlir_string(aggregate_kind),
                        operands.len() as isize,
                        operands.as_ptr(),
                    )
                }
            }
            MirRvalue::Use { operand } => {
                let operand = self.operand_attr(operand);
                unsafe { (self.api.rvalue_use_attr_get)(self.context, operand) }
            }
            MirRvalue::Unsupported { kind, debug } => self.debug_payload(kind, debug),
        }
    }

    fn debug_payload(&self, kind: &str, debug: &str) -> MlirAttribute {
        unsafe {
            (self.api.rvalue_debug_attr_get)(self.context, mlir_string(kind), mlir_string(debug))
        }
    }

    fn require_block(&self, block: MlirBlock, owner: &str) -> io::Result<()> {
        if block.ptr.is_null() {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("{owner} did not provide a body block"),
            ));
        }
        Ok(())
    }
}

impl Drop for MlirEmitter {
    fn drop(&mut self) {
        unsafe {
            if !self.module.ptr.is_null() {
                (self.api.module_destroy)(self.module);
            }
            if !self.context.ptr.is_null() {
                (self.api.context_destroy)(self.context);
            }
        }
    }
}

fn merge_text_modules_to_file(
    inputs: &[PathBuf],
    output: &str,
    format: OutputFormat,
) -> io::Result<()> {
    if inputs.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "expected at least one MLIR module to merge",
        ));
    }

    let api = MlirApi::load()?;
    let input_strings = inputs
        .iter()
        .map(|path| path.to_string_lossy().into_owned())
        .collect::<Vec<_>>();
    let input_refs = input_strings
        .iter()
        .map(|path| mlir_string(path))
        .collect::<Vec<_>>();
    let ok = unsafe {
        (api.merge_text_modules_to_file)(
            input_refs.len() as isize,
            input_refs.as_ptr(),
            mlir_string(output),
            matches!(format, OutputFormat::Bytecode),
        )
    };
    if !ok {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            format!("failed to merge cargo MLIR output into {output}"),
        ));
    }
    Ok(())
}

fn variant_name(debug: &str) -> &str {
    let end = debug
        .find(|c: char| c == '(' || c == '{' || c.is_whitespace())
        .unwrap_or(debug.len());
    &debug[..end]
}

fn aggregate_kind_name(kind: &AggregateKind) -> &str {
    match kind {
        AggregateKind::Array(_) => "Array",
        AggregateKind::Tuple => "Tuple",
        AggregateKind::Adt(_, _, _, _, _) => "Adt",
        AggregateKind::Closure(_, _) => "Closure",
        AggregateKind::Coroutine(_, _) => "Coroutine",
        AggregateKind::CoroutineClosure(_, _) => "CoroutineClosure",
        AggregateKind::RawPtr(_, _) => "RawPtr",
    }
}

fn span_string<T: std::fmt::Debug>(span: &T) -> String {
    format!("{span:?}")
}

fn constant_scalar_text(constant: &rustc_public::mir::ConstOperand) -> Option<String> {
    let ConstantKind::Allocated(allocation) = constant.const_.kind() else {
        return None;
    };

    let ty = format!("{:?}", constant.const_.ty());
    if ty.contains("RigidTy(Bool)") {
        return allocation.read_bool().ok().map(|value| value.to_string());
    }
    if ty.contains("RigidTy(Int(") {
        return allocation.read_int().ok().map(|value| value.to_string());
    }
    if ty.contains("RigidTy(Uint(") {
        return allocation.read_uint().ok().map(|value| value.to_string());
    }

    None
}

fn statement_op_name(kind: &str) -> &'static str {
    match kind {
        "Assign" => "rust.mir.assign",
        "FakeRead" => "rust.mir.fake_read",
        "SetDiscriminant" => "rust.mir.set_discriminant",
        "Deinit" => "rust.mir.deinit",
        "StorageLive" => "rust.mir.storage_live",
        "StorageDead" => "rust.mir.storage_dead",
        "Retag" => "rust.mir.retag",
        "PlaceMention" => "rust.mir.place_mention",
        "AscribeUserType" => "rust.mir.ascribe_user_type",
        "Coverage" => "rust.mir.coverage",
        "Intrinsic" => "rust.mir.intrinsic",
        "ConstEvalCounter" => "rust.mir.const_eval_counter",
        "Nop" => "rust.mir.nop",
        _ => "rust.mir.unsupported_statement",
    }
}

fn terminator_op_name(kind: &str) -> &'static str {
    match kind {
        "Goto" => "rust.mir.goto",
        "SwitchInt" => "rust.mir.switch_int",
        "Resume" => "rust.mir.resume",
        "Abort" => "rust.mir.abort",
        "Return" => "rust.mir.return",
        "Unreachable" => "rust.mir.unreachable",
        "Drop" => "rust.mir.drop",
        "Call" => "rust.mir.call",
        "Assert" => "rust.mir.assert",
        "InlineAsm" => "rust.mir.inline_asm",
        _ => "rust.mir.unsupported_terminator",
    }
}

fn parse_options(args: impl IntoIterator<Item = String>) -> Result<Options, String> {
    let mut opts = Options::default();
    let mut iter = args.into_iter().peekable();

    while let Some(arg) = iter.next() {
        if arg == "--" {
            opts.passthrough.extend(iter);
            break;
        }

        match arg.as_str() {
            "--crate-root" => {
                opts.crate_root = iter.next();
                if opts.crate_root.is_none() {
                    return Err("--crate-root requires a path".into());
                }
            }
            "--emit-bytecode" => opts.format = OutputFormat::Bytecode,
            "--cargo" => {
                opts.cargo_dir = iter.next();
                if opts.cargo_dir.is_none() {
                    return Err("--cargo requires a manifest path or package directory".into());
                }
            }
            "-S" => opts.format = OutputFormat::Text,
            "-o" | "--output" => {
                opts.output = iter.next();
                if opts.output.is_none() {
                    return Err("-o/--output requires a path".into());
                }
            }
            _ => return Err(format!("unknown argument: {arg}")),
        }
    }

    match (opts.crate_root.is_some(), opts.cargo_dir.is_some()) {
        (true, false) | (false, true) => {}
        (false, false) => return Err("--crate-root or --cargo is required".into()),
        (true, true) => return Err("--crate-root and --cargo are mutually exclusive".into()),
    }

    Ok(opts)
}

fn cargo_program() -> String {
    env::var("CARGO")
        .ok()
        .or_else(|| option_env!("RUST_TO_LLVM_CARGO_EXECUTABLE").map(str::to_string))
        .unwrap_or_else(|| "cargo".to_string())
}

fn rustc_program() -> String {
    env::var("RUSTC")
        .ok()
        .or_else(|| option_env!("RUST_TO_LLVM_RUSTC_EXECUTABLE").map(str::to_string))
        .unwrap_or_else(|| "rustc".to_string())
}

fn rustc_args_for_crate_root(
    root: &str,
    passthrough: &[String],
) -> io::Result<(Vec<String>, RustcOutputDir)> {
    let out_dir = RustcOutputDir::create()?;
    let mut args = vec![
        rustc_program(),
        root.to_string(),
        "--edition=2021".to_string(),
        "--crate-type=lib".to_string(),
        "--out-dir".to_string(),
        out_dir.path().to_string_lossy().into_owned(),
    ];
    args.extend_from_slice(passthrough);
    Ok((args, out_dir))
}

fn run_rustc_public(args: Vec<String>, output: &str, format: OutputFormat) -> io::Result<()> {
    let mut emit_error = None;
    let result = rustc_public::run!(&args, || {
        let program = MirProgram::collect();
        match MlirEmitter::new().and_then(|emitter| {
            emitter.emit_program(&program)?;
            emitter.write(output, format)
        }) {
            Ok(()) => std::ops::ControlFlow::Continue(()),
            Err(err) => {
                eprintln!("rust-mir-extract: failed to emit MLIR: {err}");
                emit_error = Some(err);
                std::ops::ControlFlow::Break(())
            }
        }
    });

    if let Some(err) = emit_error {
        return Err(err);
    }

    match result {
        Ok(()) => Ok(()),
        Err(err) => Err(io::Error::new(
            io::ErrorKind::Other,
            format!("rustc_public compiler run failed: {err:?}"),
        )),
    }
}

fn cargo_manifest_path(path: &str) -> PathBuf {
    let path = Path::new(path);
    if path.is_dir() {
        return path.join("Cargo.toml");
    }
    path.to_path_buf()
}

fn rustc_arg_value<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    for (index, arg) in args.iter().enumerate() {
        if arg == name {
            return args.get(index + 1).map(String::as_str);
        }
        if let Some(value) = arg
            .strip_prefix(name)
            .and_then(|rest| rest.strip_prefix('='))
        {
            return Some(value);
        }
    }
    None
}

fn sanitized_filename_component(input: &str) -> String {
    input
        .chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() || ch == '_' || ch == '-' {
                ch
            } else {
                '_'
            }
        })
        .collect()
}

fn cargo_fragment_path(args: &[String], out_dir: &Path) -> io::Result<PathBuf> {
    let crate_name = rustc_arg_value(args, "--crate-name").unwrap_or("crate");
    let crate_name = sanitized_filename_component(crate_name);
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(invalid_input)?
        .as_nanos();
    Ok(out_dir.join(format!("{crate_name}-{}-{nonce}.mlir", process::id())))
}

fn is_primary_cargo_rustc_invocation(args: &[String]) -> bool {
    if env::var_os("CARGO_PRIMARY_PACKAGE").is_none() {
        return false;
    }

    !matches!(
        rustc_arg_value(args, "--crate-name"),
        Some("build_script_build")
    )
}

fn run_rustc_passthrough(args: &[String]) -> io::Result<i32> {
    let rustc = args.first().ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "RUSTC_WRAPPER invocation did not include a rustc path",
        )
    })?;
    let status = Command::new(rustc).args(&args[1..]).status()?;
    Ok(status.code().unwrap_or(1))
}

fn run_rustc_wrapper() -> io::Result<i32> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    if !is_primary_cargo_rustc_invocation(&args) {
        return run_rustc_passthrough(&args);
    }

    let out_dir = env::var_os("RUST_TO_LLVM_WRAPPER_OUT_DIR")
        .map(PathBuf::from)
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "RUST_TO_LLVM_WRAPPER_OUT_DIR is required in wrapper mode",
            )
        })?;
    fs::create_dir_all(&out_dir)?;

    let output = cargo_fragment_path(&args, &out_dir)?;
    let output = output.to_string_lossy().into_owned();
    run_rustc_public(args, &output, OutputFormat::Text)?;
    Ok(0)
}

fn run_cargo(opts: &Options, cargo_dir: &str, output: &str) -> io::Result<()> {
    let work_dir = RustcOutputDir::create()?;
    let fragment_dir = work_dir.path().join("mlir-fragments");
    fs::create_dir(&fragment_dir)?;

    let manifest_path = cargo_manifest_path(cargo_dir);
    if !manifest_path.exists() {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("Cargo manifest not found: {}", manifest_path.display()),
        ));
    }

    let mut command = Command::new(cargo_program());
    command
        .arg("rustc")
        .arg("--quiet")
        .arg("--manifest-path")
        .arg(&manifest_path)
        .args(&opts.passthrough)
        .env("RUSTC", rustc_program())
        .env("RUSTC_WRAPPER", env::current_exe()?)
        .env("RUST_TO_LLVM_RUSTC_WRAPPER", "1")
        .env("RUST_TO_LLVM_WRAPPER_OUT_DIR", &fragment_dir)
        .env("CARGO_TARGET_DIR", work_dir.path().join("cargo-target"))
        .stdout(process::Stdio::null());

    let status = command.status()?;
    if !status.success() {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            format!("cargo rustc failed with status {status}"),
        ));
    }

    let mut fragments = fs::read_dir(&fragment_dir)?
        .map(|entry| entry.map(|entry| entry.path()))
        .collect::<io::Result<Vec<_>>>()?;
    fragments.retain(|path| {
        path.extension()
            .is_some_and(|extension| extension == "mlir")
    });
    fragments.sort();

    if fragments.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            "cargo did not compile a primary package target to extract",
        ));
    }

    merge_text_modules_to_file(&fragments, output, opts.format)
}

fn main() {
    if env::var_os("RUST_TO_LLVM_RUSTC_WRAPPER").is_some() {
        match run_rustc_wrapper() {
            Ok(code) => process::exit(code),
            Err(err) => {
                eprintln!("rust-mir-extract wrapper: {err}");
                process::exit(1);
            }
        }
    }

    let opts = match parse_options(env::args().skip(1)) {
        Ok(opts) => opts,
        Err(err) => {
            eprintln!("rust-mir-extract: {err}");
            process::exit(2);
        }
    };

    let output = opts.output.as_deref().unwrap_or("-");
    if let Some(cargo_dir) = opts.cargo_dir.as_deref() {
        if let Err(err) = run_cargo(&opts, cargo_dir, output) {
            eprintln!("rust-mir-extract: {err}");
            process::exit(1);
        }
        return;
    }

    let root = opts.crate_root.as_deref().expect("validated crate root");
    let (rustc_args, _out_dir) = match rustc_args_for_crate_root(root, &opts.passthrough) {
        Ok(args) => args,
        Err(err) => {
            eprintln!("rust-mir-extract: failed to prepare rustc output directory: {err}");
            process::exit(1);
        }
    };
    if let Err(err) = run_rustc_public(rustc_args, output, opts.format) {
        eprintln!("rust-mir-extract: {err}");
        process::exit(1);
    }
}
