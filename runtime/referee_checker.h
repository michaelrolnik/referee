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

/*
 *  The ABI an ahead-of-time compiled checker exposes. `referee build` emits an
 *  object defining a single symbol, `referee_module`, that returns the table
 *  below. A host links (or dlopens) the object and drives it -- no LLVM, no
 *  ANTLR, no .ref at check time.
 *
 *  This header is the contract between the emitted object and whatever runs it.
 *  It is plain C so a host in any language can bind to it.
 */
#ifndef REFEREE_CHECKER_H
#define REFEREE_CHECKER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  A state row and the configuration are opaque here: their layout is the one
 *  the compiled requirement functions were built against, described by the
 *  embedded schema and produced by the runtime's loader. A driver moves a
 *  `state` cursor by the schema's row stride; it does not read fields itself.
 */
typedef void referee_state;
typedef void referee_conf;

/*
 *  One requirement. `label` is the source position or @name, exactly what
 *  `referee execute` prints -- it rides as data because it is a poor ELF
 *  symbol. `eval` returns non-zero iff the trace satisfies the requirement,
 *  evaluated at the first real state; (frst, last) bracket the states and conf
 *  is the configuration blob.
 */
typedef struct referee_requirement_v1
{
    const char* label;
    int       (*eval)(const referee_state* frst,
                      const referee_state* last,
                      const referee_conf*  conf);
} referee_requirement_v1;

typedef struct referee_module_v1
{
    uint32_t                        version;        /* 1 */
    uint32_t                        count;          /* number of requirements */
    const referee_requirement_v1*   requirements;   /* count entries, in report order */
    void                          (*prepare)(referee_state*       frst,
                                             referee_state*       last,
                                             const referee_conf*  conf);
                                                    /* fills computed signals; call once first */
    const uint8_t*                  schema;         /* .rdb type encoding, or NULL */
    uint64_t                        schemaBytes;    /* 0 when schema is NULL */
} referee_module_v1;

/*  The one exported symbol. Everything else in the object is internal. */
const referee_module_v1* referee_module(void);

#ifdef __cplusplus
}
#endif

#endif  /* REFEREE_CHECKER_H */
