#pragma once

#include <string>
#include <vector>
#include <cstdlib>

namespace eon::sdk {

/// <summary>
/// 极简 semver 版本号解析与范围匹配。
/// 格式支持：">=1.0.0", "<2.0.0", ">=1.5.0 <2.0.0", "1.0.0", "1.5"
/// </summary>
struct Semver {
    int major = 0;
    int minor = 0;
    int patch = 0;

    explicit Semver(const std::string& ver) {
        auto parts = split(ver, '.');
        if (parts.size() >= 1) major = std::atoi(parts[0].c_str());
        if (parts.size() >= 2) minor = std::atoi(parts[1].c_str());
        if (parts.size() >= 3) patch = std::atoi(parts[2].c_str());
    }

    int compare(const Semver& other) const {
        if (major != other.major) return major - other.major;
        if (minor != other.minor) return minor - other.minor;
        return patch - other.patch;
    }

    bool operator>=(const Semver& other) const { return compare(other) >= 0; }
    bool operator<=(const Semver& other) const { return compare(other) <= 0; }
    bool operator>(const Semver& other) const  { return compare(other) > 0; }
    bool operator<(const Semver& other) const  { return compare(other) < 0; }
    bool operator==(const Semver& other) const { return compare(other) == 0; }

    /// 检查版本是否在 semver 范围中，如 ">=1.5.0 <2.0.0"
    static bool matches(const std::string& version, const std::string& range) {
        Semver ver(version);
        auto conds = split(range, ' ');
        for (const auto& cond : conds) {
            if (cond.empty()) continue;
            if (cond.substr(0, 2) == ">=") {
                if (!(ver >= Semver(cond.substr(2)))) return false;
            } else if (cond.substr(0, 2) == "<=") {
                if (!(ver <= Semver(cond.substr(2)))) return false;
            } else if (cond[0] == '>') {
                if (!(ver > Semver(cond.substr(1)))) return false;
            } else if (cond[0] == '<') {
                if (!(ver < Semver(cond.substr(1)))) return false;
            } else if (cond[0] == '=') {
                if (!(ver == Semver(cond.substr(1)))) return false;
            } else {
                // 精确版本
                if (!(ver == Semver(cond))) return false;
            }
        }
        return true;
    }

private:
    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> parts;
        size_t start = 0, end;
        while ((end = s.find(delim, start)) != std::string::npos) {
            parts.push_back(s.substr(start, end - start));
            start = end + 1;
        }
        parts.push_back(s.substr(start));
        return parts;
    }
};

} // namespace eon::sdk
