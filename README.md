# Case 06501550: OpenGL procedurally generated night nebula shader malfunctioning in Minecraft.

This project is a reduced OpenGL/GLSL reproduction for a shader issue where changing a float literal inside a `vec2` constructor changes the rendered result in a way that becomes clearly visible once the value is used inside interpolated noise.[1][2]

The original suspicious expression comes from the Intel Communities case about an OpenGL procedurally generated night nebula shader malfunction, in the reduced `rand(vec2 inCoord)` path discussed there.[3]

```glsl
return fract(sinM(dot(inCoord, vec2(23.53, 44.0))) * 42350.45);
```

Changing it to the following was reported to fix the visible issue in that case.[3]

```glsl
return fract(sinM(dot(inCoord, vec2(23.5, 44.0))) * 42350.45);
```

## Goal

The purpose of this repro is to isolate how a tiny change in the literal `23.53` versus `23.5` propagates through the shader pipeline.[1][2]

The code compares two paths:

- **Path A** uses `vec2(23.53, 44.0)`.[2]
- **Path B** uses `vec2(23.5, 44.0)`.[2]

These values are passed through the same reduced perlin-style noise stages derived from the original shader: `dot`, `sinM`, `fract`, `rand`, and `perlin`.[1][2]

## What the repro demonstrates

Earlier reduced tests showed that the raw hash stage (`rand`) can look superficially similar even when the underlying values differ.[4][5] However, once those values are fed into the reduced `perlin()` stage, the two outputs diverge in a structured and clearly visible way, which is much closer to the behavior seen in the original nebula shader.[5][6]

A later 4-panel diagnostic made the progression clearer:[7]

- The **dot product** stage already differs as a smooth linear gradient.
- The **sine stage** turns that difference into oscillatory variation.
- The **rand/hash stage** makes the difference more irregular.
- The **perlin stage** interpolates those differences into coherent large-scale visible structure.

This means the first divergence begins at the dot-product stage, but the visible artifact becomes much easier to observe after the reduced perlin interpolation stage.[7]

## Current repro code

The current `repro.cpp` opens a GLFW window, compiles a fullscreen fragment shader, and compares two reduced perlin paths side by side.[2]

In the fragment shader:

- `randA(vec2)` uses `vec2(23.53, 44.0)`.[2]
- `randB(vec2)` uses `vec2(23.5, 44.0)`.[2]
- `perlinA(vec2)` is built from `randA(vec2)`.[2]
- `perlinB(vec2)` is built from `randB(vec2)`.[2]

The left half of the screen renders Path A, and the right half renders Path B.[2]

## How to build on Windows

This project was compiled from a Native Tools x64 Command Prompt using Microsoft `cl` with GLFW and GLEW include/library paths.[2]

Use this command exactly:

```bat
cl /EHsc /MD repro.cpp /I"C:\_dev\_opengl\glfw\include" /I"C:\_dev\_opengl\glew\include" /DWIN32 /D_WINDOWS /DGLFW_DLL /link /LIBPATH:"C:\_dev\_opengl\glfw\lib-vc2022" /LIBPATH:"C:\_dev\_opengl\glew\lib\Release\x64" opengl32.lib user32.lib gdi32.lib shell32.lib glfw3dll.lib glew32.lib
```

## How to run

1. Open a Native Tools x64 Command Prompt.
2. Build with the command above.
3. Ensure the required GLFW/GLEW DLLs are available at runtime if they are not already on the system path.
4. Run the produced executable.
5. Press `ESC` to close the window.[2]

## How to interpret the image

In the side-by-side version:

- **Left** = reduced perlin output using `23.53`.[2]
- **Right** = reduced perlin output using `23.5`.[2]

If both sides were effectively equivalent for this shader path, the two panels would look nearly identical. When they differ visibly, it indicates that the literal change is materially altering the reduced noise field.[5][6]

In the diagnostic versions created during investigation, the strongest evidence came from the difference views, especially the perlin-difference view and the 4-panel breakdown, which showed that the divergence starts at `dot(...)` and becomes visually significant after the later nonlinear and interpolated stages.[6][7]

## Why this matters

The original bug report concern is not merely that `23.53` and `23.5` are mathematically different. That is expected. The issue is that a very small literal change can produce a disproportionately visible result in a real shader path derived from production code.[1][5][7]

This repro helps determine whether the observed behavior is:

- a normal consequence of the shader's nonlinear hash/noise chain,
- a platform- or driver-specific sensitivity,
- or an unexpected compiler/runtime issue in how the literal is handled in the reduced path.[1][7]
