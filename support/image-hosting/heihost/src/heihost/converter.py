from PIL import Image
import numpy as np
import sys


def png_to_raw_bitmap(png_file_path):
    with Image.open(png_file_path) as img:
        img = img.convert('L')
        width, height = img.size
        pixel_data = np.array(img)
        raw_data = pixel_data.flatten()
        return raw_data, width, height


def save_pgm_bitmap(raw_data, width, height, output_file):
    header = f"P5\n{width} {height}\n255\n".encode()
    with open(output_file, 'wb') as f:
        f.write(header)
        f.write(raw_data.tobytes())


def png_to_pgm(input_file, output_file):
    try:
        raw_data, width, height = png_to_raw_bitmap(input_file)
        save_pgm_bitmap(raw_data, width, height, output_file)
        print(f"Successfully converted {input_file} to PGM format.")
        print(f"Output saved as {output_file}")
        print(f"Image dimensions: {width}x{height}")
        print(f"Total pixels: {width * height}")
        print(f"Raw data size: {len(raw_data)} bytes")
    except Exception as e:
        print(f"An error occurred: {e}")
        sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print("Usage: png_to_pgm <input_png_file> <output_pgm_file>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    png_to_pgm(input_file, output_file)


if __name__ == "__main__":
    main()
