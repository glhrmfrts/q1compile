#pragma once 

#include "config.h"

struct OpenConfigState;

namespace compile {

void StartHelpJob(config::CompileStepType);

void StartCompileJob(OpenConfigState* cfg, bool run_quake, bool ignore_diff = false, bool nocompile = false);

void EnqueueCompileJob(OpenConfigState* cfg, bool run_quake, bool ignore_diff = false, bool nocompile = false);

}