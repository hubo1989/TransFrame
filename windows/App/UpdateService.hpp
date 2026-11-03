#pragma once
namespace tf {
// Check the installed App Installer feed off the UI thread. Unpackaged builds
// explain how to enable updates; Windows owns background update installation.
void checkForAppUpdate(void* owner);
}
