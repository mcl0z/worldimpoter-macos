#include "init.h"
#include "RegionModelExporter.h"
#include <thread>
#include <iostream>

#ifdef _WIN32
extern "C" {
    __declspec(dllimport) void* __stdcall GetCurrentProcess();
    __declspec(dllimport) int __stdcall SetPriorityClass(void* hProcess, unsigned int dwPriorityClass);
}
#ifndef REALTIME_PRIORITY_CLASS
#define REALTIME_PRIORITY_CLASS 0x00000100
#endif
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/resource.h>
#endif

void SetHighPriority() {
#ifdef _WIN32
    // Windows实现
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
    }
#elif defined(__linux__) || defined(__APPLE__)
    // Linux/MacOS实现
    if (setpriority(PRIO_PROCESS, 0, -20) != 0) {
        std::cerr << "警告：无法设置进程优先级。" << std::endl;
    }
#endif
}

void init() {
    std::cout << "[DEBUG] Starting init() function" << std::endl;

    // 尝试设置高优先级
    SetHighPriority();
    std::cout << "[DEBUG] High priority set" << std::endl;

    // 配置必须先加载
    SetGlobalLocale();
    std::cout << "[DEBUG] Global locale set" << std::endl;

    // 删除纹理文件夹
    DeleteTexturesFolder();
    std::cout << "[DEBUG] Textures folder deleted" << std::endl;

    std::cout << "[DEBUG] About to load config" << std::endl;
    config = LoadConfig("config_macos/config.json");
    std::cout << "[DEBUG] Config loaded successfully" << std::endl;

    // 配置加载完成后，再初始化缓存
    std::cout << "[DEBUG] About to initialize caches" << std::endl;
    InitializeAllCaches();
    std::cout << "[DEBUG] Caches initialized" << std::endl;

    std::cout << "[DEBUG] About to load solid blocks from: " << config.solidBlocksFile << std::endl;
    LoadSolidBlocks(config.solidBlocksFile);
    std::cout << "[DEBUG] Solid blocks loaded" << std::endl;

    std::cout << "[DEBUG] About to load fluid blocks from: " << config.fluidsFile << std::endl;
    LoadFluidBlocks(config.fluidsFile);
    std::cout << "[DEBUG] Fluid blocks loaded" << std::endl;

    RegisterFluidTextures();
    std::cout << "[DEBUG] Fluid textures registered" << std::endl;

    InitializeGlobalBlockPalette();
    std::cout << "[DEBUG] Global block palette initialized" << std::endl;
}