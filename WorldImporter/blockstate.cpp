﻿#include "blockstate.h"
#include "fileutils.h"
#include "ObjExporter.h"
#include <regex>
#include <random>
#include <numeric>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <iostream>
#include <sstream>
#include <mutex>
#include <shared_mutex>

std::unordered_map<std::string, std::unordered_map<std::string, ModelData>> BlockModelCache;

std::unordered_map<std::string,std::unordered_map<std::string,std::vector<WeightedModelData>>> VariantModelCache;

std::unordered_map<std::string,std::unordered_map<std::string,std::vector<std::vector<WeightedModelData>>>> MultipartModelCache;

// 将互斥锁类型更改为 std::shared_mutex
std::shared_mutex blockstateCachesMutex;

// --------------------------------------------------------------------------------
// 条件匹配函数
// --------------------------------------------------------------------------------
bool matchConditions(const std::unordered_map<std::string, std::string>& blockConditions, const nlohmann::json& when) {
    if (when.is_object()) {
        // 处理空对象条件
        if (when.empty()) {
            return false; // 触发警告:No elements found in selector
        }

        // 检查是否为单一OR或AND条件
        if (when.size() == 1) {
            // 处理OR条件
            if (when.contains("OR")) {
                const auto& orCond = when["OR"];
                if (!orCond.is_array()) {
                    return false; // 触发警告:OR应为数组
                }
                for (const auto& cond : orCond) {
                    if (matchConditions(blockConditions, cond)) {
                        return true;
                    }
                }
                return false;
            }
            // 处理AND条件
            else if (when.contains("AND")) {
                const auto& andCond = when["AND"];
                if (!andCond.is_array()) {
                    return false; // 触发警告:AND应为数组
                }
                for (const auto& cond : andCond) {
                    if (!matchConditions(blockConditions, cond)) {
                        return false;
                    }
                }
                return true;
            }
        }

        // 处理普通多条件检查
        for (const auto& item : when.items()) {
            const std::string& prop = item.key();
            const auto& valueJson = item.value();

            // 检查值类型是否为字符串
            if (!valueJson.is_string()) {
                return false; // 触发异常警告
            }
            std::string valueStr = valueJson.get<std::string>();

            // 解析反转标记
            bool invert = false;
            if (!valueStr.empty() && valueStr[0] == '!') {
                invert = true;
                valueStr = valueStr.substr(1);
            }

            // 分割选项值
            std::vector<std::string> options;
            size_t pos;
            while ((pos = valueStr.find('|')) != std::string::npos) {
                options.push_back(valueStr.substr(0, pos));
                valueStr.erase(0, pos + 1);
            }
            options.push_back(valueStr);

            // 空选项检查
            if (options.empty() || (options.size() == 1 && options[0].empty())) {
                return false; // 触发警告:空属性值
            }

            // 检查方块属性是否存在
            auto blockIt = blockConditions.find(prop);
            if (blockIt == blockConditions.end()) {
                return false; // 触发警告:未知属性
            }

            // 判断属性值是否匹配
            bool valueMatched = std::find(options.begin(), options.end(), blockIt->second) != options.end();
            if (invert) {
                if (valueMatched) return false;
            }
            else {
                if (!valueMatched) return false;
            }
        }
        return true;
    }
    // 当条件不存在时默认匹配
    else if (when.is_null()) {
        return true;
    }
    // 非对象/null类型无效
    return false;
}

// 辅助函数:将键值对字符串解析为map
std::unordered_map<std::string, std::string> ParseKeyValuePairs(const std::string& input) {
    std::unordered_map<std::string, std::string> result;
    if (input.empty()) return result;

    std::stringstream ss(input);
    std::string pair;
    while (std::getline(ss, pair, ',')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = pair.substr(0, eqPos);
            std::string value = pair.substr(eqPos + 1);
            result[key] = value;
        }
    }
    return result;
}

// 检查 subset 的所有键值对是否存在于 superset 中且值相同
bool IsSubset(const std::unordered_map<std::string, std::string>& subset,const std::unordered_map<std::string, std::string>& superset) {
    for (const auto& kv : subset) {
        auto it = superset.find(kv.first);
        
        if (it == superset.end() || it->second != kv.second) {
            return false;
        }
    }
    return true;
}
// --------------------------------------------------------------------------------
// 字符串分割函数
// --------------------------------------------------------------------------------
std::vector<std::string> SplitString(const std::string& input, char delimiter) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        result.push_back(token);
    }
    return result;
}

// --------------------------------------------------------------------------------
// 编码和排序函数
// --------------------------------------------------------------------------------
std::string SortedVariantKey(const std::string& key) {
    static const std::regex keyRegex(R"(([^,=]+)=([^,]+))");
    std::smatch keyMatch;

    std::map<std::string, std::string> keyMap;
    std::vector<std::string> parts = SplitString(key, ',');

    for (const auto& part : parts) {
        std::smatch match;
        if (std::regex_match(part, match, keyRegex)) {
            std::string name = match[1].str();
            std::string value = match[2].str();
            keyMap[name] = value;
        }
    }

    std::stringstream ss;
    bool first = true;
    for (const auto& entry : keyMap) {
        if (!first) {
            ss << ",";
        }
        ss << entry.first << "=" << entry.second;
        first = false;
    }

    return ss.str();
}

// --------------------------------------------------------------------------------
// JSON 文件读取函数
// --------------------------------------------------------------------------------
nlohmann::json GetBlockstateJson(const std::string& namespaceName, const std::string& blockId) {
    std::lock_guard<std::mutex> lock(GlobalCache::cacheMutex);

    // 按照 JAR 文件的加载顺序逐个查找
    for (size_t i = 0; i < GlobalCache::jarOrder.size(); ++i) {
        const std::string& modId = GlobalCache::jarOrder[i];
        std::string cacheKey = modId + ":" + namespaceName + ":" + blockId;
        auto it = GlobalCache::blockstates.find(cacheKey);
        if (it != GlobalCache::blockstates.end()) {
            return it->second;
        }
    }

    std::cerr << "Blockstate not found: " << namespaceName << ":" << blockId << std::endl;
    return nlohmann::json();
}

// --------------------------------------------------------------------------------
// 方块状态 JSON 处理
// --------------------------------------------------------------------------------
ModelData GetRandomModelFromCache(const std::string& namespaceName, const std::string& blockId) {
    std::shared_lock<std::shared_mutex> lock(blockstateCachesMutex); // 使用 shared_lock 进行读操作
    // 先检查主缓存
    if (BlockModelCache.count(namespaceName) &&
        BlockModelCache[namespaceName].count(blockId)) {
        return BlockModelCache[namespaceName][blockId];
    }
    
    // 检查 variant 缓存
    if (VariantModelCache.count(namespaceName) &&
        VariantModelCache[namespaceName].count(blockId)) {
        auto& models = VariantModelCache[namespaceName][blockId];
        int totalWeight = 0;
        for (const auto& wm : models) {
            totalWeight += wm.weight;
        }

        if (totalWeight > 0) {
            thread_local static std::mt19937 gen(std::random_device{}()); // 使用 thread_local 随机数生成器
            std::uniform_int_distribution<> dis(1, totalWeight);
            int randomWeight = dis(gen);
            int cumulative = 0;
            for (const auto& wm : models) {
                cumulative += wm.weight;
                if (randomWeight <= cumulative) {
                    return wm.model;
                }
            }
        }

    }

    // 检查 multipart 缓存:在 multipart 时只进行一次随机,
    // 对每个组选取对应位置的模型(如果该位置没有则使用第一个)
    if (MultipartModelCache.count(namespaceName) &&
        MultipartModelCache[namespaceName].count(blockId)) {
        auto& partList = MultipartModelCache[namespaceName][blockId];

        // 计算所有组中模型数的最大值作为随机索引的范围
        size_t maxCount = 0;
        for (const auto& parts : partList) {
            if (parts.size() > maxCount) {
                maxCount = parts.size();
            }
        }
        if (maxCount == 0) {
            return ModelData();
        }

        thread_local static std::mt19937 gen_multi(std::random_device{}()); // 为 multipart 使用单独的 thread_local 生成器
        std::uniform_int_distribution<> dis(0, maxCount - 1);
        int randomIndex = dis(gen_multi);

        ModelData merged;
        for (auto& parts : partList) {
            int index = randomIndex;
            if (index >= parts.size()) {
                index = 0; // 如果当前组中没有该位置的模型,则默认选第一个
            }
            merged = MergeModelData(merged, parts[index].model);
        }
        return merged;
    }

    // 返回空模型
    return ModelData();
}

// 此方法会处理对应的json文件 
// 然后计算出方块的模型数据存储在BlockModelCache / VariantModelCache / MultipartModelCache 里面
// 你可以使用 GetRandomModelFromCache 方法来获取模型
void ProcessBlockstate(const std::string& namespaceName, const std::vector<std::string>& blockIds) {
    for (const auto& blockId : blockIds) {
        // 解析 blockId 和条件
        static const std::regex blockIdRegex(R"(^(.*?)\[(.*)\]$)");
        std::smatch match;
        std::string baseBlockId = blockId;
        std::string condition;
        std::unordered_map<std::string, std::string> blockConditions;
        std::string blockstateName = namespaceName + ":" + blockId;

        if (std::regex_match(blockId, match, blockIdRegex)) {
            baseBlockId = match.str(1);
            condition = match.str(2);

            static const std::regex conditionRegex(R"((\w+)=([^,]+))");
            auto conditionsBegin = std::sregex_iterator(condition.begin(), condition.end(), conditionRegex);
            auto conditionsEnd = std::sregex_iterator();

            for (auto i = conditionsBegin; i != conditionsEnd; ++i) {
                std::smatch submatch = *i;
                blockConditions[submatch[1].str()] = submatch[2].str();
            }
        }

        // 读取 blockstate JSON
        nlohmann::json blockstateJson = GetBlockstateJson(namespaceName, baseBlockId);


        if (blockstateJson.is_null()) {
            continue;
        }

        ModelData mergedModel;
        std::vector<ModelData> selectedModels;

        // 处理 variants
        if (blockstateJson.contains("variants")) {
            for (auto& variant : blockstateJson["variants"].items()) {
                std::string variantKey = variant.key();
                std::string normalizedCondition = SortedVariantKey(condition);
                std::string normalizedVariantKey = SortedVariantKey(variantKey);

                // 解析为键值对
                auto conditionMap = ParseKeyValuePairs(normalizedCondition);
                auto variantMap = ParseKeyValuePairs(normalizedVariantKey);

                // 判断条件:variantMap 的所有键值对需存在于 conditionMap 中
                if (condition.empty() || IsSubset(variantMap, conditionMap)) {
                    int rotationX = 0, rotationY = 0;
                    bool uvlock = false;


                    if (variant.value().contains("x")) {
                        rotationX = variant.value()["x"].get<int>();
                        rotationX = (rotationX % 360 + 360) % 360; 
                    }
                    if (variant.value().contains("y")) {
                        rotationY = variant.value()["y"].get<int>();
                        rotationY = (rotationY % 360 + 360) % 360;
                    }
                    if (variant.value().contains("uvlock")) {
                        uvlock = variant.value()["uvlock"].get<bool>();
                    }

                    // 处理模型加权数组
                    if (variant.value().is_array()) {
                        std::vector<WeightedModelData> weightedModels;
                        std::string cacheKey = namespaceName + ":" + baseBlockId + ":" + variantKey;
                        ModelData model;
                        int t = 0;
                        for (const auto& item : variant.value()) {
                            int rotationX = 0, rotationY = 0;
                            bool uvlock = false;
                            if (item.contains("x")) {
                                rotationX = item["x"].get<int>();
                            }
                            if (item.contains("y")) {
                                rotationY = item["y"].get<int>();
                            }
                            if (item.contains("uvlock")) {
                                uvlock = item["uvlock"].get<bool>();
                            }

                            int weight = item.contains("weight") ? item["weight"].get<int>() : 1;
                            std::string modelId = item.contains("model") ? item["model"].get<std::string>() : "";

                            if (!modelId.empty()) {
                                // 处理模型命名空间
                                size_t colonPos = modelId.find(':');
                                std::string modelNamespace = namespaceName;
                                if (colonPos != std::string::npos) {
                                    modelNamespace = modelId.substr(0, colonPos);
                                    modelId = modelId.substr(colonPos + 1);
                                }

                                // 生成模型数据
                                model = ProcessModelJson(modelNamespace, modelId,
                                    rotationX, rotationY, uvlock, t, blockstateName);

                                weightedModels.push_back({ model, weight });
                                t = t + 1;
                            }

                        }
                        // 存入缓存
                        {
                            std::unique_lock<std::shared_mutex> lock(blockstateCachesMutex); // 使用 unique_lock 进行写操作
                            VariantModelCache[namespaceName][blockId] = weightedModels;
                        }
                        continue;

                    }
                    else {
                        std::string modelId = variant.value().contains("model") ? variant.value()["model"].get<std::string>() : "";
                        if (!modelId.empty()) {
                            size_t colonPos = modelId.find(':');
                            std::string modelNamespace = namespaceName;

                            if (colonPos != std::string::npos) {
                                modelNamespace = modelId.substr(0, colonPos);
                                modelId = modelId.substr(colonPos + 1);
                            }

                            mergedModel = ProcessModelJson(modelNamespace, modelId, rotationX, rotationY, uvlock, 0, blockstateName);
                           
                            {
                                std::unique_lock<std::shared_mutex> lock(blockstateCachesMutex); // 使用 unique_lock 进行写操作
                                BlockModelCache[namespaceName][blockId] = mergedModel;
                            }
                        }
                    }
                }
            }
            continue;
        }

        // 处理 multipart
        if (blockstateJson.contains("multipart")) {
            auto multipart = blockstateJson["multipart"];
            bool useMultipartModelCache = false;
            // 第一次遍历:检测是否存在列表格式的 apply
            for (const auto& item : multipart) {
                if (item.contains("apply") && item["apply"].is_array()) {
                    useMultipartModelCache = true;
                    break;
                }
            }

            if (useMultipartModelCache) {
                // 存储所有 multipart 项的模型组,每项都作为列表处理
                std::vector<std::vector<WeightedModelData>> multipartModelsList;
                for (const auto& item : multipart) {
                    if (!item.contains("apply"))
                        continue;
                    bool conditionMatched = true;
                    if (item.contains("when")) {
                        conditionMatched = matchConditions(blockConditions, item["when"]);
                    }
                    if (!conditionMatched)
                        continue;

                    std::vector<WeightedModelData> multipartModels;
                    int t = 0;
                    // 如果 apply 为数组,直接遍历,否则将对象包装为单元素数组
                    if (item["apply"].is_array()) {
                        for (const auto& modelItem : item["apply"]) {
                            int rotationX = 0, rotationY = 0;
                            bool uvlock = false;
                            if (modelItem.contains("x")) {
                                rotationX = modelItem["x"].get<int>();
                            }
                            if (modelItem.contains("y")) {
                                rotationY = modelItem["y"].get<int>();
                            }
                            if (modelItem.contains("uvlock")) {
                                uvlock = modelItem["uvlock"].get<bool>();
                            }
                            int weight = modelItem.contains("weight") ? modelItem["weight"].get<int>() : 1;
                            std::string modelId = modelItem.contains("model") ? modelItem["model"].get<std::string>() : "";
                            if (!modelId.empty()) {
                                // 处理模型命名空间
                                size_t colonPos = modelId.find(':');
                                std::string modelNamespace = namespaceName;
                                if (colonPos != std::string::npos) {
                                    modelNamespace = modelId.substr(0, colonPos);
                                    modelId = modelId.substr(colonPos + 1);
                                }
                                // 生成模型数据
                                ModelData model = ProcessModelJson(modelNamespace, modelId,
                                    rotationX, rotationY, uvlock, t, blockstateName);
                                multipartModels.push_back({ model, weight });
                                ++t;
                            }
                        }
                    }
                    else if (item["apply"].is_object()) {
                        auto apply = item["apply"];
                        int rotationX = 0, rotationY = 0;
                        bool uvlock = false;
                        if (apply.contains("x")) {
                            rotationX = apply["x"].get<int>();
                        }
                        if (apply.contains("y")) {
                            rotationY = apply["y"].get<int>();
                        }
                        if (apply.contains("uvlock")) {
                            uvlock = apply["uvlock"].get<bool>();
                        }
                        int weight = apply.contains("weight") ? apply["weight"].get<int>() : 1;
                        std::string modelId = apply.contains("model") ? apply["model"].get<std::string>() : "";
                        if (!modelId.empty()) {
                            size_t colonPos = modelId.find(':');
                            std::string modelNamespace = namespaceName;
                            if (colonPos != std::string::npos) {
                                modelNamespace = modelId.substr(0, colonPos);
                                modelId = modelId.substr(colonPos + 1);
                            }
                            ModelData model = ProcessModelJson(modelNamespace, modelId, rotationX, rotationY, uvlock, t, blockstateName);
                            multipartModels.push_back({ model, weight });
                        }
                    }

                    if (!multipartModels.empty()) {
                        multipartModelsList.push_back(multipartModels);
                    }
                }
                // 存入 MultipartModelCache
                {
                    std::unique_lock<std::shared_mutex> lock(blockstateCachesMutex); // 使用 unique_lock 进行写操作
                    MultipartModelCache[namespaceName][blockId] = multipartModelsList;
                }
            }
            else {
                // 如果所有 apply 均为对象,则按照原来的逻辑处理,合并模型后存入 BlockModelCache
                std::vector<ModelData> selectedModels;
                for (const auto& item : multipart) {
                    if (!item.contains("apply"))
                        continue;
                    bool conditionMatched = true;
                    if (item.contains("when")) {
                        conditionMatched = matchConditions(blockConditions, item["when"]);
                    }
                    if (!conditionMatched)
                        continue;

                    auto apply = item["apply"];
                    int rotationX = 0, rotationY = 0;
                    bool uvlock = false;
                    if (apply.contains("x")) {
                        rotationX = apply["x"].get<int>();
                    }
                    if (apply.contains("y")) {
                        rotationY = apply["y"].get<int>();
                    }
                    if (apply.contains("uvlock")) {
                        uvlock = apply["uvlock"].get<bool>();
                    }
                    std::string modelId = apply.contains("model") ? apply["model"].get<std::string>() : "";
                    if (!modelId.empty()) {
                        size_t colonPos = modelId.find(':');
                        std::string modelNamespace = namespaceName;
                        if (colonPos != std::string::npos) {
                            modelNamespace = modelId.substr(0, colonPos);
                            modelId = modelId.substr(colonPos + 1);
                        }
                        ModelData selectedModel = ProcessModelJson(modelNamespace, modelId, rotationX, rotationY, uvlock, 0, blockstateName);
                        selectedModels.push_back(selectedModel);
                    }
                }
                // 合并多个模型
                if (!selectedModels.empty()) {
                    mergedModel = selectedModels[0];
                    for (size_t i = 1; i < selectedModels.size(); ++i) {
                        mergedModel = MergeModelData(mergedModel, selectedModels[i]);
                    }
                }
                {
                    std::unique_lock<std::shared_mutex> lock(blockstateCachesMutex); // 使用 unique_lock 进行写操作
                    BlockModelCache[namespaceName][blockId] = mergedModel;
                }
            }
        }


    }
}

void ProcessBlockstateForBlocks(const std::vector<Block>& blocks) {
    std::unordered_map<std::string, std::vector<std::string>> namespaceToBlockIdsMap;

    // 将 Block 列表按命名空间分组
    for (const auto& block : blocks) {
        std::string namespaceName = block.GetNamespace(); // 使用 Block 结构体的 GetNamespace 方法
        std::string blockId = block.GetModifiedName();
        namespaceToBlockIdsMap[namespaceName].push_back(blockId);
    }

    // 处理每个命名空间下的方块 ID
    for (const auto& entry : namespaceToBlockIdsMap) {
        const std::string& namespaceName = entry.first;
        const std::vector<std::string>& blockIds = entry.second;

        ProcessBlockstate(namespaceName, blockIds);
       
    }

}
