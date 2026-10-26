# The virtual pointer commands for live-drag.sh: press on the text at X Y, 3000 moves on
# a wavy path (a 1 ms sleep after every PACE moves, 0 floods), then a release 50,20 off.
#   live-drag-moves.py X Y PACE
import math
import sys

x, y, pace = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
print(f"move {x} {y}")
print("sleep 300")
print("press")
print("sleep 100")
for i in range(1, 3001):
    dx = int(400 * math.sin(i / 300.0)) + (i % 2)
    dy = int(150 * math.sin(i / 170.0))
    print(f"move {x + dx} {y + dy}")
    if pace and i % pace == 0:
        print("sleep 1")
print(f"move {x + 50} {y + 20}")
print("release")
print("sleep 500")
