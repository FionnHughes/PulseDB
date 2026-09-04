#include "ToastNotifier.h"

#include <winrt/base.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <iostream>

using namespace winrt;
using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::Data::Xml::Dom;

namespace pulsedb {

    // swap this for your real registered aumid once the installer sets one up
    static const wchar_t* AUMID = L"PulseDB.Daemon";

    bool ToastNotifier::initialize() {
        try {
            winrt::init_apartment();
            return true;
        }
        catch (const winrt::hresult_error& e) {
            std::cerr << "toast init failed: " << winrt::to_string(e.message()) << "\n";
            return false;
        }
    }

    void ToastNotifier::show(const std::string& title, const std::string& body) {
        try {
            std::wstring wtitle(title.begin(), title.end());
            std::wstring wbody(body.begin(), body.end());

            // basic two line toast template, nothing fancy
            std::wstring xml =
                L"<toast><visual><binding template='ToastGeneric'>"
                L"<text>" + wtitle + L"</text>"
                L"<text>" + wbody + L"</text>"
                L"</binding></visual></toast>";

            XmlDocument doc;
            doc.LoadXml(xml);

            ToastNotification toast(doc);
            ToastNotificationManager::CreateToastNotifier(AUMID).Show(toast);
        }
        catch (const winrt::hresult_error& e) {
            // don't let a failed toast take down the alert engine, just log it
            std::cerr << "toast show failed: " << winrt::to_string(e.message()) << "\n";
        }
    }

}