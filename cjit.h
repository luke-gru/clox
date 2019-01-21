#ifndef clox_cjit_h
#define clox_cjit_h

#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "nodes.h"

#ifdef __cplusplus
extern "C" {
#endif

FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode);
int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode);

#ifdef __cplusplus
}
#endif

#endif
