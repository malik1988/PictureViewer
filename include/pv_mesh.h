#pragma once

#include "pv_common.h"

namespace pv {

struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MeshTriangle {
    UINT a = 0;
    UINT b = 0;
    UINT c = 0;
};

// 归一化到原点的三角网格，供软件渲染器直接使用。
struct Mesh3D {
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;

    bool valid() const noexcept { return !vertices.empty() && !triangles.empty(); }
    void Normalize();
};

class MeshLoader {
public:
    // ext 为大写扩展名（不含点）。
    static bool IsSupported(const std::wstring& ext);

    // 支持 OBJ、STL（二进制/ASCII）、PLY（ASCII/二进制）、OFF、GLB、glTF。
    // GLB/glTF 只读取三角网格的位置与索引，不处理材质/贴图/骨骼动画。
    static std::unique_ptr<Mesh3D> Load(const std::wstring& path);
};

} // namespace pv
