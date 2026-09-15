#pragma once
#include "peer_update.h"
#include "pair_session.h"
#include <cassert>
#include <cstdio>
#include <vector>
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
