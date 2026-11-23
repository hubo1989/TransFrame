// AppNotifications.cpp — FR-50.7 notification seam (see header).
#include "pch.h"

#include "tf/AppNotifications.hpp"

#include <mutex>
#include <utility>

namespace tf::appnotify {
namespace {
std::mutex g_mutex;
Backend g_backend;
}  // namespace

void setBackend(Backend backend) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backend = std::move(backend);
}

void clearBackend() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backend = nullptr;
}

bool hasBackend() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<bool>(g_backend);
}

bool notify(const std::wstring& title, const std::wstring& body, Type type) {
    Backend backend;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        backend = g_backend;
    }
    if (!backend) return false;
    return backend(title, body, type);
}

}  // namespace tf::appnotify
