#ifdef _WIN32

#include <windows.h>
#include <cstring>
#include "GameApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp> // Needed for complete type in Application
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux/macOS/iOS

#include "GameApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
// iOS requires the app entry point to go through SDL3's own UIKit shim
// rather than a bare main() -- see apps/tools/MetalSmokeTest/main.cpp for
// the same requirement on the Metal smoke test.
#include <SDL3/SDL_main.h>
#endif
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    GameApplication app;
    return app.Run(argc, argv);
}

#endif
