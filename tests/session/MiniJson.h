// SPDX-License-Identifier: GPL-3.0-or-later
//
// 测试用的最小 JSON 读取：只为核对快照的键名与顺序、取几个值。不是通用解析器（不处理转义与 Unicode）。
#pragma once

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace miciri::testing
{
struct JsonNode
{
    enum class Kind
    {
        Object,
        Array,
        String,
        Number,
        Bool,
        Null
    };
    Kind kind = Kind::Null;
    std::vector<std::pair<std::string, JsonNode>> members; // 按出现顺序
    std::vector<JsonNode> items;
    std::string text;
    double number = 0.0;
    bool flag = false;

    const JsonNode& operator[](const std::string& key) const
    {
        for (const auto& m : members)
            if (m.first == key)
                return m.second;
        throw std::runtime_error("missing key " + key);
    }
    bool has(const std::string& key) const
    {
        for (const auto& m : members)
            if (m.first == key)
                return true;
        return false;
    }
    std::vector<std::string> keys() const
    {
        std::vector<std::string> out;
        for (const auto& m : members)
            out.push_back(m.first);
        return out;
    }
};

class JsonReader
{
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    JsonNode parse()
    {
        JsonNode n = value();
        skip();
        if (i_ != s_.size())
            throw std::runtime_error("trailing characters");
        return n;
    }

private:
    void skip()
    {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])))
            ++i_;
    }
    void expect(char c)
    {
        skip();
        if (i_ >= s_.size() || s_[i_] != c)
            throw std::runtime_error(std::string("expected ") + c);
        ++i_;
    }
    std::string string()
    {
        expect('"');
        const std::size_t end = s_.find('"', i_);
        if (end == std::string::npos)
            throw std::runtime_error("unterminated string");
        std::string out = s_.substr(i_, end - i_);
        i_ = end + 1;
        return out;
    }
    JsonNode value()
    {
        skip();
        JsonNode n;
        const char c = i_ < s_.size() ? s_[i_] : '\0';
        if (c == '{')
        {
            n.kind = JsonNode::Kind::Object;
            ++i_;
            skip();
            if (s_[i_] == '}')
                return ++i_, n;
            for (;;)
            {
                std::string k = string();
                expect(':');
                n.members.emplace_back(std::move(k), value());
                skip();
                if (s_[i_] == ',')
                    ++i_;
                else
                    return expect('}'), n;
            }
        }
        if (c == '[')
        {
            n.kind = JsonNode::Kind::Array;
            ++i_;
            skip();
            if (s_[i_] == ']')
                return ++i_, n;
            for (;;)
            {
                n.items.push_back(value());
                skip();
                if (s_[i_] == ',')
                    ++i_;
                else
                    return expect(']'), n;
            }
        }
        if (c == '"')
        {
            n.kind = JsonNode::Kind::String;
            n.text = string();
            return n;
        }
        if (s_.compare(i_, 4, "true") == 0 || s_.compare(i_, 5, "false") == 0)
        {
            n.kind = JsonNode::Kind::Bool;
            n.flag = s_[i_] == 't';
            i_ += n.flag ? 4 : 5;
            return n;
        }
        if (s_.compare(i_, 4, "null") == 0)
        {
            i_ += 4;
            return n;
        }
        char* end = nullptr;
        n.kind = JsonNode::Kind::Number;
        n.number = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_)
            throw std::runtime_error("bad value");
        i_ = static_cast<std::size_t>(end - s_.c_str());
        return n;
    }

    const std::string& s_;
    std::size_t i_ = 0;
};
} // namespace miciri::testing
