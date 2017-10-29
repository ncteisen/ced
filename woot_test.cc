#include "woot.h"
#include "gtest/gtest.h"

namespace woot {
namespace testing {

TEST(String, NoOp) { String<char> s; }

TEST(String, MutateThenRender) {
  String<char> s;
  SitePtr site = Site::Make();
  auto r = s.Insert(site, 'a', String<char>::Begin());
  EXPECT_EQ(s.Render(), "");
  EXPECT_EQ(r.str.Render(), "a");
  s = r.str;
  r = s.Insert(site, 'b', r.command->id());
  EXPECT_EQ(r.str.Render(), "ab");
  s = r.str;
  auto rr = s.Remove(r.command->id());
  EXPECT_EQ(rr.str.Render(), "a");
}

}  // namespace testing
}  // namespace woot
