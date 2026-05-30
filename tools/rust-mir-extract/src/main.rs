#![feature(rustc_private)]

extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_public;

use rustc_public::crate_def::CrateDef;
use rustc_public::mir::{
    AggregateKind, AssertMessage, BorrowKind, CastKind, CoroutineDesugaring, CoroutineKind,
    CoroutineSource, FakeBorrowKind, FakeReadCause, MutBorrowKind, Mutability,
    NonDivergingIntrinsic, Operand, Place, PointerCoercion, ProjectionElem, RawPtrKind, RetagKind,
    RuntimeChecks, Rvalue, Safety, Statement, StatementKind, TerminatorKind, UnOp, UnwindAction,
    Variance,
};
use rustc_public::target::{Endian, MachineInfo};
use rustc_public::ty::{ConstantKind, Movability};
use std::env;
use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::{self, Command};

#[derive(Default)]
struct Options {
    crate_root: Option<String>,
    cargo_dir: Option<String>,
    output: Option<String>,
    emit_ndjson: bool,
    passthrough: Vec<String>,
}

fn json_escape(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    for ch in input.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn variant_name(debug: &str) -> &str {
    let end = debug
        .find(|c: char| c == '(' || c == '{' || c.is_whitespace())
        .unwrap_or(debug.len());
    &debug[..end]
}

fn debug_json<T: std::fmt::Debug>(value: &T) -> String {
    json_escape(&format!("{value:?}"))
}

fn emit_debug_variant<W: Write, T: std::fmt::Debug>(
    out: &mut W,
    record: &str,
    value: &T,
) -> io::Result<()> {
    let debug = format!("{value:?}");
    let kind = variant_name(&debug);
    writeln!(
        out,
        "{{\"record\":\"{}\",\"kind\":\"{}\",\"payload\":{{\"debug\":\"{}\"}}}}",
        json_escape(record),
        json_escape(kind),
        json_escape(&debug)
    )
}

fn emit_place<W: Write>(out: &mut W, place: &Place) -> io::Result<()> {
    writeln!(
        out,
        "{{\"record\":\"place\",\"kind\":\"Place\",\"payload\":{}}}",
        place_payload_json(place)
    )?;
    for projection in &place.projection {
        emit_projection_elem(out, projection)?;
    }
    Ok(())
}

fn emit_projection_elem<W: Write>(out: &mut W, elem: &ProjectionElem) -> io::Result<()> {
    let debug = format!("{elem:?}");
    let kind = variant_name(&debug);
    writeln!(
        out,
        "{{\"record\":\"projection_elem\",\"kind\":\"{}\",\"payload\":{}}}",
        json_escape(kind),
        projection_elem_payload_json(elem)
    )
}

fn emit_operand<W: Write>(out: &mut W, operand: &Operand) -> io::Result<()> {
    let debug = format!("{operand:?}");
    let kind = variant_name(&debug);
    writeln!(
        out,
        "{{\"record\":\"operand\",\"kind\":\"{}\",\"payload\":{}}}",
        json_escape(kind),
        operand_payload_json(operand)
    )?;
    match operand {
        Operand::Copy(place) | Operand::Move(place) => emit_place(out, place),
        Operand::Constant(_) => Ok(()),
        Operand::RuntimeChecks(checks) => emit_runtime_checks(out, checks),
    }
}

fn emit_runtime_checks<W: Write>(out: &mut W, checks: &RuntimeChecks) -> io::Result<()> {
    emit_debug_variant(out, "runtime_checks", checks)
}

fn emit_aggregate_kind<W: Write>(out: &mut W, kind: &AggregateKind) -> io::Result<()> {
    emit_debug_variant(out, "aggregate_kind", kind)?;
    match kind {
        AggregateKind::RawPtr(_, mutability) => emit_mutability(out, mutability),
        _ => Ok(()),
    }
}

fn emit_rvalue<W: Write>(out: &mut W, rvalue: &Rvalue) -> io::Result<()> {
    let debug = format!("{rvalue:?}");
    let kind = variant_name(&debug);
    let payload = rvalue_payload_json(rvalue)
        .unwrap_or_else(|| format!("{{\"debug\":\"{}\"}}", json_escape(&debug)));
    writeln!(
        out,
        "{{\"record\":\"rvalue\",\"kind\":\"{}\",\"payload\":{}}}",
        json_escape(kind),
        payload
    )?;
    match rvalue {
        Rvalue::AddressOf(kind, place) => {
            emit_raw_ptr_kind(out, kind)?;
            emit_place(out, place)
        }
        Rvalue::Aggregate(kind, operands) => {
            emit_aggregate_kind(out, kind)?;
            for operand in operands {
                emit_operand(out, operand)?;
            }
            Ok(())
        }
        Rvalue::BinaryOp(op, lhs, rhs) | Rvalue::CheckedBinaryOp(op, lhs, rhs) => {
            emit_binop(out, op)?;
            emit_operand(out, lhs)?;
            emit_operand(out, rhs)
        }
        Rvalue::Cast(kind, operand, _) => {
            emit_cast_kind(out, kind)?;
            emit_operand(out, operand)
        }
        Rvalue::CopyForDeref(place) | Rvalue::Discriminant(place) | Rvalue::Len(place) => {
            emit_place(out, place)
        }
        Rvalue::Ref(_, kind, place) => {
            emit_borrow_kind(out, kind)?;
            emit_place(out, place)
        }
        Rvalue::Repeat(operand, _) | Rvalue::UnaryOp(_, operand) | Rvalue::Use(operand) => {
            if let Rvalue::UnaryOp(op, _) = rvalue {
                emit_unop(out, op)?;
            }
            emit_operand(out, operand)
        }
        Rvalue::ThreadLocalRef(_) => Ok(()),
    }
}

fn emit_binop<W: Write>(out: &mut W, op: &rustc_public::mir::BinOp) -> io::Result<()> {
    emit_debug_variant(out, "binop", op)
}

fn emit_unop<W: Write>(out: &mut W, op: &UnOp) -> io::Result<()> {
    emit_debug_variant(out, "unop", op)
}

fn emit_cast_kind<W: Write>(out: &mut W, kind: &CastKind) -> io::Result<()> {
    emit_debug_variant(out, "cast_kind", kind)?;
    match kind {
        CastKind::PointerCoercion(coercion) => emit_pointer_coercion(out, coercion),
        _ => Ok(()),
    }
}

fn emit_borrow_kind<W: Write>(out: &mut W, kind: &BorrowKind) -> io::Result<()> {
    emit_debug_variant(out, "borrow_kind", kind)?;
    match kind {
        BorrowKind::Fake(fake) => emit_fake_borrow_kind(out, fake),
        BorrowKind::Mut { kind } => emit_mut_borrow_kind(out, kind),
        BorrowKind::Shared => Ok(()),
    }
}

fn emit_raw_ptr_kind<W: Write>(out: &mut W, kind: &RawPtrKind) -> io::Result<()> {
    emit_debug_variant(out, "raw_ptr_kind", kind)
}

fn emit_fake_read_cause<W: Write>(out: &mut W, cause: &FakeReadCause) -> io::Result<()> {
    emit_debug_variant(out, "fake_read_cause", cause)
}

fn emit_retag_kind<W: Write>(out: &mut W, kind: &RetagKind) -> io::Result<()> {
    emit_debug_variant(out, "retag_kind", kind)
}

fn emit_variance<W: Write>(out: &mut W, variance: &Variance) -> io::Result<()> {
    emit_debug_variant(out, "variance", variance)
}

fn emit_intrinsic<W: Write>(out: &mut W, intrinsic: &NonDivergingIntrinsic) -> io::Result<()> {
    emit_debug_variant(out, "non_diverging_intrinsic", intrinsic)?;
    match intrinsic {
        NonDivergingIntrinsic::Assume(operand) => emit_operand(out, operand),
        NonDivergingIntrinsic::CopyNonOverlapping(copy) => {
            emit_operand(out, &copy.src)?;
            emit_operand(out, &copy.dst)?;
            emit_operand(out, &copy.count)
        }
    }
}

fn emit_unwind_action<W: Write>(out: &mut W, unwind: &UnwindAction) -> io::Result<()> {
    emit_debug_variant(out, "unwind_action", unwind)
}

fn emit_assert_message<W: Write>(out: &mut W, msg: &AssertMessage) -> io::Result<()> {
    emit_debug_variant(out, "assert_message", msg)?;
    match msg {
        AssertMessage::BoundsCheck { len, index } => {
            emit_operand(out, len)?;
            emit_operand(out, index)
        }
        AssertMessage::Overflow(op, lhs, rhs) => {
            emit_binop(out, op)?;
            emit_operand(out, lhs)?;
            emit_operand(out, rhs)
        }
        AssertMessage::OverflowNeg(operand)
        | AssertMessage::DivisionByZero(operand)
        | AssertMessage::RemainderByZero(operand)
        | AssertMessage::InvalidEnumConstruction(operand) => emit_operand(out, operand),
        AssertMessage::ResumedAfterReturn(kind)
        | AssertMessage::ResumedAfterPanic(kind)
        | AssertMessage::ResumedAfterDrop(kind) => emit_coroutine_kind(out, kind),
        AssertMessage::MisalignedPointerDereference { required, found } => {
            emit_operand(out, required)?;
            emit_operand(out, found)
        }
        AssertMessage::NullPointerDereference => Ok(()),
    }
}

fn emit_mut_borrow_kind<W: Write>(out: &mut W, kind: &MutBorrowKind) -> io::Result<()> {
    emit_debug_variant(out, "mut_borrow_kind", kind)
}

fn emit_fake_borrow_kind<W: Write>(out: &mut W, kind: &FakeBorrowKind) -> io::Result<()> {
    emit_debug_variant(out, "fake_borrow_kind", kind)
}

fn emit_mutability<W: Write>(out: &mut W, mutability: &Mutability) -> io::Result<()> {
    emit_debug_variant(out, "mutability", mutability)
}

fn emit_safety<W: Write>(out: &mut W, safety: &Safety) -> io::Result<()> {
    emit_debug_variant(out, "safety", safety)
}

fn emit_pointer_coercion<W: Write>(out: &mut W, coercion: &PointerCoercion) -> io::Result<()> {
    emit_debug_variant(out, "pointer_coercion", coercion)?;
    match coercion {
        PointerCoercion::ReifyFnPointer(safety) | PointerCoercion::ClosureFnPointer(safety) => {
            emit_safety(out, safety)
        }
        _ => Ok(()),
    }
}

fn emit_coroutine_kind<W: Write>(out: &mut W, kind: &CoroutineKind) -> io::Result<()> {
    emit_debug_variant(out, "coroutine_kind", kind)?;
    match kind {
        CoroutineKind::Desugared(desugaring, source) => {
            emit_coroutine_desugaring(out, desugaring)?;
            emit_coroutine_source(out, source)
        }
        CoroutineKind::Coroutine(movability) => emit_movability(out, movability),
    }
}

fn emit_coroutine_source<W: Write>(out: &mut W, source: &CoroutineSource) -> io::Result<()> {
    emit_debug_variant(out, "coroutine_source", source)
}

fn emit_coroutine_desugaring<W: Write>(
    out: &mut W,
    desugaring: &CoroutineDesugaring,
) -> io::Result<()> {
    emit_debug_variant(out, "coroutine_desugaring", desugaring)
}

fn emit_movability<W: Write>(out: &mut W, movability: &Movability) -> io::Result<()> {
    emit_debug_variant(out, "movability", movability)
}

fn projection_elem_payload_json(elem: &ProjectionElem) -> String {
    let debug = debug_json(elem);
    match elem {
        ProjectionElem::Deref => format!("{{\"kind\":\"Deref\",\"debug\":\"{}\"}}", debug),
        ProjectionElem::Field(index, ty) => format!(
            "{{\"kind\":\"Field\",\"debug\":\"{}\",\"index\":{},\"ty\":\"{}\"}}",
            debug,
            index,
            debug_json(ty)
        ),
        ProjectionElem::Index(local) => format!(
            "{{\"kind\":\"Index\",\"debug\":\"{}\",\"local\":{}}}",
            debug, local
        ),
        ProjectionElem::ConstantIndex {
            offset,
            min_length,
            from_end,
        } => format!(
            "{{\"kind\":\"ConstantIndex\",\"debug\":\"{}\",\"offset\":{},\"min_length\":{},\"from_end\":{}}}",
            debug, offset, min_length, from_end
        ),
        ProjectionElem::Subslice { from, to, from_end } => format!(
            "{{\"kind\":\"Subslice\",\"debug\":\"{}\",\"from\":{},\"to\":{},\"from_end\":{}}}",
            debug, from, to, from_end
        ),
        ProjectionElem::Downcast(variant) => format!(
            "{{\"kind\":\"Downcast\",\"debug\":\"{}\",\"variant\":\"{}\"}}",
            debug,
            debug_json(variant)
        ),
        ProjectionElem::OpaqueCast(ty) => format!(
            "{{\"kind\":\"OpaqueCast\",\"debug\":\"{}\",\"ty\":\"{}\"}}",
            debug,
            debug_json(ty)
        ),
    }
}

fn place_payload_json(place: &Place) -> String {
    let projection = place
        .projection
        .iter()
        .map(projection_elem_payload_json)
        .collect::<Vec<_>>()
        .join(",");
    format!(
        "{{\"debug\":\"{}\",\"local\":{},\"projection\":[{}]}}",
        debug_json(place),
        place.local,
        projection
    )
}

fn operand_payload_json(operand: &Operand) -> String {
    match operand {
        Operand::Copy(place) => format!(
            "{{\"kind\":\"Copy\",\"debug\":\"{}\",\"place\":{}}}",
            debug_json(operand),
            place_payload_json(place)
        ),
        Operand::Move(place) => format!(
            "{{\"kind\":\"Move\",\"debug\":\"{}\",\"place\":{}}}",
            debug_json(operand),
            place_payload_json(place)
        ),
        Operand::Constant(constant) => format!(
            "{{\"kind\":\"Constant\",{}\"ty\":\"{}\",\"span\":\"{}\"}}",
            constant_value_payload_fields(constant),
            debug_json(&constant.const_.ty()),
            debug_json(&constant.span)
        ),
        Operand::RuntimeChecks(checks) => format!(
            "{{\"kind\":\"RuntimeChecks\",\"debug\":\"{}\",\"checks\":\"{}\"}}",
            debug_json(operand),
            debug_json(checks)
        ),
    }
}

fn constant_value_payload_fields(constant: &rustc_public::mir::ConstOperand) -> String {
    if let Some(text) = constant_scalar_text(constant) {
        let value = text
            .parse::<i64>()
            .map(|value| format!("\"value\":{},", value))
            .unwrap_or_default();
        return format!("{}\"debug\":\"{}\",", value, json_escape(&text));
    }

    format!("\"debug\":\"{}\",", debug_json(&constant.const_))
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

fn rvalue_payload_json(rvalue: &Rvalue) -> Option<String> {
    match rvalue {
        Rvalue::BinaryOp(op, lhs, rhs) | Rvalue::CheckedBinaryOp(op, lhs, rhs) => {
            let op_debug = format!("{op:?}");
            let kind = match rvalue {
                Rvalue::BinaryOp(_, _, _) => "BinaryOp",
                Rvalue::CheckedBinaryOp(_, _, _) => "CheckedBinaryOp",
                _ => unreachable!(),
            };
            Some(format!(
                "{{\"kind\":\"{}\",\"op\":\"{}\",\"lhs\":{},\"rhs\":{}}}",
                kind,
                json_escape(variant_name(&op_debug)),
                operand_payload_json(lhs),
                operand_payload_json(rhs)
            ))
        }
        Rvalue::Use(operand) => Some(format!(
            "{{\"kind\":\"Use\",\"operand\":{}}}",
            operand_payload_json(operand)
        )),
        _ => None,
    }
}

fn statement_payload_json(statement: &Statement, debug: &str) -> String {
    let mut payload = format!("{{\"debug\":\"{}\"", json_escape(debug));
    if let StatementKind::Assign(place, rvalue) = &statement.kind {
        if let Some(rvalue_json) = rvalue_payload_json(rvalue) {
            payload.push_str(&format!(
                ",\"place\":{},\"rvalue\":{}",
                place_payload_json(place),
                rvalue_json
            ));
        }
    }
    payload.push('}');
    payload
}

fn emit_statement<W: Write>(
    out: &mut W,
    statement: &Statement,
    statement_index: usize,
) -> io::Result<()> {
    let debug = format!("{:?}", statement.kind);
    let kind = variant_name(&debug);
    let payload = statement_payload_json(statement, &debug);
    writeln!(
        out,
        "{{\"record\":\"statement\",\"kind\":\"{}\",\"index\":{},\"span\":\"{}\",\"payload\":{}}}",
        json_escape(kind),
        statement_index,
        debug_json(&statement.span),
        payload
    )?;

    match &statement.kind {
        StatementKind::Assign(place, rvalue) => {
            emit_place(out, place)?;
            emit_rvalue(out, rvalue)
        }
        StatementKind::FakeRead(cause, place) => {
            emit_fake_read_cause(out, cause)?;
            emit_place(out, place)
        }
        StatementKind::SetDiscriminant { place, .. } => emit_place(out, place),
        StatementKind::StorageLive(_) | StatementKind::StorageDead(_) => Ok(()),
        StatementKind::Retag(kind, place) => {
            emit_retag_kind(out, kind)?;
            emit_place(out, place)
        }
        StatementKind::PlaceMention(place) => emit_place(out, place),
        StatementKind::AscribeUserType {
            place, variance, ..
        } => {
            emit_place(out, place)?;
            emit_variance(out, variance)
        }
        StatementKind::Coverage(_) | StatementKind::ConstEvalCounter | StatementKind::Nop => Ok(()),
        StatementKind::Intrinsic(intrinsic) => emit_intrinsic(out, intrinsic),
    }
}

fn emit_terminator<W: Write>(
    out: &mut W,
    terminator: &TerminatorKind,
    span: &impl std::fmt::Debug,
) -> io::Result<()> {
    let debug = format!("{terminator:?}");
    let kind = variant_name(&debug);
    writeln!(
        out,
        "{{\"record\":\"terminator\",\"kind\":\"{}\",\"span\":\"{}\",\"payload\":{{\"debug\":\"{}\"}}}}",
        json_escape(kind),
        debug_json(span),
        json_escape(&debug)
    )?;

    match terminator {
        TerminatorKind::Goto { .. }
        | TerminatorKind::Resume
        | TerminatorKind::Abort
        | TerminatorKind::Return
        | TerminatorKind::Unreachable => Ok(()),
        TerminatorKind::SwitchInt { discr, .. } => emit_operand(out, discr),
        TerminatorKind::Drop { place, unwind, .. } => {
            emit_place(out, place)?;
            emit_unwind_action(out, unwind)
        }
        TerminatorKind::Call {
            func,
            args,
            destination,
            unwind,
            ..
        } => {
            emit_operand(out, func)?;
            for arg in args {
                emit_operand(out, arg)?;
            }
            emit_place(out, destination)?;
            emit_unwind_action(out, unwind)
        }
        TerminatorKind::Assert {
            cond, msg, unwind, ..
        } => {
            emit_operand(out, cond)?;
            emit_assert_message(out, msg)?;
            emit_unwind_action(out, unwind)
        }
        TerminatorKind::InlineAsm {
            operands, unwind, ..
        } => {
            for operand in operands {
                if let Some(in_value) = &operand.in_value {
                    emit_operand(out, in_value)?;
                }
                if let Some(out_place) = &operand.out_place {
                    emit_place(out, out_place)?;
                }
            }
            emit_unwind_action(out, unwind)
        }
    }
}

fn open_output(path: Option<&str>, append: bool) -> io::Result<Box<dyn Write + Send>> {
    match path {
        Some("-") | None => Ok(Box::new(io::stdout())),
        Some(path) if append => Ok(Box::new(
            OpenOptions::new().create(true).append(true).open(path)?,
        )),
        Some(path) => Ok(Box::new(File::create(path)?)),
    }
}

fn emit_module_record<W: Write>(out: &mut W) -> io::Result<()> {
    let target = MachineInfo::target();
    let endian = match target.endian {
        Endian::Little => "little",
        Endian::Big => "big",
    };
    writeln!(
        out,
        "{{\"record\":\"module\",\"source\":\"rustc_public\",\"target_pointer_width\":{},\"target_endian\":\"{}\"}}",
        target.pointer_width.bits(),
        endian
    )
}

fn emit_body<W: Write>(out: &mut W) -> io::Result<()> {
    emit_module_record(out)?;

    for item in rustc_public::all_local_items() {
        if !item.has_body() {
            continue;
        }

        let body = item.expect_body();
        let name = item.name().to_string();
        let signature = format!("{:?}", item.ty());
        writeln!(
            out,
            "{{\"record\":\"function\",\"event\":\"begin\",\"name\":\"{}\",\"signature\":\"{}\",\"item_kind\":\"{}\",\"arg_count\":{},\"span\":\"{}\"}}",
            json_escape(&name),
            json_escape(&signature),
            debug_json(&item.kind()),
            body.arg_locals().len(),
            debug_json(&body.span)
        )?;

        let ret_ty = format!("{:?}", body.ret_local().ty);
        writeln!(
            out,
            "{{\"record\":\"local\",\"index\":0,\"name\":\"_0\",\"role\":\"return\",\"ty\":\"{}\",\"mutability\":\"{:?}\",\"span\":\"{}\"}}",
            json_escape(&ret_ty),
            body.ret_local().mutability,
            debug_json(&body.ret_local().span)
        )?;

        for (arg_index, local) in body.arg_locals().iter().enumerate() {
            let local_index = arg_index + 1;
            let ty = format!("{:?}", local.ty);
            writeln!(
                out,
                "{{\"record\":\"local\",\"index\":{},\"name\":\"_{}\",\"role\":\"arg\",\"ty\":\"{}\",\"mutability\":\"{:?}\",\"span\":\"{}\"}}",
                local_index,
                local_index,
                json_escape(&ty),
                local.mutability,
                debug_json(&local.span)
            )?;
        }

        for (inner_index, local) in body.inner_locals().iter().enumerate() {
            let local_index = inner_index + body.arg_locals().len() + 1;
            let ty = format!("{:?}", local.ty);
            writeln!(
                out,
                "{{\"record\":\"local\",\"index\":{},\"name\":\"_{}\",\"role\":\"inner\",\"ty\":\"{}\",\"mutability\":\"{:?}\",\"span\":\"{}\"}}",
                local_index,
                local_index,
                json_escape(&ty),
                local.mutability,
                debug_json(&local.span)
            )?;
        }

        for (block_index, block) in body.blocks.iter().enumerate() {
            writeln!(
                out,
                "{{\"record\":\"block\",\"event\":\"begin\",\"index\":{}}}",
                block_index
            )?;

            for (statement_index, statement) in block.statements.iter().enumerate() {
                emit_statement(out, statement, statement_index)?;
            }

            emit_terminator(out, &block.terminator.kind, &block.terminator.span)?;
            writeln!(
                out,
                "{{\"record\":\"block\",\"event\":\"end\",\"index\":{}}}",
                block_index
            )?;
        }

        writeln!(
            out,
            "{{\"record\":\"function\",\"event\":\"end\",\"name\":\"{}\"}}",
            json_escape(&name)
        )?;
    }

    Ok(())
}

fn run_rustc_public(args: Vec<String>, output: Option<&str>, append: bool) -> io::Result<()> {
    let mut out = open_output(output, append)?;
    let result = rustc_public::run!(&args, || {
        match emit_body(&mut out) {
            Ok(()) => ControlFlow::Continue(()),
            Err(err) => {
                eprintln!("rust-mir-extract: failed to emit NDJSON: {err}");
                ControlFlow::Break(())
            }
        }
    });

    match result {
        Ok(()) => Ok(()),
        Err(err) => Err(io::Error::new(
            io::ErrorKind::Other,
            format!("rustc_public compiler run failed: {err:?}"),
        )),
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
            "--cargo" => {
                opts.cargo_dir = iter.next();
                if opts.cargo_dir.is_none() {
                    return Err("--cargo requires a package directory".into());
                }
            }
            "--emit-ndjson" => opts.emit_ndjson = true,
            "-o" | "--output" => {
                opts.output = iter.next();
                if opts.output.is_none() {
                    return Err("-o/--output requires a path".into());
                }
            }
            _ => return Err(format!("unknown argument: {arg}")),
        }
    }

    if opts.crate_root.is_some() == opts.cargo_dir.is_some() {
        return Err("choose exactly one of --crate-root or --cargo".into());
    }
    if !opts.emit_ndjson {
        return Err("only --emit-ndjson is supported".into());
    }

    Ok(opts)
}

fn rustc_program() -> String {
    env::var("RUSTC").unwrap_or_else(|_| "rustc".to_string())
}

fn cargo_program() -> String {
    env::var("CARGO").unwrap_or_else(|_| "cargo".to_string())
}

fn rustc_args_for_crate_root(root: &str, passthrough: &[String]) -> Vec<String> {
    let mut args = vec![
        rustc_program(),
        root.to_string(),
        "--crate-type=lib".to_string(),
    ];
    args.extend_from_slice(passthrough);
    args
}

fn temp_output_path() -> PathBuf {
    let mut path = env::temp_dir();
    path.push(format!("rust-mir-extract-{}.ndjson", process::id()));
    path
}

fn run_cargo(opts: &Options) -> io::Result<()> {
    let output_path = opts
        .output
        .as_ref()
        .map(PathBuf::from)
        .unwrap_or_else(temp_output_path);

    let exe = env::current_exe()?;
    let package_dir = opts.cargo_dir.as_deref().unwrap();
    let mut command = Command::new(cargo_program());
    command
        .arg("check")
        .current_dir(package_dir)
        .env("RUSTC_WRAPPER", exe)
        .env("RUST_MIR_EXTRACT_WRAPPER_OUT", &output_path);
    for arg in &opts.passthrough {
        command.arg(arg);
    }

    let status = command.status()?;
    if !status.success() {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            format!("cargo check failed with status {status}"),
        ));
    }

    if opts.output.is_none() {
        let mut file = File::open(output_path)?;
        let mut stdout = io::stdout();
        io::copy(&mut file, &mut stdout)?;
    }
    Ok(())
}

fn wrapper_mode(args: &[String]) -> Option<io::Result<i32>> {
    let output = env::var("RUST_MIR_EXTRACT_WRAPPER_OUT").ok()?;
    if args.len() < 2 {
        return Some(Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "RUSTC_WRAPPER invocation is missing rustc path",
        )));
    }

    let rustc = &args[1];
    let rustc_args: Vec<String> = args[1..].to_vec();
    if let Err(err) = run_rustc_public(rustc_args, Some(&output), true) {
        eprintln!("rust-mir-extract: rustc_public extraction failed: {err}");
    }

    let status = Command::new(rustc)
        .args(&args[2..])
        .env_remove("RUSTC_WRAPPER")
        .env_remove("RUST_MIR_EXTRACT_WRAPPER_OUT")
        .status();

    Some(status.map(|status| status.code().unwrap_or(1)))
}

fn main() {
    let args: Vec<String> = env::args().collect();

    if let Some(result) = wrapper_mode(&args) {
        match result {
            Ok(code) => process::exit(code),
            Err(err) => {
                eprintln!("rust-mir-extract: {err}");
                process::exit(1);
            }
        }
    }

    let opts = match parse_options(args.into_iter().skip(1)) {
        Ok(opts) => opts,
        Err(err) => {
            eprintln!("rust-mir-extract: {err}");
            process::exit(2);
        }
    };

    let result = if let Some(root) = opts.crate_root.as_deref() {
        let rustc_args = rustc_args_for_crate_root(root, &opts.passthrough);
        run_rustc_public(rustc_args, opts.output.as_deref(), false)
    } else {
        run_cargo(&opts)
    };

    if let Err(err) = result {
        eprintln!("rust-mir-extract: {err}");
        process::exit(1);
    }
}
