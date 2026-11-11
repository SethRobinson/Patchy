# The virtual pointer commands for live-scrub.sh: press on the Size label at X Y, 3000
# moves left and right of it (a 1 ms sleep after every PACE moves, 0 floods), a last move
# 200 px right of the press, then a release.
#   live-scrub-moves.py X Y PACE
import math
import sys

x, y, pace = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
print(f"move {x} {y}")
print("sleep 300")
print("press")
print("sleep 100")
for i in range(1, 3001):
    dx = int(240 * math.sin(i / 300.0)) + (i % 2)
    print(f"move {max(2, x + dx)} {y}")
    if pace and i % pace == 0:
        print("sleep 1")
print(f"move {x + 200} {y}")
print("release")
print("sleep 500")
