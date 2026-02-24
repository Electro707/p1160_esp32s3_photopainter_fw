# Memory Storage Management
This firmware uses the built-in NVS flash-base memory, and an external SD card on the board. The NVS is generally used for configurations (such as WiFi credentials), while the SD card is used for assets (images)

# NVS
todo: docs

# SD Card
The SD card is mainly used for image storing for now.
Right now only one folder shall exist in the SD card named `img`, where all "images" will be stored

## Image format
The image shall be a custom `.raw` format. Unlike what the name may imply, this is NOT a raw image file out of an SLR, rather a home-brew format. It's simply the frame buffer of the display directly stored, which is 4-bits (nibble) per pixel, for a total of 192000 bytes of data. Each nibble can be 0 to 3, 5, or 6.

Yes we could bit-pack a bit more (no pun intended) and reduce the file size to 144000 bytes, but because the display itself expects a nibble per pixel, the format shall stay. Plus 192kB isn't _that_ much in the context of SD cards