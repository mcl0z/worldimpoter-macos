#ifndef ENTITYBLOCK_H
#define ENTITYBLOCK_H

#include <iostream>
#include <vector>
#include "model.h" // 假设 ModelData 定义在这个头文件中

struct EntityBlock {
    std::string id;  // 实体方块的ID
    int x, y, z;     // 实体方块的坐标

    virtual ~EntityBlock() = default;  // 虚析构函数,以便正确析构派生类对象
    virtual void PrintDetails() const;
    // 新增的虚函数
    virtual ModelData GenerateModel() const = 0;  // 纯虚函数,要求派生类实现
};

struct YuushyaBlockEntry {
    int blockid;
    std::vector<double> showPos;
    std::vector<float> showRotation;
    std::vector<float> showScales;
    bool isShown;
    int slot;
};

struct YuushyaShowBlockEntity : public EntityBlock {
    // 特有属性
    std::vector<YuushyaBlockEntry> blocks;  // 存储每个 Block 的信息
    int controlSlot;  // ControlSlot
    bool keepPacked;  // keepPacked

    // 输出实体详细信息
    // 输出实体详细信息
    void PrintDetails() const override;
    // 生成模型的声明
    ModelData GenerateModel() const override;
};

enum class LittleFaceState {
    UNLOADED,
    INSIDE_UNCOVERED,
    INSIDE_PARTIALLY_COVERED,
    INSIDE_COVERED,
    OUTSIDE_UNCOVERED,
    OUTSIDE_PARTIALLY_COVERED,
    OUTSIDE_COVERED
};

// 定义 littletiles:tiles 需要的额外数据结构
struct LittleTilesTileEntry {
    // 若 color 数组中第一个数为 -1,则使用 key(即 tiles 下的名字)作为区分,否则可用于覆盖 block 名称
    std::string blockName;
    std::vector<int> color;
    // 用列表保存任意多个 boxData,每个 boxData 是一个 int 数组
    std::vector<std::vector<int>> boxDataList; // 每个boxData包含12个整数。前6个为面状态(顺序:EAST,WEST,UP,DOWN,SOUTH,NORTH),后6个为边界(minX,minY,minZ,maxX,maxY,maxZ)
};

struct LittleTilesChildEntry {
    std::vector<int> coord;
    std::vector<LittleTilesTileEntry> tiles;
};

// 新增 littletiles 实体类
struct LittleTilesTilesEntity : public EntityBlock {
    std::vector<LittleTilesTileEntry> tiles;
    std::vector<LittleTilesChildEntry> children; // 新增: 用于存储子结构
    int grid = 16; // 小方块的精度,默认为16

    void PrintDetails() const override;
    ModelData GenerateModel() const override;
};

#endif