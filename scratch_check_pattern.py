from PIL import Image
import numpy as np

mask = np.array(Image.open('C:/Users/123/.gemini/antigravity/brain/11391b0e-d82c-4787-99e7-387d82a25a01/scratch/dots_extracted.png'))
# Find connected components or lines
y_coords, x_coords = np.where(mask > 0)
print(f"Total dot pixels: {len(x_coords)}")
# Let's inspect density along y
hist, _ = np.histogram(y_coords, bins=20)
print("Distribution along Y bins:", hist)
