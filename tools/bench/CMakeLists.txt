add_library(thoom_bench OBJECT
  bench.cc)

target_compile_features(thoom_bench PRIVATE cxx_std_20)
