#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <mutex>
#include "include/json.hpp"
#include "config.h"
#include "JarReader.h"

// 前向声明依赖类型
class JarReader;

// 外部声明
extern std::unordered_set<std::string> solidBlocks;
extern std::unordered_set<std::string> fluidBlocks;

// ========= 全局缓存声明 =========
namespace GlobalCache {
	// 纹理缓存 [namespace:resource_path -> PNG数据]
	extern std::unordered_map<std::string, std::vector<unsigned char>> textures;

	//动态材质缓存 [namespace:resource_path -> JSON]
	extern std::unordered_map<std::string, nlohmann::json> mcmetaCache;

	// 方块状态缓存 [namespace:block_id -> JSON]
	extern std::unordered_map<std::string, nlohmann::json> blockstates;

	// 模型缓存 [namespace:model_path -> JSON]
	extern std::unordered_map<std::string, nlohmann::json> models;

	// 生物群系缓存 [namespace:biome_id -> JSON]
	extern std::unordered_map<std::string, nlohmann::json> biomes;

	// 色图缓存 [namespace:colormap_name -> PNG数据]
	extern std::unordered_map<std::string, std::vector<unsigned char>> colormaps;

	// 同步原语
	extern std::once_flag initFlag;
	extern std::mutex cacheMutex;
	extern std::vector<std::string> jarOrder;
}



// ========= 初始化方法 =========
void InitializeAllCaches();

