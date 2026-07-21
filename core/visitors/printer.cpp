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

#include "printer.hpp"
#include "../factory.hpp"

std::string multichar(int data)
{
    std::ostringstream os;

    data    = ((data & 0xffff0000) >> 16)
            | ((data & 0x0000ffff) << 16);
    data    = ((data & 0xff00ff00) >>  8)
            | ((data & 0x00ff00ff) <<  8);            

    //  An operator is packed into an int, so anything shorter than four
    //  characters carries NUL padding. Emitting those verbatim put raw zero
    //  bytes in the middle of the output ("\0\0==" for '==').
    while(data)
    {
        char    c   = (char)(data & 0xff);
        if(c != '\0')
            os << c;
        data >>= 8;
    }

    return os.str();
}

struct PrinterImpl
    : Visitor< Expr
             , ExprBinary
             , ExprParen
             , Temporal<ExprBinary>
             , ExprUnary
             , Temporal<ExprUnary>
             , ExprContext
             , ExprData
             , ExprConf
             , ExprMmbr
             , ExprConstBoolean
             , ExprConstInteger
             , ExprConstNumber
             , ExprConstString
             , ExprIndx
             , ExprChoice
             , ExprAt
             , Time
             , TimeMax
             , TimeMin
             , SpecUniversality
             , SpecAbsence
             , SpecExistence
             , SpecTransientState
             , SpecSteadyState
             , SpecMinimunDuration
             , SpecMaximumDuration
             , SpecRecurrence
             , SpecPrecedence
             , SpecPrecedenceChain12
             , SpecPrecedenceChain21
             , SpecResponse
             , SpecResponseChain12
             , SpecResponseChain21
             , SpecResponseInvariance
             , SpecUntil
             , SpecGlobally
             , SpecBefore
             , SpecAfter
             , SpecWhile
             , SpecBetweenAnd
             , SpecAfterUntil>
{
    PrinterImpl(std::ostream& os)
        : os(os)
    {
    }
    
    void            output( Base*   base);
    std::string     make(   Base*   base);

    void    visit(Expr*                 expr) override;
    void    visit(ExprAt*               expr) override;
    void    visit(ExprBinary*           expr) override;
    void    visit(ExprConstBoolean*     expr) override;
    void    visit(ExprConstInteger*     expr) override;
    void    visit(ExprConstNumber*      expr) override;
    void    visit(ExprConstString*      expr) override;
    void    visit(ExprIndx*             expr) override;
    void    visit(ExprChoice*           expr) override;
    void    visit(ExprContext*          expr) override;
    void    visit(ExprData*             expr) override;
    void    visit(ExprConf*             expr) override;
    void    visit(ExprMmbr*             expr) override;
    void    visit(ExprParen*            expr) override;
    void    visit(ExprUnary*            expr) override;
    void    visit(Temporal<ExprBinary>* expr) override;
    void    visit(Temporal<ExprUnary>*  expr) override;
    void    visit(Time*                 expr) override;
    void    visit(TimeMin*              expr) override;
    void    visit(TimeMax*              expr) override;

    void    visit(SpecUniversality*         spec) override;
    void    visit(SpecAbsence*              spec) override;
    void    visit(SpecExistence*            spec) override;
    void    visit(SpecTransientState*       spec) override;
    void    visit(SpecSteadyState*          spec) override;
    void    visit(SpecMinimunDuration*      spec) override;
    void    visit(SpecMaximumDuration*      spec) override;
    void    visit(SpecRecurrence*           spec) override;
    void    visit(SpecPrecedence*           spec) override;
    void    visit(SpecPrecedenceChain12*    spec) override;
    void    visit(SpecPrecedenceChain21*    spec) override;
    void    visit(SpecResponse*             spec) override;
    void    visit(SpecResponseChain12*      spec) override;
    void    visit(SpecResponseChain21*      spec) override;
    void    visit(SpecResponseInvariance*   spec) override;
    void    visit(SpecUntil*                spec) override;
    void    visit(SpecGlobally*            spec) override;
    void    visit(SpecBefore*              spec) override;
    void    visit(SpecAfter*               spec) override;
    void    visit(SpecWhile*               spec) override;
    void    visit(SpecBetweenAnd*          spec) override;
    void    visit(SpecAfterUntil*          spec) override;

    std::ostream&   os;
};

void    PrinterImpl::visit( Expr*           expr)   
{
    os << "???";
}

void    PrinterImpl::visit( ExprParen*              expr)
{
    os << "(";
    expr->arg->accept(*this);
    os << ")";
}


void    PrinterImpl::visit( ExprUnary*              expr)
{
    os << multichar(expr->op) << " ";
    expr->arg->accept(*this);
}


void    PrinterImpl::visit( ExprBinary*             expr)
{
    expr->lhs->accept(*this);
    os << " " << multichar(expr->op) << " ";
    expr->rhs->accept(*this);
}

void    PrinterImpl::visit( Temporal<ExprUnary>*    expr)
{
    os << multichar(expr->op);
    if(expr->time)
        expr->time->accept(*this);
    os << "(";
    expr->arg->accept(*this);
    os << ")";
}

void    PrinterImpl::visit( Temporal<ExprBinary>*   expr)
{
    os << multichar(expr->op);
    if(expr->time)
        expr->time->accept(*this);
    os << "(";
    expr->lhs->accept(*this);
    os << ", ";
    expr->rhs->accept(*this);
    os << ")";
}

void    PrinterImpl::visit( ExprContext*   expr)
{
    if(expr->name != "__curr__")
    {
        os << expr->name << ".";
    }
}

void    PrinterImpl::visit( ExprData*       expr)
{
    expr->ctxt->accept(*this);
    os << expr->name;
}

void    PrinterImpl::visit( ExprConf*       expr)
{
    os << expr->name;
}

void    PrinterImpl::visit(ExprMmbr*        expr)
{
    expr->arg->accept(*this);
    os << "." << expr->mmbr;
}

void    PrinterImpl::visit(ExprConstInteger*    expr)
{
    os << expr->value;
}

void    PrinterImpl::visit(ExprConstString*     expr)
{
    os << "\"" << expr->value << "\"";
}

//  Indexing is a binary node, so without its own case it printed through the
//  generic infix path as `arr [] 2`.
void    PrinterImpl::visit(ExprIndx*            expr)
{
    expr->lhs->accept(*this);
    os << "[";
    expr->rhs->accept(*this);
    os << "]";
}

void    PrinterImpl::visit(ExprChoice*          expr)
{
    expr->lhs->accept(*this);
    os << " ? ";
    expr->mhs->accept(*this);
    os << " : ";
    expr->rhs->accept(*this);
}

void    PrinterImpl::visit(ExprConstNumber*     expr)
{
    os << expr->value; 
}

void    PrinterImpl::visit(ExprConstBoolean*    expr)
{
    os << (expr->value ? "true" : "false"); 
}

void    PrinterImpl::visit(Time*                expr)
{
    os << "[";
    expr->lo->accept(*this);
    os << ":";
    expr->hi->accept(*this);
    os << "]";
}

void    PrinterImpl::visit(TimeMax*             expr)
{
    os << "[";
    os << ":";
    expr->hi->accept(*this);
    os << "]";
}

void    PrinterImpl::visit(TimeMin*             expr)
{
    os << "[";
    expr->lo->accept(*this);
    os << ":";
    os << "]";
}

void    PrinterImpl::visit(ExprAt*              expr)
{
    os << expr->name << "@";
    expr->arg->accept(*this);
}                  

void    PrinterImpl::output(Base*               base)
{
    base->accept(*this);
}

void    PrinterImpl::visit(SpecUniversality*         spec)
{
    os  <<  "it is always the case that " << make(spec->P) << " holds " << make(spec->tP);
}

void    PrinterImpl::visit(SpecAbsence*              spec)
{
    os  <<  "it is never the case that " << make(spec->P) << " holds " << make(spec->tP);
}

void    PrinterImpl::visit(SpecExistence*            spec)
{
    os  <<  make(spec->P) << " eventually holds " << make(spec->tP);
}

void    PrinterImpl::visit(SpecTransientState*       spec)
{
    os  <<  make(spec->P) << " holds after " << make(spec->tP) << " nanoseconds";
}

void    PrinterImpl::visit(SpecSteadyState*          spec)
{
    os  <<  make(spec->P) << " holds in the long run";
}

void    PrinterImpl::visit(SpecMinimunDuration*      spec)
{
    os  <<  "once " << make(spec->P) << " becomes satisfied it remains so for at least " << make(spec->tP) << " nanoseconds";
}

void    PrinterImpl::visit(SpecMaximumDuration*      spec)
{
    os  <<  "once " << make(spec->P) << " becomes satisfied it remains so for less than " << make(spec->tP) << " nanoseconds";
}

void    PrinterImpl::visit(SpecRecurrence*           spec)
{
    os  <<  make(spec->P) << " holds repeatedly every " << make(spec->tP) << " nanoseconds";
}

void    PrinterImpl::visit(SpecPrecedence*           spec)
{
    os  <<  "if " << make(spec->P) << " holds, then it must have been the case that " << make(spec->S) << " has occurred " << make(spec->tPS) << " before it";
}

void    PrinterImpl::visit(SpecPrecedenceChain12*    spec)
{
    os  <<  "if " << make(spec->S) << " and afterwards " << make(spec->T) << " " << make(spec->tST) << " holds, then it must have been the case that " << make(spec->P) << " has occurred " << make(spec->tPS) << " before it";
}

void    PrinterImpl::visit(SpecPrecedenceChain21*    spec)
{
    os  <<  "if " << make(spec->P) << " holds, then it must have been the case that " << make(spec->S) << " and afterwards " << make(spec->T) << " " << make(spec->tST) << " have occurred " << make(spec->tPS) << " before it";
}

void    PrinterImpl::visit(SpecResponse*             spec)
{
    os  <<  "if " << make(spec->P) << " has occurred, then in response " << make(spec->S) << " eventually holds " << make(spec->tPS) << " " << make(spec->cPS);
}

void    PrinterImpl::visit(SpecResponseChain12*      spec)
{
    os  <<  "if " << make(spec->P) << " has occurred, then in response " << make(spec->tPS) << " " << make(spec->cPS) << " " << make(spec->S) << " followed by " << make(spec->T) << " " << make(spec->tST) << " " << make(spec->cST) << " eventually holds";
}

void    PrinterImpl::visit(SpecResponseChain21*      spec)
{
    os  <<  "if " << make(spec->S) << " followed by " << make(spec->T) << " " << make(spec->tST) << " " << make(spec->cST) << " have occurred, then in response " << make(spec->P) << " eventually holds " << make(spec->tTP) << " " << make(spec->cTP) << "";
}

void    PrinterImpl::visit(SpecResponseInvariance*   spec)
{
    os  <<  "if " << make(spec->P) << " has occurred, then in response " << make(spec->S) << " holds continually " << make(spec->tPS);
}

void    PrinterImpl::visit(SpecUntil*                spec)
{
    os  <<  make(spec->P) << " holds without interruption until " << make(spec->S) << " holds " << make(spec->tPS);
}

void    PrinterImpl::visit(SpecGlobally*            spec)
{
    os  << "globally, " << make(spec->spec);
}

void    PrinterImpl::visit(SpecBefore*              spec)
{
    os  << "before " << make(spec->arg) << ", " << make(spec->spec);
}

void    PrinterImpl::visit(SpecAfter*               spec)
{
    os  << "after " << make(spec->arg) << ", " << make(spec->spec);
}

void    PrinterImpl::visit(SpecWhile*               spec)
{
    os  << "while " << make(spec->lhs) << ", " << make(spec->spec);
}

void    PrinterImpl::visit(SpecBetweenAnd*          spec)
{
    os  << "between " << make(spec->lhs) << " and " << make(spec->rhs) << ", " << make(spec->spec);
}

void    PrinterImpl::visit(SpecAfterUntil*          spec)
{
    os  << "after " << make(spec->lhs) << " until " << make(spec->rhs) << ", " << make(spec->spec);
}

std::string PrinterImpl::make(  Base*   base)
{
    std::ostringstream  os;

    if(base != nullptr)
    {
        Printer::output(os, base);
    }

    return  os.str();
}

std::ostream&   Printer::output(std::ostream& os, Base* base)
{
    PrinterImpl impl(os);

    impl.output(base);

    return os;
}
