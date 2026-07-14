#pragma once

#include <stddef.h>

typedef enum {
    SIM_VAR_BOOL,
    SIM_VAR_INT,
    SIM_VAR_DINT,
    SIM_VAR_REAL,
    SIM_VAR_LREAL
} SimVarType;

typedef struct {
    const char *name;
    SimVarType type;
    void *value;
    int forced;
    double forced_number;
} SimVar;

extern SimVar sim_vars[];
extern const size_t sim_var_count;
