#![feature(rustc_private)]

extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_public;
extern crate rustc_public_bridge;

use rustc_public::crate_def::CrateDef;
use rustc_public::mir::alloc::GlobalAlloc;
use rustc_public::mir::mono::Instance;
use rustc_public::mir::{
    AggregateKind, Mutability, Operand, Place, ProjectionElem, Rvalue, Statement, StatementKind,
    TerminatorKind, UnwindAction,
};
use rustc_public::target::{Endian, MachineInfo};
use rustc_public::ty::{
    Abi, AdtDef, Allocation, ConstantKind, FloatTy, GenericArgKind, GenericArgs, IntTy, RigidTy,
    Ty, TyConstKind, TyKind, UintTy,
};
use rustc_public::CrateItem;
use rustc_public_bridge::IndexedVal;
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
type RustMirBoolTypeGet = unsafe extern "C" fn(MlirContext) -> MlirType;
type RustMirCharTypeGet = unsafe extern "C" fn(MlirContext) -> MlirType;
type RustMirUnitTypeGet = unsafe extern "C" fn(MlirContext) -> MlirType;
type RustMirNeverTypeGet = unsafe extern "C" fn(MlirContext) -> MlirType;
type RustMirIntTypeGet = unsafe extern "C" fn(MlirContext, MlirStringRef) -> MlirType;
type RustMirFloatTypeGet = unsafe extern "C" fn(MlirContext, u32) -> MlirType;
type RustMirAdtTypeGetIdentified = unsafe extern "C" fn(MlirContext, MlirStringRef) -> MlirType;
type RustMirAdtTypeSetBody = unsafe extern "C" fn(MlirType, isize, *const MlirType);
type RustMlirFunctionTypeGet =
    unsafe extern "C" fn(MlirContext, isize, *const MlirType, isize, *const MlirType) -> MlirType;
type RustTypedTupleTypeGet = unsafe extern "C" fn(MlirContext, isize, *const MlirType) -> MlirType;
type RustTypedArrayTypeGet = unsafe extern "C" fn(MlirContext, MlirType, u64) -> MlirType;
type RustTypedSliceTypeGet = unsafe extern "C" fn(MlirContext, MlirType) -> MlirType;
type RustTypedRefTypeGet = unsafe extern "C" fn(MlirContext, MlirStringRef, MlirType) -> MlirType;
type RustTypedRawPtrTypeGet =
    unsafe extern "C" fn(MlirContext, MlirStringRef, MlirType) -> MlirType;
type RustMirSwitchTargetsAttrGet =
    unsafe extern "C" fn(MlirContext, i64, isize, *const i64, *const i64) -> MlirAttribute;
type RustMirProjectionCreate =
    unsafe extern "C" fn(MlirLocation, MlirStringRef, MlirStringRef) -> MlirOperation;
type RustMirProjectionDerefCreate = unsafe extern "C" fn(MlirLocation) -> MlirOperation;
type RustMirProjectionFieldCreate =
    unsafe extern "C" fn(MlirLocation, i64, MlirStringRef) -> MlirOperation;
type RustMirProjectionIndexCreate = unsafe extern "C" fn(MlirLocation, i64) -> MlirOperation;
type RustMirProjectionConstantIndexCreate =
    unsafe extern "C" fn(MlirLocation, i64, i64, bool) -> MlirOperation;
type RustMirProjectionSubsliceCreate =
    unsafe extern "C" fn(MlirLocation, i64, i64, bool) -> MlirOperation;
type RustMirProjectionDowncastCreate = unsafe extern "C" fn(MlirLocation, i64) -> MlirOperation;
type RustMirPlaceCreate =
    unsafe extern "C" fn(MlirLocation, i64, isize, *const MlirOperation) -> MlirOperation;
type RustMirCopyCreate = unsafe extern "C" fn(MlirLocation, MlirOperation) -> MlirOperation;
type RustMirMoveCreate = unsafe extern "C" fn(MlirLocation, MlirOperation) -> MlirOperation;
type RustMirConstantI64Create =
    unsafe extern "C" fn(MlirLocation, i64, MlirStringRef, MlirType) -> MlirOperation;
type RustMirConstantCreate =
    unsafe extern "C" fn(MlirLocation, MlirStringRef, MlirType) -> MlirOperation;
type RustMirConstantStringCreate =
    unsafe extern "C" fn(MlirLocation, MlirStringRef, MlirStringRef, MlirType) -> MlirOperation;
type RustMirOperandDebugCreate =
    unsafe extern "C" fn(MlirLocation, MlirStringRef, MlirStringRef) -> MlirOperation;
type RustMirRvalueBinaryOpCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirOperation,
    MlirOperation,
) -> MlirOperation;
type RustMirRvalueUnaryOpCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirOperation,
) -> MlirOperation;
type RustMirRvalueCastCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirOperation,
    MlirStringRef,
    MlirStringRef,
) -> MlirOperation;
type RustMirRvalueAggregateCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    i64,
    MlirStringRef,
    isize,
    *const MlirOperation,
) -> MlirOperation;
type RustMirRvalueCopyForDerefCreate =
    unsafe extern "C" fn(MlirLocation, MlirOperation, MlirStringRef) -> MlirOperation;
type RustMirRvalueRepeatCreate =
    unsafe extern "C" fn(MlirLocation, MlirOperation, i64, MlirStringRef) -> MlirOperation;
type RustMirRvalueUseCreate = unsafe extern "C" fn(MlirLocation, MlirOperation) -> MlirOperation;
type RustMirRvalueLenCreate = unsafe extern "C" fn(MlirLocation, MlirOperation) -> MlirOperation;
type RustMirRvalueDiscriminantCreate =
    unsafe extern "C" fn(MlirLocation, MlirOperation) -> MlirOperation;
type RustMirRvalueRefCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirOperation,
    MlirStringRef,
) -> MlirOperation;
type RustMirRvalueAddressOfCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirOperation,
    MlirStringRef,
) -> MlirOperation;
type RustMirDebugOpCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
) -> MlirOperation;
type RustMirGotoCreate = unsafe extern "C" fn(MlirLocation, i64) -> MlirOperation;
type RustMirSwitchIntCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirOperation,
    MlirAttribute,
    MlirStringRef,
) -> MlirOperation;
type RustMirAssertCreate =
    unsafe extern "C" fn(MlirLocation, MlirOperation, bool, i64, MlirStringRef) -> MlirOperation;
type RustMirDropCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirOperation,
    i64,
    MlirStringRef,
    MlirStringRef,
) -> MlirOperation;
type RustMirCallCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirOperation,
    MlirOperation,
    bool,
    i64,
    MlirStringRef,
    isize,
    *const MlirOperation,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    MlirStringRef,
    bool,
    MlirStringRef,
) -> MlirOperation;
type RustMirTargetTerminatorCreate = unsafe extern "C" fn(
    MlirLocation,
    MlirStringRef,
    MlirStringRef,
    i64,
    MlirStringRef,
) -> MlirOperation;
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
type RustMirAssignCreate =
    unsafe extern "C" fn(MlirLocation, i64, MlirOperation, MlirOperation) -> MlirOperation;
type RustMirReturnCreate = unsafe extern "C" fn(MlirLocation) -> MlirOperation;

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
    mir_bool_type_get: RustMirBoolTypeGet,
    mir_char_type_get: RustMirCharTypeGet,
    mir_unit_type_get: RustMirUnitTypeGet,
    mir_never_type_get: RustMirNeverTypeGet,
    mir_int_type_get: RustMirIntTypeGet,
    mir_float_type_get: RustMirFloatTypeGet,
    mir_adt_type_get_identified: RustMirAdtTypeGetIdentified,
    mir_adt_type_set_body: RustMirAdtTypeSetBody,
    function_type_get: RustMlirFunctionTypeGet,
    typed_tuple_type_get: RustTypedTupleTypeGet,
    typed_array_type_get: RustTypedArrayTypeGet,
    typed_slice_type_get: RustTypedSliceTypeGet,
    typed_ref_type_get: RustTypedRefTypeGet,
    typed_raw_ptr_type_get: RustTypedRawPtrTypeGet,
    switch_targets_attr_get: RustMirSwitchTargetsAttrGet,
    projection_create: RustMirProjectionCreate,
    projection_deref_create: RustMirProjectionDerefCreate,
    projection_field_create: RustMirProjectionFieldCreate,
    projection_index_create: RustMirProjectionIndexCreate,
    projection_constant_index_create: RustMirProjectionConstantIndexCreate,
    projection_subslice_create: RustMirProjectionSubsliceCreate,
    projection_downcast_create: RustMirProjectionDowncastCreate,
    place_create: RustMirPlaceCreate,
    copy_create: RustMirCopyCreate,
    move_create: RustMirMoveCreate,
    constant_i64_create: RustMirConstantI64Create,
    constant_create: RustMirConstantCreate,
    constant_string_create: RustMirConstantStringCreate,
    operand_debug_create: RustMirOperandDebugCreate,
    rvalue_binary_op_create: RustMirRvalueBinaryOpCreate,
    rvalue_unary_op_create: RustMirRvalueUnaryOpCreate,
    rvalue_cast_create: RustMirRvalueCastCreate,
    rvalue_aggregate_create: RustMirRvalueAggregateCreate,
    rvalue_copy_for_deref_create: RustMirRvalueCopyForDerefCreate,
    rvalue_repeat_create: RustMirRvalueRepeatCreate,
    rvalue_use_create: RustMirRvalueUseCreate,
    rvalue_len_create: RustMirRvalueLenCreate,
    rvalue_discriminant_create: RustMirRvalueDiscriminantCreate,
    rvalue_ref_create: RustMirRvalueRefCreate,
    rvalue_address_of_create: RustMirRvalueAddressOfCreate,
    debug_op_create: RustMirDebugOpCreate,
    goto_create: RustMirGotoCreate,
    switch_int_create: RustMirSwitchIntCreate,
    assert_create: RustMirAssertCreate,
    drop_create: RustMirDropCreate,
    call_create: RustMirCallCreate,
    target_terminator_create: RustMirTargetTerminatorCreate,
    func_create: RustMirFuncCreate,
    block_create: RustMirBlockCreate,
    operation_get_body_block: RustMirOperationGetBodyBlock,
    local_create: RustMirLocalCreate,
    assign_create: RustMirAssignCreate,
    return_create: RustMirReturnCreate,
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
                mir_bool_type_get: load_symbol(handle, "rustMirBoolTypeGet")?,
                mir_char_type_get: load_symbol(handle, "rustMirCharTypeGet")?,
                mir_unit_type_get: load_symbol(handle, "rustMirUnitTypeGet")?,
                mir_never_type_get: load_symbol(handle, "rustMirNeverTypeGet")?,
                mir_int_type_get: load_symbol(handle, "rustMirIntTypeGet")?,
                mir_float_type_get: load_symbol(handle, "rustMirFloatTypeGet")?,
                mir_adt_type_get_identified: load_symbol(handle, "rustMirAdtTypeGetIdentified")?,
                mir_adt_type_set_body: load_symbol(handle, "rustMirAdtTypeSetBody")?,
                function_type_get: load_symbol(handle, "rustMlirFunctionTypeGet")?,
                typed_tuple_type_get: load_symbol(handle, "rustTypedTupleTypeGet")?,
                typed_array_type_get: load_symbol(handle, "rustTypedArrayTypeGet")?,
                typed_slice_type_get: load_symbol(handle, "rustTypedSliceTypeGet")?,
                typed_ref_type_get: load_symbol(handle, "rustTypedRefTypeGet")?,
                typed_raw_ptr_type_get: load_symbol(handle, "rustTypedRawPtrTypeGet")?,
                switch_targets_attr_get: load_symbol(handle, "rustMirSwitchTargetsAttrGet")?,
                projection_create: load_symbol(handle, "rustMirProjectionCreate")?,
                projection_deref_create: load_symbol(handle, "rustMirProjectionDerefCreate")?,
                projection_field_create: load_symbol(handle, "rustMirProjectionFieldCreate")?,
                projection_index_create: load_symbol(handle, "rustMirProjectionIndexCreate")?,
                projection_constant_index_create: load_symbol(
                    handle,
                    "rustMirProjectionConstantIndexCreate",
                )?,
                projection_subslice_create: load_symbol(handle, "rustMirProjectionSubsliceCreate")?,
                projection_downcast_create: load_symbol(handle, "rustMirProjectionDowncastCreate")?,
                place_create: load_symbol(handle, "rustMirPlaceCreate")?,
                copy_create: load_symbol(handle, "rustMirCopyCreate")?,
                move_create: load_symbol(handle, "rustMirMoveCreate")?,
                constant_i64_create: load_symbol(handle, "rustMirConstantI64Create")?,
                constant_create: load_symbol(handle, "rustMirConstantCreate")?,
                constant_string_create: load_symbol(handle, "rustMirConstantStringCreate")?,
                operand_debug_create: load_symbol(handle, "rustMirOperandDebugCreate")?,
                rvalue_binary_op_create: load_symbol(handle, "rustMirRvalueBinaryOpCreate")?,
                rvalue_unary_op_create: load_symbol(handle, "rustMirRvalueUnaryOpCreate")?,
                rvalue_cast_create: load_symbol(handle, "rustMirRvalueCastCreate")?,
                rvalue_aggregate_create: load_symbol(handle, "rustMirRvalueAggregateCreate")?,
                rvalue_copy_for_deref_create: load_symbol(
                    handle,
                    "rustMirRvalueCopyForDerefCreate",
                )?,
                rvalue_repeat_create: load_symbol(handle, "rustMirRvalueRepeatCreate")?,
                rvalue_use_create: load_symbol(handle, "rustMirRvalueUseCreate")?,
                rvalue_len_create: load_symbol(handle, "rustMirRvalueLenCreate")?,
                rvalue_discriminant_create: load_symbol(handle, "rustMirRvalueDiscriminantCreate")?,
                rvalue_ref_create: load_symbol(handle, "rustMirRvalueRefCreate")?,
                rvalue_address_of_create: load_symbol(handle, "rustMirRvalueAddressOfCreate")?,
                debug_op_create: load_symbol(handle, "rustMirDebugOpCreate")?,
                goto_create: load_symbol(handle, "rustMirGotoCreate")?,
                switch_int_create: load_symbol(handle, "rustMirSwitchIntCreate")?,
                assert_create: load_symbol(handle, "rustMirAssertCreate")?,
                drop_create: load_symbol(handle, "rustMirDropCreate")?,
                call_create: load_symbol(handle, "rustMirCallCreate")?,
                target_terminator_create: load_symbol(handle, "rustMirTargetTerminatorCreate")?,
                func_create: load_symbol(handle, "rustMirFuncCreate")?,
                block_create: load_symbol(handle, "rustMirBlockCreate")?,
                operation_get_body_block: load_symbol(handle, "rustMirOperationGetBodyBlock")?,
                local_create: load_symbol(handle, "rustMirLocalCreate")?,
                assign_create: load_symbol(handle, "rustMirAssignCreate")?,
                return_create: load_symbol(handle, "rustMirReturnCreate")?,
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

fn mlir_optional_string(text: Option<&str>) -> MlirStringRef {
    match text {
        Some(text) => mlir_string(text),
        None => MlirStringRef {
            data: std::ptr::null(),
            length: 0,
        },
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
    Call {
        span: String,
        func: MirOperand,
        args: Vec<MirOperand>,
        destination: MirPlace,
        target: Option<usize>,
        unwind: String,
        metadata: Option<MirCallMetadata>,
        debug: String,
    },
    Drop {
        span: String,
        place: MirPlace,
        target: usize,
        unwind: String,
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
    Deref,
    Field {
        index: usize,
        ty: String,
    },
    Index {
        local: usize,
    },
    ConstantIndex {
        offset: u64,
        min_length: u64,
        from_end: bool,
    },
    Subslice {
        from: u64,
        to: u64,
        from_end: bool,
    },
    Downcast {
        variant_index: usize,
    },
    Unsupported {
        kind: String,
        debug: String,
    },
}

#[derive(Clone)]
enum MirType {
    Debug(String),
    Bool,
    Char,
    Unit,
    Never,
    /// Carries the canonical Rust spelling (e.g. "i32", "usize"), derived from
    /// the typed `rustc_public` integer kind rather than its Debug rendering.
    Int(&'static str),
    /// Carries the Rust float bit width (e.g. 32 for `f32`, 64 for `f64`).
    Float(u32),
    /// A nominal ADT (struct/enum/union) with its full structure. `name` is the
    /// canonical identity (def path + generic args); `variants` holds each
    /// variant's field types. Built into a recursion-capable identified MLIR
    /// type via `getIdentified`/`setBody`.
    Adt {
        name: String,
        variants: Vec<Vec<MirType>>,
    },
    /// A back-edge to an ADT currently being built (breaks self-referential
    /// recursion); resolved by name to the same identified MLIR type.
    AdtRef(String),
    Tuple(Vec<MirType>),
    Array {
        element: Box<MirType>,
        length: u64,
    },
    Slice {
        element: Box<MirType>,
    },
    Ref {
        pointee: Box<MirType>,
        mutability: String,
    },
    RawPtr {
        pointee: Box<MirType>,
        mutability: String,
    },
    FnPtr {
        inputs: Vec<MirType>,
        output: Box<MirType>,
    },
}

fn reference_mutability(mutability: Mutability) -> &'static str {
    match mutability {
        Mutability::Mut => "mut",
        Mutability::Not => "shared",
    }
}

fn raw_pointer_mutability(mutability: Mutability) -> &'static str {
    match mutability {
        Mutability::Mut => "mut",
        Mutability::Not => "const",
    }
}

fn signed_int_spelling(int_ty: IntTy) -> &'static str {
    match int_ty {
        IntTy::Isize => "isize",
        IntTy::I8 => "i8",
        IntTy::I16 => "i16",
        IntTy::I32 => "i32",
        IntTy::I64 => "i64",
        IntTy::I128 => "i128",
    }
}

fn unsigned_int_spelling(uint_ty: UintTy) -> &'static str {
    match uint_ty {
        UintTy::Usize => "usize",
        UintTy::U8 => "u8",
        UintTy::U16 => "u16",
        UintTy::U32 => "u32",
        UintTy::U64 => "u64",
        UintTy::U128 => "u128",
    }
}

fn float_bit_width(float_ty: FloatTy) -> u32 {
    match float_ty {
        FloatTy::F16 => 16,
        FloatTy::F32 => 32,
        FloatTy::F64 => 64,
        FloatTy::F128 => 128,
    }
}

impl MirType {
    fn spelling(&self) -> String {
        match self {
            Self::Debug(spelling) => spelling.clone(),
            Self::Bool => "bool".to_string(),
            Self::Char => "char".to_string(),
            Self::Unit => "()".to_string(),
            Self::Never => "!".to_string(),
            Self::Int(spelling) => (*spelling).to_string(),
            Self::Float(bit_width) => format!("f{bit_width}"),
            Self::Adt { name, .. } => name.clone(),
            Self::AdtRef(name) => name.clone(),
            Self::Tuple(elements) => {
                let elements = elements
                    .iter()
                    .map(Self::spelling)
                    .collect::<Vec<_>>()
                    .join(", ");
                format!("tuple({elements})")
            }
            Self::Array { element, length } => {
                format!("[{}; {length}]", element.spelling())
            }
            Self::Slice { element } => format!("[{}]", element.spelling()),
            Self::Ref {
                pointee,
                mutability,
            } => format!("&{mutability} {}", pointee.spelling()),
            Self::RawPtr {
                pointee,
                mutability,
            } => format!("*{mutability} {}", pointee.spelling()),
            Self::FnPtr { inputs, output } => {
                let inputs = inputs
                    .iter()
                    .map(Self::spelling)
                    .collect::<Vec<_>>()
                    .join(", ");
                format!("fn({inputs}) -> {}", output.spelling())
            }
        }
    }

    fn from_public(ty: Ty) -> Self {
        let mut building = Vec::new();
        Self::from_public_rec(ty, &mut building)
    }

    fn from_public_rec(ty: Ty, building: &mut Vec<String>) -> Self {
        match ty.kind() {
            TyKind::RigidTy(RigidTy::Bool) => Self::Bool,
            TyKind::RigidTy(RigidTy::Char) => Self::Char,
            TyKind::RigidTy(RigidTy::Int(int_ty)) => Self::Int(signed_int_spelling(int_ty)),
            TyKind::RigidTy(RigidTy::Uint(uint_ty)) => Self::Int(unsigned_int_spelling(uint_ty)),
            TyKind::RigidTy(RigidTy::Float(float_ty)) => Self::Float(float_bit_width(float_ty)),
            TyKind::RigidTy(RigidTy::Never) => Self::Never,
            TyKind::RigidTy(RigidTy::Tuple(elements)) if elements.is_empty() => Self::Unit,
            TyKind::RigidTy(RigidTy::Tuple(elements)) => Self::Tuple(
                elements
                    .iter()
                    .map(|element| Self::from_public_rec(*element, building))
                    .collect::<Vec<_>>(),
            ),
            TyKind::RigidTy(RigidTy::Array(element, length)) => match length.eval_target_usize() {
                Ok(length) => Self::Array {
                    element: Box::new(Self::from_public_rec(element, building)),
                    length,
                },
                Err(_) => Self::Debug(format!("{ty:?}")),
            },
            TyKind::RigidTy(RigidTy::Slice(element)) => Self::Slice {
                element: Box::new(Self::from_public_rec(element, building)),
            },
            TyKind::RigidTy(RigidTy::Ref(_, pointee, mutability)) => Self::Ref {
                pointee: Box::new(Self::from_public_rec(pointee, building)),
                mutability: reference_mutability(mutability).to_string(),
            },
            TyKind::RigidTy(RigidTy::RawPtr(pointee, mutability)) => Self::RawPtr {
                pointee: Box::new(Self::from_public_rec(pointee, building)),
                mutability: raw_pointer_mutability(mutability).to_string(),
            },
            TyKind::RigidTy(RigidTy::FnPtr(sig)) => {
                let sig = &sig.value;
                Self::FnPtr {
                    inputs: sig
                        .inputs()
                        .iter()
                        .map(|input| Self::from_public_rec(*input, building))
                        .collect::<Vec<_>>(),
                    output: Box::new(Self::from_public_rec(sig.output(), building)),
                }
            }
            TyKind::RigidTy(RigidTy::Adt(def, args)) => {
                Self::adt_from_public(def, &args, building)
            }
            _ => Self::Debug(format!("{ty:?}")),
        }
    }

    fn adt_from_public(def: AdtDef, args: &GenericArgs, building: &mut Vec<String>) -> Self {
        // Ranges keep a flat usize-tuple shape so existing slice-index lowering
        // (which reads start/end as usize fields) is preserved. Detection is by
        // the structured def path, not substring matching of a Debug string.
        if let Some(field_count) = range_field_count(&def.name()) {
            // A fieldless range (`..`) is zero-sized; model it as unit rather
            // than an empty tuple (which has no round-trippable spelling).
            if field_count == 0 {
                return Self::Unit;
            }
            return Self::Tuple((0..field_count).map(|_| Self::Int("usize")).collect());
        }

        let name = adt_key(&def, args);
        if building.iter().any(|pending| pending == &name) {
            return Self::AdtRef(name);
        }

        building.push(name.clone());
        let variants = def
            .variants()
            .iter()
            .map(|variant| {
                variant
                    .fields()
                    .iter()
                    .map(|field| Self::from_public_rec(field.ty_with_args(args), building))
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        building.pop();
        Self::Adt { name, variants }
    }
}

/// Number of usize fields used to model a `core::ops::Range*` family type, or
/// `None` if the def path is not a range type.
fn range_field_count(def_name: &str) -> Option<usize> {
    let segment = def_name.rsplit("::").next().unwrap_or(def_name);
    let segment = segment.split('<').next().unwrap_or(segment);
    match segment {
        "Range" | "RangeInclusive" => Some(2),
        "RangeFrom" | "RangeTo" | "RangeToInclusive" => Some(1),
        "RangeFull" => Some(0),
        _ => None,
    }
}

/// Canonical identity for an ADT: its def path plus a structural encoding of the
/// type generic arguments, so each monomorphization uniques to a distinct type.
fn adt_key(def: &AdtDef, args: &GenericArgs) -> String {
    let name = def.name();
    let base = name.split('<').next().unwrap_or(&name);
    let type_args: Vec<String> = args
        .0
        .iter()
        .filter_map(|arg| match arg {
            GenericArgKind::Type(ty) => Some(type_identity(*ty)),
            _ => None,
        })
        .collect();
    if type_args.is_empty() {
        base.to_string()
    } else {
        format!("{base}<{}>", type_args.join(", "))
    }
}

/// Lightweight type identity used to build ADT keys: recurses through generic
/// arguments by identity only (never expands ADT bodies).
fn type_identity(ty: Ty) -> String {
    match ty.kind() {
        TyKind::RigidTy(RigidTy::Bool) => "bool".to_string(),
        TyKind::RigidTy(RigidTy::Char) => "char".to_string(),
        TyKind::RigidTy(RigidTy::Int(int_ty)) => signed_int_spelling(int_ty).to_string(),
        TyKind::RigidTy(RigidTy::Uint(uint_ty)) => unsigned_int_spelling(uint_ty).to_string(),
        TyKind::RigidTy(RigidTy::Float(float_ty)) => format!("f{}", float_bit_width(float_ty)),
        TyKind::RigidTy(RigidTy::Never) => "!".to_string(),
        TyKind::RigidTy(RigidTy::Tuple(elements)) if elements.is_empty() => "()".to_string(),
        TyKind::RigidTy(RigidTy::Tuple(elements)) => format!(
            "({})",
            elements
                .iter()
                .map(|element| type_identity(*element))
                .collect::<Vec<_>>()
                .join(", ")
        ),
        TyKind::RigidTy(RigidTy::Array(element, length)) => {
            let length = length
                .eval_target_usize()
                .map(|value| value.to_string())
                .unwrap_or_else(|_| "_".to_string());
            format!("[{}; {length}]", type_identity(element))
        }
        TyKind::RigidTy(RigidTy::Slice(element)) => format!("[{}]", type_identity(element)),
        TyKind::RigidTy(RigidTy::Ref(_, pointee, mutability)) => {
            format!("&{} {}", reference_mutability(mutability), type_identity(pointee))
        }
        TyKind::RigidTy(RigidTy::RawPtr(pointee, mutability)) => {
            format!("*{} {}", raw_pointer_mutability(mutability), type_identity(pointee))
        }
        TyKind::RigidTy(RigidTy::FnPtr(sig)) => {
            let sig = &sig.value;
            let inputs = sig
                .inputs()
                .iter()
                .map(|input| type_identity(*input))
                .collect::<Vec<_>>()
                .join(", ");
            format!("fn({inputs}) -> {}", type_identity(sig.output()))
        }
        TyKind::RigidTy(RigidTy::Adt(def, args)) => adt_key(&def, &args),
        _ => format!("{ty:?}"),
    }
}

enum MirOperand {
    Copy(MirPlace),
    Move(MirPlace),
    Constant(MirConstant),
    RuntimeChecks { debug: String },
}

struct MirConstant {
    ty: MirType,
    debug: String,
    value: Option<i64>,
    string: Option<String>,
}

#[derive(Clone)]
struct MirCallMetadata {
    name: String,
    // These are rustc debug/provenance strings, not canonical call identity.
    // Keep them isolated until the call ABI path carries structured DefId,
    // generic args, and function type metadata.
    def: String,
    ty: String,
    generic_args: String,
    inputs: String,
    output: String,
    abi: String,
    c_variadic: bool,
    // Structured range-expression kind when this is a slice/array index call,
    // derived from the callee's generic arguments (the enum symbol, e.g.
    // "FromToInclusive"); `None` for non-range calls.
    range_kind: Option<String>,
}

/// Maps a `core::ops::Range*` def path to the dialect range-kind enum symbol.
fn range_kind_symbol(generic_args: &GenericArgs) -> Option<&'static str> {
    for arg in &generic_args.0 {
        let GenericArgKind::Type(ty) = arg else {
            continue;
        };
        let TyKind::RigidTy(RigidTy::Adt(def, _)) = ty.kind() else {
            continue;
        };
        let name = def.name();
        let segment = name.rsplit("::").next().unwrap_or(&name);
        let segment = segment.split('<').next().unwrap_or(segment);
        return Some(match segment {
            "RangeFull" => "Full",
            "Range" => "FromTo",
            "RangeFrom" => "From",
            "RangeTo" => "To",
            "RangeInclusive" => "FromToInclusive",
            "RangeToInclusive" => "ToInclusive",
            _ => continue,
        });
    }
    None
}

impl MirCallMetadata {
    fn from_operand(operand: &Operand) -> Option<Self> {
        let Operand::Constant(constant) = operand else {
            return None;
        };

        let ty = constant.const_.ty();
        let kind = ty.kind();
        let (def, generic_args) = kind.fn_def()?;
        let sig = kind.fn_sig()?;
        let value = &sig.value;
        let inputs = format!("{} input(s)", value.inputs().len());

        // rustc's mangled name is the stable, canonical identity for the
        // resolved callee instance. For unresolvable callees (e.g. a generic
        // body's polymorphic call) fall back to the readable def path.
        let mangled = Instance::resolve(def, &generic_args)
            .ok()
            .map(|instance| instance.mangled_name().to_string())
            .unwrap_or_else(|| def.name().to_string());

        let range_kind = range_kind_symbol(&generic_args).map(String::from);

        Some(Self {
            name: def.name().to_string(),
            def: def.name().to_string(),
            ty: mangled,
            generic_args: format!("{generic_args:?}"),
            inputs,
            output: "output".to_string(),
            abi: normalize_abi(&value.abi),
            c_variadic: value.c_variadic,
            range_kind,
        })
    }
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
    Cast {
        kind: String,
        cast_kind: String,
        operand: Box<MirOperand>,
        ty: MirType,
        debug: String,
    },
    Aggregate {
        kind: String,
        aggregate_kind: String,
        variant_index: Option<usize>,
        discriminant: Option<String>,
        operands: Vec<MirOperand>,
    },
    CopyForDeref {
        place: MirPlace,
        debug: String,
    },
    Repeat {
        operand: Box<MirOperand>,
        count: u64,
        debug: String,
    },
    Use {
        operand: Box<MirOperand>,
    },
    Len {
        place: MirPlace,
    },
    Discriminant {
        place: MirPlace,
    },
    Ref {
        region: String,
        borrow_kind: String,
        mutability: String,
        place: MirPlace,
        debug: String,
    },
    AddressOf {
        raw_ptr_kind: String,
        mutability: String,
        place: MirPlace,
        debug: String,
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
            TerminatorKind::Drop {
                place,
                target,
                unwind,
            } => Self::Drop {
                span,
                place: MirPlace::from_public(place),
                target: *target,
                unwind: unwind_action_symbol(unwind).to_string(),
                debug: format!("{terminator:?}"),
            },
            TerminatorKind::Call {
                func,
                args,
                destination,
                target,
                unwind,
            } => Self::Call {
                span,
                func: MirOperand::from_public(func),
                args: args.iter().map(MirOperand::from_public).collect(),
                destination: MirPlace::from_public(destination),
                target: *target,
                unwind: unwind_action_symbol(unwind).to_string(),
                metadata: MirCallMetadata::from_operand(func),
                debug: format!("{terminator:?}"),
            },
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
            ProjectionElem::Deref => Self::Deref,
            ProjectionElem::Field(index, ty) => Self::Field {
                index: *index,
                ty: MirType::from_public(*ty).spelling(),
            },
            ProjectionElem::Index(local) => Self::Index { local: *local },
            ProjectionElem::ConstantIndex {
                offset,
                min_length,
                from_end,
            } => Self::ConstantIndex {
                offset: *offset,
                min_length: *min_length,
                from_end: *from_end,
            },
            ProjectionElem::Subslice { from, to, from_end } => Self::Subslice {
                from: *from,
                to: *to,
                from_end: *from_end,
            },
            ProjectionElem::Downcast(variant_index) => Self::Downcast {
                variant_index: variant_index.to_index(),
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
            Operand::Constant(constant) => {
                let scalar = constant_scalar_text(constant);
                let string = constant_string_text(constant);
                Self::Constant(MirConstant {
                    ty: MirType::from_public(constant.const_.ty()),
                    debug: string
                        .as_ref()
                        .or(scalar.as_ref())
                        .cloned()
                        .unwrap_or_else(|| format!("{:?}", constant.const_)),
                    value: scalar.and_then(|text| text.parse::<i64>().ok()),
                    string,
                })
            }
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
            Rvalue::Cast(cast_kind, operand, ty) => {
                let debug = format!("{rvalue:?}");
                let cast_debug = format!("{cast_kind:?}");
                Self::Cast {
                    kind: "Cast".to_string(),
                    cast_kind: variant_name(&cast_debug).to_string(),
                    operand: Box::new(MirOperand::from_public(operand)),
                    ty: MirType::from_public(*ty),
                    debug,
                }
            }
            Rvalue::Aggregate(kind, operands) => {
                let (variant_index, discriminant) = match kind {
                    AggregateKind::Adt(def, variant_index, _, _, _) if def.kind().is_enum() => {
                        let discr = def.discriminant_for_variant(*variant_index);
                        (
                            Some(variant_index.to_index()),
                            Some(discriminant_text(&discr)),
                        )
                    }
                    _ => (None, None),
                };
                Self::Aggregate {
                    kind: "Aggregate".to_string(),
                    aggregate_kind: aggregate_kind_name(kind).to_string(),
                    variant_index,
                    discriminant,
                    operands: operands.iter().map(MirOperand::from_public).collect(),
                }
            }
            Rvalue::CopyForDeref(place) => Self::CopyForDeref {
                place: MirPlace::from_public(place),
                debug: format!("{rvalue:?}"),
            },
            Rvalue::Repeat(operand, count) => {
                let debug = format!("{rvalue:?}");
                match count.eval_target_usize() {
                    Ok(count) => Self::Repeat {
                        operand: Box::new(MirOperand::from_public(operand)),
                        count,
                        debug,
                    },
                    Err(_) => Self::Unsupported {
                        kind: "Repeat".to_string(),
                        debug,
                    },
                }
            }
            Rvalue::Use(operand) => Self::Use {
                operand: Box::new(MirOperand::from_public(operand)),
            },
            Rvalue::Len(place) => Self::Len {
                place: MirPlace::from_public(place),
            },
            Rvalue::Discriminant(place) => Self::Discriminant {
                place: MirPlace::from_public(place),
            },
            Rvalue::Ref(region, borrow_kind, place) => {
                let debug = format!("{rvalue:?}");
                let borrow_debug = format!("{borrow_kind:?}");
                Self::Ref {
                    region: format!("{region:?}"),
                    borrow_kind: variant_name(&borrow_debug).to_string(),
                    mutability: reference_mutability(borrow_kind.to_mutable_lossy()).to_string(),
                    place: MirPlace::from_public(place),
                    debug,
                }
            }
            Rvalue::AddressOf(raw_ptr_kind, place) => {
                let debug = format!("{rvalue:?}");
                let kind_debug = format!("{raw_ptr_kind:?}");
                Self::AddressOf {
                    raw_ptr_kind: variant_name(&kind_debug).to_string(),
                    mutability: raw_pointer_mutability(raw_ptr_kind.to_mutable_lossy()).to_string(),
                    place: MirPlace::from_public(place),
                    debug,
                }
            }
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
                let place = self.place_op(place, span);
                let rvalue = self.rvalue_op(rvalue, span);
                unsafe {
                    (self.api.assign_create)(self.location(span), *index as i64, place, rvalue)
                }
            }
            MirStatement::Unsupported {
                span,
                kind,
                debug,
                op_name,
            } => self.debug_op(span, op_name, kind, debug),
        }
    }

    fn create_terminator(&self, terminator: &MirTerminator) -> MlirOperation {
        match terminator {
            MirTerminator::Return { span } => unsafe {
                (self.api.return_create)(self.location(span))
            },
            MirTerminator::Goto { span, target } => unsafe {
                (self.api.goto_create)(self.location(span), *target as i64)
            },
            MirTerminator::SwitchInt {
                span,
                discr,
                targets,
                debug,
            } => {
                let discr = self.operand_op(discr, span);
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
                unsafe {
                    (self.api.switch_int_create)(
                        self.location(span),
                        discr,
                        targets_attr,
                        mlir_string(debug),
                    )
                }
            }
            MirTerminator::Assert {
                span,
                cond,
                expected,
                target,
                debug,
            } => {
                let cond = self.operand_op(cond, span);
                unsafe {
                    (self.api.assert_create)(
                        self.location(span),
                        cond,
                        *expected,
                        *target as i64,
                        mlir_string(debug),
                    )
                }
            }
            MirTerminator::Call {
                span,
                func,
                args,
                destination,
                target,
                unwind,
                metadata,
                debug,
            } => {
                let func = self.operand_op(func, span);
                let destination = self.place_op(destination, span);
                let args = args
                    .iter()
                    .map(|arg| self.operand_op(arg, span))
                    .collect::<Vec<_>>();
                unsafe {
                    (self.api.call_create)(
                        self.location(span),
                        func,
                        destination,
                        target.is_some(),
                        target.unwrap_or(0) as i64,
                        mlir_string(unwind),
                        args.len() as isize,
                        args.as_ptr(),
                        mlir_string(debug),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.name.as_str()),
                        ),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.def.as_str()),
                        ),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.ty.as_str()),
                        ),
                        mlir_optional_string(
                            metadata
                                .as_ref()
                                .map(|metadata| metadata.generic_args.as_str()),
                        ),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.inputs.as_str()),
                        ),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.output.as_str()),
                        ),
                        mlir_optional_string(
                            metadata.as_ref().map(|metadata| metadata.abi.as_str()),
                        ),
                        metadata
                            .as_ref()
                            .map(|metadata| metadata.c_variadic)
                            .unwrap_or(false),
                        mlir_optional_string(
                            metadata
                                .as_ref()
                                .and_then(|metadata| metadata.range_kind.as_deref()),
                        ),
                    )
                }
            }
            MirTerminator::Drop {
                span,
                place,
                target,
                unwind,
                debug,
            } => {
                let place = self.place_op(place, span);
                unsafe {
                    (self.api.drop_create)(
                        self.location(span),
                        place,
                        *target as i64,
                        mlir_string(unwind),
                        mlir_string(debug),
                    )
                }
            }
            MirTerminator::Target {
                span,
                kind,
                target,
                debug,
                op_name,
            } => unsafe {
                (self.api.target_terminator_create)(
                    self.location(span),
                    mlir_string(op_name),
                    mlir_string(kind),
                    *target as i64,
                    mlir_string(debug),
                )
            },
            MirTerminator::Unsupported {
                span,
                kind,
                debug,
                op_name,
            } => self.debug_op(span, op_name, kind, debug),
        }
    }

    fn debug_op(&self, span: &str, op_name: &str, mir_kind: &str, debug: &str) -> MlirOperation {
        unsafe {
            (self.api.debug_op_create)(
                self.location(span),
                mlir_string(op_name),
                mlir_string(mir_kind),
                mlir_string(debug),
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
            MirType::Bool => unsafe { (self.api.mir_bool_type_get)(self.context) },
            MirType::Char => unsafe { (self.api.mir_char_type_get)(self.context) },
            MirType::Unit => unsafe { (self.api.mir_unit_type_get)(self.context) },
            MirType::Never => unsafe { (self.api.mir_never_type_get)(self.context) },
            MirType::Int(spelling) => unsafe {
                (self.api.mir_int_type_get)(self.context, mlir_string(spelling))
            },
            MirType::Float(bit_width) => unsafe {
                (self.api.mir_float_type_get)(self.context, *bit_width)
            },
            MirType::Adt { name, variants } => {
                // Create the identified handle first so self-referential field
                // types (AdtRef) resolve to the same type, then set the body.
                let adt = unsafe {
                    (self.api.mir_adt_type_get_identified)(self.context, mlir_string(name))
                };
                let variant_types = variants
                    .iter()
                    .map(|fields| {
                        // A zero-field variant is zero-sized: use unit, since an
                        // empty tuple type has no round-trippable spelling.
                        if fields.is_empty() {
                            return unsafe { (self.api.mir_unit_type_get)(self.context) };
                        }
                        let field_types = fields
                            .iter()
                            .map(|field| self.type_from_mir(field))
                            .collect::<Vec<_>>();
                        unsafe {
                            (self.api.typed_tuple_type_get)(
                                self.context,
                                field_types.len() as isize,
                                field_types.as_ptr(),
                            )
                        }
                    })
                    .collect::<Vec<_>>();
                unsafe {
                    (self.api.mir_adt_type_set_body)(
                        adt,
                        variant_types.len() as isize,
                        variant_types.as_ptr(),
                    );
                }
                adt
            }
            MirType::AdtRef(name) => unsafe {
                (self.api.mir_adt_type_get_identified)(self.context, mlir_string(name))
            },
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
            MirType::Array { element, length } => {
                let element_type = self.type_from_mir(element);
                unsafe { (self.api.typed_array_type_get)(self.context, element_type, *length) }
            }
            MirType::Slice { element } => {
                let element_type = self.type_from_mir(element);
                unsafe { (self.api.typed_slice_type_get)(self.context, element_type) }
            }
            MirType::Ref {
                pointee,
                mutability,
            } => {
                let pointee_type = self.type_from_mir(pointee);
                unsafe {
                    (self.api.typed_ref_type_get)(
                        self.context,
                        mlir_string(mutability),
                        pointee_type,
                    )
                }
            }
            MirType::RawPtr {
                pointee,
                mutability,
            } => {
                let pointee_type = self.type_from_mir(pointee);
                unsafe {
                    (self.api.typed_raw_ptr_type_get)(
                        self.context,
                        mlir_string(mutability),
                        pointee_type,
                    )
                }
            }
            MirType::FnPtr { inputs, output } => {
                let input_types = inputs
                    .iter()
                    .map(|input| self.type_from_mir(input))
                    .collect::<Vec<_>>();
                let result_types = if matches!(**output, MirType::Unit | MirType::Never) {
                    Vec::new()
                } else {
                    vec![self.type_from_mir(output)]
                };
                unsafe {
                    (self.api.function_type_get)(
                        self.context,
                        input_types.len() as isize,
                        input_types.as_ptr(),
                        result_types.len() as isize,
                        result_types.as_ptr(),
                    )
                }
            }
        }
    }

    fn projection_op(&self, projection: &MirProjection, span: &str) -> MlirOperation {
        match projection {
            MirProjection::Deref => unsafe {
                (self.api.projection_deref_create)(self.location(span))
            },
            MirProjection::Field { index, ty } => unsafe {
                (self.api.projection_field_create)(
                    self.location(span),
                    *index as i64,
                    mlir_string(ty),
                )
            },
            MirProjection::Index { local } => unsafe {
                (self.api.projection_index_create)(self.location(span), *local as i64)
            },
            MirProjection::ConstantIndex {
                offset,
                min_length,
                from_end,
            } => unsafe {
                (self.api.projection_constant_index_create)(
                    self.location(span),
                    *offset as i64,
                    *min_length as i64,
                    *from_end,
                )
            },
            MirProjection::Subslice { from, to, from_end } => unsafe {
                (self.api.projection_subslice_create)(
                    self.location(span),
                    *from as i64,
                    *to as i64,
                    *from_end,
                )
            },
            MirProjection::Downcast { variant_index } => unsafe {
                (self.api.projection_downcast_create)(self.location(span), *variant_index as i64)
            },
            MirProjection::Unsupported { kind, debug } => unsafe {
                (self.api.projection_create)(
                    self.location(span),
                    mlir_string(kind),
                    mlir_string(debug),
                )
            },
        }
    }

    fn place_op(&self, place: &MirPlace, span: &str) -> MlirOperation {
        let projections = place
            .projection
            .iter()
            .map(|projection| self.projection_op(projection, span))
            .collect::<Vec<_>>();
        unsafe {
            (self.api.place_create)(
                self.location(span),
                place.local as i64,
                projections.len() as isize,
                projections.as_ptr(),
            )
        }
    }

    fn operand_op(&self, operand: &MirOperand, span: &str) -> MlirOperation {
        match operand {
            MirOperand::Copy(place) => {
                let place = self.place_op(place, span);
                unsafe { (self.api.copy_create)(self.location(span), place) }
            }
            MirOperand::Move(place) => {
                let place = self.place_op(place, span);
                unsafe { (self.api.move_create)(self.location(span), place) }
            }
            MirOperand::Constant(constant) => {
                let ty = self.type_from_mir(&constant.ty);
                if let Some(value) = constant.value {
                    unsafe {
                        (self.api.constant_i64_create)(
                            self.location(span),
                            value,
                            mlir_string(&constant.debug),
                            ty,
                        )
                    }
                } else if let Some(value) = &constant.string {
                    unsafe {
                        (self.api.constant_string_create)(
                            self.location(span),
                            mlir_string(value),
                            mlir_string(&constant.debug),
                            ty,
                        )
                    }
                } else {
                    unsafe {
                        (self.api.constant_create)(
                            self.location(span),
                            mlir_string(&constant.debug),
                            ty,
                        )
                    }
                }
            }
            MirOperand::RuntimeChecks { debug } => unsafe {
                (self.api.operand_debug_create)(
                    self.location(span),
                    mlir_string("RuntimeChecks"),
                    mlir_string(debug),
                )
            },
        }
    }

    fn rvalue_op(&self, rvalue: &MirRvalue, span: &str) -> MlirOperation {
        match rvalue {
            MirRvalue::BinaryOp { kind, op, lhs, rhs } => {
                let lhs = self.operand_op(lhs, span);
                let rhs = self.operand_op(rhs, span);
                unsafe {
                    (self.api.rvalue_binary_op_create)(
                        self.location(span),
                        mlir_string(kind),
                        mlir_string(op),
                        lhs,
                        rhs,
                    )
                }
            }
            MirRvalue::UnaryOp { kind, op, operand } => {
                let operand = self.operand_op(operand, span);
                unsafe {
                    (self.api.rvalue_unary_op_create)(
                        self.location(span),
                        mlir_string(kind),
                        mlir_string(op),
                        operand,
                    )
                }
            }
            MirRvalue::Cast {
                kind,
                cast_kind,
                operand,
                ty,
                debug,
            } => {
                let operand = self.operand_op(operand, span);
                let ty = ty.spelling();
                unsafe {
                    (self.api.rvalue_cast_create)(
                        self.location(span),
                        mlir_string(kind),
                        mlir_string(cast_kind),
                        operand,
                        mlir_string(&ty),
                        mlir_string(debug),
                    )
                }
            }
            MirRvalue::Aggregate {
                kind,
                aggregate_kind,
                variant_index,
                discriminant,
                operands,
            } => {
                let operands = operands
                    .iter()
                    .map(|operand| self.operand_op(operand, span))
                    .collect::<Vec<_>>();
                let variant_index = variant_index.map(|index| index as i64).unwrap_or(-1);
                let discriminant = discriminant.as_deref().unwrap_or("");
                unsafe {
                    (self.api.rvalue_aggregate_create)(
                        self.location(span),
                        mlir_string(kind),
                        mlir_string(aggregate_kind),
                        variant_index,
                        mlir_string(discriminant),
                        operands.len() as isize,
                        operands.as_ptr(),
                    )
                }
            }
            MirRvalue::CopyForDeref { place, debug } => {
                let place = self.place_op(place, span);
                unsafe {
                    (self.api.rvalue_copy_for_deref_create)(
                        self.location(span),
                        place,
                        mlir_string(debug),
                    )
                }
            }
            MirRvalue::Repeat {
                operand,
                count,
                debug,
            } => {
                let operand = self.operand_op(operand, span);
                unsafe {
                    (self.api.rvalue_repeat_create)(
                        self.location(span),
                        operand,
                        *count as i64,
                        mlir_string(debug),
                    )
                }
            }
            MirRvalue::Use { operand } => {
                let operand = self.operand_op(operand, span);
                unsafe { (self.api.rvalue_use_create)(self.location(span), operand) }
            }
            MirRvalue::Len { place } => {
                let place = self.place_op(place, span);
                unsafe { (self.api.rvalue_len_create)(self.location(span), place) }
            }
            MirRvalue::Discriminant { place } => {
                let place = self.place_op(place, span);
                unsafe { (self.api.rvalue_discriminant_create)(self.location(span), place) }
            }
            MirRvalue::Ref {
                region,
                borrow_kind,
                mutability,
                place,
                debug,
            } => {
                let place = self.place_op(place, span);
                unsafe {
                    (self.api.rvalue_ref_create)(
                        self.location(span),
                        mlir_string(region),
                        mlir_string(borrow_kind),
                        mlir_string(mutability),
                        place,
                        mlir_string(debug),
                    )
                }
            }
            MirRvalue::AddressOf {
                raw_ptr_kind,
                mutability,
                place,
                debug,
            } => {
                let place = self.place_op(place, span);
                unsafe {
                    (self.api.rvalue_address_of_create)(
                        self.location(span),
                        mlir_string(raw_ptr_kind),
                        mlir_string(mutability),
                        place,
                        mlir_string(debug),
                    )
                }
            }
            MirRvalue::Unsupported { kind, debug } => {
                self.debug_op(span, rvalue_op_name(kind), kind, debug)
            }
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

fn normalize_abi(abi: &Abi) -> String {
    match abi {
        Abi::Rust => "rust".to_string(),
        Abi::C { .. } => "c".to_string(),
        _ => format!("{abi:?}"),
    }
}

fn unwind_action_symbol(unwind: &UnwindAction) -> &'static str {
    match unwind {
        UnwindAction::Continue => "Continue",
        UnwindAction::Unreachable => "Unreachable",
        UnwindAction::Terminate => "Terminate",
        UnwindAction::Cleanup(_) => "Cleanup",
    }
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

fn scalar_text_from_allocation(ty: &str, allocation: &Allocation) -> Option<String> {
    if ty.contains("RigidTy(Bool)") {
        return allocation.read_bool().ok().map(|value| value.to_string());
    }
    if ty.contains("RigidTy(Int(") {
        return allocation.read_int().ok().map(|value| value.to_string());
    }
    if ty.contains("RigidTy(Uint(") {
        return allocation.read_uint().ok().map(|value| value.to_string());
    }
    if ty.contains("RigidTy(Float(F32))") {
        let bits = allocation.read_uint().ok()? as u32;
        return Some(f32::from_bits(bits).to_string());
    }
    if ty.contains("RigidTy(Float(F64))") {
        let bits = allocation.read_uint().ok()? as u64;
        return Some(f64::from_bits(bits).to_string());
    }

    None
}

fn constant_scalar_text(constant: &rustc_public::mir::ConstOperand) -> Option<String> {
    match constant.const_.kind() {
        ConstantKind::Allocated(allocation) => {
            let ty = format!("{:?}", constant.const_.ty());
            scalar_text_from_allocation(&ty, &allocation)
        }
        ConstantKind::Ty(ty_const) => {
            let TyConstKind::Value(ty, allocation) = ty_const.kind() else {
                return None;
            };
            let ty = format!("{ty:?}");
            scalar_text_from_allocation(&ty, allocation)
        }
        _ => None,
    }
}

fn constant_string_text(constant: &rustc_public::mir::ConstOperand) -> Option<String> {
    let ConstantKind::Allocated(allocation) = constant.const_.kind() else {
        return None;
    };

    let ty = format!("{:?}", constant.const_.ty());
    if !ty.contains("RigidTy(Ref(") || !ty.contains("RigidTy(Str)") {
        return None;
    }

    let pointer_width = MachineInfo::target_pointer_width().bytes();
    let data_offset: usize = allocation
        .read_partial_uint(0..pointer_width)
        .ok()?
        .try_into()
        .ok()?;
    let len: usize = allocation
        .read_partial_uint(pointer_width..(pointer_width * 2))
        .ok()?
        .try_into()
        .ok()?;
    let data_alloc_id = allocation
        .provenance
        .ptrs
        .iter()
        .find_map(|(offset, provenance)| (*offset == 0).then_some(provenance.0))?;
    let GlobalAlloc::Memory(data_alloc) = GlobalAlloc::from(data_alloc_id) else {
        return None;
    };
    let data = data_alloc.raw_bytes().ok()?;
    let end = data_offset.checked_add(len)?;
    String::from_utf8(data.get(data_offset..end)?.to_vec()).ok()
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

fn discriminant_text(discr: &rustc_public::ty::Discr) -> String {
    discr.val.to_string()
}

fn rvalue_op_name(kind: &str) -> &'static str {
    match kind {
        "AddressOf" => "rust.mir.address_of",
        "Aggregate" => "rust.mir.aggregate",
        "BinaryOp" => "rust.mir.binary_op",
        "Cast" => "rust.mir.cast",
        "CheckedBinaryOp" => "rust.mir.checked_binary_op",
        "CopyForDeref" => "rust.mir.copy_for_deref",
        "Discriminant" => "rust.mir.discriminant",
        "Len" => "rust.mir.len",
        "Ref" => "rust.mir.ref",
        "Repeat" => "rust.mir.repeat",
        "ShallowInitBox" => "rust.mir.shallow_init_box",
        "ThreadLocalRef" => "rust.mir.thread_local_ref",
        "NullaryOp" => "rust.mir.nullary_op",
        "UnaryOp" => "rust.mir.unary_op",
        "Use" => "rust.mir.use",
        _ => "rust.mir.unsupported_statement",
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

pub fn run_from_env() -> i32 {
    if env::var_os("RUST_TO_LLVM_RUSTC_WRAPPER").is_some() {
        return match run_rustc_wrapper() {
            Ok(code) => code,
            Err(err) => {
                eprintln!("rust-mir-extract wrapper: {err}");
                1
            }
        };
    }

    run_cli(env::args().skip(1))
}

pub fn run_cli(args: impl IntoIterator<Item = String>) -> i32 {
    let opts = match parse_options(args) {
        Ok(opts) => opts,
        Err(err) => {
            eprintln!("rust-mir-extract: {err}");
            return 2;
        }
    };

    let output = opts.output.as_deref().unwrap_or("-");
    if let Some(cargo_dir) = opts.cargo_dir.as_deref() {
        if let Err(err) = run_cargo(&opts, cargo_dir, output) {
            eprintln!("rust-mir-extract: {err}");
            return 1;
        }
        return 0;
    }

    let root = opts.crate_root.as_deref().expect("validated crate root");
    let (rustc_args, _out_dir) = match rustc_args_for_crate_root(root, &opts.passthrough) {
        Ok(args) => args,
        Err(err) => {
            eprintln!("rust-mir-extract: failed to prepare rustc output directory: {err}");
            return 1;
        }
    };
    if let Err(err) = run_rustc_public(rustc_args, output, opts.format) {
        eprintln!("rust-mir-extract: {err}");
        return 1;
    }

    0
}
