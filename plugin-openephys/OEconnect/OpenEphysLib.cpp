/*
 * OEconnect — Open Ephys GUI plugin entry point.
 *
 * Exports the two symbols the OE GUI PluginManager looks up (getLibInfo,
 * getPluginInfo) and registers OEconnectJuceProcessor as a SINK processor.
 * Mirrors the bundled-plugin convention in
 * external/plugin-GUI/Plugins/<Name>/OpenEphysLib.cpp.
 */

#include "Source/OEconnectJuceProcessor.h"
#include <PluginInfo.h>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

using namespace Plugin;
#define NUM_PLUGINS 1

extern "C" EXPORT void getLibInfo(Plugin::LibraryInfo* info)
{
    info->apiVersion = PLUGIN_API_VER;
    info->name = "OEconnect";
    info->libVersion = ProjectInfo::versionString;
    info->numPlugins = NUM_PLUGINS;
}

extern "C" EXPORT int getPluginInfo(int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
        case 0:
            info->type = Plugin::PROCESSOR;
            info->processor.name = "OEconnect";
            info->processor.type = Plugin::Processor::SINK;
            info->processor.creator =
                &(Plugin::createProcessor<oec::plugin::OEconnectJuceProcessor>);
            break;
        default:
            return -1;
    }
    return 0;
}

#ifdef _WIN32
BOOL WINAPI DllMain(IN HINSTANCE hDllHandle, IN DWORD nReason, IN LPVOID Reserved)
{
    (void)hDllHandle; (void)nReason; (void)Reserved;
    return TRUE;
}
#endif
