# Memory Storage Management
This firmware uses the built-in NVS flash-base memory, and an external SD card on the board. The NVS is generally used for configurations (such as WiFi credentials), while the SD card is used for assets (images)

# Memory Types
## NVS
todo: docs

## SD Card
The SD card is mainly used for image storing for now.
Right now only one folder shall exist in the SD card named `img`, where all "images" will be stored

# Image/Frame Buffer Format
An image frame buffer is a 192000 byte data block, each nibble (4-bits) describes one pixel color. Each nibble can be 0 to 3, 5, or 6.

The image file stored on the SDCard shall be the same format as the frame buffer with the `.raw` extension.

Yes we could bit-pack a bit more (no pun intended) and reduce the file size to 144000 bytes, but because the display itself expects a nibble per pixel, the format shall stay. Plus 192kB isn't _that_ much in the context of SD cards, This also allows easy loading of a buffer unto the display without any extra data mangling.

# Frame Buffers
The firmware has two frame buffers allocated: a display frame buffer, and an SDCard image framebuffer. This is setup to allow uploading of an image to the SD card without interfering with the display buffer, in case in the future there are background processes (such as a clock time) that needs access to the buffer.

The display frame buffer is used to manipulate the buffer that will be sent down to the display with the `/disp/update` command.

The SDCard image frame buffer is used to store uploaded images before writing to the card. Right now only the `/img/upload` command uses this buffer.