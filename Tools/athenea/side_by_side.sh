#!/bin/sh
# Copyright (c) 2026 jesus luque.
# Two frames side by side, each with its label on a dark band (oiiotool):
#   side_by_side.sh LEFT.png "left label" RIGHT.png "right label" OUT.png
set -e
band() {
  oiiotool "$1" --ch R,G,B --fill:color=0,0,0 10000x56+0+0 \
           --text:x=16:y=40:size=30:color=1,1,1 "$2" -o "$3"
}
T=$(mktemp -d)
band "$1" "$2" "$T/l.png"
band "$3" "$4" "$T/r.png"
oiiotool "$T/l.png" "$T/r.png" --mosaic 2x1 -o "$5"
rm -rf "$T"
