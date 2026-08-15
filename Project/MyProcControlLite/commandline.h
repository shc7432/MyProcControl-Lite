#pragma once
#include "targetver.h"
#include <string>
#include <array>

int RunCommandLineInterface(std::wstring name, std::wstring action, const std::array<std::string, 16>& u8extras);

