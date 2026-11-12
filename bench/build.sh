#!/usr/bin/env bash
set -euo pipefail
python3 bench/prepare.py
for variant in baseline patched; do
  patched=0
  if [ "$variant" = patched ]; then patched=1; fi
  gcc -O3 -flto=auto -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -Wformat -Werror=format-security -pthread -std=c11 -D_GNU_SOURCE \
    -DPCRE2_CODE_UNIT_WIDTH=8 -DPATCHED="$patched" \
    -I"$variant/lib" -I"$variant/include" -Ibench \
    $(pkg-config --cflags glib-2.0 json-glib-1.0 openssl libpcre2-8) \
    bench/bench.c "$variant/lib/bencode.c" -o "bench/$variant" \
    $(pkg-config --libs glib-2.0) -lm
  "bench/$variant" --verify
done
