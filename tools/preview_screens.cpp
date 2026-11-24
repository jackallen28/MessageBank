// Draws MessageBox's e-paper screens on a PC, with the firmware's own drawing code
// (../MessageBox/screen.h), so the layout can be checked without the hardware.
//   g++ -std=c++17 -DHOST_PREVIEW -Ihost -I../MessageBox -I<Arduino>/libraries/Adafruit_GFX_Library \
//       preview_screens.cpp -o preview && ./preview out_dir
// Writes one .pgm per screen; make_previews.py turns them into one picture.
#include "Arduino.h"
#include "Adafruit_GFX.h"
FakeGFX epd;
#include "screen.h"

static void save(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.pgm", dir, name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P5 %d %d 255\n", epd.W, epd.H);
  for (uint8_t p : epd.px) fputc(p ? 0x20 : 0xF2, f);
  fclose(f);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  for (int rot : {0, 1}) {
    cfg.rotation = rot;
    String sfx = rot ? "_landscape" : "";
    View v;
    v.scr = SCR_ENVELOPE; v.count = 3;
    drawView(v); save(dir, ("1_envelope" + sfx).c_str());

    View r;
    r.scr = SCR_READ; r.from = "Alex"; r.when = "Wed 18 Nov 21:14"; r.pos = 1; r.total = 3;
    r.lines = layoutMessage("Thinking of you",
      "Hey you! I hope your day went well. I saw a dog that looked exactly like Biscuit and it made me "
      "miss you heaps.\n\nDinner on Friday? I'll cook. Bring the good chocolate and that playlist you made. "
      "Also, don't forget we promised your mum a call on Sunday afternoon.\n\nLove you <3");
    drawView(r); save(dir, ("2_read_top" + sfx).c_str());
    r.scroll = maxScroll(r.lines);
    drawView(r); save(dir, ("3_read_end" + sfx).c_str());

    View s = r;
    s.lines = layoutMessage("", "Love you <3");
    s.scroll = 0;
    drawView(s); save(dir, ("4_read_short" + sfx).c_str());

    View d;
    d.scr = SCR_DECIDE; d.from = "Alex"; d.title = "Thinking of you"; d.pos = 1; d.total = 3; d.choice = 0;
    drawView(d); save(dir, ("5_decide" + sfx).c_str());
    d.choice = -1; drawView(d); save(dir, ("6_decide_delete" + sfx).c_str());
    d.choice = 1; drawView(d); save(dir, ("7_decide_save" + sfx).c_str());

    View h; h.scr = SCR_HAPPY;
    drawView(h); save(dir, ("8_happy" + sfx).c_str());

    for (int f = 0; f < ARRIVE_FRAMES; f++) {
      View a; a.scr = SCR_ARRIVE; a.frame = f;
      drawView(a); save(dir, ("a" + String(f) + "_arrive" + sfx).c_str());
    }
    View n; n.scr = SCR_NOMSG; n.savedCount = 4;
    drawView(n); save(dir, ("b_nomsg" + sfx).c_str());
    View c; c.scr = SCR_CAROUSEL; c.from = "Alex"; c.when = "27/11/26"; c.title = "Thinking of you"; c.pos = 2; c.total = 4; c.sentReaction = 0;
    drawView(c); save(dir, ("c_carousel" + sfx).c_str());
    View rr; rr.scr = SCR_REACT; rr.from = "Alex"; rr.pos = 1; rr.total = 3; rr.reaction = 1; rr.sentReaction = 0;
    drawView(rr); save(dir, ("d_react" + sfx).c_str());
    View se; se.scr = SCR_SENT; se.from = "Alex"; se.reaction = 0; se.replyQueued = true;
    drawView(se); save(dir, ("e_sent" + sfx).c_str());
    View m; m.scr = SCR_MENU; m.menuSel = 3; m.menuEditing = true;
    m.menu = { {"Lights", "On"}, {"Buzz", "On"}, {"Bedtime", "On"}, {"Quiet from", "22:00"}, {"Quiet until", "07:00"},
               {"Change Wi-Fi", ""}, {"Shut down", ""}, {"Exit", ""} };
    drawView(m); save(dir, ("f_menu" + sfx).c_str());
    View sl; sl.scr = SCR_SLEEP;
    drawView(sl); save(dir, ("g_sleep" + sfx).c_str());
    View en; en.scr = SCR_ENVELOPE; en.count = 2;
    drawView(en); save(dir, ("1_envelope" + sfx).c_str());
  }  return 0;
}
