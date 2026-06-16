#include "Utils.h"

namespace pulsedb {
	std::string to_utf8(const wchar_t* w_string, int char_count) {
		// first call with null output buffer to get the size needed
		int size = WideCharToMultiByte(CP_UTF8, 0, w_string, char_count, NULL, 0, NULL, NULL);
		if (!size) {
			return std::string();
		}
		std::string utf8(size, '\0');

		// second call does the actual conversion
		WideCharToMultiByte(CP_UTF8, 0, w_string, char_count, utf8.data(), size, NULL, NULL);

		return utf8;
	}
}
