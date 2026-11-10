#include "bench.h"

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define PAYLOAD_INDEX_SIZE 4

static uint64_t mix(uint64_t seed, uint32_t index) {
  uint64_t value =
      seed + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(index) + 1);
  value ^= value >> 30;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27;
  value *= 0x94D049BB133111EBULL;
  value ^= value >> 31;
  return value;
}

static uint32_t body_size(uint64_t seed, uint32_t index,
                          const BenchOptions& options) {
  const uint32_t span = options.max_bytes - options.min_bytes + 1;
  return options.min_bytes + static_cast<uint32_t>(mix(seed, index) % span);
}

static void usage(const char* program) {
  std::fprintf(stderr,
               "usage: %s [--loopback] [--messages N] [--min-bytes N] "
               "[--max-bytes N] [--seed N]\n"
               "  --loopback     run only the in-process state machine checks\n"
               "  --messages N   messages per direction (default 1000)\n"
               "  --min-bytes N  smallest random body in bytes (default 1)\n"
               "  --max-bytes N  largest random body in bytes (default 1024)\n"
               "  --seed N       payload seed (default 1)\n",
               program);
}

static bool parse_u64(const char* text, uint64_t* value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }

  *value = parsed;
  return true;
}

int parse_bench_options(int argc, char** argv, BenchOptions* options) {
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 1;
    }

    if (arg == "--loopback") {
      options->loopback = true;
      continue;
    }

    if (i + 1 >= argc) {
      usage(argv[0]);
      return -1;
    }

    uint64_t value = 0;
    if (!parse_u64(argv[++i], &value)) {
      usage(argv[0]);
      return -1;
    }

    if (arg == "--messages") {
      if (value == 0 || value > UINT32_MAX) {
        usage(argv[0]);
        return -1;
      }
      options->messages = static_cast<uint32_t>(value);
    } else if (arg == "--min-bytes") {
      if (value > UINT32_MAX) {
        usage(argv[0]);
        return -1;
      }
      options->min_bytes = static_cast<uint32_t>(value);
    } else if (arg == "--max-bytes") {
      if (value > UINT32_MAX) {
        usage(argv[0]);
        return -1;
      }
      options->max_bytes = static_cast<uint32_t>(value);
    } else if (arg == "--seed") {
      options->seed = value;
    } else {
      usage(argv[0]);
      return -1;
    }
  }

  if (options->max_bytes < options->min_bytes) {
    std::fprintf(stderr, "--max-bytes must not be smaller than --min-bytes\n");
    return -1;
  }

  return 0;
}

void print_bench_options(const std::string& name, const BenchOptions& options) {
  std::printf("%s: seed=%" PRIu64 " messages=%u body=%u..%u%s\n", name.c_str(),
              options.seed, options.messages, options.min_bytes,
              options.max_bytes, options.loopback ? " loopback" : "");
}

void BenchReport::pass(const std::string& what) {
  this->passed++;
  std::printf("ok    %s\n", what.c_str());
}

void BenchReport::fail(const std::string& what) {
  this->failed++;
  std::printf("FAIL  %s\n", what.c_str());
}

bool BenchReport::check(bool condition, const std::string& what) {
  if (condition) {
    this->pass(what);
  } else {
    this->fail(what);
  }

  return condition;
}

int BenchReport::finish(const std::string& name) {
  std::printf("%s: %u passed, %u failed\n", name.c_str(), this->passed,
              this->failed);
  return this->failed == 0 ? 0 : 1;
}

uint64_t bench_random(uint64_t* state) {
  *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
  return *state;
}

// Deterministic in (seed, index), with the index in the first four bytes, so a
// receiver verifies a payload without being told which one it should be.
std::vector<uint8_t> make_payload(uint64_t seed, uint32_t index,
                                  const BenchOptions& options) {
  const uint32_t body = body_size(seed, index, options);
  std::vector<uint8_t> bytes(PAYLOAD_INDEX_SIZE + body);

  bytes[0] = static_cast<uint8_t>(index);
  bytes[1] = static_cast<uint8_t>(index >> 8);
  bytes[2] = static_cast<uint8_t>(index >> 16);
  bytes[3] = static_cast<uint8_t>(index >> 24);

  uint64_t value = mix(seed, index);
  for (uint32_t i = PAYLOAD_INDEX_SIZE; i < bytes.size(); i++) {
    value = value * 6364136223846793005ULL + 1442695040888963407ULL;
    bytes[i] = static_cast<uint8_t>(value >> 33);
  }

  return bytes;
}

bool match_payload(const uint8_t* data, uint32_t size, uint64_t seed,
                   const BenchOptions& options, uint32_t* index) {
  if (data == nullptr || size < PAYLOAD_INDEX_SIZE) {
    return false;
  }

  const uint32_t value = static_cast<uint32_t>(data[0]) |
                         (static_cast<uint32_t>(data[1]) << 8) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 24);

  const std::vector<uint8_t> expected = make_payload(seed, value, options);
  if (expected.size() != size) {
    return false;
  }

  if (std::memcmp(expected.data(), data, size) != 0) {
    return false;
  }

  if (index != nullptr) {
    *index = value;
  }

  return true;
}

int64_t bench_now_ms() {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::steady_clock;

  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}
