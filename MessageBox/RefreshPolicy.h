#pragma once
#include <stdint.h>

enum class DisplayPage : uint8_t { Blank, Setup, Pattern, Content };
enum class RefreshMode : uint8_t { Skip, Partial, Full };

// Full refreshes follow display events, never an update count or timer.
class RefreshPolicy {
 public:
  RefreshMode plan(DisplayPage page, bool requestedFull, uint8_t rotation) const {
    if (!initialized_ || requestedFull || rotation != rotation_ ||
        page == DisplayPage::Pattern || page_ == DisplayPage::Pattern) {
      return RefreshMode::Full;
    }
    if (page == DisplayPage::Blank) {
      return page_ == DisplayPage::Blank ? RefreshMode::Skip : RefreshMode::Full;
    }
    if (page == DisplayPage::Setup && page_ != DisplayPage::Setup) {
      return RefreshMode::Full;
    }
    return RefreshMode::Partial;
  }

  void commit(DisplayPage page, uint8_t rotation) {
    initialized_ = true;
    page_ = page;
    rotation_ = rotation;
  }

 private:
  bool initialized_ = false;
  DisplayPage page_ = DisplayPage::Blank;
  uint8_t rotation_ = 0;
};
