#!/bin/python3
import click
from PIL import Image, ImagePalette, ImageOps
from pathlib import Path
import numpy as np

@click.command()
@click.argument('filename', type=str)
@click.argument('out_name', type=str, default=None)
def main(filename, out_name):
    filename = Path(filename)
    if out_name is None:
        outPngFile = filename.with_name(filename.stem + "C.png")
        outputRawFile = filename.with_name(filename.stem + "Raw.bin")
    else:
        outputRawFile = Path(out_name)
        outPngFile = outputRawFile.with_name(outputRawFile.stem + "C.png")

    img = Image.open(filename)
    imgW, imgH = img.size
    targW, targH = 800, 480

    # Computed scaling
    scale_ratio = min(targW / imgW, targH / imgH)

    # Calculate the size after scaling
    resized_width = int(imgW * scale_ratio)
    resized_height = int(imgH * scale_ratio)

    # Resize image
    output_image = img.resize((resized_width, resized_height))

    # Create the target image and center the resized image
    resized_image = Image.new('RGB', (targW, targH), (255, 255, 255))
    left = (targW - resized_width) // 2
    top = (targH - resized_height) // 2
    resized_image.paste(output_image, (left, top))

    # Create a palette object
    pal_image = Image.new("P", (1, 1))
    # pallette from Waveshare example, which doesn't match the actual colors
    pal_image.putpalette(
            (0,0,0,         # black
            255,255,255,    # white
            255,255,0,      # yellow
            255,0,0,        # red
            0,0,255,        # blue
            0,255,0))       # green
    
    # pallette measured through my camera. of course it will depend on lightness (I don't have a true color reference sheet yet),
    # so experimentation may be needed
    # pal_image.putpalette((
    #     0,0,41,                 # black
    #     142,153,161,                 # white
    #     173,157,28,                  # yellow
    #     127,6,3,                    # red
    #     0,52,136,                      # blue
    #     15,86,75))                    # green
    pal_image.putpalette((
        0,0,42,                 # black
        198,214,224,                 # white
        242,220,8,                  # yellow
        176,0,0,                    # red
        0,64,190,                      # blue
        0,118,101))                    # green

    # The color quantization and dithering algorithms are performed, and the results are converted to RGB mode
    display_dither = Image.Dither(Image.FLOYDSTEINBERG)
    quantized_image = resized_image.quantize(colors=6, method=Image.Quantize.MEDIANCUT, 
                                             dither=display_dither, palette=pal_image)
    

    index_to_enum = [0, 1, 2, 3, 5, 6]

    lut = np.zeros(256)
    for i, v in enumerate(index_to_enum):
        lut[i] = v

    coded = quantized_image.point(lut, mode="L")  # "L" image where each pixel is your enum value
    print(quantized_image.getpixel((400, 100)))
    print(coded.getpixel((400, 100)))
    # print(f'Successfully converted {filename} to {outputName}')

    data = coded.tobytes()

    packed = bytearray((len(data) + 1) // 2)

    for i in range(0, len(data), 2):
        lo = data[i] & 0x0F
        hi = (data[i + 1] & 0x0F) << 4 if i + 1 < len(data) else 0
        packed[i // 2] = lo | hi

    outputRawFile.write_bytes(packed)

    # Save output image
    quantConv = quantized_image.convert('RGB')
    quantized_image.save(outPngFile)



if __name__ == "__main__":
    main()