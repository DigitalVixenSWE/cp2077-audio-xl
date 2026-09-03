#ifndef AUDIOXL_JSONLITE_HPP
#define AUDIOXL_JSONLITE_HPP

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace AudioXLNS {

struct JsonValue {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::vector<JsonValue> arr;
  std::map<std::string, JsonValue> obj;

  bool IsObject() const { return kind == Kind::Object; }
  bool IsArray() const { return kind == Kind::Array; }
  bool IsString() const { return kind == Kind::String; }
  bool IsNumber() const { return kind == Kind::Number; }
  bool IsBool() const { return kind == Kind::Bool; }

  const JsonValue* Get(const std::string& aKey) const {
    if (kind != Kind::Object) return nullptr;
    auto it = obj.find(aKey);
    return it == obj.end() ? nullptr : &it->second;
  }
  std::string GetString(const std::string& aKey, const std::string& aDefault = "") const {
    const JsonValue* v = Get(aKey);
    return (v && v->kind == Kind::String) ? v->str : aDefault;
  }
  double GetNumber(const std::string& aKey, double aDefault) const {
    const JsonValue* v = Get(aKey);
    if (!v) return aDefault;
    if (v->kind == Kind::Number) return v->num;
    if (v->kind == Kind::String) {
      char* end = nullptr;
      const double d = std::strtod(v->str.c_str(), &end);
      return (end && *end == '\0') ? d : aDefault;
    }
    return aDefault;
  }
  bool GetBool(const std::string& aKey, bool aDefault) const {
    const JsonValue* v = Get(aKey);
    if (!v) return aDefault;
    if (v->kind == Kind::Bool) return v->b;
    if (v->kind == Kind::Number) return v->num != 0.0;
    if (v->kind == Kind::String) return v->str == "true" || v->str == "1";
    return aDefault;
  }
};

class JsonParser {
 public:
  
  static bool Parse(const std::string& aText, JsonValue& aOut, std::string& aError) {
    JsonParser p(aText);
    p.SkipWs();
    if (!p.ParseValue(aOut)) {
      aError = p.m_error + " at offset " + std::to_string(p.m_pos);
      return false;
    }
    p.SkipWs();
    if (p.m_pos != p.m_text.size()) {
      aError = "trailing content at offset " + std::to_string(p.m_pos);
      return false;
    }
    return true;
  }

 private:
  explicit JsonParser(const std::string& aText) : m_text(aText) {}

  const std::string& m_text;
  size_t m_pos = 0;
  std::string m_error;

  bool Fail(const std::string& aWhy) {
    if (m_error.empty()) m_error = aWhy;
    return false;
  }

  void SkipWs() {
    while (m_pos < m_text.size()) {
      const char c = m_text[m_pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++m_pos;
      } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
        while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
      } else {
        break;
      }
    }
  }

  bool ParseValue(JsonValue& aOut) {
    if (m_pos >= m_text.size()) return Fail("unexpected end");
    const char c = m_text[m_pos];
    if (c == '{') return ParseObject(aOut);
    if (c == '[') return ParseArray(aOut);
    if (c == '"') {
      aOut.kind = JsonValue::Kind::String;
      return ParseString(aOut.str);
    }
    if (c == 't' && m_text.compare(m_pos, 4, "true") == 0) {
      aOut.kind = JsonValue::Kind::Bool;
      aOut.b = true;
      m_pos += 4;
      return true;
    }
    if (c == 'f' && m_text.compare(m_pos, 5, "false") == 0) {
      aOut.kind = JsonValue::Kind::Bool;
      aOut.b = false;
      m_pos += 5;
      return true;
    }
    if (c == 'n' && m_text.compare(m_pos, 4, "null") == 0) {
      aOut.kind = JsonValue::Kind::Null;
      m_pos += 4;
      return true;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      const char* start = m_text.c_str() + m_pos;
      char* end = nullptr;
      aOut.kind = JsonValue::Kind::Number;
      aOut.num = std::strtod(start, &end);
      if (end == start) return Fail("bad number");
      m_pos += static_cast<size_t>(end - start);
      return true;
    }
    return Fail(std::string("unexpected character '") + c + "'");
  }

  bool ParseString(std::string& aOut) {
    ++m_pos;  
    while (m_pos < m_text.size()) {
      const char c = m_text[m_pos++];
      if (c == '"') return true;
      if (c == '\\') {
        if (m_pos >= m_text.size()) return Fail("bad escape");
        const char e = m_text[m_pos++];
        switch (e) {
          case '"': aOut += '"'; break;
          case '\\': aOut += '\\'; break;
          case '/': aOut += '/'; break;
          case 'b': aOut += '\b'; break;
          case 'f': aOut += '\f'; break;
          case 'n': aOut += '\n'; break;
          case 'r': aOut += '\r'; break;
          case 't': aOut += '\t'; break;
          case 'u': {
            if (m_pos + 4 > m_text.size()) return Fail("bad unicode escape");
            const unsigned cp = static_cast<unsigned>(std::strtoul(m_text.substr(m_pos, 4).c_str(), nullptr, 16));
            m_pos += 4;
            
            if (cp < 0x80) {
              aOut += static_cast<char>(cp);
            } else if (cp < 0x800) {
              aOut += static_cast<char>(0xC0 | (cp >> 6));
              aOut += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              aOut += static_cast<char>(0xE0 | (cp >> 12));
              aOut += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              aOut += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
          }
          default: return Fail("bad escape");
        }
      } else {
        aOut += c;
      }
    }
    return Fail("unterminated string");
  }

  bool ParseArray(JsonValue& aOut) {
    aOut.kind = JsonValue::Kind::Array;
    ++m_pos;
    SkipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == ']') {
      ++m_pos;
      return true;
    }
    while (true) {
      SkipWs();
      if (m_pos < m_text.size() && m_text[m_pos] == ']') {  
        ++m_pos;
        return true;
      }
      JsonValue v;
      if (!ParseValue(v)) return false;
      aOut.arr.push_back(std::move(v));
      SkipWs();
      if (m_pos >= m_text.size()) return Fail("unterminated array");
      if (m_text[m_pos] == ',') {
        ++m_pos;
        continue;
      }
      if (m_text[m_pos] == ']') {
        ++m_pos;
        return true;
      }
      return Fail("expected , or ] in array");
    }
  }

  bool ParseObject(JsonValue& aOut) {
    aOut.kind = JsonValue::Kind::Object;
    ++m_pos;
    SkipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == '}') {
      ++m_pos;
      return true;
    }
    while (true) {
      SkipWs();
      if (m_pos < m_text.size() && m_text[m_pos] == '}') {  
        ++m_pos;
        return true;
      }
      if (m_pos >= m_text.size() || m_text[m_pos] != '"') return Fail("expected key string");
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (m_pos >= m_text.size() || m_text[m_pos] != ':') return Fail("expected : after key");
      ++m_pos;
      SkipWs();
      JsonValue v;
      if (!ParseValue(v)) return false;
      aOut.obj[key] = std::move(v);
      SkipWs();
      if (m_pos >= m_text.size()) return Fail("unterminated object");
      if (m_text[m_pos] == ',') {
        ++m_pos;
        continue;
      }
      if (m_text[m_pos] == '}') {
        ++m_pos;
        return true;
      }
      return Fail("expected , or } in object");
    }
  }
};

}  

#endif  
