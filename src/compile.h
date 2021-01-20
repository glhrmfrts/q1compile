#pragma once 

#include "config.h"

namespace compile {

void StartHelpJob(config::CompileStepType);

void StartCompileJob(const config::Config& cfg, bool run_quake, bool ignore_diff = false);

void EnqueueCompileJob(const config::Config& cfg, bool run_quake, bool ignore_diff = false);

}