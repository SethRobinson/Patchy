# Content-Aware Move real-photo fixtures

Crops of public-domain (CC0) photos from Wikimedia Commons, used by
`ui_content_aware_move_photo_fill_has_no_object_pixels` (tests/ui/brush_engine_stroke_tests_healing_patch.cpp)
to check Content-Aware Move on real photos ([content-aware-move.md](../../docs/content-aware-move.md)).
Each file is a JPEG crop (quality 90) of the 960 px Commons thumbnail scaled to 800 px on its longer side.
Object boxes and drop offsets are listed in the test.

| File | Source | Author | License | Crop of the 800 px image (x, y, right, bottom) |
|---|---|---|---|---|
| `beach-person.jpg` | [File:Person walking on windy beach (Unsplash).jpg](https://commons.wikimedia.org/wiki/File:Person_walking_on_windy_beach_(Unsplash).jpg) | Timothy Kolczak | CC0 | 150, 200, 650, 500 |
| `wall-sconce.jpg` | [File:Elegant Interior with Stylish Wall Sconce Lighting.jpg](https://commons.wikimedia.org/wiki/File:Elegant_Interior_with_Stylish_Wall_Sconce_Lighting.jpg) | Jedodo756 | CC0 | 120, 0, 480, 480 |

The wider real-photo comparison (`ui_content_aware_move_real_photo_sheets`) reads more photos, some
CC BY-SA, from the untracked `local-test-fixtures/content-aware-move` (its README lists them) and skips
when they are missing.
