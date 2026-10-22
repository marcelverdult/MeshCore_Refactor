# Links the native_companion test program against the AddressSanitizer runtime
# (companion_asan_probe.patch); build_flags only reach the compiler.
Import("env")
env.Append(LINKFLAGS=["-fsanitize=address"])
