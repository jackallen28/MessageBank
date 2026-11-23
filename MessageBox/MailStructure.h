#pragma once
#include <Arduino.h>

// These limits bound RAM, transfer time and recursion for arbitrary MIME messages.
constexpr uint32_t MAIL_IMAGE_MAX_BYTES = 64 * 1024;
constexpr uint32_t MAIL_IMAGE_MAX_ENCODED = MAIL_IMAGE_MAX_BYTES * 4 / 3 + 3072;
constexpr uint8_t MAIL_IMAGE_CANDIDATES = 4;

struct MailPart {
  String section;
  String encoding;
  String charset;
  uint32_t size = 0;
  bool attachment = false;
};

struct MailStructure {
  MailPart plain, html;
  MailPart images[MAIL_IMAGE_CANDIDATES];
  uint8_t imageCount = 0;
  bool hadImage = false;

  void addImage(const MailPart &part) {
    if (imageCount < MAIL_IMAGE_CANDIDATES) {
      images[imageCount++] = part;
    } else if (part.attachment) {
      // Keep explicit attachments ahead of decorative inline images.
      for (int i = imageCount - 1; i >= 0; i--) {
        if (!images[i].attachment) { images[i] = part; break; }
      }
    }
  }
};

inline bool mailNumber(const String &text, size_t start, size_t end, uint32_t &number) {
  number = 0;
  if (start >= end) return false;
  for (size_t i = start; i < end; i++) {
    const char c = text[i];
    if (c < '0' || c > '9' || number > (UINT32_MAX - (c - '0')) / 10) return false;
    number = number * 10 + c - '0';
  }
  return true;
}

class MailStructureParser {
 public:
  MailStructureParser(const String &data, size_t offset) : data_(data), pos_(offset) {}

  void parse(MailStructure &result) { part("", 0, result); }

 private:
  const String &data_;
  size_t pos_;

  char peek() const { return data_[pos_]; }

  void skipSpace() {
    while (peek() == ' ' || peek() == '\r' || peek() == '\n' || peek() == '\t') ++pos_;
  }

  bool take(char expected) {
    skipSpace();
    if (peek() != expected) return false;
    ++pos_;
    return true;
  }

  // A BODYSTRUCTURE string can be quoted, or be a literal preceded by {length}\r\n.
  bool literal(String *value) {
    const int close = data_.indexOf('}', pos_ + 1);
    uint32_t length;
    if (close < 0 || !mailNumber(data_, pos_ + 1, close, length)) return false;
    size_t start = close + 1;
    if (data_[start] == '\r') ++start;
    if (data_[start] != '\n' || length > data_.length() - start - 1) return false;
    ++start;
    pos_ = start + length;
    if (value) *value = data_.substring(start, pos_);
    return true;
  }

  bool token(String *value = nullptr) {
    skipSpace();
    if (peek() == '"') {
      ++pos_;
      while (peek() && peek() != '"') {
        if (peek() == '\\' && pos_ + 1 < data_.length()) ++pos_;
        if (value) *value += peek();
        ++pos_;
      }
      if (peek() != '"') return false;
      ++pos_;
      return true;
    }
    if (peek() == '{') return literal(value);
    const size_t start = pos_;
    while (peek() && peek() != ' ' && peek() != '\r' && peek() != '\n' &&
           peek() != '\t' && peek() != ')' && peek() != '(') ++pos_;
    if (start == pos_) return false;
    if (value) *value = data_.substring(start, pos_);
    return true;
  }

  bool skipValue() { return take('(') ? skipList() : token(); }

  // Called immediately after reading a list's opening parenthesis.
  bool skipList() {
    unsigned depth = 1;
    while (depth) {
      if (take('(')) ++depth;
      else if (take(')')) --depth;
      else if (!token()) return false;
    }
    return true;
  }

  bool parameters(String &charset, String &name) {
    if (!take('(')) return token();
    while (!take(')')) {
      String key, value;
      if (!token(&key) || !token(&value)) return false;
      key.toUpperCase();
      if (key == "CHARSET") { value.toUpperCase(); charset = value; }
      if (key == "NAME" || key == "FILENAME") { value.toLowerCase(); name = value; }
    }
    return true;
  }

  bool part(const String &path, unsigned depth, MailStructure &result) {
    if (!take('(')) return false;
    if (depth >= 12) return skipList();
    skipSpace();
    if (peek() == '(') {
      unsigned index = 1;
      while (peek() == '(') {
        const String child = path.length() ? path + "." + String(index) : String(index);
        if (!part(child, depth + 1, result)) return false;
        ++index;
        skipSpace();
      }
      return skipList();  // multipart subtype and extension fields
    }

    String type, subtype, filename, size;
    MailPart info;
    if (!token(&type) || !token(&subtype) || !parameters(info.charset, filename) ||
        !token() || !token() || !token(&info.encoding) || !token(&size)) return false;
    type.toUpperCase();
    subtype.toUpperCase();
    info.encoding.toUpperCase();
    if (!mailNumber(size, 0, size.length(), info.size)) return false;

    // TEXT adds a line count before extensions. Skip encapsulated messages as a unit.
    if (type == "MESSAGE") return skipList();
    if (type == "TEXT" && !token()) return false;
    if (!take(')')) {
      if (!skipValue()) return false;  // content MD5, when present
      if (!take(')')) {
        if (take('(')) {
          String disposition, unusedCharset;
          if (!token(&disposition) || !parameters(unusedCharset, filename) || !take(')')) return false;
          disposition.toUpperCase();
          info.attachment = disposition == "ATTACHMENT";
        } else if (!skipValue()) return false;
        if (!skipList()) return false;
      }
    }

    if (type == "TEXT" && !info.attachment && info.size) {
      info.section = path.length() ? path : "TEXT";
      if (subtype == "PLAIN" && !result.plain.section.length()) result.plain = info;
      if (subtype == "HTML" && !result.html.section.length()) result.html = info;
    }

    const bool namedImage = type == "APPLICATION" && subtype == "OCTET-STREAM" &&
        (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || filename.endsWith(".png"));
    if (type == "IMAGE" || namedImage) result.hadImage = true;
    const bool supported = namedImage || (type == "IMAGE" &&
        (subtype == "JPEG" || subtype == "JPG" || subtype == "PNG"));
    const bool base64 = info.encoding == "BASE64";
    const bool binary = info.encoding == "BINARY" || info.encoding == "8BIT" || info.encoding == "7BIT";
    if (supported && info.size && ((base64 && info.size <= MAIL_IMAGE_MAX_ENCODED) ||
        (binary && info.size <= MAIL_IMAGE_MAX_BYTES))) {
      info.section = path.length() ? path : "1";
      result.addImage(info);
    }
    return true;
  }
};
