#include "pv_mesh.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace pv {
namespace {

constexpr std::size_t kMaxTriangles = 300000;
constexpr std::size_t kMaxVertices = 1000000;

// ---------------------------------------------------------------------------
// OBJ：位置(v) + 面(f)，支持 a/b/c 索引与多边形三角化。
// ---------------------------------------------------------------------------
bool LoadOBJ(const std::wstring& path, Mesh3D& mesh) {
    std::ifstream file{ std::filesystem::path(path) };
    if (!file) {
        return false;
    }

    std::vector<MeshVertex> positions;
    std::string line;
    while (std::getline(file, line)) {
        if (line.size() < 2) {
            continue;
        }
        std::istringstream stream(line);
        std::string kind;
        stream >> kind;

        if (kind == "v") {
            MeshVertex v{};
            if (stream >> v.x >> v.y >> v.z) {
                positions.push_back(v);
            }
        } else if (kind == "f") {
            std::vector<UINT> face;
            std::string token;
            while (stream >> token) {
                const std::size_t slash = token.find('/');
                std::string indexText = slash == std::string::npos ? token : token.substr(0, slash);
                if (indexText.empty()) {
                    continue;
                }
                int index = 0;
                try {
                    index = std::stoi(indexText);
                } catch (...) {
                    continue;
                }
                if (index < 0) {
                    index += static_cast<int>(positions.size());
                }
                if (index > 0 && static_cast<std::size_t>(index) <= positions.size()) {
                    face.push_back(static_cast<UINT>(index - 1));
                }
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                if (mesh.triangles.size() >= kMaxTriangles) {
                    break;
                }
                mesh.triangles.push_back({ face[0], face[i], face[i + 1] });
            }
        }
        if (mesh.triangles.size() >= kMaxTriangles) {
            break;
        }
    }

    if (positions.empty() || mesh.triangles.empty()) {
        return false;
    }
    mesh.vertices = std::move(positions);
    return true;
}

// ---------------------------------------------------------------------------
// STL：二进制（80 字节头 + uint32 面数 + 每面 50 字节）与 ASCII。
// ---------------------------------------------------------------------------
bool LoadBinarySTL(std::ifstream& file, std::uint64_t fileSize, Mesh3D& mesh) {
    file.seekg(0, std::ios::beg);
    char header[80]{};
    file.read(header, 80);
    std::uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), 4);
    if (!file || 84ull + 50ull * count > fileSize) {
        return false;
    }

    mesh.vertices.reserve(static_cast<std::size_t>(count) * 3);
    mesh.triangles.reserve(static_cast<std::size_t>(count));
    for (std::uint32_t i = 0; i < count && mesh.triangles.size() < kMaxTriangles; ++i) {
        char record[50]{};
        file.read(record, 50);
        if (!file) {
            return false;
        }
        const float* values = reinterpret_cast<const float*>(record + 12);
        const UINT base = static_cast<UINT>(mesh.vertices.size());
        for (int v = 0; v < 3; ++v) {
            mesh.vertices.push_back({ values[v * 3], values[v * 3 + 1], values[v * 3 + 2] });
        }
        mesh.triangles.push_back({ base, base + 1, base + 2 });
    }
    return mesh.valid();
}

bool LoadAsciiSTL(std::ifstream& file, Mesh3D& mesh) {
    file.clear();
    file.seekg(0, std::ios::beg);
    std::string line;
    std::vector<MeshVertex> tri;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;
        if (keyword != "vertex") {
            continue;
        }
        MeshVertex v{};
        if (!(stream >> v.x >> v.y >> v.z)) {
            continue;
        }
        tri.push_back(v);
        if (tri.size() == 3) {
            if (mesh.triangles.size() >= kMaxTriangles) {
                break;
            }
            const UINT base = static_cast<UINT>(mesh.vertices.size());
            mesh.vertices.push_back(tri[0]);
            mesh.vertices.push_back(tri[1]);
            mesh.vertices.push_back(tri[2]);
            mesh.triangles.push_back({ base, base + 1, base + 2 });
            tri.clear();
        }
    }
    return mesh.valid();
}

bool LoadSTL(const std::wstring& path, Mesh3D& mesh) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(file.tellg());
    if (size < 84) {
        return false;
    }
    if (LoadBinarySTL(file, size, mesh)) {
        return true;
    }
    mesh.vertices.clear();
    mesh.triangles.clear();
    return LoadAsciiSTL(file, mesh);
}

// ---------------------------------------------------------------------------
// PLY：ASCII 与 little-endian 二进制。只取 x/y/z 顶点与 face 列表。
// ---------------------------------------------------------------------------
bool LoadPLY(const std::wstring& path, Mesh3D& mesh) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }

    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    // 去掉可能的 CR。
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != "ply") {
        return false;
    }

    enum class Format { Ascii, BinaryLE, BinaryBE };
    Format format = Format::Ascii;
    std::size_t vertexCount = 0;
    std::size_t faceCount = 0;
    std::size_t vertexProps = 0;
    bool inVertexElement = false;
    bool sawEndHeader = false;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream stream(line);
        std::string key;
        stream >> key;
        if (key == "format") {
            std::string name;
            stream >> name;
            if (name == "binary_little_endian") {
                format = Format::BinaryLE;
            } else if (name == "binary_big_endian") {
                format = Format::BinaryBE;
            } else {
                format = Format::Ascii;
            }
        } else if (key == "element") {
            std::string name;
            std::size_t count = 0;
            stream >> name >> count;
            inVertexElement = false;
            if (name == "vertex") {
                vertexCount = count;
                vertexProps = 0;
                inVertexElement = true;
            } else if (name == "face") {
                faceCount = count;
            }
        } else if (key == "property") {
            // 只统计 vertex 段的属性数量，face 段的 property list 不计入。
            if (inVertexElement) {
                std::string rest;
                std::getline(stream, rest);
                if (!rest.empty()) {
                    ++vertexProps;
                }
            }
        } else if (key == "end_header") {
            sawEndHeader = true;
            break;
        }
    }

    if (!sawEndHeader || vertexCount == 0 || faceCount == 0 ||
        vertexCount > kMaxVertices) {
        return false;
    }

    // 常见 PLY 顶点为 float x/y/z（3 个属性）。仅支持这种布局；其它布局拒绝，
    // 避免误读出错乱几何。
    const bool standardVertex = vertexProps >= 3;

    if (format == Format::Ascii) {
        for (std::size_t i = 0; i < vertexCount; ++i) {
            MeshVertex v{};
            if (!(file >> v.x >> v.y >> v.z)) {
                return false;
            }
            // 跳过该顶点剩余属性。
            for (std::size_t p = 3; p < vertexProps; ++p) {
                std::string ignored;
                if (!(file >> ignored)) {
                    return false;
                }
            }
            mesh.vertices.push_back(v);
        }
        for (std::size_t i = 0; i < faceCount && mesh.triangles.size() < kMaxTriangles; ++i) {
            int n = 0;
            if (!(file >> n) || n < 3 || n > 64) {
                return false;
            }
            std::vector<UINT> face;
            face.reserve(static_cast<std::size_t>(n));
            for (int j = 0; j < n; ++j) {
                int index = 0;
                if (!(file >> index)) {
                    return false;
                }
                if (index >= 0 && static_cast<std::size_t>(index) < mesh.vertices.size()) {
                    face.push_back(static_cast<UINT>(index));
                }
            }
            for (std::size_t j = 1; j + 1 < face.size(); ++j) {
                mesh.triangles.push_back({ face[0], face[j], face[j + 1] });
            }
        }
        return mesh.valid();
    }

    if (!standardVertex) {
        return false;
    }

    // 二进制：逐顶点读取，前三个 float 作为位置，其余属性按 4 字节跳过。
    mesh.vertices.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        float values[3]{};
        for (int c = 0; c < 3; ++c) {
            std::uint8_t raw[4]{};
            file.read(reinterpret_cast<char*>(raw), 4);
            if (!file) {
                return false;
            }
            std::uint32_t bits = 0;
            if (format == Format::BinaryLE) {
                bits = static_cast<std::uint32_t>(raw[0]) |
                       (static_cast<std::uint32_t>(raw[1]) << 8) |
                       (static_cast<std::uint32_t>(raw[2]) << 16) |
                       (static_cast<std::uint32_t>(raw[3]) << 24);
            } else {
                bits = (static_cast<std::uint32_t>(raw[0]) << 24) |
                       (static_cast<std::uint32_t>(raw[1]) << 16) |
                       (static_cast<std::uint32_t>(raw[2]) << 8) |
                       static_cast<std::uint32_t>(raw[3]);
            }
            std::memcpy(&values[c], &bits, 4);
        }
        for (std::size_t p = 3; p < vertexProps; ++p) {
            file.seekg(4, std::ios::cur);
        }
        mesh.vertices.push_back({ values[0], values[1], values[2] });
    }

    for (std::size_t i = 0; i < faceCount && mesh.triangles.size() < kMaxTriangles; ++i) {
        std::uint8_t countRaw = 0;
        file.read(reinterpret_cast<char*>(&countRaw), 1);
        if (!file || countRaw < 3 || countRaw > 64) {
            return false;
        }
        std::vector<UINT> face;
        face.reserve(countRaw);
        for (int j = 0; j < countRaw; ++j) {
            std::uint8_t raw[4]{};
            file.read(reinterpret_cast<char*>(raw), 4);
            if (!file) {
                return false;
            }
            std::uint32_t index = 0;
            if (format == Format::BinaryLE) {
                index = static_cast<std::uint32_t>(raw[0]) |
                        (static_cast<std::uint32_t>(raw[1]) << 8) |
                        (static_cast<std::uint32_t>(raw[2]) << 16) |
                        (static_cast<std::uint32_t>(raw[3]) << 24);
            } else {
                index = (static_cast<std::uint32_t>(raw[0]) << 24) |
                        (static_cast<std::uint32_t>(raw[1]) << 16) |
                        (static_cast<std::uint32_t>(raw[2]) << 8) |
                        static_cast<std::uint32_t>(raw[3]);
            }
            if (index < mesh.vertices.size()) {
                face.push_back(static_cast<UINT>(index));
            }
        }
        for (std::size_t j = 1; j + 1 < face.size(); ++j) {
            mesh.triangles.push_back({ face[0], face[j], face[j + 1] });
        }
    }
    return mesh.valid();
}

// ---------------------------------------------------------------------------
// OFF：ASCII 简单网格格式。
// ---------------------------------------------------------------------------
bool LoadOFF(const std::wstring& path, Mesh3D& mesh) {
    std::ifstream file{ std::filesystem::path(path) };
    if (!file) {
        return false;
    }
    std::string magic;
    file >> magic;
    if (magic != "OFF") {
        return false;
    }
    std::size_t vertexCount = 0;
    std::size_t faceCount = 0;
    std::size_t edgeCount = 0;
    if (!(file >> vertexCount >> faceCount >> edgeCount) || vertexCount == 0 ||
        faceCount == 0 || vertexCount > kMaxVertices) {
        return false;
    }

    for (std::size_t i = 0; i < vertexCount; ++i) {
        MeshVertex v{};
        if (!(file >> v.x >> v.y >> v.z)) {
            return false;
        }
        mesh.vertices.push_back(v);
    }
    for (std::size_t i = 0; i < faceCount && mesh.triangles.size() < kMaxTriangles; ++i) {
        int n = 0;
        if (!(file >> n) || n < 3 || n > 64) {
            return false;
        }
        std::vector<UINT> face;
        face.reserve(static_cast<std::size_t>(n));
        for (int j = 0; j < n; ++j) {
            int index = 0;
            if (!(file >> index)) {
                return false;
            }
            if (index >= 0 && static_cast<std::size_t>(index) < mesh.vertices.size()) {
                face.push_back(static_cast<UINT>(index));
            }
        }
        for (std::size_t j = 1; j + 1 < face.size(); ++j) {
            mesh.triangles.push_back({ face[0], face[j], face[j + 1] });
        }
    }
    return mesh.valid();
}

// ---------------------------------------------------------------------------
// 轻量 JSON 解析器：只用于 glTF 清单，避免引入第三方依赖。
// ---------------------------------------------------------------------------
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> array;
    std::unordered_map<std::string, Json> object;

    const Json* find(const std::string& key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
    const Json* at(std::size_t index) const {
        if (type != Type::Array || index >= array.size()) {
            return nullptr;
        }
        return &array[index];
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : cur_(input.data()), end_(input.data() + input.size()) {}

    bool Parse(Json& out) {
        SkipWhitespace();
        if (!ParseValue(out)) {
            return false;
        }
        SkipWhitespace();
        return cur_ == end_;
    }

private:
    const char* cur_;
    const char* end_;

    void SkipWhitespace() {
        while (cur_ < end_ && (*cur_ == ' ' || *cur_ == '\t' || *cur_ == '\r' || *cur_ == '\n')) {
            ++cur_;
        }
    }

    bool Consume(char c) {
        if (cur_ < end_ && *cur_ == c) {
            ++cur_;
            return true;
        }
        return false;
    }

    bool ParseValue(Json& out) {
        SkipWhitespace();
        if (cur_ >= end_) {
            return false;
        }
        switch (*cur_) {
        case '{': return ParseObject(out);
        case '[': return ParseArray(out);
        case '"': {
            out.type = Json::Type::String;
            return ParseString(out.text);
        }
        case 't': {
            if (end_ - cur_ < 4 || std::memcmp(cur_, "true", 4) != 0) return false;
            cur_ += 4; out.type = Json::Type::Bool; out.boolean = true; return true;
        }
        case 'f': {
            if (end_ - cur_ < 5 || std::memcmp(cur_, "false", 5) != 0) return false;
            cur_ += 5; out.type = Json::Type::Bool; out.boolean = false; return true;
        }
        case 'n': {
            if (end_ - cur_ < 4 || std::memcmp(cur_, "null", 4) != 0) return false;
            cur_ += 4; out.type = Json::Type::Null; return true;
        }
        default:
            return ParseNumber(out);
        }
    }

    bool ParseString(std::string& out) {
        if (!Consume('"')) {
            return false;
        }
        out.clear();
        while (cur_ < end_) {
            const char c = *cur_++;
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (cur_ >= end_) {
                    return false;
                }
                const char escape = *cur_++;
                switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    // 支持基本 BMP 转义；glTF 常见字段为 ASCII，此处够用。
                    if (end_ - cur_ < 4) {
                        return false;
                    }
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = *cur_++;
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(Json& out) {
        const char* start = cur_;
        if (Consume('-')) {
            // ok
        }
        while (cur_ < end_ && ((*cur_ >= '0' && *cur_ <= '9') || *cur_ == '.' ||
                               *cur_ == 'e' || *cur_ == 'E' || *cur_ == '+' || *cur_ == '-')) {
            ++cur_;
        }
        if (cur_ == start) {
            return false;
        }
        std::string text(start, cur_);
        try {
            out.type = Json::Type::Number;
            out.number = std::stod(text);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ParseObject(Json& out) {
        if (!Consume('{')) {
            return false;
        }
        out.type = Json::Type::Object;
        SkipWhitespace();
        if (Consume('}')) {
            return true;
        }
        while (cur_ < end_) {
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return false;
            }
            Json value;
            if (!ParseValue(value)) {
                return false;
            }
            out.object[key] = std::move(value);
            SkipWhitespace();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
        return false;
    }

    bool ParseArray(Json& out) {
        if (!Consume('[')) {
            return false;
        }
        out.type = Json::Type::Array;
        SkipWhitespace();
        if (Consume(']')) {
            return true;
        }
        while (cur_ < end_) {
            Json value;
            if (!ParseValue(value)) {
                return false;
            }
            out.array.push_back(std::move(value));
            SkipWhitespace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// glTF / GLB：读取 POSITION 与索引，生成三角网格。
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> ReadBinaryFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(size);
    if (size && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }
    return bytes;
}

int DecodeHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> DecodeBase64(const std::string& input) {
    std::vector<std::uint8_t> output;
    if (input.size() % 4 != 0) {
        return {};
    }
    output.reserve(input.size() / 4 * 3);
    auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (std::size_t i = 0; i < input.size(); i += 4) {
        int a = valueOf(input[i]);
        int b = valueOf(input[i + 1]);
        int c = input[i + 2] == '=' ? 0 : valueOf(input[i + 2]);
        int d = input[i + 3] == '=' ? 0 : valueOf(input[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            return {};
        }
        output.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
        if (input[i + 2] != '=') {
            output.push_back(static_cast<std::uint8_t>(((b & 0x0F) << 4) | (c >> 2)));
        }
        if (input[i + 3] != '=') {
            output.push_back(static_cast<std::uint8_t>(((c & 0x03) << 6) | d));
        }
    }
    return output;
}

const Json* AccessorFor(const Json& root, const std::string& name, int index) {
    const Json* array = root.find(name);
    return array ? array->at(static_cast<std::size_t>(index)) : nullptr;
}

bool AccessorFloat3(const Json& root, const std::vector<std::vector<std::uint8_t>>& buffers,
                    int accessorIndex, std::vector<MeshVertex>& out) {
    const Json* accessor = AccessorFor(root, "accessors", accessorIndex);
    if (!accessor || accessor->type != Json::Type::Object) {
        return false;
    }
    const Json* type = accessor->find("type");
    const Json* component = accessor->find("componentType");
    const Json* count = accessor->find("count");
    const Json* viewIndex = accessor->find("bufferView");
    if (!type || type->text != "VEC3" || !component || component->number != 5126 ||
        !count || count->number <= 0 || !viewIndex) {
        return false;
    }
    const Json* view = AccessorFor(root, "bufferViews", static_cast<int>(viewIndex->number));
    if (!view || view->type != Json::Type::Object) {
        return false;
    }
    const Json* bufferIndex = view->find("buffer");
    if (!bufferIndex || bufferIndex->number < 0 ||
        static_cast<std::size_t>(bufferIndex->number) >= buffers.size()) {
        return false;
    }
    const std::vector<std::uint8_t>& data = buffers[static_cast<std::size_t>(bufferIndex->number)];
    const Json* viewOffset = view->find("byteOffset");
    const Json* viewStride = view->find("byteStride");
    const Json* accessorOffset = accessor->find("byteOffset");
    const std::size_t base = static_cast<std::size_t>(viewOffset ? viewOffset->number : 0.0) +
                             static_cast<std::size_t>(accessorOffset ? accessorOffset->number : 0.0);
    const std::size_t stride = static_cast<std::size_t>(viewStride ? viewStride->number : 12.0);
    const std::size_t countValue = static_cast<std::size_t>(count->number);
    if (countValue > kMaxVertices || stride < 12) {
        return false;
    }

    out.clear();
    out.reserve(countValue);
    for (std::size_t i = 0; i < countValue; ++i) {
        const std::size_t offset = base + i * stride;
        if (offset + 12 > data.size()) {
            return false;
        }
        float values[3]{};
        std::memcpy(values, data.data() + offset, 12);
        out.push_back({ values[0], values[1], values[2] });
    }
    return !out.empty();
}

bool AccessorIndices(const Json& root, const std::vector<std::vector<std::uint8_t>>& buffers,
                     int accessorIndex, std::vector<UINT>& out) {
    const Json* accessor = AccessorFor(root, "accessors", accessorIndex);
    if (!accessor || accessor->type != Json::Type::Object) {
        return false;
    }
    const Json* type = accessor->find("type");
    const Json* component = accessor->find("componentType");
    const Json* count = accessor->find("count");
    const Json* viewIndex = accessor->find("bufferView");
    if (!type || type->text != "SCALAR" || !component || !count || !viewIndex ||
        count->number <= 0) {
        return false;
    }
    const int componentType = static_cast<int>(component->number);
    std::size_t componentSize = 0;
    if (componentType == 5121) componentSize = 1;
    else if (componentType == 5123) componentSize = 2;
    else if (componentType == 5125) componentSize = 4;
    else return false;

    const Json* view = AccessorFor(root, "bufferViews", static_cast<int>(viewIndex->number));
    if (!view || view->type != Json::Type::Object) {
        return false;
    }
    const Json* bufferIndex = view->find("buffer");
    if (!bufferIndex || bufferIndex->number < 0 ||
        static_cast<std::size_t>(bufferIndex->number) >= buffers.size()) {
        return false;
    }
    const std::vector<std::uint8_t>& data = buffers[static_cast<std::size_t>(bufferIndex->number)];
    const Json* viewOffset = view->find("byteOffset");
    const Json* viewStride = view->find("byteStride");
    const Json* accessorOffset = accessor->find("byteOffset");
    const std::size_t base = static_cast<std::size_t>(viewOffset ? viewOffset->number : 0.0) +
                             static_cast<std::size_t>(accessorOffset ? accessorOffset->number : 0.0);
    const std::size_t stride = static_cast<std::size_t>(viewStride ? viewStride->number : componentSize);
    const std::size_t countValue = static_cast<std::size_t>(count->number);
    if (countValue > kMaxTriangles * 3 || stride < componentSize) {
        return false;
    }

    out.clear();
    out.reserve(countValue);
    for (std::size_t i = 0; i < countValue; ++i) {
        const std::size_t offset = base + i * stride;
        if (offset + componentSize > data.size()) {
            return false;
        }
        UINT value = 0;
        if (componentType == 5121) {
            value = data[offset];
        } else if (componentType == 5123) {
            value = static_cast<UINT>(data[offset]) | (static_cast<UINT>(data[offset + 1]) << 8);
        } else {
            value = static_cast<UINT>(data[offset]) |
                    (static_cast<UINT>(data[offset + 1]) << 8) |
                    (static_cast<UINT>(data[offset + 2]) << 16) |
                    (static_cast<UINT>(data[offset + 3]) << 24);
        }
        out.push_back(value);
    }
    return !out.empty();
}

bool BuildMeshFromGltf(const Json& root, const std::vector<std::vector<std::uint8_t>>& buffers,
                       Mesh3D& mesh) {
    const Json* meshes = root.find("meshes");
    if (!meshes || meshes->type != Json::Type::Array) {
        return false;
    }
    for (const Json& meshNode : meshes->array) {
        const Json* primitives = meshNode.find("primitives");
        if (!primitives || primitives->type != Json::Type::Array) {
            continue;
        }
        for (const Json& primitive : primitives->array) {
            const Json* attributes = primitive.find("attributes");
            const Json* positionIndex = attributes ? attributes->find("POSITION") : nullptr;
            if (!positionIndex || positionIndex->type != Json::Type::Number) {
                continue;
            }
            std::vector<MeshVertex> vertices;
            if (!AccessorFloat3(root, buffers, static_cast<int>(positionIndex->number), vertices)) {
                continue;
            }
            const UINT base = static_cast<UINT>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());

            std::vector<UINT> indices;
            const Json* indexIndex = primitive.find("indices");
            if (indexIndex && indexIndex->type == Json::Type::Number &&
                AccessorIndices(root, buffers, static_cast<int>(indexIndex->number), indices)) {
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                    if (mesh.triangles.size() >= kMaxTriangles) {
                        break;
                    }
                    const UINT a = indices[i];
                    const UINT b = indices[i + 1];
                    const UINT c = indices[i + 2];
                    if (a < vertices.size() && b < vertices.size() && c < vertices.size()) {
                        mesh.triangles.push_back({ base + a, base + b, base + c });
                    }
                }
            } else {
                for (std::size_t i = 0; i + 2 < vertices.size() && mesh.triangles.size() < kMaxTriangles; i += 3) {
                    mesh.triangles.push_back({ base + static_cast<UINT>(i),
                                               base + static_cast<UINT>(i + 1),
                                               base + static_cast<UINT>(i + 2) });
                }
            }
        }
    }
    return mesh.valid();
}

bool LoadGLB(const std::wstring& path, Mesh3D& mesh) {
    const std::vector<std::uint8_t> bytes = ReadBinaryFile(path);
    if (bytes.size() < 20) {
        return false;
    }
    // glTF magic 为小端 'glTF' = 0x46546C67。
    if (bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F') {
        return false;
    }
    std::size_t offset = 12;  // header: magic + version + length
    std::string jsonText;
    std::vector<std::uint8_t> binary;
    while (offset + 8 <= bytes.size()) {
        std::uint32_t chunkLength = static_cast<std::uint32_t>(bytes[offset]) |
                                    (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        const char* chunkType = reinterpret_cast<const char*>(bytes.data() + offset + 4);
        offset += 8;
        if (offset + chunkLength > bytes.size()) {
            return false;
        }
        if (std::memcmp(chunkType, "JSON", 4) == 0) {
            jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), chunkLength);
        } else if (std::memcmp(chunkType, "BIN", 3) == 0) {
            binary.assign(bytes.begin() + offset, bytes.begin() + offset + chunkLength);
        }
        offset += chunkLength;
    }
    if (jsonText.empty()) {
        return false;
    }
    Json root;
    if (!JsonParser(jsonText).Parse(root)) {
        return false;
    }
    return BuildMeshFromGltf(root, { binary }, mesh);
}

bool LoadGLTF(const std::wstring& path, Mesh3D& mesh) {
    const std::vector<std::uint8_t> bytes = ReadBinaryFile(path);
    if (bytes.empty()) {
        return false;
    }
    const std::string jsonText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Json root;
    if (!JsonParser(jsonText).Parse(root)) {
        return false;
    }
    const Json* buffers = root.find("buffers");
    if (!buffers || buffers->type != Json::Type::Array) {
        return false;
    }
    std::vector<std::vector<std::uint8_t>> decoded;
    const std::filesystem::path baseDir = std::filesystem::path(path).parent_path();
    for (const Json& buffer : buffers->array) {
        const Json* uri = buffer.find("uri");
        if (!uri || uri->type != Json::Type::String) {
            return false;
        }
        if (uri->text.rfind("data:", 0) == 0) {
            const std::size_t comma = uri->text.find(',');
            if (comma == std::string::npos) {
                return false;
            }
            decoded.push_back(DecodeBase64(uri->text.substr(comma + 1)));
        } else {
            decoded.push_back(ReadBinaryFile((baseDir / uri->text).wstring()));
        }
    }
    return BuildMeshFromGltf(root, decoded, mesh);
}

} // namespace

void Mesh3D::Normalize() {
    if (vertices.empty()) {
        return;
    }
    float minX = vertices[0].x, maxX = vertices[0].x;
    float minY = vertices[0].y, maxY = vertices[0].y;
    float minZ = vertices[0].z, maxZ = vertices[0].z;
    for (const MeshVertex& v : vertices) {
        minX = (std::min)(minX, v.x); maxX = (std::max)(maxX, v.x);
        minY = (std::min)(minY, v.y); maxY = (std::max)(maxY, v.y);
        minZ = (std::min)(minZ, v.z); maxZ = (std::max)(maxZ, v.z);
    }
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    const float cz = (minZ + maxZ) * 0.5f;
    const float extent = (std::max)({ maxX - minX, maxY - minY, maxZ - minZ });
    const float scale = extent > 1e-6f ? 2.0f / extent : 1.0f;
    for (MeshVertex& v : vertices) {
        v.x = (v.x - cx) * scale;
        v.y = (v.y - cy) * scale;
        v.z = (v.z - cz) * scale;
    }
}

bool MeshLoader::IsSupported(const std::wstring& ext) {
    return ext == L"OBJ" || ext == L"STL" || ext == L"PLY" ||
           ext == L"OFF" || ext == L"GLB" || ext == L"GLTF";
}

std::unique_ptr<Mesh3D> MeshLoader::Load(const std::wstring& path) {
    const std::wstring ext = Upper(ExtensionOf(path));
    auto mesh = std::make_unique<Mesh3D>();
    bool ok = false;
    if (ext == L"OBJ") {
        ok = LoadOBJ(path, *mesh);
    } else if (ext == L"STL") {
        ok = LoadSTL(path, *mesh);
    } else if (ext == L"PLY") {
        ok = LoadPLY(path, *mesh);
    } else if (ext == L"OFF") {
        ok = LoadOFF(path, *mesh);
    } else if (ext == L"GLB") {
        ok = LoadGLB(path, *mesh);
    } else if (ext == L"GLTF") {
        ok = LoadGLTF(path, *mesh);
    }
    if (!ok || !mesh->valid()) {
        return nullptr;
    }
    mesh->Normalize();
    return mesh;
}

} // namespace pv
