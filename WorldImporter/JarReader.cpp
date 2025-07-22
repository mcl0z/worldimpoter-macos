#ifdef _WIN32
// 声明需要的Windows API函数
extern "C" {
    __declspec(dllimport) unsigned long __stdcall GetLastError();
}
#define MAX_PATH 260
#endif

#include "JarReader.h"
#include <iostream>
#include <zip.h>
#include <vector>
#include <sstream>
#include <cstring>
#include "include/json.hpp"
#include "fileutils.h"
namespace {
    std::vector<std::string> splitPath(const std::string& path) {
        std::vector<std::string> parts;
        std::stringstream ss(path);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }
        return parts;
    }
}

//将换行符替换为 JSON 能够识别的转义字符
std::string preprocessJson(const std::string& jsonStr) {
    std::string result;
    bool inString = false;
    for (char c : jsonStr) {
        if (c == '\"') {
            inString = !inString;
            result.push_back(c);
        }
        else if (inString && (c == '\n' || c == '\r')) {
            // 将换行符替换为 JSON 能够识别的转义字符
            result.push_back('\\');
            if (c == '\n') {
                result.push_back('n');
            }
            else if (c == '\r') {
                result.push_back('r');
            }
        }
        else {
            result.push_back(c);
        }
    }
    return result;
}

JarReader::JarReader(const std::wstring& jarFilePath)
    : jarFilePath(jarFilePath), zipFile(nullptr), modType(ModType::Unknown), modNamespace("") {
    // 在构造函数中只初始化成员变量,不打开文件
    // 文件打开操作移至open()方法中
}

JarReader::~JarReader() {
    // 关闭 .jar 文件
    if (zipFile) {
        zip_close(zipFile);
    }
}

std::string JarReader::getNamespaceForModType(ModType type) {
    switch (type) {
    case ModType::Vanilla:
        return "minecraft";
    case ModType::Mod: {
        std::string forgeModId = getForgeModId();
        if (!forgeModId.empty()) {
            return forgeModId;
        }
        return "";
    }
    default:
        return "";
    }
}

std::string JarReader::getFileContent(const std::string& filePathInJar) {
    if (!zipFile) {
        std::cerr << "Error: Attempt to read from unopened jar file: " << wstring_to_string(jarFilePath) << std::endl;
        return "";
    }

    // 查找文件在 .jar 文件中的索引
    zip_file_t* fileInJar = zip_fopen(zipFile, filePathInJar.c_str(), 0);
    if (!fileInJar) {
        // 静默失败,但记录日志
        // std::cerr << "Could not find file in jar: " << filePathInJar << std::endl;
        return "";
    }

    // 获取文件的大小
    zip_stat_t fileStat;
    if (zip_stat(zipFile, filePathInJar.c_str(), 0, &fileStat) != 0) {
        std::cerr << "Failed to get file stats: " << filePathInJar << std::endl;
        zip_fclose(fileInJar);
        return "";
    }

    // 读取文件内容
    std::string fileContent;
    try {
        fileContent.resize(fileStat.size);
        zip_fread(fileInJar, &fileContent[0], fileStat.size);
    } catch (const std::exception& e) {
        std::cerr << "Error reading file content: " << e.what() << std::endl;
        fileContent.clear();
    }

    // 关闭文件
    zip_fclose(fileInJar);

    return fileContent;
}

std::vector<unsigned char> JarReader::getBinaryFileContent(const std::string& filePathInJar) {
    std::vector<unsigned char> fileContent;

    if (!zipFile) {
        std::cerr << "Error: Attempt to read binary from unopened jar file: " << wstring_to_string(jarFilePath) << std::endl;
        return fileContent;
    }

    // 查找文件在 .jar 文件中的索引
    zip_file_t* fileInJar = zip_fopen(zipFile, filePathInJar.c_str(), 0);
    if (!fileInJar) {
        // 静默失败,但可以根据需要添加日志
        return fileContent;
    }

    // 获取文件的大小
    zip_stat_t fileStat;
    if (zip_stat(zipFile, filePathInJar.c_str(), 0, &fileStat) != 0) {
        std::cerr << "Failed to get binary file stats: " << filePathInJar << std::endl;
        zip_fclose(fileInJar);
        return fileContent;
    }

    // 读取文件内容
    try {
        fileContent.resize(fileStat.size);
        if (zip_fread(fileInJar, fileContent.data(), fileStat.size) != static_cast<zip_int64_t>(fileStat.size)) {
            std::cerr << "Failed to read complete binary file: " << filePathInJar << std::endl;
            fileContent.clear();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading binary file content: " << e.what() << std::endl;
        fileContent.clear();
    }

    // 关闭文件
    zip_fclose(fileInJar);

    return fileContent;
}

void JarReader::cacheAllResources(
    std::unordered_map<std::string, std::vector<unsigned char>>& textureCache,
    std::unordered_map<std::string, nlohmann::json>& blockstateCache,
    std::unordered_map<std::string, nlohmann::json>& modelCache,
    std::unordered_map<std::string, nlohmann::json>& mcmetaCache,
    std::unordered_map<std::string, nlohmann::json>& biomeCache,
    std::unordered_map<std::string, std::vector<unsigned char>>& colormapCache)
{
    if (!zipFile) {
        std::cerr << "Zip file is not open." << std::endl;
        return;
    }


    zip_int64_t numEntries = zip_get_num_entries(zipFile, 0);
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* name = zip_get_name(zipFile, i, 0);
        if (!name) continue;

        std::string filePath(name);

        // 处理 assets/ 目录下的资源
        if (filePath.find("assets/") == 0) {
            size_t nsStart = 7; // "assets/" 长度
            size_t nsEnd = filePath.find('/', nsStart);
            if (nsEnd == std::string::npos) continue;
            std::string namespaceName = filePath.substr(nsStart, nsEnd - nsStart);

            // 处理纹理
            if (filePath.find("/textures/") != std::string::npos &&
                filePath.size() > 4 &&
                filePath.substr(filePath.size() - 4) == ".png")
            {
                // 检查是否为 colormap
                if (filePath.find("/textures/colormap/") != std::string::npos) {
                     auto parts = splitPath(filePath);
                     //验证路径结构: assets/<namespace>/textures/colormap/<name>.png
                     if (parts.size() == 5 && parts[0] == "assets" && parts[2] == "textures" && parts[3] == "colormap") {
                         std::string mapName = parts[4].substr(0, parts[4].size() - 4); // 移除 .png 后缀
                         std::string cacheKey = namespaceName + ":" + mapName;

                         // 防止重复处理
                         if (colormapCache.find(cacheKey) == colormapCache.end()) {
                             // 读取二进制数据
                             auto data = getBinaryFileContent(filePath);
                             if (!data.empty()) {
                                 colormapCache.emplace(cacheKey, std::move(data));
                             }
                         }
                     }
                } else {
                    size_t resStart = filePath.find("/textures/", nsEnd) + 10;
                    std::string resourcePath = filePath.substr(resStart, filePath.size() - resStart - 4);
                    std::string cacheKey = namespaceName + ":" + resourcePath;

                    if (textureCache.find(cacheKey) == textureCache.end()) {
                        zip_file_t* file = zip_fopen_index(zipFile, i, 0);
                        if (file) {
                            zip_stat_t fileStat;
                            if (zip_stat_index(zipFile, i, 0, &fileStat) == 0) {
                                std::vector<unsigned char> data(fileStat.size);
                                if (zip_fread(file, data.data(), fileStat.size) == fileStat.size) {
                                    textureCache.emplace(cacheKey, std::move(data));
                                }
                            }
                            zip_fclose(file);
                        }
                    }
                }
            }
            // 处理 blockstate
            else if (filePath.find("/blockstates/") != std::string::npos &&
                filePath.size() > 5 &&
                filePath.substr(filePath.size() - 5) == ".json")
            {
                size_t resStart = filePath.find("/blockstates/", nsEnd) + 13;
                std::string resourcePath = filePath.substr(resStart, filePath.size() - resStart - 5);
                std::string cacheKey = namespaceName + ":" + resourcePath;

                if (blockstateCache.find(cacheKey) == blockstateCache.end()) {
                    std::string content = this->getFileContent(filePath);
                    if (!content.empty()) {
                        try {
                            blockstateCache.emplace(cacheKey, nlohmann::json::parse(content));
                        } catch (const std::exception& e) {
                            std::cerr << "Blockstate JSON Error: " << filePath << " - " << e.what() << std::endl;
                        }
                    }
                }
            }
            // 处理模型
            else if (filePath.find("/models/") != std::string::npos &&
                filePath.size() > 5 &&
                filePath.substr(filePath.size() - 5) == ".json")
            {
                size_t resStart = filePath.find("/models/", nsEnd) + 8;

                std::string modelPath = filePath.substr(resStart, filePath.size() - resStart - 5);
                std::string cacheKey = namespaceName + ":" + modelPath;

                if (modelCache.find(cacheKey) == modelCache.end()) {
                    std::string content = this->getFileContent(filePath);
                    if (!content.empty()) {
                        try {
                            modelCache.emplace(cacheKey, nlohmann::json::parse(content));
                        } catch (const std::exception& e) {
                            std::cerr << "Model JSON Error: " << filePath << " - " << e.what() << std::endl;
                        }
                    }
                }
            }
            // 处理 .mcmeta 文件(新增)
            else if (filePath.find("/textures/") != std::string::npos &&
                filePath.size() > 7 &&
                filePath.substr(filePath.size() - 7) == ".mcmeta")
            {
                size_t metaStart = filePath.find("/textures/", nsEnd) + 10;
                std::string metaPathWithExtension = filePath.substr(metaStart, filePath.size() - metaStart - 7);

                // 去掉 .png 后缀
                size_t pngPos = metaPathWithExtension.find(".png");
                if (pngPos != std::string::npos) {
                    std::string metaPath = metaPathWithExtension.substr(0, pngPos);
                    std::string cacheKey = namespaceName + ":" + metaPath;

                    if (mcmetaCache.find(cacheKey) == mcmetaCache.end()) {
                        std::string content = this->getFileContent(filePath);
                        if (!content.empty()) {
                            try {
                                mcmetaCache.emplace(cacheKey, nlohmann::json::parse(content));
                            } catch (const std::exception& e) {
                                std::cerr << ".mcmeta JSON Error: " << filePath << " - " << e.what() << std::endl;
                            }
                        }
                    }
                }
                else {
                    std::string metaPath = metaPathWithExtension;
                    std::string cacheKey = namespaceName + ":" + metaPath;

                    if (mcmetaCache.find(cacheKey) == mcmetaCache.end()) {
                        std::string content = this->getFileContent(filePath);
                        if (!content.empty()) {
                            try {
                                mcmetaCache.emplace(cacheKey, nlohmann::json::parse(content));
                            } catch (const std::exception& e) {
                                std::cerr << ".mcmeta JSON Error: " << filePath << " - " << e.what() << std::endl;
                            }
                        }
                    }
                }
            }
        }
        // 处理 data/ 目录下的资源 (例如 biomes)
        else if (filePath.find("data/") == 0) {
            auto parts = splitPath(filePath);
            // 验证路径结构:data/<namespace>/worldgen/biome/[...]/<name>.json
            if (parts.size() >= 5 && parts[0] == "data" && parts[2] == "worldgen" && parts[3] == "biome") {
                // 提取命名空间
                std::string namespaceName = parts[1];

                // 提取子路径并构建 biomeId (例如:cave/andesite_caves)
                std::vector<std::string> biomeParts;
                for (auto it = parts.begin() + 4; it != parts.end(); ++it) {
                    if (it == parts.end() - 1) { // 处理文件名
                        if ((*it).size() < 5 || (*it).substr((*it).size() - 5) != ".json")
                            break;
                        biomeParts.push_back((*it).substr(0, (*it).size() - 5));
                    }
                    else {
                        biomeParts.push_back(*it);
                    }
                }
                if (biomeParts.empty()) continue;

                std::string biomeId;
                for (size_t idx = 0; idx < biomeParts.size(); ++idx) {
                    if (idx != 0) biomeId += "/";
                    biomeId += biomeParts[idx];
                }

                std::string cacheKey = namespaceName + ":" + biomeId;

                if (biomeCache.find(cacheKey) == biomeCache.end()) {
                    // 读取并解析 JSON
                    std::string content = getFileContent(filePath);
                    if (!content.empty()) {
                        try {
                            biomeCache.emplace(cacheKey, nlohmann::json::parse(content));
                        } catch (...) {
                            std::cerr << "Invalid biome JSON: " << filePath << std::endl;
                        }
                    }
                }
            }
        }
    }
}

std::vector<std::string> JarReader::getFilesInSubDirectory(const std::string& subDir) {
    std::vector<std::string> filesInSubDir;

    if (!zipFile) {
        std::cerr << "Zip file is not open." << std::endl;
        return filesInSubDir;
    }

    // 获取文件条目数量
    int numFiles = zip_get_num_entries(zipFile, 0);
    for (int i = 0; i < numFiles; ++i) {
        const char* fileName = zip_get_name(zipFile, i, 0);
        if (fileName && std::string(fileName).find(subDir) == 0) {
            filesInSubDir.push_back(fileName);
        }
    }

    return filesInSubDir;
}

bool JarReader::isVanilla() {
    return !getFileContent("version.json").empty();
}

bool JarReader::isFabric() {
    return !getFileContent("fabric.mod.json").empty();
}

bool JarReader::isForge() {
    return !getFileContent("META-INF/mods.toml").empty();
}

bool JarReader::isNeoForge() {
    return !getFileContent("META-INF/neoforge.mods.toml").empty();
}

std::string JarReader::getID() {
    if (!zipFile) {
        std::cerr << "Error: Attempt to get ID from unopened jar file: " << wstring_to_string(jarFilePath) << std::endl;
        return "";
    }

    // 确保 modType 已经被正确初始化
    // open() 方法中已经进行了类型判断和 modType 的设置

    switch (modType) {
    case ModType::Vanilla:
        return getVanillaVersionId();
    case ModType::Mod: {
        // 优化 Mod 类型判断逻辑
        // 优先检查 fabric.mod.json，因为它最独特
        std::string fabricContent = getFileContent("fabric.mod.json");
        if (!fabricContent.empty()) {
            return getFabricModId(); // 实际上这里可以直接解析 fabricContent，避免再次调用 getFabricModId() 里的 getFileContent
        }

        // 然后检查 META-INF/neoforge.mods.toml，因为它比 mods.toml 更具体
        std::string neoforgeContent = getFileContent("META-INF/neoforge.mods.toml");
        if (!neoforgeContent.empty()) {
            return getNeoForgeModId(); // 同上，可以优化
        }

        // 最后检查 META-INF/mods.toml
        std::string forgeContent = getFileContent("META-INF/mods.toml");
        if (!forgeContent.empty()) {
            return getForgeModId(); // 同上，可以优化
        }
        return ""; // 未知 Mod 类型
    }
    case ModType::Unknown:
    default:
        // 如果 open() 未能识别类型，则尝试在这里最后判断一次
        if (isVanilla()) {
            modType = ModType::Vanilla;
            return getVanillaVersionId();
        }
        // 顺序很重要，NeoForge 和 Forge 都可能有 mods.toml，但 NeoForge 的 neoforge.mods.toml 更特定
        if (isNeoForge()) {
            modType = ModType::Mod;
            return getNeoForgeModId();
        }
        if (isForge()) {
            modType = ModType::Mod;
            return getForgeModId();
        }
        if (isFabric()) {
            modType = ModType::Mod;
            return getFabricModId();
        }
        return ""; // 确实无法识别
    }
}


// 获取原版 Minecraft 版本 ID
std::string JarReader::getVanillaVersionId() {
    if (modType != ModType::Vanilla) {
        return "";
    }

    std::string versionJsonContent = getFileContent("version.json");
    versionJsonContent = preprocessJson(versionJsonContent);
    nlohmann::json json = nlohmann::json::parse(versionJsonContent);
    return json["id"].get<std::string>();
}

std::string JarReader::getFabricModId() {
    if (modType != ModType::Mod) {
        return "";
    }

    std::string modJsonContent = getFileContent("fabric.mod.json");
    modJsonContent = preprocessJson(modJsonContent);
    nlohmann::json json = nlohmann::json::parse(modJsonContent);
    return json["id"].get<std::string>();
}

std::string JarReader::getForgeModId() {
    if (modType != ModType::Mod) {
        return "";
    }

    std::string modsTomlContent = getFileContent("META-INF/mods.toml");
    modsTomlContent = preprocessJson(modsTomlContent);
    std::string modId = extractModId(modsTomlContent);
    return modId;
}

std::string JarReader::getNeoForgeModId() {
    if (modType != ModType::Mod) {
        return "";
    }

    std::string neoforgeTomlContent = getFileContent("META-INF/neoforge.mods.toml");
    neoforgeTomlContent = preprocessJson(neoforgeTomlContent);
    std::string modId = extractModId(neoforgeTomlContent);
    return modId;
}

std::string JarReader::extractModId(const std::string& content) {
    // 解析 .toml 文件,提取 modId
    std::string modId;

    // 清理 content,去除多余的空格和非打印字符
    std::string cleanedContent = cleanUpContent(content);
    size_t startPos = cleanedContent.find("modId=\"");
    if (startPos != std::string::npos) {
        // 从 "modId=\"" 后开始提取,跳过 7 个字符(modId=")
        size_t endPos = cleanedContent.find("\"", startPos + 7); // 查找结束的引号位置
        if (endPos != std::string::npos) {
            modId = cleanedContent.substr(startPos + 7, endPos - (startPos + 7)); // 提取 modId 字符串
        }
    }

    // 如果没有找到 modId,则返回空字符串
    return modId;
}

std::string JarReader::cleanUpContent(const std::string& content) {
    std::string cleaned;
    bool inQuotes = false;

    for (char c : content) {
        // 跳过空格,但保留换行符
        if (std::isspace(c) && c != '\n' && !inQuotes) {
            continue; // 跳过空格,除非在引号内
        }

        // 处理引号内的内容,保留其中的所有字符
        if (c == '\"') {
            inQuotes = !inQuotes; // 切换在引号内外
        }

        // 保留可打印字符
        if (std::isprint(c) || inQuotes) {
            cleaned.push_back(c);
        }
    }

    return cleaned;
}

bool JarReader::open() {
    if (zipFile) return true;
    int error;
    zipFile = zip_open(wstring_to_string(jarFilePath).c_str(), 0, &error);
    if (!zipFile) {
        std::cerr << "Failed to open .jar file: " << wstring_to_string(jarFilePath) << ", error code: " << error << std::endl;
        return false;
    }

    // 初始化mod类型和命名空间,这些操作从构造函数移到这里
    // 检查是否为原版
    if (isVanilla()) {
        modType = ModType::Vanilla;
    }
    // 检查是否为Fabric
    else if (isFabric()) {
        modType = ModType::Mod;
    }
    // 检查是否为Forge
    else if (isForge()) {
        modType = ModType::Mod;
    }
    // 检查是否为NeoForge
    else if (isNeoForge()) {
        modType = ModType::Mod;
    }

    // 获取命名空间
    modNamespace = getNamespaceForModType(modType);
    
    return true;
}

