// The messages, kept in flash (LittleFS, /msgs.dat) so new and saved ones survive a power cut.
#pragma once
#include <LittleFS.h>
#include <vector>
#include "config.h"

enum MsgState : uint8_t { MSG_NEW = 0, MSG_SAVED = 1 };

struct Message {
  uint32_t id;
  uint8_t state;
  String from, subject, body, when;
};

std::vector<Message> store;  // oldest first
uint32_t nextMsgId = 1;
bool fsOk = false;

// Fields are split by 0x1F and messages by 0x1E: the mail text never holds those characters.
void storeSave() {
  if (!fsOk) return;
  File f = LittleFS.open("/msgs.tmp", "w");
  if (!f) { logf("Store: can't write"); return; }
  f.printf("MB1\x1f%lu\x1e", (unsigned long)nextMsgId);
  for (const Message &m : store) {
    f.printf("%lu\x1f%u\x1f", (unsigned long)m.id, m.state);
    f.print(m.from); f.write(0x1F);
    f.print(m.subject); f.write(0x1F);
    f.print(m.when); f.write(0x1F);
    f.print(m.body); f.write(0x1E);
  }
  f.close();
  LittleFS.remove("/msgs.dat");
  LittleFS.rename("/msgs.tmp", "/msgs.dat");  // a power cut mid-write leaves the old file whole
}

void storeLoad() {
  fsOk = LittleFS.begin(true);  // formats the partition the very first time
  if (!fsOk) { logf("Store: no flash file system, messages won't survive a restart"); return; }
  File f = LittleFS.open("/msgs.dat", "r");
  if (!f) return;
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
    store.push_back(m);
    if (m.id >= nextMsgId) nextMsgId = m.id + 1;
  }
  f.close();
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

uint32_t storeAdd(const String &from, const String &subject, const String &body, const String &when) {
  if ((int)store.size() >= MAX_MSGS) {  // full: drop the oldest saved one, else the oldest of all
    int drop = 0;
    for (int i = 0; i < (int)store.size(); i++) if (store[i].state == MSG_SAVED) { drop = i; break; }
    logf("Store full, dropping '%s'", store[drop].subject.c_str());
    store.erase(store.begin() + drop);
  }
  Message m{ nextMsgId++, MSG_NEW, from, subject, body, when };
  store.push_back(m);
  storeSave();
  return m.id;
}

void storeDelete(uint32_t id) {
  int i = findMsg(id);
  if (i < 0) return;
  store.erase(store.begin() + i);
  storeSave();
}

void storeSetState(uint32_t id, uint8_t s) {
  int i = findMsg(id);
  if (i < 0) return;
  store[i].state = s;
  storeSave();
}

void storeClear() {
  store.clear();
  storeSave();
}
