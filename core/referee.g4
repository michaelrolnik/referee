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

grammar referee;

program     : (statement ';')*
            ;

statement   : declaraion
            | expression
            | specPattern
            ;

declaraion  : declType
            | declData
            | declConf
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

STRING      : '"' [a-zA-Z_0-9?!.]* '"' ;
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

            | dataID                                            # ExprData
            | expression '.' mmbrID                             # ExprMmbr
            | expression '[' expression ']'                     # ExprIndx

            | '!' expression                                    # ExprNot
            | '-' expression                                    # ExprNeg

            | expression '+'   expression                       # ExprAdd
            | expression '-'   expression                       # ExprSub
            | expression '*'   expression                       # ExprMul
            | expression '/'   expression                       # ExprDiv
            | expression '%'   expression                       # ExprMod

            | expression '||'  expression                       # ExprOr
            | expression '&&'  expression                       # ExprAnd
            | expression '^'   expression                       # ExprXor
            | expression '=>'  expression                       # ExprImp
            | expression '<=>' expression                       # ExprEqu

            | expression '=='  expression                       # ExprEq
            | expression '!='  expression                       # ExprNe

            | expression '<'   expression                       # ExprLt
            | expression '<='  expression                       # ExprLe

            | expression '>'   expression                       # ExprGt
            | expression '>='  expression                       # ExprGe

            | expression '?' expression ':' expression          # ExprTer

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

            | 'I'  time? '(' expression ',' expression ')'      # ExprInt
            | 'I'  time? '(' expression ')'                     # ExprInt

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

size        : integer
            ;

type        : 'struct' '{' mmbrList '}'                         # TypeStruct
            | 'enum'   '{' itemList '}'                         # TypeEnum
            | typeID                                            # TypeAlias
            | type     '[' size     ']'                         # TypeArray
            ;

declType    : 'type' typeID ':' type
            ;

declData    : 'data' dataID ':' type
            ;

declConf    : 'conf' confID ':' type
            ;
