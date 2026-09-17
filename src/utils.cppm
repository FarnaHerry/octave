// utils.cppm — 纯字符串/字节工具（无 UI、无系统依赖），ectave.* 各层共用。
// 软换行、分类等需要字体度量或引擎状态的部分在 ectave.ui.utils / ectave.store。
export module ectave.utils;

import std;

export namespace ectave {

// 去首尾空白。
inline std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return {};
    }
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// 按 \n 拆行（顺带剥掉 \r）。空串返回单元素 {""}。
inline std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const auto nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            out.emplace_back(text.substr(start));
            break;
        }
        out.emplace_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    for (auto& line : out) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }
    return out;
}

// UTF-8 边界：pos 之后第一个码点的起始下标（越界返回 size）。
inline std::size_t utf8Next(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) {
        return s.size();
    }
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    int seq = 1;
    if ((c & 0x80) == 0) seq = 1;
    else if ((c & 0xE0) == 0xC0) seq = 2;
    else if ((c & 0xF0) == 0xE0) seq = 3;
    else if ((c & 0xF8) == 0xF0) seq = 4;
    return std::min(s.size(), pos + static_cast<std::size_t>(seq));
}

// pos 之前一个码点的起始下标。
inline std::size_t utf8Prev(const std::string& s, std::size_t pos) {
    if (pos == 0) {
        return 0;
    }
    std::size_t i = pos - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        --i;
    }
    return i;
}

} // namespace ectave
