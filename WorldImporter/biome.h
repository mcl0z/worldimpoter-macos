#pragma once
#include <string>
#include <unordered_map>
#include <map>
#include <mutex>
#include <shared_mutex>
#include "include/json.hpp"

enum class BiomeColorType {
    Foliage,
    DryFoliage,
    Grass,
    Fog,
    Sky,
    Water,
    WaterFog
};

struct BiomeColors {
    // 直接颜色值
    int foliage = -1;
    int dryFoliage = -1;
    int grass = -1;
    int fog = 0;
    int sky = 0;
    int water = 0;
    int waterFog = 0;

    // 色图计算参数
    float adjTemperature = 0.0f;
    float adjDownfall = 0.0f;
};

namespace BiomeUtils {
    template<typename T>
    constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
        return (v < lo) ? lo : (hi < v) ? hi : v;
    }
}

struct BiomeInfo {
    explicit BiomeInfo(int id, BiomeColors&& initialColors)
        : id(id), colors(std::move(initialColors)) {
    }

    int id;
    std::string namespaceName;
    std::string biomeName;
    BiomeColors colors;
    mutable std::mutex colorMutex;
};

    // 全局生物群系地图数据
    static std::vector<std::vector<int>> g_biomeMap;
    extern int g_biomeMapMinX;
    extern int g_biomeMapMinZ;

class Biome {
public:
    // 初始化生物群系地图尺寸
    static void InitializeBiomeMap(int minX, int minZ, int maxX, int maxZ);
public:
    // 获取或注册群系ID(线程安全)
    static int GetId(const std::string& fullName);

    static int GetColor(int biomeId, BiomeColorType colorType);

    static int GetBiomeColor(int blockX, int blockY, int blockZ, BiomeColorType colorType);

    static void GenerateBiomeMap(int minX, int minZ, int maxX, int maxZ);

    static bool ExportToPNG(const std::string& filename, BiomeColorType colorType);

    static nlohmann::json GetBiomeJson(const std::string& namespaceName, const std::string& biomeId);

    static std::string GetColormapData(const std::string& namespaceName, const std::string& colormapName);



private:
    static std::unordered_map<std::string, BiomeInfo> biomeRegistry;
    static std::shared_mutex registryMutex; // 改用读写锁
    static int CalculateColorFromColormap(const std::string& filePath,float temperature,float downfall);
    static BiomeColors ParseBiomeColors(const nlohmann::json& biomeJson);


    // 禁止实例化
    Biome() = delete;
};

// 通过坐标获取生物群系ID
int GetBiomeId(int blockX, int blockY, int blockZ);