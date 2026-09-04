#pragma once

#include <string>

namespace pulsedb {

    // wraps the winrt toast stuff so alertengine doesn't need to know about com/winrt directly
    class ToastNotifier {
    public:
        // call once at startup, sets up the winrt apartment
        static bool initialize();

        static void show(const std::string& title, const std::string& body);
    };

}