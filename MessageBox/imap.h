// Minimal IMAP client for Gmail (imap.gmail.com:993, TLS), plus the text decoding a mail needs:
// RFC 2047 headers (=?UTF-8?B?...?=), quoted-printable and base64 bodies, HTML stripping, and
// folding UTF-8 down to the ASCII the e-paper fonts can draw.
#pragma once
#include <WiFiClientSecure.h>
#include "certs.h"

// ---------------------------------------------------------------- text helpers

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static String decodeBase64(const String &in) {
  String out;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;  // '=', CR, LF and anything else
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((acc >> bits) & 0xFF);
    }
  }
  return out;
}

// Quoted-printable. underscoreIsSpace is for RFC 2047 "Q" words in headers.
static String decodeQP(const String &in, bool underscoreIsSpace) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '=' && i + 1 < in.length()) {
      if (in[i + 1] == '\r' || in[i + 1] == '\n') {  // soft line break
        i++;
        if (in[i] == '\r' && i + 1 < in.length() && in[i + 1] == '\n') i++;
        continue;
      }
      if (i + 2 < in.length()) {
        int h = hexVal(in[i + 1]), l = hexVal(in[i + 2]);
        if (h >= 0 && l >= 0) {
          out += (char)(h * 16 + l);
          i += 2;
          continue;
        }
      }
    }
    out += (underscoreIsSpace && c == '_') ? ' ' : c;
  }
  return out;
}

static String latin1ToUtf8(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = in[i];
    if (c < 0x80) out += (char)c;
    else {
      out += (char)(0xC0 | (c >> 6));
      out += (char)(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// "=?UTF-8?B?SGk=?= there" -> "Hi there"
static String decodeHeaderWords(const String &in) {
  String out;
  size_t i = 0;
  bool lastWasEncoded = false;
  while (i < in.length()) {
    int start = in.indexOf("=?", i);
    if (start < 0) {
      out += in.substring(i);
      break;
    }
    int q1 = in.indexOf('?', start + 2);
    int q2 = q1 < 0 ? -1 : in.indexOf('?', q1 + 1);
    int end = q2 < 0 ? -1 : in.indexOf("?=", q2 + 1);
    if (end < 0) {
      out += in.substring(i);
      break;
    }
    String between = in.substring(i, start);
    between.trim();
    if (!(lastWasEncoded && between.length() == 0)) out += in.substring(i, start);  // spaces between two encoded words are dropped
    String charset = in.substring(start + 2, q1);
    charset.toUpperCase();
    char enc = toupper(in[q1 + 1]);
    String text = in.substring(q2 + 1, end);
    String decoded = (enc == 'B') ? decodeBase64(text) : decodeQP(text, true);
    if (charset.startsWith("ISO-8859") || charset.startsWith("WINDOWS-125")) decoded = latin1ToUtf8(decoded);
    out += decoded;
    i = end + 2;
    lastWasEncoded = true;
  }
  return out;
}

// Turns UTF-8 into the plain ASCII the GFX fonts have: smart quotes, dashes, ellipsis, accents,
// and a few hearts and smileys. Anything else is dropped.
static String foldToAscii(const String &in) {
  String out;
  size_t i = 0;
  while (i < in.length()) {
    uint8_t c = in[i];
    if (c < 0x80) {
      if (c == '\t') out += ' ';
      else if (c >= 0x20 || c == '\n') out += (char)c;
      i++;
      continue;
    }
    uint32_t cp = 0;
    int n = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else { i++; continue; }
    if (i + n >= in.length()) break;  // cut off in the middle of a character
    for (int k = 1; k <= n; k++) cp = (cp << 6) | (in[i + k] & 0x3F);
    i += n + 1;
    switch (cp) {
      case 0x2018: case 0x2019: case 0x201B: case 0x2032: out += '\''; break;
      case 0x201C: case 0x201D: case 0x201E: case 0x2033: out += '"'; break;
      case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
      case 0x2026: out += "..."; break;
      case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: out += ' '; break;
      case 0x2022: case 0x00B7: out += '*'; break;
      case 0x2764: case 0x2665: case 0x1F496: case 0x1F497: case 0x1F498: case 0x1F499:
      case 0x1F49A: case 0x1F49B: case 0x1F49C: case 0x1F49D: case 0x1F495: case 0x1F493:
      case 0x1F970: case 0x1F60D: case 0x1F618: case 0x1F9E1: case 0x1F90D: case 0x1F5A4:
        out += "<3"; break;
      case 0x1F600: case 0x1F601: case 0x1F603: case 0x1F604: case 0x1F60A: case 0x263A:
      case 0x1F642: out += ":)"; break;
      case 0x1F602: case 0x1F923: out += ":D"; break;
      case 0x1F609: out += ";)"; break;
      case 0x1F622: case 0x1F62D: case 0x1F641: out += ":("; break;
      case 0x1F61B: case 0x1F61C: out += ":P"; break;
      case 0x1F44D: out += "(y)"; break;
      case 0x2728: case 0x2B50: case 0x1F31F: out += '*'; break;
      default:
        if (cp >= 0xC0 && cp <= 0xFF) {  // Latin-1 letters without their accents
          static const char *map = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
          out += map[cp - 0xC0];
        }
        // other symbols, emoji and the variation selectors (U+FE0F) are dropped
    }
  }
  return out;
}

// Very small HTML to text, for mails that have no plain-text part.
static String htmlToText(const String &html) {
  String out;
  bool inTag = false;
  String tag;
  int skipDepth = 0;  // inside <style> or <script>
  for (size_t i = 0; i < html.length(); i++) {
    char c = html[i];
    if (inTag) {
      if (c == '>') {
        inTag = false;
        tag.toLowerCase();
        if (tag.startsWith("style") || tag.startsWith("script")) skipDepth++;
        else if (tag.startsWith("/style") || tag.startsWith("/script")) { if (skipDepth) skipDepth--; }
        else if (tag.startsWith("br") || tag.startsWith("/p") || tag.startsWith("/div") || tag.startsWith("/li")) out += '\n';
      } else if (tag.length() < 12) tag += c;
      continue;
    }
    if (c == '<') { inTag = true; tag = ""; continue; }
    if (skipDepth) continue;
    if (c == '&') {
      int semi = html.indexOf(';', i);
      if (semi > 0 && semi - (int)i < 8) {
        String ent = html.substring(i + 1, semi);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos" || ent == "#39") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else out += ' ';
        i = semi;
        continue;
      }
    }
    if (c == '\r') continue;
    out += c;
  }
  return out;
}

// Keeps only what the sender typed: drops quoted replies ("On ... wrote:", "> ..."), the
// signature ("-- "), "Sent from my iPhone", extra blank lines and trailing spaces.
static String cleanBody(const String &in) {
  String out;
  int blank = 0;
  size_t pos = 0;
  while (pos <= in.length()) {
    int nl = in.indexOf('\n', pos);
    if (nl < 0) nl = in.length();
    String line = in.substring(pos, nl);
    pos = nl + 1;
    line.replace("\r", "");
    String t = line;
    t.trim();
    if (t.startsWith(">")) continue;
    if (line == "-- " || t == "--") break;
    if (t.startsWith("On ") && t.endsWith("wrote:")) break;
    if (t.startsWith("-----Original Message")) break;
    if (t.startsWith("Sent from my ") || t.startsWith("Get Outlook for")) continue;
    if (t.length() == 0) {
      if (out.length() == 0) continue;
      if (++blank > 1) continue;
    } else blank = 0;
    out += t;
    out += '\n';
    if (nl >= (int)in.length()) break;
  }
  out.trim();
  return out;
}

// "Alex Smith <alex@example.com>" -> name "Alex Smith", address "alex@example.com"
static void splitFrom(const String &from, String &name, String &addr) {
  int lt = from.indexOf('<'), gt = from.indexOf('>');
  if (lt >= 0 && gt > lt) {
    addr = from.substring(lt + 1, gt);
    name = from.substring(0, lt);
  } else {
    addr = from;
    name = "";
  }
  name.trim();
  if (name.startsWith("\"") && name.endsWith("\"") && name.length() >= 2) name = name.substring(1, name.length() - 1);
  addr.trim();
  if (name.length() == 0) {
    int at = addr.indexOf('@');
    name = at > 0 ? addr.substring(0, at) : addr;
  }
}

// The value of one header line ("From:", "Subject:") in an unfolded header block.
static String headerValue(const String &hdr, const char *name) {
  String low = hdr;
  low.toLowerCase();
  String key = "\n" + String(name) + ":";
  int p = ("\n" + low).indexOf(key);  // the block may start right at the header
  if (p < 0) return "";
  int start = p + key.length() - 1;   // index into hdr (the "\n" we added shifts by one)
  int end = hdr.indexOf("\r\n", start);
  if (end < 0) end = hdr.length();
  String v = hdr.substring(start, end);
  v.trim();
  return v;
}

// ---------------------------------------------------------------- BODYSTRUCTURE

// Finds the part to show: the first text/plain, else the first text/html.
struct BodyPick {
  String part;      // "TEXT" for a single-part mail, else "1", "1.1", ...
  String encoding;  // 7BIT, 8BIT, QUOTED-PRINTABLE, BASE64
  String charset;
  bool html = false;
  bool found = false;
  bool plain = false;
};

static void bsSkipSpace(const String &s, int &i) {
  while (i < (int)s.length() && s[i] == ' ') i++;
}

// Reads one token: a quoted string, an atom/NIL/number, or a whole (...) list as raw text.
static String bsToken(const String &s, int &i) {
  bsSkipSpace(s, i);
  if (i >= (int)s.length()) return "";
  if (s[i] == '"') {
    String out;
    i++;
    while (i < (int)s.length() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < (int)s.length()) i++;
      out += s[i++];
    }
    i++;
    return out;
  }
  if (s[i] == '(') {
    int depth = 0, start = i;
    bool q = false;
    for (; i < (int)s.length(); i++) {
      char c = s[i];
      if (q) { if (c == '\\') i++; else if (c == '"') q = false; continue; }
      if (c == '"') q = true;
      else if (c == '(') depth++;
      else if (c == ')' && --depth == 0) { i++; break; }
    }
    return s.substring(start, i);
  }
  int start = i;
  while (i < (int)s.length() && s[i] != ' ' && s[i] != ')' && s[i] != '(') i++;
  return s.substring(start, i);
}

static void bsParse(const String &s, int &i, const String &path, BodyPick &pick) {
  bsSkipSpace(s, i);
  if (i >= (int)s.length() || s[i] != '(') return;
  i++;
  bsSkipSpace(s, i);
  if (i < (int)s.length() && s[i] == '(') {  // multipart: children first, then the subtype etc.
    int n = 1;
    while (i < (int)s.length() && s[i] == '(') {
      bsParse(s, i, path.length() ? path + "." + String(n) : String(n), pick);
      n++;
      bsSkipSpace(s, i);
    }
  } else {  // a leaf: type subtype (params) id description encoding size ...
    String f[6];
    for (int k = 0; k < 6; k++) f[k] = bsToken(s, i);
    f[0].toUpperCase();
    f[1].toUpperCase();
    f[5].toUpperCase();
    bool isPlain = f[0] == "TEXT" && f[1] == "PLAIN";
    bool isHtml = f[0] == "TEXT" && f[1] == "HTML";
    if ((isPlain && !pick.plain) || (isHtml && !pick.found)) {
      pick.found = true;
      pick.plain = isPlain;
      pick.html = isHtml;
      pick.part = path.length() ? path : "TEXT";
      pick.encoding = f[5];
      String p = f[2];
      p.toUpperCase();
      int cs = p.indexOf("\"CHARSET\" \"");
      pick.charset = cs >= 0 ? p.substring(cs + 11, p.indexOf('"', cs + 11)) : "UTF-8";
    }
  }
  // skip whatever is left of this list
  int depth = 1;
  bool q = false;
  for (; i < (int)s.length(); i++) {
    char c = s[i];
    if (q) { if (c == '\\') i++; else if (c == '"') q = false; continue; }
    if (c == '"') q = true;
    else if (c == '(') depth++;
    else if (c == ')' && --depth == 0) { i++; break; }
  }
}

// ---------------------------------------------------------------- the IMAP session

struct MailItem {
  uint32_t uid;
  String fromName, fromAddr, subject, body;
};

class GmailImap {
 public:
  String error;  // last error, readable

  bool connect(const String &user, const String &appPassword) {
    error = "";
    tag = 0;
    client.setCACert(GMAIL_ROOT_CA);
    client.setTimeout(15);
    client.setHandshakeTimeout(20);
    if (!client.connect("imap.gmail.com", 993)) {
      char buf[100];
      int e = client.lastError(buf, sizeof(buf));
      error = "Can't reach imap.gmail.com (TLS " + String(e) + ": " + String(buf) + ")";
      return false;
    }
    String greet;
    if (!readLine(greet, 10000) || !greet.startsWith("* OK")) {
      error = "No greeting from Gmail";
      stop();
      return false;
    }
    String pass = appPassword;
    pass.replace(" ", "");  // app passwords are shown as "abcd efgh ijkl mnop"
    if (!command("LOGIN " + quote(user) + " " + quote(pass))) {
      error = "Login refused: " + error + " (use an app password, and turn on IMAP in Gmail)";
      stop();
      return false;
    }
    if (!command("SELECT INBOX")) {
      error = "SELECT INBOX failed: " + error;
      stop();
      return false;
    }
    return true;
  }

  // UIDs of unread mails, oldest first. allowFrom: comma separated addresses, empty = anyone.
  bool searchUnseen(const String &allowFrom, uint32_t *uids, int maxUids, int &count) {
    count = 0;
    String crit = "UNSEEN";
    String list[8];
    int n = 0;
    int pos = 0;
    while (n < 8 && pos < (int)allowFrom.length()) {
      int comma = allowFrom.indexOf(',', pos);
      if (comma < 0) comma = allowFrom.length();
      String a = allowFrom.substring(pos, comma);
      a.trim();
      if (a.length()) list[n++] = a;
      pos = comma + 1;
    }
    if (n > 0) {
      String from;
      for (int k = 0; k < n - 1; k++) from += "OR ";
      for (int k = 0; k < n; k++) from += "FROM " + quote(list[k]) + " ";
      from.trim();
      crit += " " + from;
    }
    String untagged;
    if (!command("UID SEARCH " + crit, &untagged)) {
      error = "SEARCH failed: " + error;
      return false;
    }
    int s = untagged.indexOf("* SEARCH");
    if (s < 0) return true;
    int e = untagged.indexOf('\n', s);
    String nums = untagged.substring(s + 8, e < 0 ? untagged.length() : e);
    nums.trim();
    pos = 0;
    while (pos < (int)nums.length() && count < maxUids) {
      int sp = nums.indexOf(' ', pos);
      if (sp < 0) sp = nums.length();
      uint32_t u = strtoul(nums.substring(pos, sp).c_str(), nullptr, 10);
      if (u) uids[count++] = u;
      pos = sp + 1;
    }
    return true;
  }

  bool fetch(uint32_t uid, MailItem &m) {
    m.uid = uid;
    String u = String(uid);
    // headers
    if (!command("UID FETCH " + u + " (BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])")) {
      error = "FETCH header failed: " + error;
      return false;
    }
    String hdr = literal;
    hdr.replace("\r\n ", " ");  // unfold long header lines
    hdr.replace("\r\n\t", " ");
    String from = headerValue(hdr, "from");
    m.subject = foldToAscii(decodeHeaderWords(headerValue(hdr, "subject")));
    m.subject.trim();
    from = decodeHeaderWords(from);
    splitFrom(from, m.fromName, m.fromAddr);
    m.fromName = foldToAscii(m.fromName);
    // which part holds the text
    String untagged;
    if (!command("UID FETCH " + u + " (BODYSTRUCTURE)", &untagged)) {
      error = "FETCH structure failed: " + error;
      return false;
    }
    BodyPick pick;
    int b = untagged.indexOf("BODYSTRUCTURE ");
    if (b >= 0) {
      int i = b + 14;
      bsParse(untagged, i, "", pick);
    }
    m.body = "";
    if (pick.found) {
      if (!command("UID FETCH " + u + " (BODY.PEEK[" + pick.part + "]<0.6000>)")) {
        error = "FETCH body failed: " + error;
        return false;
      }
      String raw = literal;
      if (pick.encoding == "BASE64") raw = decodeBase64(raw);
      else if (pick.encoding == "QUOTED-PRINTABLE") raw = decodeQP(raw, false);
      if (pick.charset.startsWith("ISO-8859") || pick.charset.startsWith("WINDOWS-125")) raw = latin1ToUtf8(raw);
      if (pick.html) raw = htmlToText(raw);
      m.body = cleanBody(foldToAscii(raw));
      if (m.body.length() > 900) m.body = m.body.substring(0, 900) + "...";
    }
    return true;
  }

  bool markSeen(uint32_t uid) {
    return command("UID STORE " + String(uid) + " +FLAGS (\\Seen)");
  }

  void logout() {
    if (client.connected()) command("LOGOUT");
    stop();
  }

  void stop() { client.stop(); }

 private:
  WiFiClientSecure client;
  int tag = 0;
  String literal;  // contents of the last {n} literal in a response

  static String quote(const String &s) {
    String o = "\"";
    for (size_t i = 0; i < s.length(); i++) {
      if (s[i] == '"' || s[i] == '\\') o += '\\';
      o += s[i];
    }
    return o + "\"";
  }

  bool readLine(String &line, uint32_t timeoutMs) {
    line = "";
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      int c = client.read();
      if (c < 0) {
        if (!client.connected() && !client.available()) return false;
        delay(1);
        continue;
      }
      if (c == '\n') {
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        return true;
      }
      if (line.length() < 4096) line += (char)c;
    }
    return false;
  }

  // Sends one tagged command, collects untagged lines (if wanted) and the last literal.
  bool command(const String &cmd, String *untagged = nullptr) {
    String t = "A" + String(++tag);
    literal = "";
    client.print(t + " " + cmd + "\r\n");
    while (true) {
      String line;
      if (!readLine(line, 20000)) {
        error = "no answer (timeout)";
        return false;
      }
      if (line.startsWith(t + " ")) {
        String st = line.substring(t.length() + 1);
        if (st.startsWith("OK")) return true;
        error = st;
        return false;
      }
      if (untagged && untagged->length() < 4096) {
        *untagged += line;
        *untagged += '\n';
      }
      if (line.endsWith("}")) {  // a literal of n bytes follows
        int open = line.lastIndexOf('{');
        if (open >= 0) {
          long n = line.substring(open + 1, line.length() - 1).toInt();
          literal = "";
          uint8_t buf[256];
          uint32_t t0 = millis();
          while (n > 0 && millis() - t0 < 20000) {
            int got = client.read(buf, n > (long)sizeof(buf) ? sizeof(buf) : n);
            if (got <= 0) { delay(1); continue; }
            if (literal.length() < 8000) literal.concat((const char *)buf, got);
            n -= got;
          }
        }
      }
    }
  }
};
