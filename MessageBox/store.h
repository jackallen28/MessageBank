// The messages, kept in flash (LittleFS, /msgs.dat) so new and saved ones survive a power cut.
#pragma once
#include <LittleFS.h>
#include <memory>
#include <vector>
#include "config.h"
#include "MailImage.h"

enum MsgState : uint8_t { MSG_NEW = 0, MSG_SAVED = 1 };

struct Message {
  uint32_t id;
  uint8_t state;
  String from, subject, body, when;
  bool hasPicture = false;
};

std::vector<Message> store;  // oldest first
uint32_t nextMsgId = 1;
bool fsOk = false;

String picturePath(uint32_t id) {
  char path[24];
  snprintf(path, sizeof(path), "/pictures/%08lx.bin", (unsigned long)id);
  return path;
}

bool storeSavePicture(uint32_t id, const MailImage &picture) {
  if (!fsOk || !picture.valid()) return false;
  const String path = picturePath(id);
  File f = LittleFS.open(path, "w", true);
  if (!f) return false;
  const uint8_t header[] = { 'M', 'B', 'P', '1', (uint8_t)picture.width, (uint8_t)picture.height };
  const bool written = f.write(header, sizeof(header)) == sizeof(header) &&
      f.write(picture.pixels.data(), picture.pixels.size()) == picture.pixels.size();
  f.close();
  if (!written) LittleFS.remove(path);
  return written;
}

std::shared_ptr<const MailImage> storeLoadPicture(uint32_t id) {
  if (!fsOk) return {};
  File f = LittleFS.open(picturePath(id), "r");
  uint8_t header[6];
  if (!f || f.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "MBP1", 4) != 0 ||
      !header[4] || !header[5] || header[4] > MAIL_IMAGE_MAX_WIDTH || header[5] > MAIL_IMAGE_MAX_HEIGHT) return {};
  const size_t size = ((header[4] + 7) / 8) * header[5];
  if (f.size() != sizeof(header) + size) return {};
  auto picture = std::make_shared<MailImage>();
  picture->width = header[4];
  picture->height = header[5];
  picture->pixels.resize(size);
  if (f.read(picture->pixels.data(), size) != size) return {};
  return picture;
}

void prunePictures() {
  File dir = LittleFS.open("/pictures");
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> obsolete;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const String path = f.path();
    bool keep = false;
    for (const Message &m : store) {
      if (m.hasPicture && picturePath(m.id) == path) { keep = true; break; }
    }
    if (!keep) obsolete.push_back(path);
  }
  dir.close();
  for (const String &path : obsolete) LittleFS.remove(path);
}

// Fields are split by 0x1F and messages by 0x1E: the mail text never holds those characters.
bool storeSave() {
  if (!fsOk) return false;
  File f = LittleFS.open("/msgs.tmp", "w");
  if (!f) { logf("Store: can't write"); return false; }
  bool written = f.printf("MB1\x1f%lu\x1e", (unsigned long)nextMsgId) > 0;
  for (const Message &m : store) {
    written = written && f.printf("%lu\x1f%u\x1f", (unsigned long)m.id, m.state) > 0 &&
        f.print(m.from) == m.from.length() && f.write(0x1F) == 1 &&
        f.print(m.subject) == m.subject.length() && f.write(0x1F) == 1 &&
        f.print(m.when) == m.when.length() && f.write(0x1F) == 1 &&
        f.print(m.body) == m.body.length() && f.write(0x1E) == 1;
  }
  f.close();
  // LittleFS replaces the destination atomically, so a power cut leaves a complete file.
  if (written && LittleFS.rename("/msgs.tmp", "/msgs.dat")) return true;
  LittleFS.remove("/msgs.tmp");
  logf("Store: could not save messages");
  return false;
}

void storeLoad() {
  fsOk = LittleFS.begin(true);  // formats the partition the very first time
  if (!fsOk) { logf("Store: no flash file system, messages won't survive a restart"); return; }
  File f = LittleFS.open("/msgs.dat", "r");
  if (!f) { prunePictures(); return; }
  String head = f.readStringUntil(0x1E);
  if (!head.startsWith("MB1\x1f")) { f.close(); return; }
  nextMsgId = strtoul(head.c_str() + 4, nullptr, 10);
  while (f.available() && (int)store.size() < MAX_MSGS) {
    String rec = f.readStringUntil(0x1E);
    String part[6];
    int p = 0, start = 0;
    for (int i = 0; i <= (int)rec.length() && p < 6; i++) {
      if (i == (int)rec.length() || (rec[i] == 0x1F && p < 5)) {
        part[p++] = rec.substring(start, i);
        start = i + 1;
      }
    }
    if (p < 6) continue;
    Message m{ (uint32_t)strtoul(part[0].c_str(), nullptr, 10), (uint8_t)part[1].toInt(), part[2], part[3], part[5], part[4] };
    m.hasPicture = LittleFS.exists(picturePath(m.id));
    store.push_back(m);
    if (m.id >= nextMsgId) nextMsgId = m.id + 1;
  }
  f.close();
  prunePictures();
  logf("Store: %d message(s)", (int)store.size());
}

int countState(uint8_t s) {
  int n = 0;
  for (const Message &m : store) if (m.state == s) n++;
  return n;
}

int findMsg(uint32_t id) {
  for (int i = 0; i < (int)store.size(); i++) if (store[i].id == id) return i;
  return -1;
}

uint32_t storeAdd(const String &from, const String &subject, const String &body, const String &when,
                  const MailImage *picture = nullptr) {
  uint32_t obsoletePicture = 0;
  if ((int)store.size() >= MAX_MSGS) {  // full: drop the oldest saved one, else the oldest of all
    int drop = 0;
    for (int i = 0; i < (int)store.size(); i++) if (store[i].state == MSG_SAVED) { drop = i; break; }
    logf("Store full, dropping '%s'", store[drop].subject.c_str());
    if (store[drop].hasPicture) obsoletePicture = store[drop].id;
    store.erase(store.begin() + drop);
  }
  Message m{ nextMsgId++, MSG_NEW, from, subject, body, when };
  if (picture) {
    m.hasPicture = storeSavePicture(m.id, *picture);
    if (!m.hasPicture) logf("Store: could not save the picture");
  }
  store.push_back(m);
  if (storeSave() && obsoletePicture) LittleFS.remove(picturePath(obsoletePicture));
  return m.id;
}

void storeDelete(uint32_t id) {
  int i = findMsg(id);
  if (i < 0) return;
  const bool hasPicture = store[i].hasPicture;
  store.erase(store.begin() + i);
  if (storeSave() && hasPicture) LittleFS.remove(picturePath(id));
}

void storeSetState(uint32_t id, uint8_t s) {
  int i = findMsg(id);
  if (i < 0) return;
  store[i].state = s;
  storeSave();
}

void storeClear() {
  store.clear();
  if (storeSave()) prunePictures();
}
