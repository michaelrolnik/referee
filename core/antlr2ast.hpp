/*
 *  MIT License
 *  
 *  Copyright (c) 2022 Michael Rolnik
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

#pragma once

#include "antlr4-runtime.h"
#include "refereeParser.h"
#include "refereeLexer.h"
#include "refereeBaseVisitor.h"
#include "position.hpp"
#include "module.hpp"


class Antlr2AST
    : public referee::refereeBaseVisitor
{
public:
    Antlr2AST(std::string name);

    std::any visitDeclConf(     referee::refereeParser::DeclConfContext*    ctx) override;
    std::any visitDeclData(     referee::refereeParser::DeclDataContext*    ctx) override;
    std::any visitDeclType(     referee::refereeParser::DeclTypeContext*    ctx) override;
    std::any visitExprAdd(      referee::refereeParser::ExprAddContext*     ctx) override;
    std::any visitExprAnd(      referee::refereeParser::ExprAndContext*     ctx) override;
    std::any visitExprAt(       referee::refereeParser::ExprAtContext*      ctx) override;
    std::any visitExprConst(    referee::refereeParser::ExprConstContext*   ctx) override;
    std::any visitExprData(     referee::refereeParser::ExprDataContext*    ctx) override;
    std::any visitExprDiv(      referee::refereeParser::ExprDivContext*     ctx) override;
    std::any visitExprEq(       referee::refereeParser::ExprEqContext*      ctx) override;
    std::any visitExprEqu(      referee::refereeParser::ExprEquContext*     ctx) override;
    std::any visitExprF(        referee::refereeParser::ExprFContext*       ctx) override;
    std::any visitExprG(        referee::refereeParser::ExprGContext*       ctx) override;
    std::any visitExprGe(       referee::refereeParser::ExprGeContext*      ctx) override;
    std::any visitExprGt(       referee::refereeParser::ExprGtContext*      ctx) override;
    std::any visitExprH(        referee::refereeParser::ExprHContext*       ctx) override;
    std::any visitExprImp(      referee::refereeParser::ExprImpContext*     ctx) override;
    std::any visitExprIndx(     referee::refereeParser::ExprIndxContext*    ctx) override;
    std::any visitExprInt(      referee::refereeParser::ExprIntContext*     ctx) override;
    std::any visitInteger(      referee::refereeParser::IntegerContext*     ctx) override;
    std::any visitExprLe(       referee::refereeParser::ExprLeContext*      ctx) override;
    std::any visitExprLt(       referee::refereeParser::ExprLtContext*      ctx) override;
    std::any visitExprMmbr(     referee::refereeParser::ExprMmbrContext*    ctx) override;
    std::any visitExprMod(      referee::refereeParser::ExprModContext*     ctx) override;
    std::any visitExprMul(      referee::refereeParser::ExprMulContext*     ctx) override;
    std::any visitExprNe(       referee::refereeParser::ExprNeContext*      ctx) override;
    std::any visitExprNot(      referee::refereeParser::ExprNotContext*     ctx) override;
    std::any visitExprNeg(      referee::refereeParser::ExprNegContext*     ctx) override;
    std::any visitExprO(        referee::refereeParser::ExprOContext*       ctx) override;
    std::any visitExprOr(       referee::refereeParser::ExprOrContext*      ctx) override;
    std::any visitExprParen(    referee::refereeParser::ExprParenContext*   ctx) override;
    std::any visitExprRs(       referee::refereeParser::ExprRsContext*      ctx) override;
    std::any visitExprRw(       referee::refereeParser::ExprRwContext*      ctx) override;
    std::any visitExprSs(       referee::refereeParser::ExprSsContext*      ctx) override;
    std::any visitExprSub(      referee::refereeParser::ExprSubContext*     ctx) override;
    std::any visitExprSw(       referee::refereeParser::ExprSwContext*      ctx) override;
    std::any visitExprTer(      referee::refereeParser::ExprTerContext*     ctx) override;
    std::any visitExprTs(       referee::refereeParser::ExprTsContext*      ctx) override;
    std::any visitExprTw(       referee::refereeParser::ExprTwContext*      ctx) override;
    std::any visitExprUs(       referee::refereeParser::ExprUsContext*      ctx) override;
    std::any visitExprUw(       referee::refereeParser::ExprUwContext*      ctx) override;
    std::any visitExprXor(      referee::refereeParser::ExprXorContext*     ctx) override;
    std::any visitExprXs(       referee::refereeParser::ExprXsContext*      ctx) override;
    std::any visitExprXw(       referee::refereeParser::ExprXwContext*      ctx) override;
    std::any visitExprYs(       referee::refereeParser::ExprYsContext*      ctx) override;
    std::any visitExprYw(       referee::refereeParser::ExprYwContext*      ctx) override;
    std::any visitProgram(      referee::refereeParser::ProgramContext*     ctx) override;
    std::any visitStatement(    referee::refereeParser::StatementContext*   ctx) override;
    std::any visitTimeFull(     referee::refereeParser::TimeFullContext*    ctx) override;
    std::any visitTimeLower(    referee::refereeParser::TimeLowerContext*   ctx) override;
    std::any visitTimeUpper(    referee::refereeParser::TimeUpperContext*   ctx) override;
    std::any visitTypeAlias(    referee::refereeParser::TypeAliasContext*   ctx) override;
    std::any visitTypeArray(    referee::refereeParser::TypeArrayContext*   ctx) override;
    std::any visitTypeEnum(     referee::refereeParser::TypeEnumContext*    ctx) override;
    std::any visitTypeStruct(   referee::refereeParser::TypeStructContext*  ctx) override;

    std::any visitUnits(                    referee::refereeParser::UnitsContext*                   ctx) override;

    std::any visitConstraint(               referee::refereeParser::ConstraintContext*              ctx) override;

    std::any visitSpecBody(                 referee::refereeParser::SpecBodyContext*                ctx) override;
    std::any visitSpecGlobally(             referee::refereeParser::SpecGloballyContext*            ctx) override;
    std::any visitSpecBefore(               referee::refereeParser::SpecBeforeContext*              ctx) override;
    std::any visitSpecAfter(                referee::refereeParser::SpecAfterContext*               ctx) override;
    std::any visitSpecWhile(                referee::refereeParser::SpecWhileContext*               ctx) override;
    std::any visitSpecBetweenAnd(           referee::refereeParser::SpecBetweenAndContext*          ctx) override;
    std::any visitSpecAfterUntil(           referee::refereeParser::SpecAfterUntilContext*          ctx) override;

    std::any visitSpecUniversality(         referee::refereeParser::SpecUniversalityContext*        ctx) override;
    std::any visitSpecAbsence(              referee::refereeParser::SpecAbsenceContext*             ctx) override;
    std::any visitSpecExistence(            referee::refereeParser::SpecExistenceContext*           ctx) override;
    std::any visitSpecTransientState(       referee::refereeParser::SpecTransientStateContext*      ctx) override;
    std::any visitSpecSteadyState(          referee::refereeParser::SpecSteadyStateContext*         ctx) override;
    std::any visitSpecMinimunDuration(      referee::refereeParser::SpecMinimunDurationContext*     ctx) override;
    std::any visitSpecMaximumDuration(      referee::refereeParser::SpecMaximumDurationContext*     ctx) override;
    std::any visitSpecRecurrence(           referee::refereeParser::SpecRecurrenceContext*          ctx) override;
    std::any visitSpecPrecedence(           referee::refereeParser::SpecPrecedenceContext*          ctx) override;
    std::any visitSpecPrecedenceChain12(    referee::refereeParser::SpecPrecedenceChain12Context*   ctx) override;
    std::any visitSpecPrecedenceChain21(    referee::refereeParser::SpecPrecedenceChain21Context*   ctx) override;
    std::any visitSpecResponse(             referee::refereeParser::SpecResponseContext*            ctx) override;
    std::any visitSpecResponseChain12(      referee::refereeParser::SpecResponseChain12Context*     ctx) override;
    std::any visitSpecResponseChain21(      referee::refereeParser::SpecResponseChain21Context*     ctx) override;
    std::any visitSpecResponseInvariance(   referee::refereeParser::SpecResponseInvarianceContext*  ctx) override;
    std::any visitSpecUntil(                referee::refereeParser::SpecUntilContext*               ctx) override;

    std::any visitUpperTimeBound(           referee::refereeParser::UpperTimeBoundContext*          ctx) override;
    std::any visitLowerTimeBound(           referee::refereeParser::LowerTimeBoundContext*          ctx) override;
    std::any visitIntervalBound(            referee::refereeParser::IntervalBoundContext*           ctx) override;
    std::any visitNoTimeBound(              referee::refereeParser::NoTimeBoundContext*             ctx) override;

private:
    void    assert_non_temporal(Expr* expr);

private:
    template<typename Type, typename Ctxt>
    std::any    acceptTernary(Ctxt* ctxt);

    template<typename Type, typename Ctxt>
    std::any    acceptBinary(Ctxt* ctxt);

    template<typename Type, typename Ctxt>
    std::any    acceptUnary(Ctxt* ctxt);

    template<typename Type, typename Ctxt>
    std::any    acceptTemporalBinary(Ctxt* ctxt);

    template<typename Type, typename Ctxt>
    std::any    acceptTemporalUnary(Ctxt* ctxt);

    template<typename Type, typename ... Args> 
    Type*       build(antlr4::ParserRuleContext* rule, Args ... args);

    Position    position(antlr4::ParserRuleContext* rule);

    Module* module  = nullptr;
};
