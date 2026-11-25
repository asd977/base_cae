from PIL import Image
import numpy as np

img = Image.open('C:/Users/123/.gemini/antigravity/brain/11391b0e-d82c-4787-99e7-387d82a25a01/scratch/crop_viewport.png')
arr = np.array(img)

# Detect the dots by finding pixels with high contrast or distinct color
h, w, c = arr.shape
mask = np.zeros((h, w), dtype=np.uint8)

# The dots are brownish/tan/dark: R is higher, G is moderate, B is low, whereas terrain is bright green/cyan
# Let's find pixels where (R > 130 and G < 180 and B < 100) or high laplacian
for y in range(1, h-1):
    for x in range(1, w-1):
        r, g, b = [int(v) for v in arr[y, x][:3]]
        # brown/yellow dot on green background
        if r > 110 and b < 90 and g > 80 and g < 180:
            mask[y, x] = 255

out_img = Image.fromarray(mask)
out_img.save('C:/Users/123/.gemini/antigravity/brain/11391b0e-d82c-4787-99e7-387d82a25a01/scratch/dots_extracted.png')
print("Saved dots_extracted.png, count of dot pixels:", np.sum(mask > 0))
