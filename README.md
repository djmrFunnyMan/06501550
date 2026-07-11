# Investigation: Effect of passing the problematic float as an uniform.

In the following code the issue can be seen.

```glsl
float value_F = 23.53;
float randA(vec2 inCoord) {return fract(sinM(dot(inCoord, vec2(value_F, 44.0))) * 42350.45);}
```

Changing it to the following fixes the issue.

```glsl
uniform float value_U = 23.53;
float randB(vec2 inCoord) {return fract(sinM(dot(inCoord, vec2(value_U, 44.0))) * 42350.45);}
```

## Goal

The purpose of this repro is to isolate how changing how the literal `23.53` is provided propagates through the shader pipeline.

The code compares two paths:

- **Path A** uses `float`.
- **Path B** uses `uniform float`.

These values are passed through the same reduced perlin-style noise stages derived from the original shader: `dot`, `sinM`, `fract`, `rand`, and `perlin`.

## How to interpret the image

In the side-by-side version:

- **Left** = reduced perlin output using `float`.
- **Right** = reduced perlin output using `uniform float`.

If both sides were effectively equivalent for this shader path, the two panels would look identical.
