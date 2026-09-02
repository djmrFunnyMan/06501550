# Investigation: Effect of passing the problematic float as an uniform.

In the following code the issue can be seen.

```glsl
float value_F = 23.53;
float randA(vec2 inCoord) {return fract(sinM(dot(inCoord, vec2(value_F, 44.0))) * 42350.45);}
```

Changing it to the following fixes the issue (currently only confirmed to work on the Intel B580)

```glsl
uniform float value_U = 23.53;
float randB(vec2 inCoord) {return fract(sinM(dot(inCoord, vec2(value_U, 44.0))) * 42350.45);}
```

## Goal

The purpose of this repro is to isolate how changing the data type of the literal `23.53` propagates through the shader pipeline on Intel graphics.

The code compares two paths:

- **Path A** uses `float`.
- **Path B** uses `uniform float`.

These values are passed through the same reduced perlin-style noise stages derived from the original shader: `dot`, `sinM`, `fract`, `rand`, and `perlin`.

## How to interpret the image

In the side-by-side version:

- **Left** = reduced perlin output using `float`. **Exhibits unexpected behaviour**
- **Right** = reduced perlin output using `uniform float`. **Behaves correctly.** 

Both sides should be effectively equivalent for this shader path, the two panels should look identical.
However they differ significantly. With the left side exhibiting the issue from the original report, and the right side serves as an issue-free control.

It should be noted that the vertical lines visible on the left side cannot be reproduced on any other GPU vendor. They should not exist.
<img width="1202" height="938" alt="obraz" src="https://github.com/user-attachments/assets/6e8dc11d-82bf-4a9b-a06f-345e848ab7b8" />


## Movable version
`repro_movable.cpp` allows you to move the noise using arrow keys. You can change the speed of the movement with the `1` and `2` keys on the number row of your keyboard.
