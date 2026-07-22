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

grammar referee;

//  EOF is required: without it the rule happily matches a prefix and leaves
//  the rest of the file unconsumed, so trailing garbage parsed clean.
program     : (statement ';')* EOF
            ;

//  A requirement may be given a stable name, so a corpus of traces can say
//  which requirement each one is meant to violate without referring to a line
//  number that moves whenever the file is edited.
//
//  Spelled with a bare '@' rather than a keyword: reserving a word like `id`
//  would take it away as a signal name. No ambiguity with the freeze operator,
//  which needs an identifier to its left and so cannot begin a statement.
statement   : reqName? (declaraion | expression | specPattern)
            ;

reqName     : '@' ID
            | '@' string
            ;

declaraion  : declType
            | declData
            | declConf
            | declImport
            | declFunc
            ;

declImport  : 'import' string
            ;

//  An external function, implemented in C and resolved at run time against
//  the objects found on the -L search path. The C symbol carries a `referee_`
//  prefix, so `func crc8` binds to `referee_crc8`.
declFunc    : 'func' funcID ':' '(' funcArgs ')' '->' type
            ;

funcArgs    : (type (',' type)*)?
            ;

quant       : all                                   # QuantAll
            | some                                  # QuantSome
            | none                                  # QuantNone
            | one                                   # QuantOne
            | at least integer                      # QuantAtLeast
            | at most  integer                      # QuantAtMost
            ;

sign        : '+'
            | '-'
            ;

BOOLEAN     : 'true'
            | 'false'
            ;
boolean     : BOOLEAN;

BININT      : '0'[bB][0-1]+;
OCTINT      : '0'[oO][0-7]+;
HEXINT      : '0'[xX][0-9a-fA-F]+;

INTEGER     : [1-9][0-9]* | '0';
integer     : INTEGER
            | BININT
            | OCTINT
            | HEXINT;

FLOATING    : [1-9][0-9]*'.'[0-9]*
            | '0'?'.'[0-9]+
            ;
EXPONENT    : [eE][+-]?[1-9][0-9]*;
floating    : FLOATING EXPONENT?
            | INTEGER  EXPONENT
            ;

//  '/', '-' and ' ' are here so that `import "sub dir/my-types.ref"` lexes;
//  they are equally legal inside an ordinary string literal.
STRING      : '"' [a-zA-Z_0-9?!./\- ]* '"' ;
string      : STRING;

ID          : [a-zA-Z_][a-zA-Z0-9_]*
            ;

WHITESPACE  : [ \t\n]+ -> skip
            ;

COMMENT     : '/*' .*? '*/' -> skip
            ;

LINE_COMMENT: ('//'|'#') ~[\r\n]* -> skip
            ;

time        : '[' expression ':' expression ']'                 # TimeFull
            | '['            ':' expression ']'                 # TimeUpper
            | '[' expression ':'         ']'                    # TimeLower
            ;

expression  : sign? integer                                     # ExprConst
            | sign? floating                                    # ExprConst
            | string                                            # ExprConst
            | boolean                                           # ExprConst

            | funcID '(' (expression (',' expression)*)? ')'   # ExprCall
            | dataID                                            # ExprData
            | expression '.' mmbrID                             # ExprMmbr
            | expression '[' expression ':' expression ']'      # ExprSlice
            | expression '[' expression ']'                     # ExprIndx

            | '!' expression                                    # ExprNot
            | '-' expression                                    # ExprNeg
            | '~' expression                                    # ExprBnot

            //  Precedence is the order of these alternatives, tightest first.
            //  It follows C++ / Kotlin: multiplicative, additive, relational,
            //  equality, ^, &&, ||.  Implication and biconditional have no C++
            //  equivalent and sit where logic conventionally puts them, looser
            //  than || and tighter than the conditional.
            | expression '*'   expression                       # ExprMul
            | expression '/'   expression                       # ExprDiv
            | expression '%'   expression                       # ExprMod

            | expression '+'   expression                       # ExprAdd
            | expression '-'   expression                       # ExprSub

            | expression '<<'  expression                       # ExprShl
            | expression '>>'  expression                       # ExprShr

            | expression '<'   expression                       # ExprLt
            | expression '<='  expression                       # ExprLe
            | expression '>'   expression                       # ExprGt
            | expression '>='  expression                       # ExprGe

            | expression '=='  expression                       # ExprEq
            | expression '!='  expression                       # ExprNe

            | expression '&'   expression                       # ExprBand
            | expression '^'   expression                       # ExprXor
            | expression '|'   expression                       # ExprBor

            | expression '&&'  expression                       # ExprAnd
            | expression '||'  expression                       # ExprOr

            | expression '=>'  expression                       # ExprImp
            | expression '<=>' expression                       # ExprEqu

            | expression '?' expression ':' expression          # ExprTer

            //  Bounded quantifiers over an array's elements. The body extends
            //  maximally -- to the enclosing ')' or the terminating ';' -- so
            //  these sit looser than every operator above.
            | quant ID (',' ID)? in expression ':' expression   # ExprQuant

            | 'G'  time? '(' expression ')'                     # ExprG
            | 'F'  time? '(' expression ')'                     # ExprF
            | 'Xs'       '(' (expression ',')? expression ')'   # ExprXs
            | 'Xw'       '(' (expression ',')? expression ')'   # ExprXw
            | 'Us' time? '(' expression ',' expression ')'      # ExprUs
            | 'Uw' time? '(' expression ',' expression ')'      # ExprUw
            | 'Rs' time? '(' expression ',' expression ')'      # ExprRs
            | 'Rw' time? '(' expression ',' expression ')'      # ExprRw

            | 'H'  time? '(' expression ')'                     # ExprH
            | 'O'  time? '(' expression ')'                     # ExprO
            | 'Ys'       '(' (expression ',')? expression ')'   # ExprYs
            | 'Yw'       '(' (expression ',')? expression ')'   # ExprYw
            | 'Ss' time? '(' expression ',' expression ')'      # ExprSs
            | 'Sw' time? '(' expression ',' expression ')'      # ExprSw
            | 'Ts' time? '(' expression ',' expression ')'      # ExprTs
            | 'Tw' time? '(' expression ',' expression ')'      # ExprTw

            //  The three accumulators, named as a family: Int integrates a
            //  value over time, Sum totals it over records, Cnt counts them.
            | 'Itg' time? '(' expression ',' expression ')'     # ExprInt
            | 'Itg' time? '(' expression ')'                    # ExprInt

            //  Discrete counterparts, shaped like Itg: a condition selects
            //  the states, and states where it fails are skipped. Cnt(p) is
            //  Sum(p, 1).
            | 'Sum' time? '(' expression ',' expression ')'     # ExprSum
            | 'Cnt' time? '(' expression ')'                    # ExprCnt

            | '(' expression ')'                                # ExprParen

            |  ID '@' expression                                # ExprAt
            ;

specPattern : psbody                                                # SpecBody
            | globally                            ',' specPattern   # SpecGlobally
            | before  expression                  ',' specPattern   # SpecBefore
            | after   expression                  ',' specPattern   # SpecAfter
            | while expression                    ',' specPattern   # SpecWhile
            | between expression and expression   ',' specPattern   # SpecBetweenAnd
            | after   expression until expression ',' specPattern   # SpecAfterUntil
            ;

after           : 'after'           ;
afterwards      : 'afterwards'      ;
always          : 'always'          ;
and             : 'and'             ;
at              : 'at'              ;
becomes         : 'becomes'         ;
been            : 'been'            ;
before          : 'before'          ;
between         : 'between'         ;
by              : 'by'              ;
case            : 'case'            ;
continually     : 'continually'     ;
eventually      : 'eventually'      ;
every           : 'every'           ;
followed        : 'followed'        ;
for             : 'for'             ;
globally        : 'globally'        ;
has             : 'has'             ;
have            : 'have'            ;
holding         : 'holding'         ;
holds           : 'holds'           ;
if              : 'if'              ;
in              : 'in'              ;
interruption    : 'interruption'    ;
is              : 'is'              ;
it              : 'it'              ;
least           : 'least'           ;
less            : 'less'            ;
long            : 'long'            ;
microseconds    : 'microseconds'    ;
milliseconds    : 'milliseconds'    ;
minutes         : 'minutes'         ;
must            : 'must'            ;
nanoseconds     : 'nanoseconds'     ;
never           : 'never'           ;
all             : 'all'             ;
some            : 'some'            ;
none            : 'none'            ;
one             : 'one'             ;
most            : 'most'            ;
occured         : 'occured'         ;
occurred        : 'occurred'        ;
once            : 'once'            ;
remains         : 'remains'         ;
repeatedly      : 'repeatedly'      ;
response        : 'response'        ;
run             : 'run'             ;
satisfied       : 'satisfied'       ;
seconds         : 'seconds'         ;
so              : 'so'              ;
than            : 'than'            ;
that            : 'that'            ;
the             : 'the'             ;
then            : 'then'            ;
until           : 'until'           ;
while           : 'while'           ;
within          : 'within'          ;
without         : 'without'         ;

number          : integer
                | floating
                ;

exprP           : expression ;
exprS           : expression ;
exprT           : expression ;
exprZ           : expression ;
exprN           : expression ;


specUniversality        : it is always the case that exprP holds? timeBound
                        ;
specAbsence             : it is never the case that  exprP holds? timeBound
                        ;
specExistence           : exprP eventually holds? timeBound
                        ;
specTransientState      : exprP holds after exprN units
                        ;
specSteadyState         : exprP holds in the long run
                        ;
specMinimunDuration     : once exprP (becomes satisfied)? it remains so for at least exprN units
                        ;
specMaximumDuration     : once exprP (becomes satisfied)? it remains so for less than exprN units
                        ;
specRecurrence          : exprP holds? repeatedly (every exprN units)?
                        ;
specPrecedence          : if exprP holds? ',' then it must have been the case that exprS (has occurred)? intervalBound before it?
                        ;
specPrecedenceChain12   : if exprS and afterwards exprT upperTimeBound holds? ',' then it must have been the case that exprP (has occurred)? intervalBound before it?
                        ;
specPrecedenceChain21   : if exprP holds? ',' then it must have been the case that exprS and afterwards exprT upperTimeBound (have occurred)? intervalBound before it?
                        ;
specResponse            : if exprP (has occurred)? ',' then in response exprS (eventually holds)? timeBound constraint
                        ;
specResponseChain12     : if exprP (has occurred)? ',' then in response timeBound constraint exprS followed by exprT timeBound constraint (eventually holds)?
                        ;
specResponseChain21     : if exprS followed by exprT timeBound constraint (have occurred)? ',' then in response exprP (eventually holds)? timeBound constraint
                        ;
specResponseInvariance  : if exprP (has occurred)? ',' then in response exprS holds? continually timeBound
                        ;
specUntil               : exprP holds? without interruption until exprS holds? timeBound
                        ;

psbody      : specUniversality
            | specAbsence
            | specExistence
            | specTransientState
            | specSteadyState
            | specMinimunDuration
            | specMaximumDuration
            | specRecurrence
            | specPrecedence
            | specPrecedenceChain12
            | specPrecedenceChain21
            | specResponse
            | specResponseChain12
            | specResponseChain21
            | specResponseInvariance
            | specUntil
            ;

constraint  : without exprZ holding in between
            | /* no constraint */
            ;

timeBound   : upperTimeBound
            | lowerTimeBound
            | intervalBound
            ;

upperTimeBound
            : within exprN units
            | noTimeBound
            ;

lowerTimeBound
            : after  exprN units
            ;

intervalBound
            : between exprN and exprN units
            | noTimeBound
            ;

noTimeBound : /* no time bound */
            ;

units       : nanoseconds
            | microseconds
            | milliseconds
            | seconds
            | minutes
            ;

mmbrID      : ID
            ;

typeID      : ID
            ;

//  `::` separates namespace from name. It is a lexical convention rather than
//  a scoping construct -- `math::sqrt` is a function whose name contains a
//  `::` -- so there are no resolution rules to learn. `.` could not be used:
//  it is member access, and the head of a dotted name resolves as a signal.
funcID      : ID ('::' ID)*
            ;

dataID      : ID
            ;

confID      : ID
            ;

itemList    : (ID (',' ID)*)?
            ;

mmbrList    : (ID ':' type ';')*
            ;

index       : integer
            ;

//  An empty size means the extent is not written in the specification and is
//  taken from the trace instead. It is fixed for a whole run, so nothing
//  downstream ever sees an unsized type.
size        : integer
            |
            ;

type        : 'struct' '{' mmbrList '}'                         # TypeStruct
            | 'enum'   '{' itemList '}'                         # TypeEnum
            | typeID                                            # TypeAlias
            | type     '[' size     ']'                         # TypeArray
            ;

declType    : 'type' typeID ':' type
            ;

declData    : 'data' dataID ':' type        # DeclDataTyped
            | 'data' dataID '=' expression  # DeclDataExpr
            ;

declConf    : 'conf' confID ':' type
            ;
