#ifndef IR_REPRESENTATION_H
#define IR_REPRESENTATION_H

#include "lir.h"
#include <stdbool.h>

char *lir_to_string(LIR *lir, const bool explicit_, const bool show_lib_externs);

#endif
