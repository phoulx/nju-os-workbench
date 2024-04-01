from PIL import Image
from pathlib import Path

IMAGE_PATH = Path('bluey.jpg')

data = []

def get_image_pixels(image_path):
    img = Image.open(image_path)
    pixels = img.load() # create the pixel map

    width, height = img.size
    for i in range(height): # for every row:
        row = []
        for j in range(width): # for every column:
            r, g, b = pixels[j, i]
            color = r << 16 | g << 8 | b
            row.append(color)
        data.append(row)
    return width, height

width, height = get_image_pixels(IMAGE_PATH)

with open(IMAGE_PATH.with_suffix('.c'), 'w') as f:
    f.write(f"int image_width = {width};\n")
    f.write(f"int image_height = {height};\n")
    f.write("unsigned int image_data[] = {\n")
    for row in data:
        height -= 1
        f.write('  ' + ', '.join([f"0x{x:08x}" for x in row]) + (',\n' if height else '\n'))
    f.write("};\n")
