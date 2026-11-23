// From the repository root:
// g++ -std=c++17 -Itools/host -IMessageBox MessageBox-tests/mail_structure_tests.cpp -o /tmp/mail-structure-tests
// /tmp/mail-structure-tests
#include <cassert>
#include <cstdio>
#include "MailStructure.h"

static MailStructure parse(const char *body) {
  String input = body;
  MailStructure mail;
  MailStructureParser(input, 0).parse(mail);
  return mail;
}

int main() {
  {
    const auto mail = parse(R"(("text" "plain" ("CHARSET" "iso-8859-1") NIL NIL "8bit" 22 4 NIL NIL))");
    assert(mail.plain.section == "TEXT");
    assert(mail.plain.charset == "ISO-8859-1");
    assert(mail.plain.encoding == "8BIT");
    assert(!mail.hadImage);
  }

  {
    const auto mail = parse(R"(((("TEXT" "PLAIN" NIL NIL NIL "7BIT" 12 1)
      (("TEXT" "HTML" NIL NIL NIL "QUOTED-PRINTABLE" 40 2)
       ("IMAGE" "PNG" NIL "logo" NIL "BASE64" 164 NIL ("INLINE" NIL)) "RELATED") "ALTERNATIVE")
      ("IMAGE" "JPEG" ("NAME" "photo.jpg") NIL NIL "BASE64" 1024 NIL ("ATTACHMENT" NIL))
      ("TEXT" "PLAIN" ("NAME" "notes.txt") NIL NIL "7BIT" 20 1 NIL ("ATTACHMENT" NIL)) "MIXED"))");
    assert(mail.plain.section == "1.1");
    assert(mail.html.section == "1.2.1");
    assert(mail.hadImage && mail.imageCount == 2);
    assert(mail.images[0].section == "1.2.2" && !mail.images[0].attachment);
    assert(mail.images[1].section == "2" && mail.images[1].attachment);
  }

  {
    const auto mail = parse("(\"APPLICATION\" \"OCTET-STREAM\" NIL NIL NIL \"BINARY\" 255 NIL "
                            "(\"ATTACHMENT\" (\"FILENAME\" {11}\r\nmy pic.JPEG)))");
    assert(mail.hadImage && mail.imageCount == 1);
    assert(mail.images[0].section == "1" && mail.images[0].encoding == "BINARY");
    assert(mail.images[0].attachment && mail.images[0].size == 255);
  }

  {
    const auto mail = parse(R"((("TEXT" "PLAIN" NIL NIL NIL "7BIT" 21 1 NIL ("ATTACHMENT" NIL))
      ("TEXT" "PLAIN" NIL NIL NIL "7BIT" 0 0)
      ("TEXT" "HTML" NIL NIL NIL "7BIT" 26 1)
      ("IMAGE" "PNG" NIL NIL NIL "BASE64" 999999)
      ("IMAGE" "JPEG" NIL NIL NIL "BINARY" 65537)
      ("IMAGE" "GIF" NIL NIL NIL "BASE64" 200) "MIXED"))");
    assert(!mail.plain.section.length());
    assert(mail.html.section == "3");
    assert(mail.hadImage && mail.imageCount == 0);
  }

  {
    const auto mail = parse(R"((("IMAGE" "PNG" NIL NIL NIL "BASE64" 200)
      ("IMAGE" "PNG" NIL NIL NIL "BASE64" 200)
      ("IMAGE" "PNG" NIL NIL NIL "BASE64" 200)
      ("IMAGE" "PNG" NIL NIL NIL "BASE64" 200)
      ("IMAGE" "PNG" NIL NIL NIL "BASE64" 200 NIL ("ATTACHMENT" NIL)) "MIXED"))");
    assert(mail.imageCount == MAIL_IMAGE_CANDIDATES);
    assert(mail.images[3].section == "5" && mail.images[3].attachment);
  }

  assert(!parse("(\"IMAGE\" \"JPEG\" (\"NAME\" {99}\r\nfoo) NIL NIL \"BASE64\" 12)").imageCount);
  assert(!parse(R"(("IMAGE" "PNG" NIL NIL NIL "BASE64" 4294967296))").imageCount);
  puts("all mail structure tests passed");
}
