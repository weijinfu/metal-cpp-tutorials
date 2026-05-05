# GPT Image Diagram Prompts

These prompts are used to redraw the current SVG illustration into a more educational PNG. It is recommended that all images maintain the same visual language: clean, flat, light paper background, few accent colors, clear labels, and no use of complex perspectives.

## device-command-queue.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Create a clean educational diagram explaining Apple Metal's Device, Command Queue, and Command Buffer relationship.
Composition: Left-to-right flow. CPU on the left, Command Queue in the center, GPU Device on the right, with a Command Buffer shown below the queue.
Labels: "CPU", "Command Queue", "Command Buffer", "Device / GPU", "encode", "submit".
Style: precise flat infographic, warm off-white background, dark ink labels, teal and rust accent colors, no 3D, no photorealism.
Avoid: tiny unreadable labels, decorative icons, extra concepts not listed.

## buffer-round-trip.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Create a clear diagram showing a CPU buffer to GPU compute round trip.
Composition: Three panels from left to right: CPU writes values "1 2 3 4", GPU kernel doubles them, CPU reads values "2 4 6 8".
Labels: "CPU writes", "GPU compute kernel", "CPU reads", "same shared buffer".
Style: precise flat infographic, readable text, warm paper background, teal for CPU, dark green for GPU, rust for output.
Avoid: memory chips, circuit-board decoration, excessive arrows.

## pixels-grid.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Explain that an image is a grid of pixels and each pixel stores RGB values.
Composition: Large pixel grid, one highlighted cell, callout showing "R, G, B" and coordinate "(x, y)".
Style: clean classroom diagram, minimal, high contrast, readable labels.
Avoid: realistic photos, gradients that make the grid hard to read.

## compute-grid.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Explain compute grid, threadgroups, and threads mapped onto image pixels.
Composition: Image pixel grid with one threadgroup outlined and one thread highlighted. Include labels "grid", "threadgroup", "thread", "one thread per pixel".
Style: precise flat technical diagram, warm white background, teal and rust highlights.
Avoid: GPU hardware illustrations, 3D blocks, unreadable small text.

## blur-kernel.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Show a 3x3 blur kernel sampling neighboring pixels around a center pixel.
Composition: 3x3 grid, center cell highlighted, eight neighboring cells marked as sampled inputs, arrow to an output pixel.
Labels: "center pixel", "neighbors", "average", "output".
Style: clean math textbook style, readable labels, soft colors.
Avoid: equations covering the grid, noisy backgrounds.

## compute-vs-render.png

Use case: scientific-educational
Asset type: diagram for a programming tutorial web book
Primary request: Compare Metal compute pipeline and a render pipeline for beginners.
Composition: Two side-by-side columns. Compute column: "kernel function", "buffer / texture", "no drawable required". Render column: "vertex + fragment", "render pass", "writes drawable".
Style: balanced comparison chart, flat infographic, clear labels, consistent color system with the rest of the book.
Avoid: implying one pipeline is better, overly detailed API lists.

