#pragma once 

#include "config.h"

struct OpenConfigState;

namespace compile {

enum CompileFlags : unsigned int {
    CF_NONE = 0,
    CF_RUN_QUAKE = 1 << 0,
    CF_IGNORE_DIFF = 1 << 1,
    CF_NO_COMPILE = 1 << 2,
};

void StartHelpJob(config::CompileStepType);

void StartCompileJob(OpenConfigState* cfg, CompileFlags);

void EnqueueCompileJob(OpenConfigState* cfg, CompileFlags);

}