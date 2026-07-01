-- Round-trip test for the cart PNG encoder/decoder.
target("cartpng_roundtrip")
    set_kind("binary")
    set_languages("cxx23")
    add_files("cartpng_roundtrip.cpp")
    add_deps("lazy100-static")
target_end()
