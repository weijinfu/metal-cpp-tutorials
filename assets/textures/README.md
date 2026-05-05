# Texture Assets

These textures are used by the tutorial as external material inputs.

- `warm_metal.ppm`: local handcrafted base color texture used for the early Book2 shading progression.
- `clay.ppm`: local flat color texture used for Stanford Bunny lighting checks.
- `rusty_metal_02_basecolor.png`
- `rusty_metal_02_roughness.png`
- `rusty_metal_02_metallic.png`
- `rusty_metal_02_normal.png`
- `rusty_metal_02_ao.png`

`rusty_metal_02_*` originates from the Poly Haven CC0 texture set `Rusty Metal 02`:

- Asset page: `https://polyhaven.com/a/rusty_metal_02`
- Files were downloaded from the Poly Haven public API / download CDN in 1K resolution.
- `rusty_metal_02_metallic.png` was extracted locally from the blue channel of Poly Haven's packed `arm` texture. In this copy it is fully black, so it validates the metallic map plumbing but does not produce strong bare-metal reflections by itself.
