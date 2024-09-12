#include "slang-emit-wgsl.h"

// A note on row/column "terminology reversal".
//
// This is an "terminology reversing" implementation in the sense that
// * "column" in Slang code maps to "row" in the generated WGSL code, and
// * "row" in Slang code maps to "column" in the generated WGSL code.
//
// This means that matrices in Slang code end up getting translated to
// matrices that actually represent the transpose of what the Slang matrix
// represented.
// Both API's adopt the standard matrix multiplication convention whereby the
// column count of the matrix on the left hand side needs to match row count of
// the matrix on the right hand side.
// For these reasons, and due to the fact that (M_1 ... M_n)^T = M_n^T ... M_1^T,
// the order of matrix (and vector-matrix products) products must also reversed
// in the WGSL code.
//
// This may lead to confusion (which is why this note is referenced in several
// places), but the benefit of doing this is that the generated WGSL code is
// simpler to generate and should be faster to compile.
// A "terminology preserving" implementation would have to generate lots of
// 'transpose' calls, or else perform more complicated transformations that
// end up duplicating expressions many times.

namespace Slang {

void WGSLSourceEmitter::emitSwitchCaseSelectorsImpl(
    IRBasicType *const switchConditionType,
    const SwitchRegion::Case *const currentCase, const bool isDefault
    )
{
    // WGSL has special syntax for blocks sharing case labels:
    // "case 2, 3, 4: ...;" instead of the C-like syntax
    // "case 2: case 3: case 4: ...;".

    m_writer->emit("case ");
    for (auto caseVal : currentCase->values)
    {
        // TODO: Fix this in the front-end [1], remove the if-path and just do the else-path.
        //       We can't do that at the moment because it would break Falcor [2].
        //       [1] https://github.com/shader-slang/slang/pull/5025/commits/a32156ef52f43b8503b2c77f2f1d51220ab9bdea
        //       [2] https://github.com/shader-slang/slang/pull/5025#issuecomment-2334495120
        if (caseVal->getOp() == kIROp_IntLit)
        {
            auto caseLitInst = static_cast<IRConstant*>(caseVal);
            IRBasicType *const caseInstType = as<IRBasicType>(caseLitInst->getDataType());
            // WGSL doesn't allow switch condition and case type mismatches, see [1].
            // Thus we need to insert explicit conversions.
            // Doing a wrapping cast will match Slang's de facto semantics, according to
            // [2].
            // (This is just a bitcast, assuming a two's complement representation.)
            // [1] https://www.w3.org/TR/WGSL/#switch-statement
            // [2] https://github.com/shader-slang/slang/issues/4921
            const bool needBitcast =
                caseInstType->getBaseType() != switchConditionType->getBaseType();
            if (needBitcast)
            {
                m_writer->emit("bitcast<");
                emitType(switchConditionType);
                m_writer->emit(">(");
            }
            emitOperand(caseVal, getInfo(EmitOp::General));
            if (needBitcast)
            {
                m_writer->emit(")");
            }
        }
        else
        {
            emitOperand(caseVal, getInfo(EmitOp::General));
        }
        m_writer->emit(", ");
    }
    if (isDefault)
    {
        m_writer->emit("default, ");
    }
    m_writer->emit(":\n");
}

void WGSLSourceEmitter::emitParameterGroupImpl(
    IRGlobalParam* varDecl, IRUniformParameterGroupType* type
)
{
    auto varLayout = getVarLayout(varDecl);
    SLANG_RELEASE_ASSERT(varLayout);

    for (auto attr : varLayout->getOffsetAttrs())
    {

        const LayoutResourceKind kind = attr->getResourceKind();
        switch (kind)
        {
        case LayoutResourceKind::VaryingInput:
        case LayoutResourceKind::VaryingOutput:
            m_writer->emit("@location(");
            m_writer->emit(attr->getOffset());
            m_writer->emit(")");
            if (attr->getSpace())
            {
                // TODO: Not sure what 'space' should map to in WGSL
                SLANG_ASSERT(false);
            }
            break;

        case LayoutResourceKind::SpecializationConstant:
            // TODO:
            // Consider moving to a differently named function.
            // This is not technically an attribute, but a declaration.
            //
            // https://www.w3.org/TR/WGSL/#override-decls
            m_writer->emit("override");
            break;

        case LayoutResourceKind::Uniform:
        case LayoutResourceKind::ConstantBuffer:
        case LayoutResourceKind::ShaderResource:
        case LayoutResourceKind::UnorderedAccess:
        case LayoutResourceKind::SamplerState:
        case LayoutResourceKind::DescriptorTableSlot:
            m_writer->emit("@binding(");
            m_writer->emit(attr->getOffset());
            m_writer->emit(") ");
            m_writer->emit("@group(");
            m_writer->emit(attr->getSpace());
            m_writer->emit(") ");
            break;

        }

    }

    auto elementType = type->getElementType();
    m_writer->emit("var<uniform> ");
    m_writer->emit(getName(varDecl));
    m_writer->emit(" : ");
    emitType(elementType);
    m_writer->emit(";\n");
}

void WGSLSourceEmitter::emitEntryPointAttributesImpl(
    IRFunc* irFunc, IREntryPointDecoration* entryPointDecor
    )
{
    auto stage = entryPointDecor->getProfile().getStage();

    switch (stage)
    {

    case Stage::Fragment:
        m_writer->emit("@fragment\n");
        break;
    case Stage::Vertex:
        m_writer->emit("@vertex\n");
        break;

    case Stage::Compute:
    {
        m_writer->emit("@compute\n");

        {
            Int sizeAlongAxis[kThreadGroupAxisCount];
            getComputeThreadGroupSize(irFunc, sizeAlongAxis);

            m_writer->emit("@workgroup_size(");
            for (int ii = 0; ii < kThreadGroupAxisCount; ++ii)
            {
                if (ii != 0)
                    m_writer->emit(", ");
                m_writer->emit(sizeAlongAxis[ii]);
            }
            m_writer->emit(")\n");
        }
    }
    break;

    default:
        SLANG_ABORT_COMPILATION("unsupported stage.");
    }

}

// This is 'function_header' from the WGSL specification
void WGSLSourceEmitter::emitFuncHeaderImpl(IRFunc* func)
{
    Slang::IRType * resultType = func->getResultType();
    auto name = getName(func);

    m_writer->emit("fn ");
    m_writer->emit(name);

    emitSimpleFuncParamsImpl(func);

    // An absence of return type is expressed by skipping the optional '->' part of the
    // header.
    if (resultType->getOp() != kIROp_VoidType)
    {
        m_writer->emit(" -> ");
        emitType(resultType);
    }
}

void WGSLSourceEmitter::emitSimpleFuncParamImpl(IRParam* param)
{
    if (auto sysSemanticDecor = param->findDecoration<IRTargetSystemValueDecoration>())
    {
        m_writer->emit("@builtin(");
        m_writer->emit(sysSemanticDecor->getSemantic());
        m_writer->emit(")");
    }

    CLikeSourceEmitter::emitSimpleFuncParamImpl(param);
}

void WGSLSourceEmitter::emitMatrixType(
    IRType *const elementType, const IRIntegerValue& rowCountWGSL,
    const IRIntegerValue& colCountWGSL
    )
{
    // WGSL uses CxR convention
    m_writer->emit("mat");
    m_writer->emit(colCountWGSL);
    m_writer->emit("x");
    m_writer->emit(rowCountWGSL);
    m_writer->emit("<");
    emitType(elementType);
    m_writer->emit(">");
}

void WGSLSourceEmitter::emitStructDeclarationSeparatorImpl()
{
    m_writer->emit(",");
}

static bool isPowerOf2(const uint32_t n)
{
    return (n != 0U) && ((n - 1U) & n) == 0U;
}

void WGSLSourceEmitter::emitStructFieldAttributes(
    IRStructType * structType, IRStructField * field
    )
{
    // Tint emits errors unless we explicitly spell out the layout in some cases, so emit
    // offset and align attribtues for all fields.
    IRSizeAndAlignmentDecoration *const sizeAndAlignmentDecoration =
        structType->findDecoration<IRSizeAndAlignmentDecoration>();
    // NullDifferential struct doesn't have size and alignment decoration
    if (sizeAndAlignmentDecoration == nullptr)
        return;
    SLANG_ASSERT(sizeAndAlignmentDecoration->getAlignment() > IRIntegerValue{0});
    SLANG_ASSERT(
        sizeAndAlignmentDecoration->getAlignment() <= IRIntegerValue{UINT32_MAX}
    );
    const uint32_t structAlignment =
        static_cast<uint32_t>(sizeAndAlignmentDecoration->getAlignment());
    IROffsetDecoration *const fieldOffsetDecoration =
        field->findDecoration<IROffsetDecoration>();
    SLANG_ASSERT(fieldOffsetDecoration->getOffset() >= IRIntegerValue{0});
    SLANG_ASSERT(fieldOffsetDecoration->getOffset() <= IRIntegerValue{UINT32_MAX});
    SLANG_ASSERT(isPowerOf2(structAlignment));
    const uint32_t fieldOffset =
        static_cast<uint32_t>(fieldOffsetDecoration->getOffset());
    // Alignment is GCD(fieldOffset, structAlignment)
    // TODO: Use builtin/intrinsic (e.g. __builtin_ffs)
    uint32_t fieldAlignment = 1U;
    while (((fieldAlignment & (structAlignment | fieldOffset)) == 0U))
        fieldAlignment = fieldAlignment << 1U;

    m_writer->emit("@align(");
    m_writer->emit(fieldAlignment);
    m_writer->emit(")");
}

bool WGSLSourceEmitter::isPointerSyntaxRequiredImpl(IRInst* inst)
{
    // Structured buffers are mapped to 'array' types, which don't need dereferencing
    if (inst->getOp() == kIROp_RWStructuredBufferGetElementPtr)
        return false;

    // Don't emit "->" to access fields in resource structs
    if (inst->getOp() == kIROp_FieldAddress)
        return false;

    // Don't emit "*" to access fields in resource structs
    if (inst->getOp() == kIROp_GlobalParam)
        return false;

    // Emit 'globalVar' instead of  "*&globalVar"
    if (inst->getOp() == kIROp_GlobalVar)
        return false;

    return true;
}

void WGSLSourceEmitter::emit(const AddressSpace addressSpace)
{
    switch (addressSpace)
    {
    case AddressSpace::Uniform:
        m_writer->emit("uniform");
        break;

    case AddressSpace::StorageBuffer:
        m_writer->emit("storage");
        break;

    case AddressSpace::Generic:
        m_writer->emit("function");
        break;

    case AddressSpace::ThreadLocal:
        m_writer->emit("private");
        break;

    case AddressSpace::GroupShared:
        m_writer->emit("workgroup");
        break;
    }
}

void WGSLSourceEmitter::emitSimpleTypeImpl(IRType* type)
{
    switch (type->getOp())
    {

    case kIROp_HLSLRWStructuredBufferType:
    {
        auto structuredBufferType = as<IRHLSLStructuredBufferTypeBase>(type);
        m_writer->emit("ptr<");
        emit(AddressSpace::StorageBuffer);
        m_writer->emit(", ");
        m_writer->emit("array");
        m_writer->emit("<");
        emitType(structuredBufferType->getElementType());
        m_writer->emit(">");
        m_writer->emit(", read_write");
        m_writer->emit(">");
    }
    break;

    case kIROp_HLSLStructuredBufferType:
    {
        auto structuredBufferType = as<IRHLSLStructuredBufferTypeBase>(type);
        m_writer->emit("ptr<");
        emit(AddressSpace::StorageBuffer);
        m_writer->emit(", ");
        m_writer->emit("array");
        m_writer->emit("<");
        emitType(structuredBufferType->getElementType());
        m_writer->emit(">");
        m_writer->emit(", read");
        m_writer->emit(">");
    }
    break;

    case kIROp_VoidType:
    {
        // There is no void type in WGSL.
        // A return type of "void" is expressed by skipping the end part of the
        // 'function_header' term:
        // "
        // function_header :
        //   'fn' ident '(' param_list ? ')'
        //       ( '->' attribute * template_elaborated_ident ) ?
        // "
        // In other words, in WGSL we should never even get to the point where we're
        // asking to emit 'void'.
        SLANG_UNEXPECTED("'void' type emitted");
        return;
    }

    case kIROp_FloatType:
        m_writer->emit("f32");
        break;
    case kIROp_DoubleType:
        // There is no "f64" type in WGSL
        SLANG_UNEXPECTED("'double' type emitted");
        break;
    case kIROp_Int8Type:
    case kIROp_UInt8Type:
        // There is no "[i|u]8" type in WGSL
        SLANG_UNEXPECTED("8 bit integer type emitted");
        break;
    case kIROp_HalfType:
        m_f16ExtensionEnabled = true;
        m_writer->emit("f16");
        break;
    case kIROp_BoolType:
        m_writer->emit("bool");
        break;
    case kIROp_IntType:
        m_writer->emit("i32");
        break;
    case kIROp_UIntType:
        m_writer->emit("u32");
        break;
    case kIROp_UInt64Type:
    {
        m_writer->emit(getDefaultBuiltinTypeName(type->getOp()));
        return;
    }
    case kIROp_Int16Type:
    case kIROp_UInt16Type:
        SLANG_UNEXPECTED("16 bit integer value emitted");
        return;
    case kIROp_Int64Type:
    case kIROp_IntPtrType:
        m_writer->emit("i64");
        return;
    case kIROp_UIntPtrType:
        m_writer->emit("u64");
        return;
    case kIROp_StructType:
        m_writer->emit(getName(type));
        return;

    case kIROp_VectorType:
    {
        auto vecType = (IRVectorType*)type;
        emitVectorTypeNameImpl(
            vecType->getElementType(), getIntVal(vecType->getElementCount())
        );
        return;
    }
    case kIROp_MatrixType:
    {
        auto matType = (IRMatrixType*)type;
        // We map matrices in Slang to WGSL matrices that represent the transpose.
        // (See note on "terminology reversal".)
        const IRIntegerValue colCountWGSL = getIntVal(matType->getRowCount());
        const IRIntegerValue rowCountWGSL = getIntVal(matType->getColumnCount());
        emitMatrixType(matType->getElementType(), rowCountWGSL, colCountWGSL);
        return;
    }
    case kIROp_SamplerStateType:
    {
        m_writer->emit("sampler");
        return;
    }

    case kIROp_SamplerComparisonStateType:
    {
        m_writer->emit("sampler_comparison");
        return;
    }

    case kIROp_PtrType:
    case kIROp_InOutType:
    case kIROp_OutType:
    case kIROp_RefType:
    case kIROp_ConstRefType:
    {
        auto ptrType = cast<IRPtrTypeBase>(type);
        m_writer->emit("ptr<");
        emit((AddressSpace)ptrType->getAddressSpace());
        m_writer->emit(", ");
        emitType((IRType*)ptrType->getValueType());
        m_writer->emit(">");
        return;
    }

    case kIROp_ArrayType:
    {
        m_writer->emit("array<");
        emitType((IRType*)type->getOperand(0));
        m_writer->emit(", ");
        emitVal(type->getOperand(1), getInfo(EmitOp::General));
        m_writer->emit(">");
        return;
    }
    default:
        break;

    }

}

void WGSLSourceEmitter::emitLayoutQualifiersImpl(IRVarLayout* layout)
{

    for (auto attr : layout->getOffsetAttrs())
    {
        LayoutResourceKind kind = attr->getResourceKind();

        // TODO:
        // This is not correct. For the moment this is just here as a hack to make
        // @binding and @group unique, so that we can pass WGSL compile tests.
        // This will have to be revisited when we actually want to supply resources to
        // shaders.
        if (kind == LayoutResourceKind::DescriptorTableSlot)
        {
            m_writer->emit("@binding(");
            m_writer->emit(attr->getOffset());
            m_writer->emit(") ");
            m_writer->emit("@group(");
            m_writer->emit(attr->getSpace());
            m_writer->emit(") ");

            return;
        }
    }

}

void WGSLSourceEmitter::emitVarKeywordImpl(IRType * type, const bool isConstant)
{
    if (isConstant)
        m_writer->emit("const");
    else
        m_writer->emit("var");
    if (type->getOp() == kIROp_HLSLRWStructuredBufferType)
    {
        m_writer->emit("<");
        m_writer->emit("storage, read_write");
        m_writer->emit(">");
    }
    else if (type->getOp() == kIROp_HLSLStructuredBufferType)
    {
        m_writer->emit("<");
        m_writer->emit("storage, read");
        m_writer->emit(">");
    }
}

void WGSLSourceEmitter::_emitType(IRType* type, DeclaratorInfo* declarator)
{
    // C-like languages bake array-ness, pointer-ness and reference-ness into the
    // declarator, which happens in the default _emitType implementation.
    // WGSL on the other hand, don't have special syntax -- these are just types.
    switch (type->getOp())
    {
    case kIROp_ArrayType:
    case kIROp_AttributedType:
    case kIROp_UnsizedArrayType:
        emitSimpleTypeAndDeclarator(type, declarator);
        break;
    default:
        CLikeSourceEmitter::_emitType(type, declarator);
        break;
    }
}

void WGSLSourceEmitter::emitDeclaratorImpl(DeclaratorInfo* declarator)
{
    if (!declarator) return;

    m_writer->emit(" ");

    switch (declarator->flavor)
    {
    case DeclaratorInfo::Flavor::Name:
    {
        auto nameDeclarator = (NameDeclaratorInfo*)declarator;
        m_writer->emitName(*nameDeclarator->nameAndLoc);
    }
    break;

    case DeclaratorInfo::Flavor::SizedArray:
    {
        // Sized arrays are just types (array<T, N>) in WGSL -- they are not
        // supported at the syntax level
        // https://www.w3.org/TR/WGSL/#array
        SLANG_UNEXPECTED("Sized array declarator");
    }
    break;

    case DeclaratorInfo::Flavor::UnsizedArray:
    {
        // Unsized arrays are just types (array<T>) in WGSL -- they are not
        // supported at the syntax level
        // https://www.w3.org/TR/WGSL/#array
        SLANG_UNEXPECTED("Unsized array declarator");
    }
    break;

    case DeclaratorInfo::Flavor::Ptr:
    {
        // Pointers (ptr<AS,T,AM>) are just types in WGSL -- they are not supported at
        // the syntax level
        // https://www.w3.org/TR/WGSL/#ref-ptr-types
        SLANG_UNEXPECTED("Pointer declarator");
    }
    break;

    case DeclaratorInfo::Flavor::Ref:
    {
        // References (ref<AS,T,AM>) are just types in WGSL -- they are not supported
        // at the syntax level
        // https://www.w3.org/TR/WGSL/#ref-ptr-types
        SLANG_UNEXPECTED("Reference declarator");
    }
    break;

    case DeclaratorInfo::Flavor::LiteralSizedArray:
    {
        // Sized arrays are just types (array<T, N>) in WGSL -- they are not supported
        // at the syntax level
        // https://www.w3.org/TR/WGSL/#array
        SLANG_UNEXPECTED("Literal-sized array declarator");
    }
    break;

    case DeclaratorInfo::Flavor::Attributed:
    {
        auto attributedDeclarator = (AttributedDeclaratorInfo*)declarator;
        auto instWithAttributes = attributedDeclarator->instWithAttributes;
        for (auto attr : instWithAttributes->getAllAttrs())
        {
            _emitPostfixTypeAttr(attr);
        }
        emitDeclarator(attributedDeclarator->next);
    }
    break;

    default:
        SLANG_DIAGNOSE_UNEXPECTED(getSink(), SourceLoc(), "unknown declarator flavor");
        break;
    }
}

void WGSLSourceEmitter::emitSimpleTypeAndDeclaratorImpl(
    IRType* type, DeclaratorInfo* declarator
    )
{
    if (declarator)
    {
        emitDeclarator(declarator);
        m_writer->emit(" : ");
    }
    emitSimpleType(type);
}

void WGSLSourceEmitter::emitSimpleValueImpl(IRInst* inst)
{
    switch (inst->getOp())
    {
    case kIROp_IntLit:
    {
        auto litInst = static_cast<IRConstant*>(inst);

        IRBasicType* type = as<IRBasicType>(inst->getDataType());
        if (type)
        {
            switch (type->getBaseType())
            {
                default:

                case BaseType::Int8:
                case BaseType::UInt8:
                {
                    SLANG_UNEXPECTED("8 bit integer value emitted");
                    break;
                }
                case BaseType::Int16:
                case BaseType::UInt16:
                {
                    SLANG_UNEXPECTED("16 bit integer value emitted");
                    break;
                }
                case BaseType::Int:
                {
                    m_writer->emit("i32(");
                    m_writer->emit(int32_t(litInst->value.intVal));
                    m_writer->emit(")");
                    return;
                }
                case BaseType::UInt:
                {
                    m_writer->emit("u32(");
                    m_writer->emit(UInt(uint32_t(litInst->value.intVal)));
                    m_writer->emit(")");
                    break;
                }
                case BaseType::Int64:
                {
                    m_writer->emit("i64(");
                    m_writer->emitInt64(int64_t(litInst->value.intVal));
                    m_writer->emit(")");
                    break;
                }
                case BaseType::UInt64:
                {
                    m_writer->emit("u64(");
                    SLANG_COMPILE_TIME_ASSERT(
                        sizeof(litInst->value.intVal) >= sizeof(uint64_t)
                    );
                    m_writer->emitUInt64(uint64_t(litInst->value.intVal));
                    m_writer->emit(")");
                    break;
                }
                case BaseType::IntPtr:
                {
#if SLANG_PTR_IS_64
                    m_writer->emit("i64(");
                    m_writer->emitInt64(int64_t(litInst->value.intVal));
                    m_writer->emit(")");
#else
                    m_writer->emit("i32(");
                    m_writer->emit(int(litInst->value.intVal));
                    m_writer->emit(")");
#endif
                    break;
                }
                case BaseType::UIntPtr:
                {
#if SLANG_PTR_IS_64
                    m_writer->emit("u64(");
                    m_writer->emitUInt64(uint64_t(litInst->value.intVal));
                    m_writer->emit(")");
#else
                    m_writer->emit("u32(");
                    m_writer->emit(UInt(uint32_t(litInst->value.intVal)));
                    m_writer->emit(")");
#endif
                    break;
                }

            }
        }
        else
        {
            // If no type... just output what we have
            m_writer->emit(litInst->value.intVal);
        }
        break;
    }

    case kIROp_FloatLit:
    {
        auto litInst = static_cast<IRConstant*>(inst);

        IRBasicType* type = as<IRBasicType>(inst->getDataType());
        if (type)
        {
            switch (type->getBaseType())
            {
            default:

            case BaseType::Half:
            {
                m_writer->emit(litInst->value.floatVal);
                m_writer->emit("h");
                m_f16ExtensionEnabled = true;
            }
            break;

            case BaseType::Float:
            {
                m_writer->emit(litInst->value.floatVal);
                m_writer->emit("f");
            }
            break;

            case BaseType::Double:
            {
                 // There is not "f64" in WGSL
                SLANG_UNEXPECTED("'double' type emitted");
            }
            break;
            }
        }
        else
        {
            // If no type... just output what we have
            m_writer->emit(litInst->value.floatVal);
        }
    }
    break;

    case kIROp_BoolLit:
    {
        bool val = ((IRConstant*)inst)->value.intVal != 0;
        m_writer->emit(val ? "true" : "false");
    }
    break;

    default:
        SLANG_UNIMPLEMENTED_X("val case for emit");
        break;
    }


}

void WGSLSourceEmitter::emitParamTypeImpl(IRType* type, const String& name)
{
    emitType(type, name);
}

bool WGSLSourceEmitter::tryEmitInstExprImpl(IRInst* inst, const EmitOpInfo& inOuterPrec)
{
    EmitOpInfo outerPrec = inOuterPrec;

    switch (inst->getOp())
    {

    case kIROp_MakeVectorFromScalar:
    {
        // In WGSL this is done by calling the vec* overloads listed in [1]
        // [1] https://www.w3.org/TR/WGSL/#value-constructor-builtin-function
        emitType(inst->getDataType());
        m_writer->emit("(");
        auto prec = getInfo(EmitOp::Prefix);
        emitOperand(inst->getOperand(0), rightSide(outerPrec, prec));
        m_writer->emit(")");
        return true;
    }
    break;

    case kIROp_BitCast:
    {
        // In WGSL there is a built-in bitcast function!
        // https://www.w3.org/TR/WGSL/#bitcast-builtin
        m_writer->emit("bitcast");
        m_writer->emit("<");
        emitType(inst->getDataType());
        m_writer->emit(">");
        m_writer->emit("(");
        emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
        m_writer->emit(")");
        return true;
    }
    break;

    case kIROp_MakeArray:
    case kIROp_MakeStruct:
    {
        // It seems there are currently no designated initializers in WGSL.
        // Similarly for array initializers.
        // https://github.com/gpuweb/gpuweb/issues/4210

        // There is a constructor named like the struct/array type itself
        auto type = inst->getDataType();
        emitType(type);
        m_writer->emit("( ");
        UInt argCount = inst->getOperandCount();
        for (UInt aa = 0; aa < argCount; ++aa)
        {
            if (aa != 0) m_writer->emit(", ");
            emitOperand(inst->getOperand(aa), getInfo(EmitOp::General));
        }
        m_writer->emit(" )");

        return true;
    }
    break;

    case kIROp_MakeArrayFromElement:
    {
        // It seems there are currently no array initializers in WGSL.

        // There is a constructor named like the array type itself
        auto type = inst->getDataType();
        emitType(type);
        m_writer->emit("(");
        UInt argCount =
            (UInt)cast<IRIntLit>(
                cast<IRArrayType>(inst->getDataType())->getElementCount()
            )->getValue();
        for (UInt aa = 0; aa < argCount; ++aa)
        {
            if (aa != 0) m_writer->emit(", ");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
        }
        m_writer->emit(")");
        return true;
    }
    break;

    case kIROp_StructuredBufferLoad:
    case kIROp_RWStructuredBufferLoad:
    {
        // Structured buffers are just arrays in WGSL
        auto base = inst->getOperand(0);
        emitOperand(base, outerPrec);
        m_writer->emit("[");
        emitOperand(inst->getOperand(1), EmitOpInfo());
        m_writer->emit("]");
        return true;
    }
    break;

    case kIROp_Rsh:
    case kIROp_Lsh:
    {
        // Shift amounts must be an unsigned type in WGSL
        // https://www.w3.org/TR/WGSL/#bit-expr
        IRInst *const shiftAmount = inst->getOperand(1);
        IRType *const shiftAmountType = shiftAmount->getDataType();
        if (shiftAmountType->getOp() == kIROp_IntType)
        {
            // Dawn complains about "mixing '<<' and '|' requires parenthesis", so let's
            // add parenthesis.
            m_writer->emit("(");

            const auto emitOp = getEmitOpForOp(inst->getOp());
            const auto info = getInfo(emitOp);

            const bool needClose = maybeEmitParens(outerPrec, info);
            emitOperand(inst->getOperand(0), leftSide(outerPrec, info));
            m_writer->emit(" ");
            m_writer->emit(info.op);
            m_writer->emit(" ");
            m_writer->emit("bitcast<u32>(");
            emitOperand(inst->getOperand(1), rightSide(outerPrec, info));
            m_writer->emit(")");
            maybeCloseParens(needClose);

            m_writer->emit(")");
            return true;
        }
    }
    break;

    }

    return false;
}

void WGSLSourceEmitter::emitVectorTypeNameImpl(
    IRType* elementType, IRIntegerValue elementCount
    )
{

    if (elementCount > 1)
    {
        m_writer->emit("vec");
        m_writer->emit(elementCount);
        m_writer->emit("<");
        emitSimpleType(elementType);
        m_writer->emit(">");
    }
    else
    {
        emitSimpleType(elementType);
    }
}

void WGSLSourceEmitter::emitOperandImpl(IRInst* inst, const EmitOpInfo& outerPrec)
{
    // In WGSL, the structured buffer types are converted to ptr<AS, array<E>, AM>
    // everywhere, except for the global parameter declaration.
    // Thus, when these globals are used in expressions, we need an ampersand.

    if (inst->getOp() == kIROp_GlobalParam)
    {
        switch (inst->getDataType()->getOp())
        {
        case kIROp_HLSLStructuredBufferType:
        case kIROp_HLSLRWStructuredBufferType:

            m_writer->emit("(&");
            CLikeSourceEmitter::emitOperandImpl(inst, outerPrec);
            m_writer->emit(")");
            return;
        }
    }

    CLikeSourceEmitter::emitOperandImpl(inst, outerPrec);
}

void WGSLSourceEmitter::emitGlobalParamType(IRType* type, const String& name)
{
    // In WGSL, the structured buffer types are converted to ptr<AS, array<E>, AM>
    // everywhere, except for the global parameter declaration.

    switch (type->getOp())
    {

    case kIROp_HLSLStructuredBufferType:
    case kIROp_HLSLRWStructuredBufferType:
    {
        StringSliceLoc nameAndLoc(name.getUnownedSlice());
        NameDeclaratorInfo nameDeclarator(&nameAndLoc);
        emitDeclarator(&nameDeclarator);
        m_writer->emit(" : ");
        auto structuredBufferType = as<IRHLSLStructuredBufferTypeBase>(type);
        m_writer->emit("array");
        m_writer->emit("<");
        emitType(structuredBufferType->getElementType());
        m_writer->emit(">");
    }
    break;

    default:

        emitType(type, name);
        break;

    }

}

void WGSLSourceEmitter::emitFrontMatterImpl(TargetRequest* /* targetReq */)
{
    if (m_f16ExtensionEnabled)
    {
        m_writer->emit("enable f16;\n");
        m_writer->emit("\n");
    }
}

} // namespace Slang
