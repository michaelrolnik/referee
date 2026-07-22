/*
 *  MIT License
 *
 *  Copyright (c) 2022-2026 Michael Rolnik
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "compile.hpp"
#include "rewrite.hpp"
#include "typecalc.hpp"
#include "strings.hpp"
#include "../factory.hpp"
#include "../builtins.hpp"

#include <functional>
#include <vector>
#include <map>
#include <set>

namespace {
//  An unbounded accumulator folds the same way the until/release family does,
//  so it belongs on the same linear path. A *windowed* one does not -- the
//  window is anchored at the evaluation point, so neighbouring states sum
//  different ranges -- but it is already O(N x window), which is linear in the
//  trace. Only the unbounded form walked to the end from every state.
bool isLoopAccumulator(Expr* expr)
{
    auto*   sum = dynamic_cast<ExprSum*>(expr);

    return  sum != nullptr && sum->time == nullptr;
}

bool isLoopTemporal(Expr* expr)
{
    if (!expr) return false;
    return dynamic_cast<ExprUs*>(expr) || dynamic_cast<ExprUw*>(expr) ||
           dynamic_cast<ExprRs*>(expr) || dynamic_cast<ExprRw*>(expr) ||
           dynamic_cast<ExprSs*>(expr) || dynamic_cast<ExprSw*>(expr) ||
           dynamic_cast<ExprTs*>(expr) || dynamic_cast<ExprTw*>(expr);
}

//  Is any context name referenced in `expr` *free* -- that is, bound by a
//  freeze (`@`) further out rather than by one inside `expr` itself?
//
//  This is the eligibility test for the buffered lowering, which computes an
//  operator once and keys it on state index alone.  That is sound exactly when
//  the operator's value is a function of the state index, and a free context
//  reference is what breaks that: it makes the value depend on where the
//  enclosing freeze was evaluated.
//
//  Names bound *within* `expr` do not disqualify it.  In `Us(t@(... t.a ...), b)`
//  the freeze re-binds `t` at each state the Us itself is evaluated at, so the
//  Us as a whole is still a function of the state index and can be buffered --
//  only the temporal operators nested under that freeze cannot be, and
//  collectTemporals() declines to collect those.
bool hasFreeContext(Expr* expr, std::set<std::string> const& bound)
{
    if (!expr) return false;

    if (auto* ctx = dynamic_cast<ExprContext*>(expr))
    {
        return ctx->name != "__curr__"
            && ctx->name != "__conf__"
            && !bound.contains(ctx->name);
    }

    if (auto* data = dynamic_cast<ExprData*>(expr))
        return hasFreeContext(data->ctxt, bound);

    if (auto* conf = dynamic_cast<ExprConf*>(expr))
        return hasFreeContext(conf->ctxt, bound);

    //  Before ExprUnary: ExprAt derives from it, so the unary branch would
    //  otherwise swallow it and lose the binding it introduces.
    if (auto* at = dynamic_cast<ExprAt*>(expr))
    {
        auto    inner = bound;
        inner.insert(at->name);
        return hasFreeContext(at->arg, inner);
    }

    if (auto* ternary = dynamic_cast<ExprTernary*>(expr))
    {
        return hasFreeContext(ternary->lhs, bound)
            || hasFreeContext(ternary->mhs, bound)
            || hasFreeContext(ternary->rhs, bound);
    }

    if (auto* binary = dynamic_cast<ExprBinary*>(expr))
    {
        return hasFreeContext(binary->lhs, bound)
            || hasFreeContext(binary->rhs, bound);
    }

    if (auto* unary = dynamic_cast<ExprUnary*>(expr))
        return hasFreeContext(unary->arg, bound);

    return false;
}

//  Is `expr` loop-invariant -- the same value at every state?
//
//  This is the eligibility test for the *bounded* buffered lowering, which
//  advances its window with a monotone pointer.  That only works if the window
//  endpoints move monotonically with the evaluation point, which in turn needs
//  the bound itself to be a constant of the trace.  Literals and `conf` values
//  qualify; anything reading a `data` signal or a frozen state does not, and
//  such operators stay on the nested scan.
bool isLoopInvariant(Expr* expr)
{
    if (!expr) return true;

    if (dynamic_cast<ExprData*>(expr))      return false;   //  varies per state
    if (dynamic_cast<ExprAt*>(expr))        return false;   //  freezes a state
    if (dynamic_cast<ExprConf*>(expr))      return true;    //  constant for the run

    if (auto* ctx = dynamic_cast<ExprContext*>(expr))
        return ctx->name == "__conf__";

    if (auto* ternary = dynamic_cast<ExprTernary*>(expr))
    {
        return isLoopInvariant(ternary->lhs)
            && isLoopInvariant(ternary->mhs)
            && isLoopInvariant(ternary->rhs);
    }

    if (auto* binary = dynamic_cast<ExprBinary*>(expr))
        return isLoopInvariant(binary->lhs) && isLoopInvariant(binary->rhs);

    if (auto* unary = dynamic_cast<ExprUnary*>(expr))
        return isLoopInvariant(unary->arg);

    return true;    //  constants and other nullary leaves
}

//  Both bounds of a time window must be loop-invariant for the window to slide
//  monotonically.  An absent bound is trivially invariant.
bool hasLoopInvariantTime(Time* time)
{
    if (!time) return true;
    return isLoopInvariant(time->lo) && isLoopInvariant(time->hi);
}

void collectTemporals(Expr* expr, std::vector<Expr*>& temporals)
{
    if (!expr) return;

    //  ExprAt must be tested before ExprUnary -- it derives from it, so the
    //  ExprUnary branch would otherwise swallow it and descend anyway.
    if (dynamic_cast<ExprAt*>(expr))
    {
        //  Deliberately do NOT descend into a freeze (`@`) body.
        //
        //  A temporal operator inside a freeze that mentions the frozen name
        //  denotes different things at different evaluation points, so it can
        //  never be reduced to one buffer indexed by state.  One that does not
        //  mention the frozen name could in principle be buffered, but the
        //  buffer would have to be built outside the enclosing loop -- not
        //  worth the machinery, given how rare `@` is.
        //
        //  Note this does not disqualify a temporal operator that *contains* a
        //  freeze: `Us(t@(...), b)` is still collected below and still buffered,
        //  because the Us is evaluated once per state like any other.
    }
    else if (auto* ternary = dynamic_cast<ExprTernary*>(expr))
    {
        collectTemporals(ternary->lhs, temporals);
        collectTemporals(ternary->mhs, temporals);
        collectTemporals(ternary->rhs, temporals);
    }
    else if (auto* binary = dynamic_cast<ExprBinary*>(expr))
    {
        collectTemporals(binary->lhs, temporals);
        collectTemporals(binary->rhs, temporals);
    }
    else if (auto* unary = dynamic_cast<ExprUnary*>(expr))
    {
        collectTemporals(unary->arg, temporals);
    }

    if (isLoopTemporal(expr) || isLoopAccumulator(expr))
    {
        if (std::find(temporals.begin(), temporals.end(), expr) == temporals.end())
            temporals.push_back(expr);
    }
}
} // namespace

struct CompileTypeImpl
    : Visitor<  TypeInteger
             ,  TypeNumber
             ,  TypeString
             ,  TypeBoolean
             ,  TypeByte
             ,  TypeStruct
             ,  TypeEnum
             ,  TypeArray>
{
    CompileTypeImpl(
            llvm::LLVMContext*  context,
            llvm::Module*       module);

    llvm::Type*     make(Type* type, std::string name);
    llvm::Value*    make(Expr* expr);

    void    visit(TypeInteger*          type) override;
    void    visit(TypeNumber*           type) override;
    void    visit(TypeString*           type) override;
    void    visit(TypeBoolean*          type) override;
    void    visit(TypeByte*             type) override;
    void    visit(TypeStruct*           type) override;
    void    visit(TypeEnum*             type) override;
    void    visit(TypeArray*            type) override;

    llvm::Type*         m_type  = nullptr;
    std::string         m_name;

private:
    llvm::LLVMContext*  m_context;
    llvm::Module*       m_module;
    llvm::Function*     m_func;
    std::unique_ptr<llvm::IRBuilder<>>
                        m_builder;
};

struct CompileExprImpl
    : Visitor< ExprAdd
             , ExprAnd
             , ExprAt
             , ExprChoice
             , ExprConstBoolean
             , ExprConstInteger
             , ExprConstNumber
             , ExprConstString
             , ExprContext
             , ExprData
             , ExprConf
             , ExprDiv
             , ExprEq
             , ExprEqu
             , ExprGe
             , ExprGt
             , ExprIndx
             , ExprInt
             , ExprSum
             , ExprLe
             , ExprLt
             , ExprMmbr
             , ExprMod
             , ExprMul
             , ExprNe
             , ExprNeg
             , ExprNot
             , ExprOr
             , ExprParen
             , ExprRs
             , ExprRw
             , ExprSs
             , ExprSub
             , ExprSw
             , ExprTs
             , ExprTw
             , ExprUs
             , ExprUw
             , ExprXor
             , ExprCall
             , ExprSlice
             , ExprCount
             , ExprBinder
             , ExprBand
             , ExprBor
             , ExprShl
             , ExprShr
             , ExprBnot
             , ExprXs
             , ExprXw
             , ExprYs
             , ExprYw
             , Spec
             , SpecGlobally
             , SpecBefore
             , SpecAfter
             , SpecBetweenAnd
             , SpecAfterUntil>
{
    CompileExprImpl(
            llvm::LLVMContext*  context,
            llvm::Module*       module,
            llvm::IRBuilder<>*  builder,
            llvm::Function*     function,
            Module*             refmod,
            llvm::Type*         propType,
            llvm::Type*         confType);

    void    visit(ExprAdd*          expr) override;
    void    visit(ExprAnd*          expr) override;
    void    visit(ExprAt*           expr) override;
    void    visit(ExprChoice*       expr) override;
    void    visit(ExprConstBoolean* expr) override;
    void    visit(ExprConstInteger* expr) override;
    void    visit(ExprConstNumber*  expr) override;
    void    visit(ExprConstString*  expr) override;
    void    visit(ExprContext*      expr) override;
    void    visit(ExprConf*         expr) override;
    void    visit(ExprData*         expr) override;
    void    visit(ExprDiv*          expr) override;
    void    visit(ExprEq*           expr) override;
    void    visit(ExprEqu*          expr) override;
    void    visit(ExprGe*           expr) override;
    void    visit(ExprGt*           expr) override;
    void    visit(ExprIndx*         expr) override;
    void    visit(ExprInt*          expr) override;
    void    visit(ExprSum*          expr) override;
    void    visit(ExprLe*           expr) override;
    void    visit(ExprLt*           expr) override;
    void    visit(ExprMmbr*         expr) override;
    void    visit(ExprMod*          expr) override;
    void    visit(ExprMul*          expr) override;
    void    visit(ExprNe*           expr) override;
    void    visit(ExprNeg*          expr) override;
    void    visit(ExprNot*          expr) override;
    void    visit(ExprOr*           expr) override;
    void    visit(ExprParen*        expr) override;
    void    visit(ExprRs*           expr) override;
    void    visit(ExprRw*           expr) override;
    void    visit(ExprSs*           expr) override;
    void    visit(ExprSub*          expr) override;
    void    visit(ExprSw*           expr) override;
    void    visit(ExprTs*           expr) override;
    void    visit(ExprTw*           expr) override;
    void    visit(ExprUs*           expr) override;
    void    visit(ExprUw*           expr) override;
    void    visit(ExprXor*          expr) override;
    void    visit(ExprCall*         expr) override;
    void    visit(ExprSlice*        expr) override;
    void    visit(ExprCount*        expr) override;
    void    visit(ExprBinder*       expr) override;
    void    visit(ExprBand*      expr) override;
    void    visit(ExprBor*       expr) override;
    void    visit(ExprShl*       expr) override;
    void    visit(ExprShr*       expr) override;
    void    visit(ExprBnot*      expr) override;
    void    visit(ExprXs*           expr) override;
    void    visit(ExprXw*           expr) override;
    void    visit(ExprYs*           expr) override;
    void    visit(ExprYw*           expr) override;

    void    visit(Spec*             spec) override;
    void    visit(SpecGlobally*     spec) override;
    void    visit(SpecBefore*       spec) override;
    void    visit(SpecAfter*        spec) override;
    void    visit(SpecBetweenAnd*   spec) override;
    void    visit(SpecAfterUntil*   spec) override;

    void    compare(
                llvm::CmpInst::Predicate    ipred,
                llvm::CmpInst::Predicate    fpred, ExprBinary* expr);
    void    arithmetic(
                ExprBinary* expr,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ifunc,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ffunc);

    void    XY( ExprBinary*         expr,
                llvm::Value*        direction,
                llvm::Value*        endValue,
                std::string         name);

    void    UR( Temporal<ExprBinary>*   expr,
                llvm::Value*            rhsV,
                llvm::Value*            lhsV,
                llvm::Value*            endV,
                std::string             name);
    void    ST( Temporal<ExprBinary>*   expr,
                llvm::Value*            rhsV,
                llvm::Value*            lhsV,
                llvm::Value*            endV,
                std::string             name);

    llvm::Value*    make(Expr* expr);
    llvm::Value*    make(Spec* spec);

    llvm::Value*    getNext(llvm::Value* curr);
    llvm::Value*    getPrev(llvm::Value* curr);
    llvm::Value*    getTime(llvm::Value* curr, std::string name = "__time__");
    llvm::Value*    getPropPtr(llvm::Value* var);
    llvm::Value*    setPropPtr(llvm::Value* var, llvm::Value* val);
    llvm::Value*    getBool(llvm::Value* var);
    llvm::Value*    widenByte(llvm::Value* value, Type* type);
    Type*           valueType(Expr* expr);
    llvm::Value*    setBool(llvm::Value* var, llvm::Value* val);
    void            compileTemporalLoops(Expr* rootExpr);
    llvm::Value*    compileTemporalLoopInline(Expr*        expr,
                                              llvm::Value* rhsV,
                                              llvm::Value* lhsV,
                                              llvm::Value* endV,
                                              bool         isUR);
    llvm::Value*    compileTemporalLoopBounded(Temporal<ExprBinary>* expr,
                                               llvm::Value* rhsV,
                                               llvm::Value* lhsV,
                                               llvm::Value* endV,
                                               bool         isUR);
    llvm::Value*    sliceCount(ExprSlice* expr);
    llvm::Value*    shortCircuit(Expr* lhs, Expr* rhs, bool isAnd);
    llvm::Value*    compileAccumulatorLoop(ExprSum* expr, llvm::Type* type);
    llvm::Value*    guardIndex(llvm::Value* ptr,  llvm::Value* indx,
                               llvm::Value* count, llvm::Type* elemType, Expr* expr);

    //  The value each live quantifier binder currently stands for, by id.
    //  Innermost quantifiers simply add another entry; the ids are distinct,
    //  so nesting needs no stack discipline beyond removing the entry when the
    //  loop that owns it is done.
    std::map<unsigned, llvm::Value*>    m_binders;
    llvm::Value*    sliceExtent(Expr* expr);
    llvm::Value*    timeAtIndex(llvm::Value* idx);
    void            emitDecisivePass(Temporal<ExprBinary>* expr,
                                     llvm::Value* rhsV,
                                     llvm::Value* lhsV,
                                     bool         isUR,
                                     llvm::Value* decV,
                                     llvm::Value* decI,
                                     llvm::Value* nm2);
    llvm::Value*    emitMonotoneWalk(llvm::Value*             init,
                                     llvm::Value*             bound,
                                     bool                     probeIsNext,
                                     llvm::Value*             limit,
                                     llvm::CmpInst::Predicate keepPred,
                                     std::string const&       name);

    //  Buffers are only valid in blocks dominated by the one they were built
    //  in.  Callers that emit several independent loop nests into the same
    //  function (see __prepare__) must drop them between nests.
    void            resetTemporalBuffers()  {m_temporalBuffers.clear(); m_accumBuffers.clear();}

    std::vector<llvm::Value*>   m_frst;
    std::vector<llvm::Value*>   m_last;
    std::vector<llvm::Value*>   m_curr;

private:
    llvm::Value*        add(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);
    llvm::Value*        mul(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);
    llvm::Value*        sub(llvm::Value* lhs, llvm::Value* rhs, std::string const& name);

private:
    llvm::LLVMContext*  m_context;
    llvm::Module*       m_module;
    llvm::Function*     m_function;
    llvm::IRBuilder<>*  m_builder;

    llvm::Value*        m_value;
    std::vector<std::pair<std::string, llvm::Value*>>
                        m_name2value;

    Module*             m_refmod;
    llvm::Value*        m_0;
    llvm::Value*        m_p1;
    llvm::Value*        m_m1;
    llvm::Value*        m_T;
    llvm::Value*        m_F;
    llvm::Value*        m_conf;
    llvm::Type*         m_propType;
    llvm::Type*         m_propPtrType;
    llvm::Type*         m_confType;
    llvm::Type*         m_confPtrType;
    llvm::Type*         m_boolType;
    std::map<Expr*, llvm::Value*>
                        m_temporalBuffers;

    //  Accumulator buffers carry a value rather than a bit, so the element
    //  type travels with the pointer.
    std::map<Expr*, std::pair<llvm::Value*, llvm::Type*>>
                        m_accumBuffers;
};


CompileTypeImpl::CompileTypeImpl(
            llvm::LLVMContext*  context,
            llvm::Module*       module)
    : m_context(context)
    , m_module(module)
    , m_builder(std::make_unique<llvm::IRBuilder<>>(*m_context))
{
}

void    CompileTypeImpl::visit(TypeInteger*          type)
{
    m_type  = m_builder->getInt64Ty();
}

void    CompileTypeImpl::visit(TypeNumber*           type)
{
    m_type  = m_builder->getDoubleTy();
}

void    CompileTypeImpl::visit(TypeString*           type)
{
    m_type  = m_builder->getPtrTy();
}

void    CompileTypeImpl::visit(TypeByte*             type)
{
    m_type  = m_builder->getInt8Ty();
}

//  An array with no written extent is `{size_t count; T const* data;}` --
//  the shape the generated C header declares, the shape an external function
//  is handed, and the shape a slice evaluates to. One definition, because
//  three spellings of it is how the count came to be two bytes in one place
//  and eight in the others.
static llvm::StructType*    descriptorType(llvm::LLVMContext*  context,
                                           llvm::IRBuilder<>&  builder)
{
    return  llvm::StructType::get(*context, {builder.getInt64Ty(), builder.getPtrTy()});
}

//  An array with no written extent carries its length with it: its value is a
//  `{count, pointer}` descriptor rather than the address of some storage.
//
//  This asks about the *type*, and the earlier version of every site below
//  asked whether the expression was syntactically an `ExprSlice`. Those are
//  not the same question, and a computed signal is where they part company:
//
//      data s = pkt[0:2];      #  s is a descriptor, but `s` is an ExprData
//
//  `s.count` answered 0 -- the declared extent of an unsized array -- and
//  said so without complaint, which is worse than the crash that indexing
//  `s` produced. A descriptor is a descriptor wherever it came from.
static bool     isDescriptor(Type* type)
{
    auto*   array = dynamic_cast<TypeArray*>(type);

    return  array != nullptr && array->count == 0;
}

void    CompileTypeImpl::visit(TypeBoolean*          type)
{
    m_type  = m_builder->getInt1Ty();
}

void    CompileTypeImpl::visit(TypeStruct*           type)
{
    std::vector<llvm::Type*>    elements;

    for(auto member: type->members)
    {
        elements.push_back(Compile::make(m_context, m_module, member.data, m_name + "::" + member.name));
    }
    m_type  = llvm::StructType::create(*m_context, elements, m_name);
}

void    CompileTypeImpl::visit(TypeEnum*             type)
{
    m_type  = m_builder->getInt8Ty();
}

void    CompileTypeImpl::visit(TypeArray*            type)
{
    auto    base    = Compile::make(m_context, m_module, type->type, m_name + "[]");
    auto    size    = type->count;

    if(size == 0)
    {
        //  `{size_t count; T const* data;}` -- the same shape the generated
        //  header declares and the same one every descriptor-building site
        //  emits. This was `{i16, ptr}`, which agrees with an i64 count on a
        //  little-endian host for every length below 65536 and silently
        //  disagrees above it. Nothing reached it yet; the loader is about to.
        m_type  = descriptorType(m_context, *m_builder);
    }
    else
    {
        m_type  = llvm::ArrayType::get(base, size);
    }
}

llvm::Type* CompileTypeImpl::make(Type*  type, std::string name)
{
    m_name = name;

    type->accept(*this);

    return  this->m_type;
}

CompileExprImpl::CompileExprImpl(
            llvm::LLVMContext*  context,
            llvm::Module*       module,
            llvm::IRBuilder<>*  builder,
            llvm::Function*     function,
            Module*             refmod,
            llvm::Type*         propType,
            llvm::Type*         confType)
    : m_context(context)
    , m_module(module)
    , m_function(function)
    , m_builder(builder)
    , m_refmod(refmod)
{
    m_0     = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0);
    m_p1    = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), +1);
    m_m1    = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), -1);
    m_T     = llvm::ConstantInt::getTrue(*m_context);
    m_F     = llvm::ConstantInt::getFalse(*m_context);

    auto    iter    = function->arg_begin();

    m_frst.push_back(iter++);
    m_last.push_back(iter++);
    m_conf  = iter;
    m_propType      = propType;
    m_propPtrType   = m_frst.front()->getType();
    m_confType      = confType;
    m_confPtrType   = m_conf->getType();
    m_boolType      = m_builder->getInt1Ty();

    m_curr.push_back(getNext(m_frst.front()));
}

void    CompileExprImpl::visit(ExprAdd*          expr)
{
    arithmetic(
        expr,
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateAdd(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFAdd(lhs, rhs);});
}

//  Short-circuiting `&&` and `||`.
//
//  These were `select`, which is a value and not a branch: both operands were
//  materialised into the same block and the operator merely chose between
//  them. That is the better lowering while every expression is total -- a load
//  at a fixed offset, arithmetic, a comparison -- and REF's were, so nothing
//  was wrong with it.
//
//  An array that carries its own length ended that. `rows.count > 1 &&
//  rows[1][0] == 5` has a right-hand side that dereferences a descriptor which
//  was never written wherever the guard is false, and the load sat above the
//  `select` and ran anyway. The guard read like a guard and was not one.
//
//  So the right-hand side goes in a block reached only when the left decides
//  nothing. That is what a guard means, and it is what everyone reading one
//  already assumes.
llvm::Value*    CompileExprImpl::shortCircuit(Expr* lhs, Expr* rhs, bool isAnd)
{
    auto    func    = m_builder->GetInsertBlock()->getParent();
    auto    name    = std::string(isAnd ? "and" : "or");

    auto    lhsVal  = make(lhs);
    auto    bbLhs   = m_builder->GetInsertBlock();
    auto    bbRhs   = llvm::BasicBlock::Create(*m_context, name + ".rhs",  func);
    auto    bbDone  = llvm::BasicBlock::Create(*m_context, name + ".done", func);

    //  `&&` needs the right-hand side only when the left held; `||` only when
    //  it did not.
    if(isAnd)   m_builder->CreateCondBr(lhsVal, bbRhs, bbDone);
    else        m_builder->CreateCondBr(lhsVal, bbDone, bbRhs);

    m_builder->SetInsertPoint(bbRhs);
    auto    rhsVal  = make(rhs);

    //  The right-hand side may have built blocks of its own -- a nested
    //  short circuit, or a quantifier -- so the edge into the join leaves
    //  from wherever it ended.
    auto    bbTail  = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbDone);

    m_builder->SetInsertPoint(bbDone);
    auto    phi     = m_builder->CreatePHI(m_builder->getInt1Ty(), 2, name);

    phi->addIncoming(m_builder->getInt1(!isAnd), bbLhs);    //  the decided case
    phi->addIncoming(rhsVal, bbTail);

    return  phi;
}

void    CompileExprImpl::visit(ExprAnd*          expr)
{
    m_value = shortCircuit(expr->lhs, expr->rhs, true);
}

void    CompileExprImpl::visit(ExprAt*           expr)
{
    m_name2value.push_back(std::make_pair(expr->name, m_curr.back()));
    m_value = make(expr->arg);
    m_name2value.pop_back();
}

//  `c ? a : b`, with only the chosen arm evaluated. It was a `select` too, and
//  it is the other expression someone reaches for when they want a guard -- so
//  leaving it evaluating both arms would have moved the trap rather than
//  removed it.
void    CompileExprImpl::visit(ExprChoice*       expr)
{
    auto    func    = m_builder->GetInsertBlock()->getParent();
    auto    cond    = make(expr->lhs);
    auto    bbThen  = llvm::BasicBlock::Create(*m_context, "sel.then", func);
    auto    bbElse  = llvm::BasicBlock::Create(*m_context, "sel.else", func);
    auto    bbDone  = llvm::BasicBlock::Create(*m_context, "sel.done", func);

    m_builder->CreateCondBr(cond, bbThen, bbElse);

    m_builder->SetInsertPoint(bbThen);
    auto    mhs     = make(expr->mhs);
    auto    bbMhs   = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbDone);

    m_builder->SetInsertPoint(bbElse);
    auto    rhs     = make(expr->rhs);
    auto    bbRhs   = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbDone);

    //  One arm integer and the other a number: both widen to double. The
    //  conversion belongs in the arm that needs it, not at the join, so it is
    //  inserted before that arm's branch rather than after both.
    if(mhs->getType() != rhs->getType())
    {
        auto    widen = [&](llvm::Value* v, llvm::BasicBlock* bb)
        {
            if(v->getType()->isDoubleTy())
                return v;

            m_builder->SetInsertPoint(bb->getTerminator());
            return static_cast<llvm::Value*>(
                        m_builder->CreateSIToFP(v, m_builder->getDoubleTy()));
        };

        mhs = widen(mhs, bbMhs);
        rhs = widen(rhs, bbRhs);
    }

    m_builder->SetInsertPoint(bbDone);
    auto    phi     = m_builder->CreatePHI(mhs->getType(), 2, "sel");

    phi->addIncoming(mhs, bbMhs);
    phi->addIncoming(rhs, bbRhs);

    m_value = phi;
}

void    CompileExprImpl::visit(ExprConstBoolean* expr)
{
    m_value = llvm::ConstantInt::getBool(*m_context, expr->value);
}

void    CompileExprImpl::visit(ExprConstInteger* expr)
{
    m_value = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), expr->value);
}

void    CompileExprImpl::visit(ExprConstNumber*  expr)
{
    m_value = llvm::ConstantFP::get(m_builder->getDoubleTy(), expr->value);
}

void    CompileExprImpl::visit(ExprConstString*  expr)
{
    auto    value   = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), reinterpret_cast<int64_t>(Strings::instance()->getString(expr->value)));

    m_value = m_builder->CreateIntToPtr(value, m_builder->getPtrTy(), expr->value);
}

void    CompileExprImpl::visit(ExprContext*      expr)
{
    if(expr->name == "__curr__")
    {
        m_value = m_curr.back();
    }
    else if(expr->name == "__conf__")
    {
        m_value = m_conf;
    }
    else
    {
        for(auto it = m_name2value.rbegin(); it != m_name2value.rend(); it++)
        {
            if(expr->name == it->first)
            {
                m_value = it->second;
                return;
            }
        }

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw Exception(expr->where(), "internal: unresolved context '" + expr->name + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprConf*         expr)
{
    auto    ctxtPtr     = make(expr->ctxt);
    auto    ctxtType    = m_confType;

    auto    confPtr     = m_builder->CreateStructGEP(ctxtType, ctxtPtr, dynamic_cast<TypeContext*>(expr->ctxt->type())->index(expr->name));

    if(dynamic_cast<TypePrimitive*>(expr->type()))
    {
        //  Load through confPtr, not m_value: make() restores m_value once the
        //  sub-expression is built, so by here it holds whatever was current
        //  before this node -- null at the start of a function, and some
        //  unrelated pointer in the middle of one.
        auto    fieldType   = Compile::make(m_context, m_module, expr->type(), expr->name);
        m_value = widenByte(m_builder->CreateLoad(fieldType, confPtr, false, "val_" + expr->name),
                            m_refmod->getConf(expr->name));
    }
    else
    {
        m_value = confPtr;
    }
}

void    CompileExprImpl::visit(ExprData*         expr)
{
    auto    ctxtPtr     = make(expr->ctxt);
    auto    ctxtType    = m_propType;

    if(expr->name == "__time__")
    {
        auto    propPtr         = m_builder->CreateStructGEP(ctxtType, ctxtPtr, 0);     //  skip __time__
        m_value = m_builder->CreateLoad(m_builder->getInt64Ty(), propPtr, false, "__time__");
    }
    else
    {
        auto    propPtrPtr      = m_builder->CreateStructGEP(ctxtType, ctxtPtr, dynamic_cast<TypeContext*>(expr->ctxt->type())->index(expr->name) + 1); //  +1 to skip __time__
        auto    propPtrType     = m_builder->getPtrTy();

        m_value = m_builder->CreateLoad(propPtrType, propPtrPtr, false, "ptr_" + expr->name);

        //  A signal's value is the address of its storage, except where the
        //  storage holds something small enough to be the value itself. A
        //  primitive is loaded; so is a descriptor, whose sixteen bytes *are*
        //  the array -- a count and a pointer to elements living elsewhere.
        //  Handing back the address of a descriptor instead would make `s`
        //  and `pkt[0:2]` different kinds of thing, which is precisely the
        //  split this change exists to remove.
        if(dynamic_cast<TypePrimitive*>(expr->type()))
        {
            auto    propFieldType   = Compile::make(m_context, m_module, expr->type(), expr->name);
            m_value = widenByte(m_builder->CreateLoad(propFieldType, m_value, false, "val_" + expr->name),
                                m_refmod->getProp(expr->name));
        }
        else if(isDescriptor(expr->type()))
        {
            m_value = m_builder->CreateLoad(descriptorType(m_context, *m_builder),
                                            m_value, false, "desc_" + expr->name);
        }
    }
}

void    CompileExprImpl::visit(ExprDiv*          expr)
{
    arithmetic(
        expr,
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateSDiv(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFDiv(lhs, rhs);});
}

void    CompileExprImpl::compare(
            llvm::CmpInst::Predicate    ipred,
            llvm::CmpInst::Predicate    fpred, ExprBinary* expr)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    auto    lhsT    = valueType(expr->lhs);
    auto    rhsT    = valueType(expr->rhs);

    if(     lhsT == Factory<TypeNumber>::create()
        && rhsT == Factory<TypeInteger>::create())
    {
        rhs = m_builder->CreateSIToFP(rhs, lhs->getType());
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create()
        &&  rhsT == Factory<TypeNumber>::create())
    {
        lhs = m_builder->CreateSIToFP(lhs, rhs->getType());
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create()
        &&  rhsT == Factory<TypeInteger>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeNumber>::create()
        &&  rhsT == Factory<TypeNumber>::create())
    {
        m_value = m_builder->CreateFCmp(fpred, lhs, rhs);
    }
    else if(lhsT == Factory<TypeString>::create()
        &&  rhsT == Factory<TypeString>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);   //  TODO: check
    }
    else if(lhsT == Factory<TypeBoolean>::create()
        &&  rhsT == Factory<TypeBoolean>::create())
    {
        m_value = m_builder->CreateICmp(ipred, lhs, rhs);   //  TODO: check
    }
    else if(lhsT == rhsT
        &&  dynamic_cast<TypeEnum*>(lhsT) != nullptr)
    {
        //  An enum is modelled as a *composite*, so `visit(ExprData*)` does
        //  not load it -- a bare enum-valued expression yields a pointer to
        //  its one-byte storage. Load both sides here. Comparing the pointers
        //  instead would answer "the same state?" rather than "the same
        //  value?", which is accidentally true whenever both operands are read
        //  at one state and false as soon as they are not.
        //
        //  The stored value is the member index, so equality on it is equality
        //  on the value. Type calculation has already refused ordering and
        //  mismatched enum types, so only == and != arrive here.
        auto    slot = Compile::make(m_context, m_module, lhsT, "enum");

        m_value = m_builder->CreateICmp(ipred,
                    m_builder->CreateLoad(slot, lhs, false, "val_lhs"),
                    m_builder->CreateLoad(slot, rhs, false, "val_rhs"));
    }
    else
    {
        throw Exception(expr->where(),
            "bad type: these operands cannot be compared");
    }
}

void    CompileExprImpl::arithmetic(
                ExprBinary* expr,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ifunc,
                std::function<llvm::Value*(llvm::Value*, llvm::Value*)> ffunc)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    auto    lhsT    = valueType(expr->lhs);
    auto    rhsT    = valueType(expr->rhs);

    if(     lhsT == Factory<TypeNumber>::create()
        && rhsT == Factory<TypeInteger>::create())
    {
        rhs = m_builder->CreateSIToFP(rhs, lhs->getType());
        m_value = ffunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create()
        &&  rhsT == Factory<TypeNumber>::create())
    {
        lhs = m_builder->CreateSIToFP(lhs, rhs->getType());
        m_value = ffunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeInteger>::create()
        &&  rhsT == Factory<TypeInteger>::create())
    {
        m_value = ifunc(lhs, rhs);
    }
    else if(lhsT == Factory<TypeNumber>::create()
        &&  rhsT == Factory<TypeNumber>::create())
    {
        m_value = ffunc(lhs, rhs);
    }
    else
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw Exception(expr->where(), "bad type: these operands cannot be used in arithmetic");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprEq*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_EQ, llvm::CmpInst::Predicate::FCMP_OEQ, expr);
}

void    CompileExprImpl::visit(ExprEqu*          expr)
{
    auto    lhs     = make(expr->lhs);
    auto    rhs     = make(expr->rhs);
    m_value = m_builder->CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs);
}

void    CompileExprImpl::visit(ExprGe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGE, llvm::CmpInst::Predicate::FCMP_OGE, expr);
}

void    CompileExprImpl::visit(ExprGt*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SGT, llvm::CmpInst::Predicate::FCMP_OGT, expr);
}

//  Where an out-of-range read is recorded. Four globals rather than a call,
//  because the check is on the hot path and a fault is not: the in-range case
//  costs two compares and a branch that is never taken.
//
//  The reported verdict cannot be invented here. A requirement returns one
//  boolean and the host decides what it means -- returning `false` from inside
//  a negated requirement would read as a pass. So the fault is raised, the
//  read is answered from a zeroed buffer so evaluation stays total, and the
//  host turns the flag into a failure once the requirement has finished.
//
//  Deliberately no source position. `expr->where()` is the *interned* node's,
//  and two textually identical index expressions in different requirements are
//  one node -- so it named an arbitrary one of them, off by seven lines in the
//  first fixture that had two. The requirement is reported instead, which is
//  always right.
static llvm::GlobalVariable*    faultSlot(llvm::Module* module, llvm::LLVMContext* context,
                                          char const* name, llvm::Type* type)
{
    if(auto* found = module->getGlobalVariable(name))
        return found;

    return new llvm::GlobalVariable(*module, type, false,
                llvm::GlobalValue::ExternalLinkage,
                llvm::Constant::getNullValue(type), name);
}

llvm::Value*    CompileExprImpl::guardIndex(llvm::Value* ptr,   llvm::Value* indx,
                                            llvm::Value* count, llvm::Type* elemType,
                                            Expr*        expr)
{
    auto    i64Ty   = m_builder->getInt64Ty();
    auto    zero    = llvm::ConstantInt::get(i64Ty, 0);
    auto    inRange = m_builder->CreateAnd(
                        m_builder->CreateICmpSGE(indx, zero,  "indx >= 0"),
                        m_builder->CreateICmpSLT(indx, count, "indx < count"),
                        "inRange");

    auto    func    = m_builder->GetInsertBlock()->getParent();
    auto    bbFrom  = m_builder->GetInsertBlock();
    auto    bbOob   = llvm::BasicBlock::Create(*m_context, "oob",      func);
    auto    bbCont  = llvm::BasicBlock::Create(*m_context, "oob.cont", func);

    m_builder->CreateCondBr(inRange, bbCont, bbOob);

    //  A zeroed buffer of the element's own shape, so the read that follows is
    //  answered rather than skipped -- the caller does not have to know it was
    //  guarded, and a struct element still yields a pointer to something whose
    //  members are readable.
    auto    zeroName = "__oob_zero_" + std::to_string(
                        m_module->getDataLayout().getTypeAllocSize(elemType).getFixedValue());
    auto    zeroBuf  = faultSlot(m_module, m_context, zeroName.c_str(), elemType);

    m_builder->SetInsertPoint(bbOob);
    m_builder->CreateStore(m_builder->getInt8(1),
                           faultSlot(m_module, m_context, "__oob_flag__", m_builder->getInt8Ty()));
    m_builder->CreateStore(indx,
                           faultSlot(m_module, m_context, "__oob_indx__", i64Ty));
    m_builder->CreateStore(count,
                           faultSlot(m_module, m_context, "__oob_cnt__",  i64Ty));
    m_builder->CreateBr(bbCont);

    m_builder->SetInsertPoint(bbCont);
    auto    phi = m_builder->CreatePHI(m_builder->getPtrTy(), 2, "elem");

    phi->addIncoming(ptr,     bbFrom);
    phi->addIncoming(zeroBuf, bbOob);

    return  phi;
}

void    CompileExprImpl::visit(ExprIndx*         expr)
{
    auto    exprType    = expr->type();
    auto*   arrayType   = dynamic_cast<TypeArray*>(expr->lhs->type());
    auto    elemType    = Compile::make(m_context, m_module, arrayType->type, "[]");
    auto    indx        = make(expr->rhs);

    //  Indexing an array is a subscript into its storage. Indexing a
    //  *descriptor* starts from the pointer it carries, which already
    //  addresses the first element -- so the index is an offset from there,
    //  not from whatever the descriptor was taken from. Casting a
    //  `{count, ptr}` to an array type is what used to happen, and it aborted
    //  in LLVM rather than producing a wrong answer, which was the lucky
    //  version.
    llvm::Value*    count = nullptr;

    if(isDescriptor(expr->lhs->type()))
    {
        auto    desc = make(expr->lhs);
        auto    data = m_builder->CreateExtractValue(desc, {1}, "slice.data");

        count   = m_builder->CreateExtractValue(desc, {0}, "slice.count");
        m_value = m_builder->CreateGEP(elemType, data, indx, "ptr_[]");
    }
    else
    {
        auto    basePtr     = make(expr->lhs);
        auto    baseLLVMType= Compile::make(m_context, m_module, expr->lhs->type(), "[]");

        count   = llvm::ConstantInt::get(m_builder->getInt64Ty(), arrayType->count);
        m_value = m_builder->CreateGEP(cast<llvm::ArrayType>(baseLLVMType),
                                       basePtr, {m_0, indx}, "ptr_[]");
    }

    m_value = guardIndex(m_value, indx, count, elemType, expr);

    if(dynamic_cast<TypePrimitive*>(exprType))
    {
        m_value = widenByte(m_builder->CreateLoad(elemType, m_value, false, "val_[]"),
                            arrayType->type);
    }
    else if(isDescriptor(exprType))
    {
        //  An array of arrays: the element is itself a descriptor, so it is
        //  loaded rather than pointed at.
        m_value = m_builder->CreateLoad(descriptorType(m_context, *m_builder),
                                        m_value, false, "desc_[]");
    }
}

llvm::Value*    CompileExprImpl::add(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateAdd(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFAdd(lhs, rhs, name);
}

llvm::Value*    CompileExprImpl::mul(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateMul(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFMul(lhs, rhs, name);
}

llvm::Value*    CompileExprImpl::sub(llvm::Value* lhs, llvm::Value* rhs, std::string const& name)
{
    auto    typeInteger = m_builder->getInt64Ty();
    auto    typeNumber  = m_builder->getDoubleTy();

    if(lhs->getType() == typeInteger && rhs->getType() == typeInteger)
    {
        return  m_builder->CreateSub(lhs, rhs, name);
    }

    if(lhs->getType() == typeInteger)
    {
        lhs = m_builder->CreateSIToFP(lhs, typeNumber);
    }

    if(rhs->getType() == typeInteger)
    {
        rhs = m_builder->CreateSIToFP(rhs, typeNumber);
    }

    return  m_builder->CreateFSub(lhs, rhs, name);
}


//  Sum(v, q): total `v` across the states from here up to, but not including,
//  the first where `q` holds. Where ExprInt weights each step by its duration,
//  this counts records -- which is what a protocol's "bytes in this message"
//  or "packets before EOM" actually means.
//
//  A `[lo:hi]` window, if given, restricts which records contribute and gives
//  a second reason to stop.
void    CompileExprImpl::visit(ExprSum*          expr)
{
    //  Fast path: the O(N) fold, built once before the state loop.
    if (auto it = m_accumBuffers.find(expr); it != m_accumBuffers.end())
    {
        auto [buffer, type] = it->second;
        auto idx = m_builder->CreatePtrDiff(m_propType, m_curr.back(), m_frst.back(), "idx");

        m_value = m_builder->CreateLoad(type,
                    m_builder->CreateGEP(type, buffer, idx), false, "Sum");
        return;
    }

    //  Sum(p, v) -- the discrete twin of Itg: walk the states in the window,
    //  skip the ones where `p` does not hold, and total `v` over the rest.
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "Sum-head", m_function);
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "Sum-body", m_function);
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "Sum-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "Sum-tail", m_function);

    auto    curr0   = m_curr.back();
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto            now     = getTime(curr0);

    if(expr->time && expr->time->lo)
        timeLo  = m_builder->CreateAdd(now, make(expr->time->lo), "now + lo");
    if(expr->time && expr->time->hi)
        timeHi  = m_builder->CreateAdd(now, make(expr->time->hi), "now + hi");

    auto    isInt   = valueType(expr->rhs) == Factory<TypeInteger>::create();
    auto    type    = isInt ? static_cast<llvm::Type*>(m_builder->getInt64Ty())
                            : static_cast<llvm::Type*>(m_builder->getDoubleTy());
    auto    zero    = isInt ? static_cast<llvm::Value*>(
                                  llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0))
                            : static_cast<llvm::Value*>(
                                  llvm::ConstantFP::get(m_builder->getDoubleTy(), 0.0));

    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    //  head -- stop at the end of the trace, or once the window closes.
    m_builder->SetInsertPoint(bbHead);
    auto    total   = m_builder->CreatePHI(type, 2, "total");
    auto    curr    = m_builder->CreatePHI(curr0->getType(), 2, "curr");
    auto    cont    = m_builder->CreateICmpSLT(curr, last, "curr < last");
    if(timeHi)
    {
        auto    currT = getTime(curr, "curr->__time__");
        cont = m_builder->CreateAnd(cont,
                    m_builder->CreateICmpSLT(currT, timeHi, "currT < timeHi"));
    }
    m_builder->CreateCondBr(cont, bbBody, bbTail);

    //  body -- contribute where the condition holds, and not before the
    //  window opens. Skipped states leave the total alone rather than
    //  ending the walk, so a gap in `p` does not truncate the sum.
    m_builder->SetInsertPoint(bbBody);
    m_curr.push_back(curr);
    auto    cond    = make(expr->lhs);
    auto    value   = make(expr->rhs);
    m_curr.pop_back();

    llvm::Value*    take    = cond;
    if(timeLo)
    {
        auto    currT = getTime(curr, "curr->__time__");
        take = m_builder->CreateAnd(take,
                    m_builder->CreateICmpSGE(currT, timeLo, "currT >= timeLo"));
    }
    auto    contrib = m_builder->CreateSelect(take, value, zero, "contrib");
    auto    grown   = add(total, contrib, "total'");
    m_builder->CreateBr(bbNext);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    currNext = getNext(curr);
    auto    bbNextEnd = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    total->addIncoming(zero,  bbEntry);
    total->addIncoming(grown, bbNextEnd);
    curr->addIncoming(curr0,    bbEntry);
    curr->addIncoming(currNext, bbNextEnd);

    //  tail
    m_builder->SetInsertPoint(bbTail);

    m_value = total;
}

void    CompileExprImpl::visit(ExprInt*          expr)
{
/*
typedef long long uint64_t;
typedef struct prop_t
{
    uint64_t    __time__;
    uint64_t    a;
} prop_t;

bool        boolean(prop_t const* curr);
bool        integer(prop_t const* curr);

uint64_t    integral(uint64_t lo, uint64_t hi, prop_t const* curr, prop_t const* frst, prop_t const* last)
{
    prop_t const*   next    = curr + 1;
    uint64_t        result  = 0;

    lo  = lo + curr->__time__;
    hi  = hi + curr->__time__;

    while(next <= last)
    {
        if(hi <= curr->__time__)
        {
            break;
        }

        uint64_t    _lo = curr->__time__ < lo ? lo : curr->__time__;
        uint64_t    _hi = hi < next->__time__ ? hi : next->__time__;

        if(_lo < _hi)
        {
            if(boolean(curr))
            {
                uint64_t height = eval(curr);
                uint64_t length = (_hi - _lo);
                uint64_t volume = height * length;
                result  +=  volume;
            }
        }

        curr    = next;
        next    = next + 1;
    }

    return  result;
}

uint64_t    integral(prop_t const* curr, prop_t const* frst, prop_t const* last)
{
    prop_t const*   next    = curr + 1;
    uint64_t        result  = 0;

    while(next <= last)
    {
        uint64_t    _lo = curr->__time__;
        uint64_t    _hi = next->__time__;

        if(_lo < _hi)
        {
            if(boolean(curr))
            {
                uint64_t height = eval(curr);
                uint64_t length = (_hi - _lo);
                uint64_t volume = height * length;
                result  +=  volume;
            }
        }

        curr    = next;
        next    = next + 1;
    }

    return  result;
}
*/
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "I-head", m_function);

    auto    bbTime  = llvm::BasicBlock::Create(*m_context, "I-time", m_function);
    auto    bbCondHi= llvm::BasicBlock::Create(*m_context, "I-cond", m_function);
    auto    bbCondLo = bbCondHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "I-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "I-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "I-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    next0   = getNext(curr0);
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0);

    if(expr->time && expr->time->lo)
    {
        timeLo  = m_builder->CreateAdd(now, make(expr->time->lo), "now + lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeHi  = m_builder->CreateAdd(now, make(expr->time->hi), "now + hi");
    }

    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    auto    type    = valueType(expr->rhs) == Factory<TypeInteger>::create()
                    ? m_builder->getInt64Ty()
                    : m_builder->getDoubleTy();
    auto    zero    = valueType(expr->rhs) == Factory<TypeInteger>::create()
                    ? llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0)
                    : llvm::ConstantFP::get(m_builder->getDoubleTy(), 0.0);

    //  head
    m_builder->SetInsertPoint(bbHead);
    auto    sumHead = m_builder->CreatePHI(type, 2, "total@head");

    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr->__time__");
    auto    next    = m_builder->CreatePHI(frst->getType(), 2, "next->__time__");
    auto    cont    = m_builder->CreateICmpSLE(next, last, "next <= last");
    if(timeHi)
    {
        auto    currT   = getTime(curr, "curr->__time__");
        auto    timeHiLEcurrT
                        = m_builder->CreateICmpSGT(timeHi, currT, "timeHi > currT");
        cont    = m_builder->CreateAnd(cont, timeHiLEcurrT);
    }
    m_builder->CreateCondBr(cont, bbTime, bbTail);

    //  outer
    m_builder->SetInsertPoint(bbTime);

    auto    currT   = getTime(curr, "curr->__time__");
    auto    nextT   = getTime(next, "next->__time__");

    auto    lo      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(currT, timeLo), timeLo, currT)
                    : currT;
    auto    hi      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, nextT), timeHi, nextT)
                    : nextT;
    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbCondHi, bbNext);

    //  inner
    m_builder->SetInsertPoint(bbCondHi);
    m_curr.push_back(curr);
    auto    cond    = make(expr->lhs);
    m_curr.pop_back();
    bbCondLo       = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(cond, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(curr);
    auto    height  = make(expr->rhs);
    auto    length  = sub(hi, lo, "length");
    auto    volume  = mul(height, length, "volume");
    auto    sumBody = add(sumHead, volume, "total@body");
    m_curr.pop_back();

    bbBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbNext);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    sumNext = m_builder->CreatePHI(type, 3, "total@next");
    auto    curr1   = getNext(curr);
    auto    next1   = getNext(next);
    m_builder->CreateBr(bbHead);

    //  tail
    m_builder->SetInsertPoint(bbTail);

    //auto    result  = m_builder->CreatePHI(type, 1, "value");

    //  link
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    next->addIncoming(next0, bbEntry);
    next->addIncoming(next1, bbNext);

    //result->addIncoming(sumHead, bbHead);

    sumNext->addIncoming(sumBody, bbBodyLo);
    sumNext->addIncoming(sumHead, bbCondLo);
    sumNext->addIncoming(sumHead, bbTime);

    sumHead->addIncoming(zero,    bbEntry);
    sumHead->addIncoming(sumNext, bbNext);

    //result->addIncoming( sumHead, bbHead);

    m_value = sumHead;
}

void    CompileExprImpl::visit(ExprLe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SLE, llvm::CmpInst::Predicate::FCMP_OLE, expr);
}

void    CompileExprImpl::visit(ExprLt*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_SLT, llvm::CmpInst::Predicate::FCMP_OLT, expr);
}

void    CompileExprImpl::visit(ExprMmbr*         expr)
{
    auto    exprType    = expr->type();

    //  `xs.count`. For a sized array the extent is fixed when the AST is built,
    //  so it is a literal rather than a load -- answered before make(expr->arg),
    //  which would otherwise emit a pointer to an array nobody reads.
    //
    //  A slice has no fixed extent: its type carries count 0 and its length is
    //  `hi - lo`, known only at run time. This is the first place a `.count`
    //  is a value rather than a constant, and it is the shape every unbounded
    //  array will use once they carry their own length.
    if(auto* array = dynamic_cast<TypeArray*>(expr->arg->type()))
    {
        if(expr->mmbr == "count")
        {
            if(isDescriptor(expr->arg->type()))
            {
                m_value = sliceExtent(expr->arg);
                return;
            }

            m_value = llvm::ConstantInt::getSigned(m_builder->getInt64Ty(),
                                                   array->count);
            return;
        }
    }

    auto    basePtr     = make(expr->arg);
    auto    baseType    = Compile::make(m_context, m_module, expr->arg->type(), expr->mmbr);

    if(dynamic_cast<TypeComposite*>(exprType) || dynamic_cast<TypeArray*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        m_value = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);

        //  A member with no written extent is a descriptor, and a descriptor
        //  is a value -- the same rule `visit(ExprData*)` follows for a signal
        //  that holds one. Handing back the address of it would make
        //  `rec.raw` and `pkt` different kinds of thing again.
        if(isDescriptor(exprType))
            m_value = m_builder->CreateLoad(descriptorType(m_context, *m_builder),
                                            m_value, false, "desc_" + expr->mmbr);
    }
    else if(dynamic_cast<TypePrimitive*>(exprType))
    {
        auto    temp        = dynamic_cast<TypeComposite*>(expr->arg->type());

        if(dynamic_cast<TypeEnum*>(expr->arg->type()))
        {
            auto    data        = m_builder->CreateLoad(baseType, basePtr, false, "val_" + expr->mmbr);
            m_value = m_builder->CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, data, llvm::ConstantInt::getSigned(baseType, temp->index(expr->mmbr)));
        }
        else
        {
            auto    dataPtr     = m_builder->CreateStructGEP(baseType, basePtr, temp->index(expr->mmbr), "ptr_" + expr->mmbr);
            auto    dataType    = Compile::make(m_context, m_module, expr->type(), expr->mmbr);

            m_value = widenByte(m_builder->CreateLoad(dataType, dataPtr, false, "val_" + expr->mmbr),
                                temp->member(expr->mmbr));
        }
    }
    else
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw Exception(expr->where(), "internal: cannot read member '" + expr->mmbr + "' of this type");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprMod*          expr)
{
    m_value = m_builder->CreateURem(make(expr->lhs), make(expr->rhs));
}

void    CompileExprImpl::visit(ExprMul*          expr)
{
    arithmetic(
        expr,
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateMul(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFMul(lhs, rhs);});
}

void    CompileExprImpl::visit(ExprNe*           expr)
{
    compare(llvm::CmpInst::Predicate::ICMP_NE, llvm::CmpInst::Predicate::FCMP_ONE, expr);
}

void    CompileExprImpl::visit(ExprNeg*          expr)
{
    auto    arg     = make(expr->arg);
    auto    argT    = expr->arg->type();

    if(argT == Factory<TypeNumber>::create())
    {
        m_value = m_builder->CreateFNeg(arg);
    }

    else if(argT == Factory<TypeInteger>::create())
    {
        m_value = m_builder->CreateNeg(arg);
    }
    else
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw Exception(expr->where(), "bad type: negation needs a number");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }
}

void    CompileExprImpl::visit(ExprNot*          expr)
{
    auto    arg = make(expr->arg);
    m_value = m_builder->CreateNot(arg);
}

void    CompileExprImpl::visit(ExprOr*           expr)
{
    m_value = shortCircuit(expr->lhs, expr->rhs, false);
}

void    CompileExprImpl::visit(ExprParen*        expr)
{
    m_value = make(expr->arg);
}

void    CompileExprImpl::visit(ExprRs*           expr)
{
    UR(expr, m_F, m_T, m_F, "Rs");
}

void    CompileExprImpl::visit(ExprRw*           expr)
{
    UR(expr, m_F, m_T, m_T, "Rw");
}

void    CompileExprImpl::visit(ExprSs*           expr)
{
    ST(expr, m_T, m_F, m_F, "Ss");
}

void    CompileExprImpl::visit(ExprSub*          expr)
{
    arithmetic(
        expr,
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateSub(lhs, rhs);},
        [this](llvm::Value* lhs, llvm::Value* rhs) {return m_builder->CreateFSub(lhs, rhs);});
}

void    CompileExprImpl::visit(ExprSw*           expr)
{
    ST(expr, m_T, m_F, m_T, "Sw");
}

void    CompileExprImpl::visit(ExprTs*           expr)
{
    ST(expr, m_F, m_T, m_F, "Ts");
}

void    CompileExprImpl::visit(ExprTw*           expr)
{
    ST(expr, m_F, m_T, m_T, "Tw");
}

void    CompileExprImpl::visit(ExprUs*           expr)
{
    UR(expr, m_T, m_F, m_F, "Us");
}

void    CompileExprImpl::visit(ExprUw*           expr)
{
    UR(expr, m_T, m_F, m_T, "Uw");
}

//  A slice lowers to the descriptor an array crosses the boundary as, except
//  that both fields are values rather than one being a compile-time constant:
//  the count is `hi - lo`, and the data pointer is offset to element `lo`.
//
//  Bounds are not checked here. A slice wider than the array would read past
//  it, exactly as an out-of-range index does today -- the language has no
//  bounds checking to be consistent with, and adding it for this one construct
//  would be surprising rather than safe.
//  How many elements the thing being sliced has. A written extent is a
//  constant; a descriptor's is the count it carries.
llvm::Value*    CompileExprImpl::sliceExtent(Expr* expr)
{
    if(isDescriptor(expr->type()))
        return m_builder->CreateExtractValue(make(expr), {0}, "extent");

    auto*   array = dynamic_cast<TypeArray*>(expr->type());

    return  llvm::ConstantInt::get(m_builder->getInt64Ty(),
                                   array != nullptr ? array->count : 0);
}

//  A slice's length, clamped to the array it slices.
//
//  Two things go wrong without this. An inverted slice -- `pkt[3:1]` -- gives
//  a negative length, and a slice wider than its array -- `pkt[0:9]` over four
//  elements -- gives nine. The second is not merely a wrong answer: this count
//  is what the descriptor carries across the external-function boundary, so a
//  callee handed nine would read five elements past the end of the row.
//
//  So: bounds are clamped to [0, extent] and the length floors at zero. An
//  empty slice is representable; a negative or over-long one is not, because
//  neither describes any elements.
llvm::Value*    CompileExprImpl::sliceCount(ExprSlice* expr)
{
    //  What is being sliced bounds the slice. For an array that is its written
    //  extent; for another slice it is *that* slice's length, which is a value.
    //  Using the underlying array's extent here would let an outer slice reach
    //  past the inner one it was taken from.
    auto    extent  = sliceExtent(expr->arg);
    auto    zero    = llvm::ConstantInt::get(m_builder->getInt64Ty(), 0);
    auto    clamp   = [&](llvm::Value* v, char const* name)
    {
        v = m_builder->CreateSelect(m_builder->CreateICmpSLT(v, zero), zero, v);
        return m_builder->CreateSelect(m_builder->CreateICmpSGT(v, extent), extent, v, name);
    };

    auto    lo      = clamp(make(expr->lo), "slice.lo");
    auto    hi      = clamp(make(expr->hi), "slice.hi");
    auto    len     = m_builder->CreateSub(hi, lo, "slice.len");

    return  m_builder->CreateSelect(m_builder->CreateICmpSLT(len, zero), zero, len, "count");
}

void    CompileExprImpl::visit( ExprSlice*      expr)
{
    auto    lo      = make(expr->lo);
    auto    zero    = llvm::ConstantInt::get(m_builder->getInt64Ty(), 0);

    //  Where the elements start. Slicing an array indexes into it; slicing a
    //  descriptor starts from the pointer it carries, which already points at
    //  its own `lo`, so the offsets compose rather than being applied to the
    //  array underneath.
    llvm::Value*    data = nullptr;

    if(isDescriptor(expr->arg->type()))
    {
        auto    innerDesc = make(expr->arg);
        auto    innerPtr  = m_builder->CreateExtractValue(innerDesc, {1}, "inner.data");
        auto    elemTy    = Compile::make(m_context, m_module,
                                dynamic_cast<TypeArray*>(expr->arg->type())->type, "elem");

        data = m_builder->CreateGEP(elemTy, innerPtr, lo, "slice.data");
    }
    else
    {
        auto    base    = make(expr->arg);       //  composites are pointers already
        auto    arrayTy = Compile::make(m_context, m_module, expr->arg->type(), "slice");

        data = m_builder->CreateGEP(arrayTy, base, {zero, lo}, "slice.data");
    }
    auto    count   = sliceCount(expr);          //  clamped, and shared with `.count`

    auto    descTy  = descriptorType(m_context, *m_builder);

    m_value = m_builder->CreateInsertValue(
                m_builder->CreateInsertValue(
                    llvm::UndefValue::get(descTy), count, {0}, "slice.0"),
                data, {1}, "slice");
}

//  A call to an external function. The symbol carries a `referee_` prefix, so
//  `func crc8` binds to `referee_crc8` -- which keeps the specification's
//  namespace out of C's global one, and means a func named `read` cannot
//  accidentally resolve to read(2).
//
//  The declaration is emitted into the LLVM module as an external, and the JIT
//  resolves it against the objects loaded from the -L path. Nothing is checked
//  here beyond what type calculation already checked: C has no way to confirm
//  that the linked function matches, which is what the generated header is for.
void    CompileExprImpl::visit( ExprBinder*     expr)
{
    auto    it  = m_binders.find(expr->id);

//  LCOV_EXCL_START
    if(it == m_binders.end())
        throw std::runtime_error("binder '" + expr->name + "' used outside its quantifier");
//  LCOV_EXCL_STOP

    m_value = it->second;
}

//  A quantifier over an array that carries its own length: count the elements
//  whose body holds, and let the caller compare that against whatever the
//  quantifier form wants.
//
//  This is the loop expansion was avoiding, and it is only reachable where
//  expansion cannot go -- a written extent still unrolls, so nothing that
//  compiled before compiles differently.
void    CompileExprImpl::visit( ExprCount*      expr)
{
    auto    desc    = make(expr->arg);
    auto    count   = m_builder->CreateExtractValue(desc, {0}, "quant.count");
    auto    zero    = llvm::ConstantInt::get(m_builder->getInt64Ty(), 0);
    auto    one     = llvm::ConstantInt::get(m_builder->getInt64Ty(), 1);

    auto    func    = m_builder->GetInsertBlock()->getParent();
    auto    bbEntry = m_builder->GetInsertBlock();
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "quant.head", func);
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "quant.body", func);
    auto    bbDone  = llvm::BasicBlock::Create(*m_context, "quant.done", func);

    m_builder->CreateBr(bbHead);

    m_builder->SetInsertPoint(bbHead);
    auto    indx    = m_builder->CreatePHI(m_builder->getInt64Ty(), 2, "quant.i");
    auto    acc     = m_builder->CreatePHI(m_builder->getInt64Ty(), 2, "quant.n");

    indx->addIncoming(zero, bbEntry);
    acc ->addIncoming(zero, bbEntry);

    m_builder->CreateCondBr(m_builder->CreateICmpSLT(indx, count, "quant.more"),
                            bbBody, bbDone);

    m_builder->SetInsertPoint(bbBody);
    m_binders[expr->binder->id] = indx;
    auto    holds   = make(expr->body);
    m_binders.erase(expr->binder->id);

    auto    accNext = m_builder->CreateAdd(acc,
                        m_builder->CreateZExt(holds, m_builder->getInt64Ty()), "quant.n+");
    auto    indxNext= m_builder->CreateAdd(indx, one, "quant.i+");

    //  The body may have built blocks of its own -- a nested quantifier does
    //  -- so the edge back to the head leaves from wherever it ended, not from
    //  the block the body started in.
    auto    bbTail  = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    indx->addIncoming(indxNext, bbTail);
    acc ->addIncoming(accNext,  bbTail);

    m_builder->SetInsertPoint(bbDone);
    m_value = acc;
}

void    CompileExprImpl::visit( ExprCall*       expr)
{
    //  A built-in lowers to an intrinsic or to a couple of instructions --
    //  never to an external symbol -- so it needs no .so, no -L and no
    //  header, and a specification using only built-ins stays self-contained.
    if(auto const* first = findBuiltin(expr->name))
    {
        //  Resolve the overload the same way type calculation did -- by the
        //  call's shape -- so the two cannot disagree about which one this is.
        auto    allNumber = true;
        for(auto* arg: expr->args)
            if(arg->type() == Factory<TypeInteger>::create()
            || arg->type() == Factory<TypeByte>::create())
                allNumber = false;

        auto const* bi = first;
        for(auto const* cand: findBuiltins(expr->name))
            if(builtinAccepts(*cand, static_cast<unsigned>(expr->args.size()), allNumber))
            {
                bi = cand;
                break;
            }

        std::vector<llvm::Value*>   a;
        for(auto* arg: expr->args)
            a.push_back(make(arg));

        auto    intr = [&](llvm::Intrinsic::ID id)
        {
            return m_builder->CreateIntrinsic(id, {m_builder->getDoubleTy()}, a);
        };

        switch(bi->kind)
        {
        case Builtin::Kind::IAbs:
            //  No integer-abs instruction: compare and select.
            m_value = m_builder->CreateSelect(
                        m_builder->CreateICmpSLT(a[0],
                            llvm::ConstantInt::getSigned(m_builder->getInt64Ty(), 0)),
                        m_builder->CreateNeg(a[0]), a[0], "abs");
            return;

        case Builtin::Kind::IMin:
            m_value = m_builder->CreateSelect(
                        m_builder->CreateICmpSLT(a[0], a[1]), a[0], a[1], "min");
            return;

        case Builtin::Kind::IMax:
            m_value = m_builder->CreateSelect(
                        m_builder->CreateICmpSGT(a[0], a[1]), a[0], a[1], "max");
            return;

        case Builtin::Kind::FAbs:  m_value = intr(llvm::Intrinsic::fabs);   return;
        case Builtin::Kind::FMin:  m_value = intr(llvm::Intrinsic::minnum); return;
        case Builtin::Kind::FMax:  m_value = intr(llvm::Intrinsic::maxnum); return;
        case Builtin::Kind::Sqrt:  m_value = intr(llvm::Intrinsic::sqrt);   return;
        case Builtin::Kind::Exp:   m_value = intr(llvm::Intrinsic::exp);    return;
        case Builtin::Kind::Log:   m_value = intr(llvm::Intrinsic::log);    return;
        case Builtin::Kind::Log2:  m_value = intr(llvm::Intrinsic::log2);   return;
        case Builtin::Kind::Log10: m_value = intr(llvm::Intrinsic::log10);  return;
        case Builtin::Kind::Sin:   m_value = intr(llvm::Intrinsic::sin);    return;
        case Builtin::Kind::Cos:   m_value = intr(llvm::Intrinsic::cos);    return;
        case Builtin::Kind::Pow:   m_value = intr(llvm::Intrinsic::pow);    return;

        //  Rounded to a whole number, then narrowed: the result is an integer
        //  because that is what a specification wants to compare and index
        //  with, and REF has no cast to get there otherwise.
        //  Host functions, resolved from the process the same way `debug` is.
        //  Not intrinsics, because there are none -- but still no .so and no
        //  -L, since referee links them itself.
        case Builtin::Kind::StrLen:
        case Builtin::Kind::StrAt:
        case Builtin::Kind::StrCmp:
        case Builtin::Kind::StrStarts:
        case Builtin::Kind::StrEnds:
        case Builtin::Kind::StrFind:
        {
            auto    sym  = bi->kind == Builtin::Kind::StrLen    ? "__ref_str_len"
                         : bi->kind == Builtin::Kind::StrAt     ? "__ref_str_at"
                         : bi->kind == Builtin::Kind::StrCmp    ? "__ref_str_cmp"
                         : bi->kind == Builtin::Kind::StrStarts ? "__ref_str_starts"
                         : bi->kind == Builtin::Kind::StrEnds   ? "__ref_str_ends"
                                                                : "__ref_str_find";

            std::vector<llvm::Type*>    params(a.size(), m_builder->getPtrTy());
            if(bi->kind == Builtin::Kind::StrAt)
                params[1] = m_builder->getInt64Ty();

            auto    ret  = (bi->kind == Builtin::Kind::StrStarts
                         || bi->kind == Builtin::Kind::StrEnds)
                         ? static_cast<llvm::Type*>(m_builder->getInt1Ty())
                         : static_cast<llvm::Type*>(m_builder->getInt64Ty());

            m_value = m_builder->CreateCall(
                        m_module->getOrInsertFunction(sym,
                            llvm::FunctionType::get(ret, params, false)), a);
            return;
        }

        case Builtin::Kind::Floor:
        case Builtin::Kind::Ceil:
        case Builtin::Kind::Round:
        case Builtin::Kind::Trunc:
        {
            auto    id = bi->kind == Builtin::Kind::Floor ? llvm::Intrinsic::floor
                       : bi->kind == Builtin::Kind::Ceil  ? llvm::Intrinsic::ceil
                       : bi->kind == Builtin::Kind::Round ? llvm::Intrinsic::round
                                                          : llvm::Intrinsic::trunc;

            m_value = m_builder->CreateFPToSI(intr(id), m_builder->getInt64Ty(), "toint");
            return;
        }
        }
    }

    //  Resolve the overload from the call's own argument types, the same rule
    //  type calculation used, so the two agree without either recording it.
    std::vector<Type*>  actual;
    for(auto* arg: expr->args)
        actual.push_back(arg->type());

    auto const* pick = m_refmod->resolveFunc(expr->name, actual);

    if(pick == nullptr)
        for(auto const& cand: m_refmod->funcsNamed(expr->name))
            if(cand.args.size() == actual.size())
            {
                pick = &cand;       //  unsized array parameters, already checked
                break;
            }

    auto const& decl    = *pick;
    auto        symbol  = m_refmod->symbolFor(expr->name, decl);

    //  An array crosses as a descriptor -- { count, data } -- rather than as a
    //  bare pointer. In memory a REF array is flat and contiguous, so the
    //  descriptor is built at the call from a compile-time count and the
    //  pointer the caller already holds.
    auto    sliceType = [&]() { return descriptorType(m_context, *m_builder); };

    std::vector<llvm::Type*>    paramTypes;

    //  `(__state__)`: a handle to the state at the point of evaluation. The
    //  callee reads it through the accessors the header generates, never by
    //  reaching into it -- which is the whole reason it is a handle and not a
    //  struct anyone can lay out for themselves.
    if(decl.state)
        paramTypes.push_back(m_builder->getPtrTy());

    for(auto* type: decl.args)
    {
        if(dynamic_cast<TypeArray*>(type))
            paramTypes.push_back(sliceType());
        else if(dynamic_cast<TypeStruct*>(type))
            //  A struct crosses by `const` pointer. Its size and field types
            //  are arbitrary, so by-value passing would need the platform's
            //  aggregate classification reproduced here; by pointer needs
            //  none of it, and the pointer already exists -- a struct-valued
            //  expression is never loaded.
            paramTypes.push_back(m_builder->getPtrTy());
        else
            paramTypes.push_back(Compile::make(m_context, m_module, type, expr->name));
    }

    auto    retType     = Compile::make(m_context, m_module, decl.ret, expr->name);
    auto    funcType    = llvm::FunctionType::get(retType, paramTypes, false);
    auto    callee      = m_module->getOrInsertFunction(symbol, funcType);

    //  A `byte` parameter is one octet: the caller works in i64, so narrow at
    //  the boundary. A `byte` result is widened back, the same way a byte read
    //  out of a trace is.
    std::vector<llvm::Value*>   args;

    //  Which state that is moves: inside a temporal operator it is the one the
    //  walk has reached, not the one the requirement was evaluated at. That is
    //  exactly `m_curr`, which every signal read already uses.
    if(decl.state)
        args.push_back(m_curr.back());

    for(std::size_t i = 0; i < expr->args.size(); i++)
    {
        auto    value = make(expr->args[i]);

        if(decl.args[i] == Factory<TypeByte>::create())
            value = m_builder->CreateTrunc(value, m_builder->getInt8Ty(), "byte_arg");

        //  An enum is a composite, so the expression yields a pointer to its
        //  one-byte storage rather than the value. It crosses by value, so
        //  load it -- the same fix equality needed.
        if(dynamic_cast<TypeEnum*>(decl.args[i]))
            value = m_builder->CreateLoad(
                        Compile::make(m_context, m_module, decl.args[i], expr->name),
                        value, false, "enum_arg");

        //  An unsized array already *is* the descriptor -- it carries a runtime
        //  count, which is the whole reason to have one.
        if(isDescriptor(expr->args[i]->type()))
        {
            args.push_back(value);
            continue;
        }

        if(dynamic_cast<TypeArray*>(decl.args[i]))
        {
            //  The extent is the *argument's*: a signature may leave it out,
            //  and that is the whole point of the descriptor.
            auto*   array = dynamic_cast<TypeArray*>(expr->args[i]->type());
            //  `value` is already the address of the storage: composites are
            //  never loaded. `count` is the extent, which is fixed for the run
            //  -- it is capacity, not the meaningful length, so a caller with
            //  a shorter payload still passes its own length alongside.
            auto    desc = llvm::UndefValue::get(sliceType());
            auto    n    = llvm::ConstantInt::get(m_builder->getInt64Ty(), array->count);

            value = m_builder->CreateInsertValue(
                        m_builder->CreateInsertValue(desc, n, {0}, "slice.count"),
                        value, {1}, "slice");
        }

        args.push_back(value);
    }

    m_value = widenByte(m_builder->CreateCall(callee, args, expr->name + "()"), decl.ret);
}

void    CompileExprImpl::visit( ExprXor*        expr)
{
    auto    lhs = make(expr->lhs);
    auto    rhs = make(expr->rhs);
    m_value = m_builder->CreateXor(lhs, rhs);   //  TODO: fix
}

//  Bitwise. The operands are always i64 by the time they arrive -- a `byte` is
//  widened on load -- so there is one instruction per operator and no width
//  juggling. `>>` is arithmetic, since REF integers are signed.
void    CompileExprImpl::visit( ExprBand*       expr)
{
    m_value = m_builder->CreateAnd(make(expr->lhs), make(expr->rhs), "band");
}

void    CompileExprImpl::visit( ExprBor*        expr)
{
    m_value = m_builder->CreateOr(make(expr->lhs), make(expr->rhs), "bor");
}

void    CompileExprImpl::visit( ExprShl*        expr)
{
    m_value = m_builder->CreateShl(make(expr->lhs), make(expr->rhs), "shl");
}

void    CompileExprImpl::visit( ExprShr*        expr)
{
    m_value = m_builder->CreateAShr(make(expr->lhs), make(expr->rhs), "shr");
}

void    CompileExprImpl::visit( ExprBnot*       expr)
{
    m_value = m_builder->CreateNot(make(expr->arg), "bnot");
}

void    CompileExprImpl::XY(ExprBinary*     expr,
                            llvm::Value*    direction,
                            llvm::Value*    endV,
                            std::string     name)
{
    auto    bbHeadHi= llvm::BasicBlock::Create(*m_context, name + "-head", m_function);
    auto    bbHeadLo= bbHeadHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, name + "-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);
    auto    frst    = m_frst.back();
    auto    curr    = m_curr.back();
    auto    last    = m_last.back();

    m_builder->CreateBr(bbHeadHi);

    //  head
    m_builder->SetInsertPoint(bbHeadHi);
    auto    incr        = m_builder->CreateMul(make(expr->lhs), direction, "incr");
    auto    next        = m_builder->CreateGEP(m_propType, curr, incr, "next");
    auto    nextGElast  = m_builder->CreateICmpUGE(next, last, "next >= last");
    auto    nextLEfrst  = m_builder->CreateICmpULE(next, frst, "next <= frst");
    auto    outside     = m_builder->CreateOr(nextGElast, nextLEfrst);
    bbHeadLo            = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(outside, bbTail, bbBodyHi);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(next);
    auto    body        = make(expr->rhs);
    m_curr.pop_back();
    bbBodyLo            = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, name);

    //  link
    result->addIncoming(body, bbBodyLo);
    result->addIncoming(endV, bbHeadLo);

    m_value = result;
}

void    CompileExprImpl::UR(Temporal<ExprBinary>*   expr,
                            llvm::Value*            rhsV,
                            llvm::Value*            lhsV,
                            llvm::Value*            endV,
                            std::string             name)
{
    //  Fast path: use the pre-compiled O(N) buffer if available.
    auto it = m_temporalBuffers.find(expr);
    if (it != m_temporalBuffers.end())
    {
        auto buffer = it->second;
        auto frst   = m_frst.back();
        auto curr   = m_curr.back();
        auto idx    = m_builder->CreatePtrDiff(m_propType, curr, frst, "idx");
        auto ptr    = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idx);
        m_value     = m_builder->CreateLoad(m_builder->getInt1Ty(), ptr, false, name);
        return;
    }

    //  Slow path (O(N²)): original nested-loop implementation.
    //  Handles bounded operators (time windows) correctly.
    auto    bbWhile = llvm::BasicBlock::Create(*m_context, name + "-while", m_function);
    auto    bbOuter = llvm::BasicBlock::Create(*m_context, name + "-outer", m_function);
    auto    bbInner = llvm::BasicBlock::Create(*m_context, name + "-inner", m_function);
    auto    bbLhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-lhs", m_function);
    auto    bbLhsLo = bbLhsHi;
    auto    bbRhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-rhs", m_function);
    auto    bbRhsLo = bbRhsHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, name + "-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    next0   = getNext(curr0);
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0, "curr->__time__");

    if(expr->time && expr->time->lo)
    {
        timeLo  = m_builder->CreateAdd(now, make(expr->time->lo), "now + lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeHi  = m_builder->CreateAdd(now, make(expr->time->hi), "now + hi");
    }
    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    next    = m_builder->CreatePHI(frst->getType(), 2, "next");
    auto    nextLElast    = m_builder->CreateICmpSLE(next, last, "next <= last");
    m_builder->CreateCondBr(nextLElast, bbOuter, bbTail);

    //  outer
    m_builder->SetInsertPoint(bbOuter);
    if(timeHi)
    {
        auto    currT           = getTime(curr, "curr->__time__");
        auto    timeHiLTcurrT   = m_builder->CreateICmpSGT(timeHi, currT, "timeHi > currT");
        m_builder->CreateCondBr(timeHiLTcurrT, bbInner, bbTail);
    }
    else if(timeLo)
    {
        m_builder->CreateBr(bbInner);
    }
    else
    {
        m_builder->CreateBr(bbRhsHi);
    }

    //  inner
    m_builder->SetInsertPoint(bbInner);
    auto    currT   = getTime(curr, "curr->__time__");
    auto    nextT   = getTime(next, "next->__time__");

    auto    lo      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(currT, timeLo), timeLo, currT)
                    : currT;
    auto    hi      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, nextT), timeHi, nextT)
                    : nextT;
    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbRhsHi, bbNext);

    //  rhs
    m_builder->SetInsertPoint(bbRhsHi);
    m_curr.push_back(curr);
    auto    rhs     = make(expr->rhs);
    m_curr.pop_back();
    auto    rhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, rhs, rhsV);
    m_builder->CreateCondBr(rhsCond, bbTail, bbLhsHi);
    bbRhsLo = m_builder->GetInsertBlock();

    //  lhs
    m_builder->SetInsertPoint(bbLhsHi);
    m_curr.push_back(curr);
    auto    lhs     = make(expr->lhs);
    m_curr.pop_back();
    auto    lhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, lhsV);
    m_builder->CreateCondBr(lhsCond, bbTail, bbNext);
    bbLhsLo = m_builder->GetInsertBlock();

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = next;
    auto    next1   = getNext(curr1);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, timeHi ? 4 : 3, "result");

    //  link
    result->addIncoming(lhsV, bbLhsLo);
    result->addIncoming(rhsV, bbRhsLo);
    result->addIncoming(endV, bbWhile);
    if(timeHi)
        result->addIncoming(endV, bbOuter);

    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    next->addIncoming(next0, bbEntry);
    next->addIncoming(next1, bbNext);

    m_value = result;
}

void    CompileExprImpl::ST(Temporal<ExprBinary>*   expr,
                            llvm::Value*            rhsV,
                            llvm::Value*            lhsV,
                            llvm::Value*            endV,
                            std::string             name)
{
    //  Fast path: use the pre-compiled O(N) buffer if available.
    auto it = m_temporalBuffers.find(expr);
    if (it != m_temporalBuffers.end())
    {
        auto buffer = it->second;
        auto frst   = m_frst.back();
        auto curr   = m_curr.back();
        auto idx    = m_builder->CreatePtrDiff(m_propType, curr, frst, "idx");
        auto ptr    = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idx);
        m_value     = m_builder->CreateLoad(m_builder->getInt1Ty(), ptr, false, name);
        return;
    }

    //  Slow path (O(N²)): original nested-loop implementation.
    //  Handles bounded operators (time windows) correctly.
    auto    bbWhile = llvm::BasicBlock::Create(*m_context, name + "-while", m_function);
    auto    bbOuter = llvm::BasicBlock::Create(*m_context, name + "-outer", m_function);
    auto    bbInner = llvm::BasicBlock::Create(*m_context, name + "-inner", m_function);
    auto    bbRhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-rhs", m_function);
    auto    bbRhsLo = bbRhsHi;
    auto    bbLhsHi = llvm::BasicBlock::Create(*m_context, name + "-body-lhs", m_function);
    auto    bbLhsLo = bbLhsHi;
    auto    bbPrev  = llvm::BasicBlock::Create(*m_context, name + "-prev", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, name + "-tail", m_function);

    auto    frst    = m_frst.back();
    auto    curr0   = m_curr.back();
    auto    prev0   = getPrev(curr0);
    __attribute__((unused))
    auto    last    = m_last.back();

    llvm::Value*    timeLo  = nullptr;
    llvm::Value*    timeHi  = nullptr;
    auto    now     = getTime(curr0, "now");

    if(expr->time && expr->time->lo)
    {
        timeHi  = m_builder->CreateSub(now, make(expr->time->lo), "now - lo");
    }

    if(expr->time && expr->time->hi)
    {
        timeLo  = m_builder->CreateSub(now, make(expr->time->hi), "now - hi");
    }
    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr    = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    prev    = m_builder->CreatePHI(frst->getType(), 2, "prev");
    auto    frstLEprev    = m_builder->CreateICmpSLE(frst, prev, "frst <= prev");
    m_builder->CreateCondBr(frstLEprev, bbOuter, bbTail);

    //  outer
    //
    //  Scanning backwards, the far end of the window is timeLo (= now - hi),
    //  so it is timeLo that bounds how far back the scan may go -- mirroring
    //  UR, where the far end is timeHi.  Each guard must test the very
    //  pointer it dereferences: with only one of `[lo:]` / `[:hi]` given the
    //  other is null, and testing the wrong one crashes the compiler.
    m_builder->SetInsertPoint(bbOuter);
    if(timeLo)
    {
        auto    currT           = getTime(curr, "curr->__time__");
        auto    timeLoLEcurrT   = m_builder->CreateICmpSLE(timeLo, currT, "timeLo <= currT");
        m_builder->CreateCondBr(timeLoLEcurrT, bbInner, bbTail);
    }
    else if(timeHi)
    {
        m_builder->CreateBr(bbInner);
    }
    else
    {
        m_builder->CreateBr(bbRhsHi);
    }

    //  inner
    m_builder->SetInsertPoint(bbInner);
    auto    currT   = getTime(curr, "curr->__time__");
    auto    prevT   = getTime(prev, "prev->__time__");

    auto    lo      = timeLo
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(prevT, timeLo), timeLo, prevT)
                    : prevT;
    auto    hi      = timeHi
                    ? m_builder->CreateSelect(m_builder->CreateICmpSLT(timeHi, currT), timeHi, currT)
                    : currT;

    auto    loLThi  = m_builder->CreateICmpSLT(lo, hi);

    m_builder->CreateCondBr(loLThi, bbRhsHi, bbPrev);

    //  rhs
    m_builder->SetInsertPoint(bbRhsHi);
    m_curr.push_back(curr);
    auto    rhs     = make(expr->rhs);
    m_curr.pop_back();
    auto    rhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, rhs, rhsV);
    m_builder->CreateCondBr(rhsCond, bbTail, bbLhsHi);
    bbRhsLo = m_builder->GetInsertBlock();

    //  lhs
    m_builder->SetInsertPoint(bbLhsHi);
    m_curr.push_back(curr);
    auto    lhs     = make(expr->lhs);
    m_curr.pop_back();
    auto    lhsCond = m_builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, lhsV);
    m_builder->CreateCondBr(lhsCond, bbTail, bbPrev);
    bbLhsLo = m_builder->GetInsertBlock();

    //  next
    m_builder->SetInsertPoint(bbPrev);
    auto    curr1   = prev;
    auto    prev1   = getPrev(curr1);
    m_builder->CreateBr(bbWhile);

    //  tail
    //  bbOuter only branches here when it emitted the conditional above, i.e.
    //  when timeLo exists -- keep this in step with that guard.
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, timeLo ? 4 : 3, "result");

    //  link
    result->addIncoming(lhsV, bbLhsLo);
    result->addIncoming(rhsV, bbRhsLo);
    result->addIncoming(endV, bbWhile);
    if(timeLo)
        result->addIncoming(endV, bbOuter);

    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbPrev);

    prev->addIncoming(prev0, bbEntry);
    prev->addIncoming(prev1, bbPrev);

    m_value = result;
}

void    CompileExprImpl::visit( ExprXs*         expr)
{
    XY(expr, m_p1, m_F, "Xs");
}

void    CompileExprImpl::visit(ExprXw*           expr)
{
    XY(expr, m_p1, m_T, "Xw");
}

void    CompileExprImpl::visit(ExprYs*           expr)
{
    XY(expr, m_m1, m_F, "Ys");
}

void    CompileExprImpl::visit(ExprYw*           expr)
{
    XY(expr, m_m1, m_T, "Yw");
}

void    CompileExprImpl::visit(Spec*             spec)
{
    auto    expr    = Rewrite::make(spec);
    TypeCalc::make(m_refmod, expr);

    compileTemporalLoops(expr);

    m_value = make(expr);
}

void    CompileExprImpl::visit(SpecGlobally*     spec)
{
    m_value = make(spec->spec);
}

void    CompileExprImpl::visit(SpecBefore*       spec)
{
/*
    auto    curr    = frst + 1;
    auto    result  = true;

    while(curr < last)
    {
        if(eval(curr, spec->arg))
        {
            result  = eval(frst, curr, frst+1, spec->spec);
            break
        }

        curr    = curr + 1;
    }

    return result;
*/

    auto    bbWhile = llvm::BasicBlock::Create(*m_context, "spec-while",m_function);
    auto    bbTestHi= llvm::BasicBlock::Create(*m_context, "spec-test", m_function);
    __attribute__((unused))
    auto    bbTestLo= bbTestHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "spec-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbEntry = m_builder->GetInsertBlock();

    auto    frst    = m_frst.back();
    auto    last    = m_last.back();

    auto    curr0   = getNext(frst);

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, last, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbTestHi, bbTail);

    //  test
    m_builder->SetInsertPoint(bbTestHi);
    m_curr.push_back(curr);
    auto    test= make(spec->arg);
    m_curr.pop_back();
    bbTestLo    = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(test, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_curr.push_back(curr0);
    m_last.push_back(curr);
    auto    body    = make(spec->spec);
    m_last.pop_back();
    m_last.pop_back();
    bbBodyLo    = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, 2, "result");

    //  links
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    result->addIncoming(m_T, bbWhile);
    result->addIncoming(body,bbBodyLo);

    m_value = result;
}

void    CompileExprImpl::visit(SpecAfter*        spec)
{
/*
    auto    curr    = frst + 1;
    auto    result  = true;

    while(curr < last)
    {
        if(eval(curr, spec->arg))
        {
            result  = eval(curr - 1, last, curr, spec->spec);
            break
        }

        prev    = curr;
        curr    = curr + 1;
    }

    return result;
*/

    auto    bbWhile = llvm::BasicBlock::Create(*m_context, "spec-while",m_function);
    auto    bbTestHi= llvm::BasicBlock::Create(*m_context, "spec-test", m_function);
    __attribute__((unused))
    auto    bbTestLo= bbTestHi;
    auto    bbBodyHi= llvm::BasicBlock::Create(*m_context, "spec-body", m_function);
    auto    bbBodyLo= bbBodyHi;
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail  = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbEntry = m_builder->GetInsertBlock();

    auto    frst    = m_frst.back();
    auto    last    = m_last.back();

    auto    curr0   = getNext(frst);

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(frst->getType(), 2, "curr");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, last, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbTestHi, bbTail);

    //  test
    m_builder->SetInsertPoint(bbTestHi);
    m_curr.push_back(curr);
    auto    test= make(spec->arg);
    m_curr.pop_back();
    bbTestLo    = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(test, bbBodyHi, bbNext);

    //  body
    m_builder->SetInsertPoint(bbBodyHi);
    m_frst.push_back(getPrev(curr));
    m_curr.push_back(curr);
    auto    body    = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    bbBodyLo    = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbTail);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1   = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    result  = m_builder->CreatePHI(m_boolType, 2, "result");

    //  links
    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(curr1, bbNext);

    result->addIncoming(m_T, bbWhile);
    result->addIncoming(body,bbBodyLo);

    m_value = result;
}

void    CompileExprImpl::visit(SpecBetweenAnd*   spec)
{
    auto    bbWhile     = llvm::BasicBlock::Create(*m_context, "spec-while", m_function);
    auto    bbEvalCondHi= llvm::BasicBlock::Create(*m_context, "spec-eval-cond", m_function);
    auto    bbEvalCondLo= bbEvalCondHi;
    auto    bbEvalBodyHi= llvm::BasicBlock::Create(*m_context, "spec-eval-body", m_function);
    auto    bbEvalBodyLo= bbEvalBodyHi;
    auto    bbNext      = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbTail      = llvm::BasicBlock::Create(*m_context, "spec-tail", m_function);
    auto    bbDone      = llvm::BasicBlock::Create(*m_context, "spec-done", m_function);

    //  head
    auto    bbHead      = m_builder->GetInsertBlock();
    auto    outerFrst   = m_frst.back();
    auto    outerLast   = m_last.back();
    auto    curr0       = getNext(outerFrst);
    auto    innerFrst0  = outerFrst;

    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(m_propPtrType, 2, "curr");
    auto    mainInside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    mainInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, outerLast, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbEvalCondHi, bbTail);

    //  eval cond
    m_builder->SetInsertPoint(bbEvalCondHi);
    m_curr.push_back(curr);
    auto    lhs         = make(spec->lhs);
    auto    rhs         = make(spec->rhs);
    m_curr.pop_back();
    bbEvalCondLo        = m_builder->GetInsertBlock();
    auto    enter       = m_builder->CreateAnd({
                            m_builder->CreateNot(mainInside),
                            lhs,
                            m_builder->CreateNot(rhs)});
    auto    leave       = m_builder->CreateAnd({
                            mainInside,
                            rhs});
    auto    condInside  = m_builder->CreateSelect(
                            enter,
                            m_T,
                            m_builder->CreateSelect(
                                leave,
                                m_F,
                                mainInside),
                            "inside");
    auto    condInnerFrst
                        = m_builder->CreateSelect(
                            enter,
                            getPrev(curr),
                            mainInnerFrst,
                            "innerFrst");
    m_builder->CreateCondBr(leave, bbEvalBodyHi, bbNext);

    //  eval body
    m_builder->SetInsertPoint(bbEvalBodyHi);
    auto    bodyInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    bodyInside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    bodyInnerLast   = curr;
    m_frst.push_back(bodyInnerFrst);
    m_last.push_back(bodyInnerLast);
    m_curr.push_back(getNext(m_frst.back()));
    auto    body        = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    m_last.pop_back();
    bbEvalBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(body, bbNext, bbDone);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    nextInnerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    nextInside  = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    curr1       = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  tail
    m_builder->SetInsertPoint(bbTail);
    auto    currEQlast  = m_builder->CreateICmpEQ(curr, outerLast, "curr == last");
    auto    tail        = m_builder->CreateAnd({currEQlast, mainInside});
    m_builder->CreateCondBr(tail, bbEvalBodyHi, bbDone);

    //  done
    m_builder->SetInsertPoint(bbDone);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, "result");
    m_value = result;

    //  link
    result->addIncoming(m_T, bbTail);
    result->addIncoming(m_F, bbEvalBodyLo);

    curr->addIncoming(curr0, bbHead);
    curr->addIncoming(curr1, bbNext);

    mainInnerFrst->addIncoming( innerFrst0,     bbHead);
    mainInnerFrst->addIncoming( nextInnerFrst,  bbNext);

    mainInside->addIncoming(    m_F,            bbHead);
    mainInside->addIncoming(    nextInside,     bbNext);

    bodyInnerFrst->addIncoming( condInnerFrst,  bbEvalCondLo);
    bodyInnerFrst->addIncoming( mainInnerFrst,  bbTail);

    bodyInside->addIncoming(    condInside,     bbEvalCondLo);
    bodyInside->addIncoming(    mainInside,     bbTail);

    nextInnerFrst->addIncoming( condInnerFrst,  bbEvalCondLo);
    nextInnerFrst->addIncoming( bodyInnerFrst,  bbEvalBodyLo);

    nextInside->addIncoming(    condInside,     bbEvalCondLo);
    nextInside->addIncoming(    bodyInside,     bbEvalBodyLo);
}

void    CompileExprImpl::visit(SpecAfterUntil*   spec)
{
    auto    bbWhile     = llvm::BasicBlock::Create(*m_context, "spec-while", m_function);
    auto    bbEvalCondHi= llvm::BasicBlock::Create(*m_context, "spec-eval-cond", m_function);
    __attribute__((unused))
    auto    bbEvalCondLo= bbEvalCondHi;
    auto    bbEvalBodyHi= llvm::BasicBlock::Create(*m_context, "spec-eval-body", m_function);
    auto    bbEvalBodyLo= bbEvalBodyHi;
    auto    bbNext      = llvm::BasicBlock::Create(*m_context, "spec-next", m_function);
    auto    bbDone      = llvm::BasicBlock::Create(*m_context, "spec-done", m_function);

    //  head
    auto    bbHead      = m_builder->GetInsertBlock();
    auto    outerFrst   = m_frst.back();
    auto    outerLast   = m_last.back();
    auto    curr0       = getNext(outerFrst);
    auto    innerFrst0  = outerFrst;
    m_builder->CreateBr(bbWhile);

    //  while
    m_builder->SetInsertPoint(bbWhile);
    auto    curr        = m_builder->CreatePHI(m_propPtrType, 2, "curr");
    auto    inside      = m_builder->CreatePHI(m_boolType, 2, "inside");
    auto    innerFrst   = m_builder->CreatePHI(m_propPtrType, 2, "innerFrst");
    auto    currLTlast  = m_builder->CreateICmpSLT(curr, outerLast, "curr < last");
    m_builder->CreateCondBr(currLTlast, bbEvalCondHi, bbDone);

    //  eval cond
    m_builder->SetInsertPoint(bbEvalCondHi);
    m_curr.push_back(curr);
    auto    lhs         = make(spec->lhs);
    auto    rhs         = make(spec->rhs);
    m_curr.pop_back();
    bbEvalCondLo        = m_builder->GetInsertBlock();
    auto    enter       = m_builder->CreateAnd({
                            m_builder->CreateNot(inside),
                            lhs,
                            m_builder->CreateNot(rhs)});
    auto    leave       = m_builder->CreateAnd({
                            inside,
                            rhs});
    auto    inside1     = m_builder->CreateSelect(
                            enter,
                            m_T,
                            m_builder->CreateSelect(
                                leave,
                                m_F,
                                inside),
                            "inside");
    auto    innerFrst1  = m_builder->CreateSelect(
                            enter,
                            getPrev(curr),
                            innerFrst,
                            "innerFrst");
    m_builder->CreateCondBr(leave, bbEvalBodyHi, bbNext);

    //  eval body
    m_builder->SetInsertPoint(bbEvalBodyHi);
    auto    innerLast   = curr;
    m_frst.push_back(innerFrst1);
    m_last.push_back(innerLast);
    m_curr.push_back(getNext(innerFrst1));
    auto    body        = make(spec->spec);
    m_curr.pop_back();
    m_frst.pop_back();
    m_last.pop_back();
    bbEvalBodyLo        = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(body, bbNext, bbDone);

    //  next
    m_builder->SetInsertPoint(bbNext);
    auto    curr1       = getNext(curr);
    m_builder->CreateBr(bbWhile);

    //  done
    m_builder->SetInsertPoint(bbDone);
    auto    result      = m_builder->CreatePHI(m_boolType, 2, "result");
    m_value = result;

    //  link
    result->addIncoming(m_F, bbEvalBodyLo);
    result->addIncoming(m_T, bbWhile);

    curr->addIncoming(curr0, bbHead);
    curr->addIncoming(curr1, bbNext);

    innerFrst->addIncoming(innerFrst0, bbHead);
    innerFrst->addIncoming(innerFrst1, bbNext);

    inside->addIncoming(m_F, bbHead);
    inside->addIncoming(inside1, bbNext);
}

llvm::Value*    CompileExprImpl::getNext(llvm::Value* curr)
{
    return  m_builder->CreateGEP(m_propType, curr, m_p1, "next");
}

llvm::Value*    CompileExprImpl::getPrev(llvm::Value* curr)
{
    return  m_builder->CreateGEP(m_propType, curr, m_m1, "prev");
}

llvm::Value*    CompileExprImpl::getTime(llvm::Value* curr, std::string name)
{
    return  m_builder->CreateLoad(m_builder->getInt64Ty(), m_builder->CreateStructGEP(m_propType, curr, 0), false, name);
}

llvm::Value*    CompileExprImpl::getPropPtr(llvm::Value* var)
{
    return m_builder->CreateLoad(m_propPtrType, var);
}

llvm::Value*    CompileExprImpl::setPropPtr(llvm::Value* var, llvm::Value* val)
{
    return m_builder->CreateStore(val, var);
}

//  A byte is stored in i8 but read as an integer, so every load of one is
//  widened here rather than at each call site. Unsigned: a byte is 0..255.
//  For dispatch, a byte *is* an integer: the value has already been widened
//  by the time any operator sees it.
Type*   CompileExprImpl::valueType(Expr* expr)
{
    auto    type = expr->type();
    return type == Factory<TypeByte>::create() ? Factory<TypeInteger>::create() : type;
}

llvm::Value*    CompileExprImpl::widenByte(llvm::Value* value, Type* type)
{
    if(type == Factory<TypeByte>::create())
        return m_builder->CreateZExt(value, m_builder->getInt64Ty(), "byte");

    return value;
}

llvm::Value*    CompileExprImpl::getBool(llvm::Value* var)
{
    return m_builder->CreateLoad(m_boolType, var);
}

llvm::Value*    CompileExprImpl::setBool(llvm::Value* var, llvm::Value* val)
{
    return m_builder->CreateStore(val, var);
}
void CompileExprImpl::compileTemporalLoops(Expr* rootExpr)
{
    std::vector<Expr*> temporals;
    collectTemporals(rootExpr, temporals);

    if (temporals.empty()) return;

    for (auto* expr : temporals)
    {
        if (m_temporalBuffers.count(expr)) continue;
        if (m_accumBuffers.count(expr)) continue;
        if (hasFreeContext(expr, {})) continue;

        if (auto* sum = dynamic_cast<ExprSum*>(expr); isLoopAccumulator(expr))
        {
            auto* type = valueType(sum->rhs) == Factory<TypeInteger>::create()
                       ? static_cast<llvm::Type*>(m_builder->getInt64Ty())
                       : static_cast<llvm::Type*>(m_builder->getDoubleTy());

            m_accumBuffers[expr] = {compileAccumulatorLoop(sum, type), type};
            continue;
        }

        //  A bounded operator slides its window with a monotone pointer, which
        //  needs the bounds to be constants of the trace.  A bound reading a
        //  `data` signal or a frozen state makes the window non-monotone, and
        //  such operators stay on the nested scan.
        auto* temporal  = dynamic_cast<Temporal<ExprBinary>*>(expr);
        bool  isBounded = temporal && temporal->time != nullptr;

        if (isBounded && !hasLoopInvariantTime(temporal->time)) continue;

        //  (rhsV, lhsV, endV) must mirror the corresponding visit() call site
        //  exactly -- those are the authority on each operator's semantics.
        //  U/S short-circuit on rhs==true / lhs==false (disjunctive), while
        //  their duals R/T short-circuit on rhs==false / lhs==true
        //  (conjunctive).  Getting this table wrong silently collapses e.g.
        //  G(a) -- which canonicalises to Rw(false, a) -- into pointwise `a`.
        bool            isUR = false;
        llvm::Value*    rhsV = nullptr;
        llvm::Value*    lhsV = nullptr;
        llvm::Value*    endV = nullptr;

        if      (dynamic_cast<ExprUs*>(expr)) {isUR = true;  rhsV = m_T; lhsV = m_F; endV = m_F;}
        else if (dynamic_cast<ExprUw*>(expr)) {isUR = true;  rhsV = m_T; lhsV = m_F; endV = m_T;}
        else if (dynamic_cast<ExprRs*>(expr)) {isUR = true;  rhsV = m_F; lhsV = m_T; endV = m_F;}
        else if (dynamic_cast<ExprRw*>(expr)) {isUR = true;  rhsV = m_F; lhsV = m_T; endV = m_T;}
        else if (dynamic_cast<ExprSs*>(expr)) {isUR = false; rhsV = m_T; lhsV = m_F; endV = m_F;}
        else if (dynamic_cast<ExprSw*>(expr)) {isUR = false; rhsV = m_T; lhsV = m_F; endV = m_T;}
        else if (dynamic_cast<ExprTs*>(expr)) {isUR = false; rhsV = m_F; lhsV = m_T; endV = m_F;}
        else if (dynamic_cast<ExprTw*>(expr)) {isUR = false; rhsV = m_F; lhsV = m_T; endV = m_T;}
        else                                  continue;

        m_temporalBuffers[expr] = isBounded
            ? compileTemporalLoopBounded(temporal, rhsV, lhsV, endV, isUR)
            : compileTemporalLoopInline(expr, rhsV, lhsV, endV, isUR);
    }
}

//  Emit the O(N) recurrence for one unbounded U/R/S/T node into a
//  bool[numStates] stack buffer, and return the buffer.
//
//  The slow path is a linear scan that, walking away from the evaluation
//  point, returns rhsV on the first state where rhs==rhsV, lhsV on the first
//  state where lhs==lhsV, and endV if it runs off the trace.  Written as a
//  recurrence over the neighbouring state that is exactly:
//
//      val[i] = (rhs[i] == rhsV) ? rhsV
//             : (lhs[i] == lhsV) ? lhsV
//             :                    val[i +- 1]
//
//  which folds to `rhs || (lhs && val)` for U/S and to `rhs && (lhs || val)`
//  for their duals R/T once the constants are substituted.  Emitting the
//  select chain rather than a hand-specialised form keeps the two families
//  from drifting apart.
llvm::Value* CompileExprImpl::compileTemporalLoopInline(Expr*        expr,
                                                        llvm::Value* rhsV,
                                                        llvm::Value* lhsV,
                                                        llvm::Value* endV,
                                                        bool         isUR)
{
    auto frst = m_frst.back();
    auto last = m_last.back();

    auto diff = m_builder->CreatePtrDiff(m_propType, last, frst, "diff");
    auto numStates = m_builder->CreateAdd(diff, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1), "numStates");

    auto buffer = m_builder->CreateAlloca(m_builder->getInt1Ty(), numStates, "temp_buf");

    auto bbEntry = m_builder->GetInsertBlock();

    if (isUR)
    {
        //  Both sentinel slots get the base value.  index numStates-1 is the
        //  recurrence's base case; index 0 is never a legitimate evaluation
        //  point (the sentinels carry no prop storage, so evaluating rhs/lhs
        //  there would dereference garbage -- the slow path avoids it for the
        //  same reason), but a nested Ys at the first real state still reads
        //  the slot, so it must hold something defined rather than whatever
        //  the alloca happened to contain.
        auto lastIdx = m_builder->CreateSub(numStates, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1));
        auto lastPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, lastIdx);
        m_builder->CreateStore(endV, lastPtr);
        m_builder->CreateStore(endV, m_builder->CreateGEP(m_builder->getInt1Ty(), buffer,
                                        llvm::ConstantInt::get(m_builder->getInt64Ty(), 0)));

        auto bbWhile = llvm::BasicBlock::Create(*m_context, "while_UR", m_function);
        auto bbBody  = llvm::BasicBlock::Create(*m_context, "body_UR", m_function);
        auto bbNext  = llvm::BasicBlock::Create(*m_context, "next_UR", m_function);
        auto bbExit  = llvm::BasicBlock::Create(*m_context, "exit_UR", m_function);

        auto curr0 = getPrev(last);
        m_builder->CreateBr(bbWhile);

        m_builder->SetInsertPoint(bbWhile);
        auto curr = m_builder->CreatePHI(m_propPtrType, 2, "curr");
        auto cond = m_builder->CreateICmpSGT(curr, frst, "curr > frst");
        m_builder->CreateCondBr(cond, bbBody, bbExit);

        m_builder->SetInsertPoint(bbBody);
        m_curr.push_back(curr);

        auto idx = m_builder->CreatePtrDiff(m_propType, curr, frst, "idx");
        auto idxNext = m_builder->CreateAdd(idx, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1), "idxNext");
        auto nextPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idxNext);
        auto nextVal = m_builder->CreateLoad(m_builder->getInt1Ty(), nextPtr, false, "nextVal");

        auto binary = dynamic_cast<ExprBinary*>(expr);
        auto rhs = make(binary->rhs);
        auto lhs = make(binary->lhs);
        auto rhsHit = m_builder->CreateICmpEQ(rhs, rhsV, "rhsHit");
        auto lhsHit = m_builder->CreateICmpEQ(lhs, lhsV, "lhsHit");
        auto val = m_builder->CreateSelect(
                        rhsHit, rhsV,
                        m_builder->CreateSelect(lhsHit, lhsV, nextVal, "lhsSel"),
                        "val");

        auto currPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idx);
        m_builder->CreateStore(val, currPtr);

        m_curr.pop_back();
        m_builder->CreateBr(bbNext);

        m_builder->SetInsertPoint(bbNext);
        auto currPrev = getPrev(curr);
        m_builder->CreateBr(bbWhile);

        curr->addIncoming(curr0, bbEntry);
        curr->addIncoming(currPrev, bbNext);

        m_builder->SetInsertPoint(bbExit);
    }
    else
    {
        //  Mirror of the UR case: index 0 is the base case, index numStates-1
        //  is the far sentinel that a nested Xs at the last real state reads.
        auto zeroPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, llvm::ConstantInt::get(m_builder->getInt64Ty(), 0));
        m_builder->CreateStore(endV, zeroPtr);
        auto lastIdx = m_builder->CreateSub(numStates, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1));
        m_builder->CreateStore(endV, m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, lastIdx));

        auto bbWhile = llvm::BasicBlock::Create(*m_context, "while_ST", m_function);
        auto bbBody  = llvm::BasicBlock::Create(*m_context, "body_ST", m_function);
        auto bbNext  = llvm::BasicBlock::Create(*m_context, "next_ST", m_function);
        auto bbExit  = llvm::BasicBlock::Create(*m_context, "exit_ST", m_function);

        auto curr0 = getNext(frst);
        m_builder->CreateBr(bbWhile);

        m_builder->SetInsertPoint(bbWhile);
        auto curr = m_builder->CreatePHI(m_propPtrType, 2, "curr");
        auto cond = m_builder->CreateICmpSLT(curr, last, "curr < last");
        m_builder->CreateCondBr(cond, bbBody, bbExit);

        m_builder->SetInsertPoint(bbBody);
        m_curr.push_back(curr);

        auto idx = m_builder->CreatePtrDiff(m_propType, curr, frst, "idx");
        auto idxPrev = m_builder->CreateSub(idx, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1), "idxPrev");
        auto prevPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idxPrev);
        auto prevVal = m_builder->CreateLoad(m_builder->getInt1Ty(), prevPtr, false, "prevVal");

        auto binary = dynamic_cast<ExprBinary*>(expr);
        auto rhs = make(binary->rhs);
        auto lhs = make(binary->lhs);
        auto rhsHit = m_builder->CreateICmpEQ(rhs, rhsV, "rhsHit");
        auto lhsHit = m_builder->CreateICmpEQ(lhs, lhsV, "lhsHit");
        auto val = m_builder->CreateSelect(
                        rhsHit, rhsV,
                        m_builder->CreateSelect(lhsHit, lhsV, prevVal, "lhsSel"),
                        "val");

        auto currPtr = m_builder->CreateGEP(m_builder->getInt1Ty(), buffer, idx);
        m_builder->CreateStore(val, currPtr);

        m_curr.pop_back();
        m_builder->CreateBr(bbNext);

        m_builder->SetInsertPoint(bbNext);
        auto currNext = getNext(curr);
        m_builder->CreateBr(bbWhile);

        curr->addIncoming(curr0, bbEntry);
        curr->addIncoming(currNext, bbNext);

        m_builder->SetInsertPoint(bbExit);
    }

    return buffer;
}

//  Emit the O(N) fold for one unbounded Sum/Cnt into a value[numStates] stack
//  buffer, and return it.
//
//  Same shape as the boolean recurrence above, with an add in place of the
//  select chain and a carrier wider than a bit:
//
//      total[i] = (p[i] ? v[i] : 0) + total[i + 1]
//
//  The slow path is a forward walk from every evaluation point, so under `G`
//  it is N walks of length N. The walk itself was correct and about as tight
//  as it can be; there was simply one per state. This was never a decision --
//  the accumulators were added after the linear lowering existed and were not
//  considered for it.
llvm::Value* CompileExprImpl::compileAccumulatorLoop(ExprSum* expr, llvm::Type* type)
{
    auto frst = m_frst.back();
    auto last = m_last.back();

    auto diff = m_builder->CreatePtrDiff(m_propType, last, frst, "diff");
    auto numStates = m_builder->CreateAdd(diff, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1), "numStates");
    auto buffer = m_builder->CreateAlloca(type, numStates, "accum_buf");
    auto zero = type->isDoubleTy()
              ? static_cast<llvm::Value*>(llvm::ConstantFP::get(type, 0.0))
              : static_cast<llvm::Value*>(llvm::ConstantInt::getSigned(type, 0));

    //  Both sentinels hold the identity: index numStates-1 is the base case,
    //  and index 0 is never an evaluation point but must still be defined.
    auto lastIdx = m_builder->CreateSub(numStates, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1));
    m_builder->CreateStore(zero, m_builder->CreateGEP(type, buffer, lastIdx));
    m_builder->CreateStore(zero, m_builder->CreateGEP(type, buffer,
                                    llvm::ConstantInt::get(m_builder->getInt64Ty(), 0)));

    auto bbEntry = m_builder->GetInsertBlock();
    auto bbWhile = llvm::BasicBlock::Create(*m_context, "while_Sum", m_function);
    auto bbBody  = llvm::BasicBlock::Create(*m_context, "body_Sum", m_function);
    auto bbNext  = llvm::BasicBlock::Create(*m_context, "next_Sum", m_function);
    auto bbExit  = llvm::BasicBlock::Create(*m_context, "exit_Sum", m_function);

    auto curr0 = getPrev(last);
    m_builder->CreateBr(bbWhile);

    m_builder->SetInsertPoint(bbWhile);
    auto curr = m_builder->CreatePHI(m_propPtrType, 2, "curr");
    m_builder->CreateCondBr(m_builder->CreateICmpSGT(curr, frst, "curr > frst"), bbBody, bbExit);

    m_builder->SetInsertPoint(bbBody);
    m_curr.push_back(curr);

    auto idx = m_builder->CreatePtrDiff(m_propType, curr, frst, "idx");
    auto idxNext = m_builder->CreateAdd(idx, llvm::ConstantInt::get(m_builder->getInt64Ty(), 1), "idxNext");
    auto nextVal = m_builder->CreateLoad(type, m_builder->CreateGEP(type, buffer, idxNext), false, "nextVal");

    //  A state where the condition fails contributes nothing and does not stop
    //  the fold -- the condition selects states, it does not delimit them.
    auto cond    = make(expr->lhs);
    auto value   = make(expr->rhs);
    auto contrib = m_builder->CreateSelect(cond, value, zero, "contrib");
    auto val     = add(nextVal, contrib, "total");

    m_builder->CreateStore(val, m_builder->CreateGEP(type, buffer, idx));

    m_curr.pop_back();

    //  The operands may have built blocks of their own -- a short circuit, a
    //  quantifier -- so the back edge leaves from wherever they ended.
    auto bbBodyEnd = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbNext);

    m_builder->SetInsertPoint(bbNext);
    auto currPrev = getPrev(curr);
    m_builder->CreateBr(bbWhile);

    curr->addIncoming(curr0, bbEntry);
    curr->addIncoming(currPrev, bbNext);

    m_builder->SetInsertPoint(bbExit);

    (void)bbBodyEnd;
    return buffer;
}

//  Emit the O(N) lowering for one *bounded* U/R/S/T node.
//
//  The recurrence used for the unbounded operators does not apply here: the
//  window is anchored at the evaluation point, so val[i] and val[i+-1] are
//  quantified over different windows and the neighbour's result is not a
//  reusable sub-result.  What does hold is that the nested scan, at evaluation
//  point i, examines a *contiguous* range of states -- timestamps increase
//  strictly, so the window's two conditions each cut the index line once --
//  and returns the outcome at the first decisive state in that range, where
//  "decisive" (rhs==rhsV, else lhs==lhsV) depends only on the state, not on i.
//
//  So with
//      dec[j]  = outcome at j, and decIdx[j] = nearest decisive index from j
//                in the scan direction (one pass, since dec[] ignores i)
//      S(i), E(i) = the ends of the examined range
//  the answer is decIdx[S(i)] if that lands within E(i), else endV.
//
//  Both ends move monotonically with i (the window is anchored at t[i], which
//  increases), so a single pointer each covers the whole trace -- giving two
//  linear passes in place of the nested scan.  Working the ends out in terms
//  of "last index whose timestamp is <= X" rather than searching from i keeps
//  each pointer a plain forward walk:
//
//      UR:  S(i) = max(q, i),  q = last index with t[q] <= t[i]+lo
//           E(i) = e,          e = last index <= N-2 with t[e] < t[i]+hi
//      ST:  S(i) = min(p, i),  p = first index with t[p] >= t[i]-lo
//           E(i) = f,          f = first index with t[f] >  t[i]-hi
//
//  An absent bound drops its constraint (S(i)=i / E(i)=the far end).
llvm::Value* CompileExprImpl::compileTemporalLoopBounded(Temporal<ExprBinary>* expr,
                                                         llvm::Value* rhsV,
                                                         llvm::Value* lhsV,
                                                         llvm::Value* endV,
                                                         bool         isUR)
{
    auto    i64     = m_builder->getInt64Ty();
    auto    i1      = m_builder->getInt1Ty();
    auto    frst    = m_frst.back();
    auto    last    = m_last.back();

    auto    K       = [&](std::int64_t v) { return llvm::ConstantInt::getSigned(i64, v); };

    auto    diff    = m_builder->CreatePtrDiff(m_propType, last, frst, "diff");
    auto    n       = m_builder->CreateAdd(diff, K(1), "numStates");
    auto    nm1     = m_builder->CreateSub(n, K(1), "n-1");
    auto    nm2     = m_builder->CreateSub(n, K(2), "n-2");

    auto    decV    = m_builder->CreateAlloca(i1,  n, "decV");
    auto    decI    = m_builder->CreateAlloca(i64, n, "decI");
    auto    buffer  = m_builder->CreateAlloca(i1,  n, "temp_buf");

    //  Time bounds are loop-invariant (checked before we get here), so emit
    //  them once, outside every loop.
    llvm::Value*    loV = expr->time->lo ? make(expr->time->lo) : nullptr;
    llvm::Value*    hiV = expr->time->hi ? make(expr->time->hi) : nullptr;

    //  `none` is an index the range test can never accept, so a lookup landing
    //  on it falls through to endV.  Its decV slot is still loaded (then
    //  discarded by the select) and so must hold something defined.
    auto    none    = isUR ? nm1 : K(0);
    m_builder->CreateStore(none, m_builder->CreateGEP(i64, decI, none));
    m_builder->CreateStore(endV, m_builder->CreateGEP(i1,  decV, none));
    m_builder->CreateStore(endV, m_builder->CreateGEP(i1,  buffer, K(0)));
    m_builder->CreateStore(endV, m_builder->CreateGEP(i1,  buffer, nm1));

    emitDecisivePass(expr, rhsV, lhsV, isUR, decV, decI, nm2);

    //  ── Per evaluation point: locate the window, then chase decIdx ──────────
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "winHead", m_function);
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "winBody", m_function);
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "winNext", m_function);
    auto    bbExit  = llvm::BasicBlock::Create(*m_context, "winExit", m_function);

    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    m_builder->SetInsertPoint(bbHead);
    auto    i       = m_builder->CreatePHI(i64, 2, "i");
    auto    aCur    = m_builder->CreatePHI(i64, 2, "aCur");
    auto    bCur    = m_builder->CreatePHI(i64, 2, "bCur");
    m_builder->CreateCondBr(m_builder->CreateICmpSLE(i, nm2, "i <= n-2"), bbBody, bbExit);

    m_builder->SetInsertPoint(bbBody);
    auto    ti      = timeAtIndex(i);
    //  UR anchors the window forward from t[i], ST backward from it.
    auto    aBound  = loV ? (isUR ? m_builder->CreateAdd(ti, loV, "t+lo")
                                  : m_builder->CreateSub(ti, loV, "t-lo"))
                          : nullptr;
    auto    bBound  = hiV ? (isUR ? m_builder->CreateAdd(ti, hiV, "t+hi")
                                  : m_builder->CreateSub(ti, hiV, "t-hi"))
                          : nullptr;

    //  Near end.  UR extends forward past every t[q] <= t[i]+lo, so it probes
    //  q+1; ST steps up while t[p] < t[i]-lo, so it probes p itself.
    auto    a       = emitMonotoneWalk(aCur, aBound, /*probeIsNext=*/isUR, nm1,
                                       isUR ? llvm::CmpInst::ICMP_SLE : llvm::CmpInst::ICMP_SLT,
                                       "a");
    //  Far end.
    auto    b       = emitMonotoneWalk(bCur, bBound, /*probeIsNext=*/isUR, nm2,
                                       isUR ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_SLE,
                                       "b");

    //  The scan starts at the evaluation point, so the near end is clamped
    //  against i; the far end stands on its own.
    llvm::Value*    sIdx = i;
    if (aBound)
    {
        auto    takeA = isUR ? m_builder->CreateICmpSGT(a, i, "a > i")
                             : m_builder->CreateICmpSLT(a, i, "a < i");
        sIdx = m_builder->CreateSelect(takeA, a, i, "S");
    }
    auto    eIdx    = bBound ? b : (isUR ? nm2 : K(1));

    auto    k       = m_builder->CreateLoad(i64, m_builder->CreateGEP(i64, decI, sIdx), "k");
    auto    hit     = isUR ? m_builder->CreateICmpSLE(k, eIdx, "k <= E")
                           : m_builder->CreateICmpSGE(k, eIdx, "k >= E");
    auto    kVal    = m_builder->CreateLoad(i1, m_builder->CreateGEP(i1, decV, k), "kVal");
    m_builder->CreateStore(m_builder->CreateSelect(hit, kVal, endV, "val"),
                           m_builder->CreateGEP(i1, buffer, i));
    m_builder->CreateBr(bbNext);

    m_builder->SetInsertPoint(bbNext);
    auto    iStep   = m_builder->CreateAdd(i, K(1), "i+1");
    m_builder->CreateBr(bbHead);

    i->addIncoming(K(1),  bbEntry);
    i->addIncoming(iStep, bbNext);
    //  Both pointers only ever move forward, so they are carried across
    //  evaluation points -- that is what makes the two walks amortised O(1)
    //  per state rather than a rescan each time.  They start at 0: index 0 is
    //  a legitimate answer meaning "the window reaches past the start of the
    //  trace", which for ST is how an empty window is expressed.
    aCur->addIncoming(K(0), bbEntry);
    aCur->addIncoming(a,    bbNext);
    bCur->addIncoming(isUR ? K(0) : K(1), bbEntry);
    bCur->addIncoming(b,    bbNext);

    m_builder->SetInsertPoint(bbExit);
    return buffer;
}

//  Load the timestamp of the state at `idx`, counting from the first state.
llvm::Value*    CompileExprImpl::timeAtIndex(llvm::Value* idx)
{
    return getTime(m_builder->CreateGEP(m_propType, m_frst.back(), idx), "t");
}

//  Fill decV[j] with the outcome at j and decI[j] with the nearest decisive
//  index at or beyond j in the scan direction.  Neither depends on the
//  evaluation point, which is what lets one pass serve every point.
void    CompileExprImpl::emitDecisivePass(Temporal<ExprBinary>*  expr,
                                          llvm::Value*           rhsV,
                                          llvm::Value*           lhsV,
                                          bool                   isUR,
                                          llvm::Value*           decV,
                                          llvm::Value*           decI,
                                          llvm::Value*           nm2)
{
    auto    i64     = m_builder->getInt64Ty();
    auto    i1      = m_builder->getInt1Ty();
    auto    K       = [&](std::int64_t v) { return llvm::ConstantInt::getSigned(i64, v); };

    auto    bbHead  = llvm::BasicBlock::Create(*m_context, "decHead", m_function);
    auto    bbBody  = llvm::BasicBlock::Create(*m_context, "decBody", m_function);
    auto    bbNext  = llvm::BasicBlock::Create(*m_context, "decNext", m_function);
    auto    bbExit  = llvm::BasicBlock::Create(*m_context, "decExit", m_function);

    //  Walk away from the sentinel so each slot can chain off its neighbour.
    auto    start   = isUR ? nm2 : K(1);
    auto    bbEntry = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    m_builder->SetInsertPoint(bbHead);
    auto    j       = m_builder->CreatePHI(i64, 2, "j");
    auto    cont    = isUR ? m_builder->CreateICmpSGE(j, K(1), "j >= 1")
                           : m_builder->CreateICmpSLE(j, nm2,  "j <= n-2");
    m_builder->CreateCondBr(cont, bbBody, bbExit);

    m_builder->SetInsertPoint(bbBody);
    m_curr.push_back(m_builder->CreateGEP(m_propType, m_frst.back(), j));
    auto    rhs     = make(expr->rhs);
    auto    lhs     = make(expr->lhs);
    m_curr.pop_back();

    auto    rhsHit  = m_builder->CreateICmpEQ(rhs, rhsV, "rhsHit");
    auto    lhsHit  = m_builder->CreateICmpEQ(lhs, lhsV, "lhsHit");
    auto    isDec   = m_builder->CreateOr(rhsHit, lhsHit, "isDec");
    m_builder->CreateStore(m_builder->CreateSelect(rhsHit, rhsV, lhsV, "outcome"),
                           m_builder->CreateGEP(i1, decV, j));

    auto    nbr     = isUR ? m_builder->CreateAdd(j, K(1), "j+1")
                           : m_builder->CreateSub(j, K(1), "j-1");
    auto    chain   = m_builder->CreateLoad(i64, m_builder->CreateGEP(i64, decI, nbr), "chain");
    m_builder->CreateStore(m_builder->CreateSelect(isDec, j, chain, "nearest"),
                           m_builder->CreateGEP(i64, decI, j));
    m_builder->CreateBr(bbNext);

    m_builder->SetInsertPoint(bbNext);
    auto    jStep   = isUR ? m_builder->CreateSub(j, K(1), "j-1")
                           : m_builder->CreateAdd(j, K(1), "j+1");
    m_builder->CreateBr(bbHead);

    j->addIncoming(start, bbEntry);
    j->addIncoming(jStep, bbNext);

    m_builder->SetInsertPoint(bbExit);
}

//  Advance a monotone index while the probed state's timestamp still satisfies
//  `keepPred` against `bound`, and leave the insert point after the walk.
//  A null bound means the constraint is absent and the index does not move.
//
//  The probe is clamped into range before the load, so the iteration that
//  discovers it has run off the trace cannot read past it; `inRange` then
//  discards the value it read.
llvm::Value*    CompileExprImpl::emitMonotoneWalk(llvm::Value*             init,
                                                  llvm::Value*             bound,
                                                  bool                     probeIsNext,
                                                  llvm::Value*             limit,
                                                  llvm::CmpInst::Predicate keepPred,
                                                  std::string const&       name)
{
    if (!bound)
        return init;

    auto    i64     = m_builder->getInt64Ty();
    auto    bbHead  = llvm::BasicBlock::Create(*m_context, name + "Head", m_function);
    auto    bbDone  = llvm::BasicBlock::Create(*m_context, name + "Done", m_function);

    auto    bbPre   = m_builder->GetInsertBlock();
    m_builder->CreateBr(bbHead);

    m_builder->SetInsertPoint(bbHead);
    auto    p       = m_builder->CreatePHI(i64, 2, name);
    auto    pNext   = m_builder->CreateAdd(p, llvm::ConstantInt::getSigned(i64, 1), name + "+1");
    auto    probe   = probeIsNext ? pNext : p;
    auto    inRange = m_builder->CreateICmpSLE(probe, limit, name + " in range");
    auto    safe    = m_builder->CreateSelect(inRange, probe,
                                              llvm::ConstantInt::getSigned(i64, 0));
    auto    keep    = m_builder->CreateCmp(keepPred, timeAtIndex(safe), bound, name + " keep");
    auto    go      = m_builder->CreateAnd(inRange, keep, "adv" + name);
    auto    bbEnd   = m_builder->GetInsertBlock();
    m_builder->CreateCondBr(go, bbHead, bbDone);

    p->addIncoming(init,  bbPre);
    p->addIncoming(pNext, bbEnd);

    m_builder->SetInsertPoint(bbDone);
    return p;
}

llvm::Value*    CompileExprImpl::make(Expr* expr)
{
    auto    save    = m_value;

    expr->accept(*this);

    auto    result  = m_value;
    m_value = save;

    return  result;
}

llvm::Value*    CompileExprImpl::make(Spec* spec)
{
    auto    save    = m_value;

    spec->accept(*this);

    auto    result  = m_value;
    m_value = save;

    return  result;
}

llvm::Type* Compile::make(llvm::LLVMContext* context, llvm::Module* module, Type* type, std::string name)
{
    CompileTypeImpl impl(context, module);

    return impl.make(type, name);
}

llvm::Value*Compile::make(llvm::LLVMContext* context, llvm::Module* module, Expr* expr)
{
    return nullptr;
}

void Compile::make(llvm::LLVMContext* context, llvm::Module* module, Module* refmod)
{
    auto    builder = std::make_unique<llvm::IRBuilder<>>(*context);

    //  create __conf__
    std::vector<llvm::Type*>    confTypes;
    auto    confNames   = refmod->getConfNames();
    for(auto name: confNames)
    {
        auto    type    = refmod->getConf(name);
        confTypes.push_back(make(context, module, type, name));
    }
    auto    confType    = llvm::StructType::create(*context, confTypes, "__conf_t");
    auto    confPtrType = llvm::PointerType::get(*context, 0);
    module->getOrInsertGlobal("__conf__", confType);

    //  create __prop__
    auto    propNames   = refmod->getPropNames();
    std::vector<llvm::Type*>    propTypes;
    propTypes.push_back(builder->getInt64Ty()); //  __time__
    for(auto name: propNames)
    {
        if(name == "__time__")
            continue;

        (void)refmod->getProp(name);            // keep semantic types reachable
        propTypes.push_back(llvm::PointerType::get(*context, 0));
    }
    auto    propType    = llvm::StructType::create(*context, propTypes, "__prop_t");
    auto    propPtrType = llvm::PointerType::get(*context, 0);
    module->getOrInsertGlobal("__prop__", propPtrType);

    auto    exprs   = refmod->getExprs();
    for(std::size_t ei = 0; ei < exprs.size(); ei++)
    {
        auto    expr        = exprs[ei];
        auto    pos         = expr->where();
        //  A requirement written with `@name` is labelled by that name, so an
        //  external corpus can refer to it without depending on where it sits
        //  in the file.
        auto    named       = refmod->getExprName(ei);
        auto    funcName    = named.empty() ? pos.text() : named;
        auto    funcType    = llvm::FunctionType::get(builder->getInt1Ty(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod, propType, confType);

        auto    temp        = Rewrite::make(expr);
        TypeCalc::make(refmod, temp);
        compExpr.compileTemporalLoops(temp);
        builder->CreateRet(compExpr.make(temp));
        if(!llvm::verifyFunction(*funcBody, &llvm::outs()))
        {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
            //throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }

    auto    specs   = refmod->getSpecs();
    for(std::size_t si = 0; si < specs.size(); si++)
    {
        auto    spec        = specs[si];
        auto    pos         = spec->where();
        auto    named       = refmod->getSpecName(si);
        auto    funcName    = named.empty() ? pos.text() : named;
        auto    funcType    = llvm::FunctionType::get(builder->getInt1Ty(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod, propType, confType);

        builder->CreateRet(compExpr.make(spec));

        if(!llvm::verifyFunction(*funcBody, &llvm::outs()))
        {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
            //throw std::runtime_error(__PRETTY_FUNCTION__);
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }

    // ── Create __prepare__ JIT function ─────────────────────────────────────
    //
    //  Fills the slot of every computed (`data x = expr`) prop at every state,
    //  ahead of any requirement function running.
    //
    //  One full trace pass *per prop*, in declaration order -- not one pass
    //  over states evaluating every prop. The distinction matters because a
    //  computed prop may be defined in terms of an earlier computed prop at a
    //  *different* state (`data y = Xs(x);`). State-major order would read
    //  x[i+1] before it had been written. Prop-major order materialises each
    //  prop across the whole trace before anything that depends on it runs,
    //  and declaration order already guarantees dependencies come first
    //  (Antlr2AST resolves a computed prop's type against the props already in
    //  the module, so a forward reference cannot parse).
    //
    //  It also lets each prop's temporal buffers be built once, in a block
    //  outside its state loop, instead of being re-allocated and refilled on
    //  every iteration.
    {
        auto    funcName    = "__prepare__";
        auto    funcType    = llvm::FunctionType::get(builder->getVoidTy(), {propPtrType, propPtrType, confPtrType}, false);
        auto    funcBody    = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
        auto    funcArgs    = funcBody->args().begin();

        funcArgs->setName("frst");  funcArgs++;
        funcArgs->setName("last");  funcArgs++;
        funcArgs->setName("conf");

        auto    bb          = llvm::BasicBlock::Create(*context, "entry", funcBody);
        builder->SetInsertPoint(bb);

        CompileExprImpl compExpr(context, module, builder.get(), funcBody, refmod, propType, confType);

        auto    frst        = compExpr.m_frst.back();
        auto    last        = compExpr.m_last.back();
        auto    curr0       = compExpr.getNext(frst);

        auto    propNames   = refmod->getPropNames();

        for (std::size_t pi = 0; pi < propNames.size(); pi++)
        {
            auto const& name    = propNames[pi];
            if (!refmod->isExprData(name))
                continue;

            auto    rewritten   = Rewrite::make(refmod->getPropExpr(name));
            TypeCalc::make(refmod, rewritten);

            //  Setup: build this prop's temporal buffers once, outside its
            //  state loop.  Buffers only dominate blocks emitted after them,
            //  so anything a previous prop built is unusable here -- drop it
            //  rather than let the fast path load from a buffer that does not
            //  dominate this use.
            compExpr.resetTemporalBuffers();
            compExpr.compileTemporalLoops(rewritten);

            auto    bbWhile     = llvm::BasicBlock::Create(*context, "while_" + name, funcBody);
            auto    bbBody      = llvm::BasicBlock::Create(*context, "body_"  + name, funcBody);
            auto    bbNext      = llvm::BasicBlock::Create(*context, "next_"  + name, funcBody);
            auto    bbDone      = llvm::BasicBlock::Create(*context, "done_"  + name, funcBody);

            //  compileTemporalLoops leaves the insert point in whichever block
            //  its last loop nest ended in, so capture it rather than assume.
            auto    bbSetup     = builder->GetInsertBlock();
            builder->CreateBr(bbWhile);

            builder->SetInsertPoint(bbWhile);
            auto    curr        = builder->CreatePHI(propPtrType, 2, "curr");
            auto    currLTlast  = builder->CreateICmpSLT(curr, last, "curr < last");
            builder->CreateCondBr(currLTlast, bbBody, bbDone);

            builder->SetInsertPoint(bbBody);
            compExpr.m_curr.push_back(curr);

            llvm::Value* val    = compExpr.make(rewritten);

            auto    propPtrPtr  = builder->CreateStructGEP(propType, curr, pi + 1);
            auto    propPtr     = builder->CreateLoad(builder->getPtrTy(), propPtrPtr, false, "ptr_" + name);
            builder->CreateStore(val, propPtr);

            compExpr.m_curr.pop_back();
            builder->CreateBr(bbNext);

            builder->SetInsertPoint(bbNext);
            auto    currNext    = compExpr.getNext(curr);
            builder->CreateBr(bbWhile);

            curr->addIncoming(curr0, bbSetup);
            curr->addIncoming(currNext, bbNext);

            builder->SetInsertPoint(bbDone);
        }

        builder->CreateRetVoid();

        if(llvm::verifyFunction(*funcBody, &llvm::outs()))
        {
//  LCOV_EXCL_START
//  GCOV_EXCL_START
            throw std::runtime_error("__prepare__ failed LLVM verification");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
        }
    }
}
