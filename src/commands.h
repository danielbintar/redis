#pragma once
#include <string>
#include <vector>

#include "store.h"

// Dispatch a parsed RESP command to the store and return a RESP response.
std::string handleCommand(Store& store, const std::vector<std::string>& args);
