#pragma once

#include <string>
#include <windows.h>

namespace pulsedb {
	// converts wide string to UTF-8 which is needed because Windows APIs return wide strings but metric names are narrow
	std::string to_utf8(const wchar_t* w_string, int size);
}