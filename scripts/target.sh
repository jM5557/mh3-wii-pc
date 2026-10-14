# Sourced by the build and run scripts. MH3 targets arm32 (armhf), arm64 and
# x86-64 Linux with OpenGL ES. MH3_ARCH selects the target (default x86_64 on
# an x86-64 Linux machine, arm32 elsewhere). When the build machine's
# architecture differs, the scripts cross-compile with the matching toolchain
# file; scripts/docker-build.sh provides an arm32 cross environment. x86_64 is
# built natively only.
if [[ -z ${MH3_ARCH:-} && $(uname -s) == Linux && $(uname -m) == x86_64 ]]; then
  MH3_ARCH=x86_64
fi
MH3_ARCH=${MH3_ARCH:-arm32}
case $MH3_ARCH in
  arm32) mh3_native_arch='^(armv7l|armv8l|arm)$' ;;
  arm64) mh3_native_arch='^(aarch64|arm64)$' ;;
  x86_64) mh3_native_arch='^x86_64$' ;;
  *) echo "MH3_ARCH must be arm32, arm64 or x86_64, not $MH3_ARCH" >&2; exit 1 ;;
esac
# The toolchain files choose the compiler (arm32: clang-21, override with
# MH3_CLANG); native builds use MH3_CLANG or clang from PATH.
MH3_CROSS_ARGS=()
MH3_COMPILER_ARGS=()
# arm32 always uses its toolchain file, which also carries the Cortex-A9
# code-generation flags, so native armhf builds match cross builds.
if [[ $MH3_ARCH == x86_64 && ( $(uname -s) != Linux || ! $(uname -m) =~ $mh3_native_arch ) ]]; then
  echo "MH3_ARCH=x86_64 builds natively only; run this on an x86-64 Linux machine" >&2
  exit 1
fi
if [[ $MH3_ARCH == arm32 || $(uname -s) != Linux || ! $(uname -m) =~ $mh3_native_arch ]]; then
  MH3_CROSS_ARGS=(-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/linux-$MH3_ARCH.cmake")
else
  mh3_clang=${MH3_CLANG:-clang}
  MH3_COMPILER_ARGS=(-DCMAKE_C_COMPILER="$mh3_clang" -DCMAKE_CXX_COMPILER="${mh3_clang/clang/clang++}")
fi
MH3_BUILD_DIR=${MH3_BUILD_DIR:-build/linux-$MH3_ARCH}
