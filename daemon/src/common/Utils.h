#pragma once

#include <string>
#include <windows.h>

namespace pulsedb {

	std::string to_utf8(const wchar_t* w_string, int size);
}