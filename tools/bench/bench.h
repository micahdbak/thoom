#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct BenchOptions {
  uint32_t messages = 1000;
  uint32_t min_bytes = 1;
  uint32_t max_bytes = 1024;
  uint64_t seed = 1;
  bool loopback = false;
};

int parse_bench_options(int argc, char** argv, BenchOptions* options);

void print_bench_options(const std::string& name, const BenchOptions& options);

class BenchReport {
 public:
  void pass(const std::string& what);
  void fail(const std::string& what);
  bool check(bool condition, const std::string& what);
  int finish(const std::string& name);

 private:
  uint32_t passed = 0;
  uint32_t failed = 0;
};

uint64_t bench_random(uint64_t* state);

std::vector<uint8_t> make_payload(uint64_t seed, uint32_t index,
                                  const BenchOptions& options);

bool match_payload(const uint8_t* data, uint32_t size, uint64_t seed,
                   const BenchOptions& options, uint32_t* index);

int64_t bench_now_ms();
