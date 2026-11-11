# The -m options that change the ABI must also reach the LTO link: lto1 generates the code
# there, and without -mdouble=64 it treats double (and the math builtins such as sin) as
# 32 bit while the objects use 64 bit (avr-gcc 16 ICE in copy_to_mode_reg, issue #34).
Import("env")
abi = [f for f in env.ParseFlags(env.GetProjectOption("build_flags"))["CCFLAGS"]
       if f.startswith(("-mdouble=", "-mlong-double="))]
env.Append(LINKFLAGS=abi)
# one LTO partition: the firmware is small, and it avoids the "serial compilation" notice
env.Append(LINKFLAGS=["-flto-partition=one"])
