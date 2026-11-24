// Sends a quick-reply reaction from the box's Gmail account (smtp.gmail.com:465, TLS, the same
// app password as IMAP). The reply is threaded: "Re: <their subject>", In-Reply-To their
// Message-ID and References their chain plus that ID, so it lands in the same conversation in
// the sender's mail app. The email has the reaction's hand-drawn picture inline and a plain text
// version with an emoji.
#pragma once
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <esp_random.h>
#include <time.h>
#include <vector>
#include "certs.h"
#include "reactions.h"

// emoji for the plain-text part, in REACTIONS order
static const char *const REACTION_EMOJI[REACTION_COUNT] = {
  "\xE2\x9D\xA4\xEF\xB8\x8F",  // heart
  "\xF0\x9F\x98\x82",          // tears of joy
  "\xF0\x9F\x8C\x9F",          // glowing star
  "\xF0\x9F\x98\xAE",          // open mouth
  "\xE2\x9C\x85",              // check mark
  "\xF0\x9F\xA4\x94",          // thinking face
};

struct ReplyJob {
  uint32_t msgId;        // the message on the box
  int8_t reaction;
  String to, subject, inReplyTo, refs, toName;
  uint8_t tries = 0;
  uint32_t notBefore = 0;  // millis(): wait until then before trying again
};

static String b64(const uint8_t *data, size_t len) {
  size_t outLen = 0;
  mbedtls_base64_encode(nullptr, 0, &outLen, data, len);
  String out;
  if (!outLen) return out;
  char *buf = (char *)malloc(outLen + 1);
  if (!buf) return out;
  if (mbedtls_base64_encode((unsigned char *)buf, outLen, &outLen, data, len) == 0) {
    buf[outLen] = 0;
    out = buf;
  }
  free(buf);
  return out;
}
static String b64(const String &s) { return b64((const uint8_t *)s.c_str(), s.length()); }

// A header word that may hold UTF-8 (RFC 2047), split so no encoded word is too long.
static String encodeHeaderWord(const String &s) {
  bool ascii = true;
  for (size_t i = 0; i < s.length(); i++) if ((uint8_t)s[i] >= 0x80 || (uint8_t)s[i] < 0x20) ascii = false;
  if (ascii) return s;
  String out;
  size_t i = 0;
  while (i < s.length()) {
    size_t n = min((size_t)45, s.length() - i);
    while (i + n < s.length() && n > 1 && ((uint8_t)s[i + n] & 0xC0) == 0x80) n--;  // don't split a character
    if (out.length()) out += "\r\n ";
    out += "=?UTF-8?B?" + b64((const uint8_t *)s.c_str() + i, n) + "?=";
    i += n;
  }
  return out;
}

static String wrap76(const String &s) {
  String out;
  out.reserve(s.length() + s.length() / 76 * 2 + 4);
  for (size_t i = 0; i < s.length(); i += 76) out += s.substring(i, i + 76) + "\r\n";
  return out;
}

static String htmlEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

class GmailSmtp {
 public:
  String error;

  bool send(const String &user, const String &appPassword, const ReplyJob &job) {
    error = "";
    if (job.reaction < 0 || job.reaction >= REACTION_COUNT) { error = "no reaction"; return false; }
    if (job.to.indexOf('@') < 1 || job.to.indexOf('\r') >= 0 || job.to.indexOf('\n') >= 0 || job.to.indexOf('>') >= 0) {
      error = "no address to reply to";
      return false;
    }
    client.setCACert(GMAIL_ROOT_CA);
    client.setTimeout(15);
    client.setHandshakeTimeout(20);
    if (!client.connect("smtp.gmail.com", 465)) { error = "Can't reach smtp.gmail.com"; return false; }
    String pass = appPassword;
    pass.replace(" ", "");
    bool ok = expect(220) &&
        cmd("EHLO messagebox", 250) &&
        cmd("AUTH PLAIN " + authPlain(user, pass), 235) &&
        cmd("MAIL FROM:<" + user + ">", 250) &&
        cmd("RCPT TO:<" + job.to + ">", 250) &&
        cmd("DATA", 354);
    if (ok) {
      sendBody(user, job);
      ok = cmd(".", 250);
    }
    if (ok) cmd("QUIT", 221);
    client.stop();
    return ok;
  }

 private:
  WiFiClientSecure client;

  // "\0user\0password", base64 (a String can't hold the zero bytes, so build it by hand)
  static String authPlain(const String &user, const String &pass) {
    std::vector<uint8_t> raw;
    raw.push_back(0);
    raw.insert(raw.end(), user.c_str(), user.c_str() + user.length());
    raw.push_back(0);
    raw.insert(raw.end(), pass.c_str(), pass.c_str() + pass.length());
    return b64(raw.data(), raw.size());
  }

  // Reads one reply (all its lines); true if the code matches.
  bool expect(int code) {
    uint32_t t0 = millis();
    String line;
    while (millis() - t0 < 20000) {
      int c = client.read();
      if (c < 0) {
        if (!client.connected() && !client.available()) break;
        delay(1);
        continue;
      }
      if (c == '\r') continue;
      if (c != '\n') { if (line.length() < 400) line += (char)c; continue; }
      if (line.length() >= 4 && line[3] == '-') { line = ""; continue; }  // more lines follow
      if (line.toInt() == code) return true;
      error = line;
      return false;
    }
    error = "no answer from smtp.gmail.com";
    return false;
  }

  bool cmd(const String &c, int code) {
    client.print(c + "\r\n");
    if (expect(code)) return true;
    if (c.startsWith("AUTH")) error = "Login refused: " + error + " (check the app password)";
    return false;
  }

  void sendBody(const String &user, const ReplyJob &job) {
    const Reaction &r = REACTIONS[job.reaction];
    char rnd[20];
    snprintf(rnd, sizeof(rnd), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    const int at = user.indexOf('@');
    const String domain = at > 0 ? user.substring(at + 1) : String("messagebox.local");
    const String b1 = String("mb1_") + rnd, b2 = String("mb2_") + rnd;
    String subject = job.subject;
    String lower = subject;
    lower.toLowerCase();
    if (!lower.startsWith("re:")) subject = "Re: " + subject;

    String h;
    h.reserve(1200);
    h += "From: MessageBox <" + user + ">\r\n";
    h += "To: " + (job.toName.length() ? encodeHeaderWord(job.toName) + " " : String("")) + "<" + job.to + ">\r\n";
    h += "Subject: " + encodeHeaderWord(subject) + "\r\n";
    struct tm t;
    if (getLocalTime(&t, 10)) {
      char date[48];
      strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &t);
      h += "Date: " + String(date) + "\r\n";
    }
    h += "Message-ID: <mbx." + String(rnd) + "@" + domain + ">\r\n";
    if (job.inReplyTo.length()) {
      h += "In-Reply-To: " + job.inReplyTo + "\r\n";
      h += "References: " + (job.refs.length() ? job.refs + " " : String("")) + job.inReplyTo + "\r\n";
    }
    h += "MIME-Version: 1.0\r\n";
    h += "Content-Type: multipart/alternative; boundary=\"" + b1 + "\"\r\n\r\n";
    client.print(h);

    const String text = String(REACTION_EMOJI[job.reaction]) + "\r\n";  // just the symbol
    client.print("--" + b1 + "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n");
    client.print(wrap76(b64(text)));

    client.print("--" + b1 + "\r\nContent-Type: multipart/related; boundary=\"" + b2 + "\"\r\n\r\n");
    const String html =  // just the hand-drawn symbol
        "<div><img src=\"cid:reaction@messagebox\" width=\"120\" height=\"120\" alt=\"" +
        htmlEsc(String(REACTION_EMOJI[job.reaction])) + "\"></div>";
    client.print("--" + b2 + "\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n");
    client.print(wrap76(b64(html)));

    uint8_t *png = (uint8_t *)malloc(r.pngLen);
    if (png) {
      memcpy_P(png, r.png, r.pngLen);
      client.print("--" + b2 + "\r\nContent-Type: image/png; name=\"" + String(r.icon) + ".png\"\r\n"
                   "Content-Transfer-Encoding: base64\r\nContent-ID: <reaction@messagebox>\r\n"
                   "Content-Disposition: inline; filename=\"" + String(r.icon) + ".png\"\r\n\r\n");
      client.print(wrap76(b64(png, r.pngLen)));
      free(png);
    }
    client.print("--" + b2 + "--\r\n--" + b1 + "--\r\n");
  }
};
