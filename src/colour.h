#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace thoom {

union Colour {
  constexpr explicit Colour(uint32_t val) : val(val) {}

  Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
  }

  struct {
    uint8_t a, b, g, r;
  };
  uint32_t val;
};

static_assert(sizeof(Colour) == 4);
static_assert(offsetof(Colour, a) == 0);
static_assert(offsetof(Colour, b) == 1);
static_assert(offsetof(Colour, g) == 2);
static_assert(offsetof(Colour, r) == 3);
static_assert(std::endian::native == std::endian::little);

constexpr Colour kMask{0x00000000};
constexpr Colour kBlack{0x000000ff};
constexpr Colour kRed{0xed333bff};
constexpr Colour kGreen{0x57e389ff};
constexpr Colour kOrange{0xff7800ff};
constexpr Colour kBlue{0x62a0eaff};
constexpr Colour kMagenta{0x9141acff};
constexpr Colour kCyan{0x5bc8afff};
constexpr Colour kGrey{0xc0c0c0ff};
constexpr Colour kBrightBlack{0x808080ff};
constexpr Colour kBrightRed{0xf66151ff};
constexpr Colour kBrightGreen{0x8ff0a4ff};
constexpr Colour kYellow{0xffa348ff};
constexpr Colour kBrightBlue{0x99c1f1ff};
constexpr Colour kBrightMagenta{0xdc8addff};
constexpr Colour kBrightCyan{0x93ddc2ff};
constexpr Colour kWhite{0xffffffff};

};  // namespace thoom
