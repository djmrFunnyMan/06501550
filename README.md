# Investigation: Behavior on Vulkan.

I wished to see if the issue is reproducable on Vulkan.

It is. And not only that, but it can be worked around the same way.

(The program was converted from OpenGL to Vulkan using ChatGPT)

## Building on Windows

I use this command to build the executable.
```
   cl /std:c++17 /EHsc /MD ^
   /I"%VULKAN_SDK%\Include" ^
   /I"C:\libs\glfw\include" ^
   repro_vulkan.cpp ^
   /link ^
   /LIBPATH:"%VULKAN_SDK%\Lib" ^
   /LIBPATH:"C:\libs\glfw\build\src\Release" ^
   vulkan-1.lib ^
   glfw3.lib ^
   shaderc_combined.lib ^
   user32.lib ^
   gdi32.lib ^
   shell32.lib ^
   /OUT:repro_vulkan.exe
```

## Introduction

The code compares two paths:

- **Path A** uses `vec2(23.53, 44.0)`.
- **Path B** uses `vec2(pc.value_U, pc.value_U2)`. Where both values are numerically identical to Path A, except they're provided as push constants instead of literals.

On Path B, both values have to be push constants to work around the issue. Which is also how OpenGL now behaves since driver update 32.0.101.8974. With the caveat that Vulkan has seemeingly always behaved this way, unlike OpenGL.

## How to interpret the image

In the side-by-side version:

- **Left** = reduced perlin output using `vec2(23.53, 44.0)`. **Exhibits unexpected behaviour**
- **Right** = reduced perlin output using `vec2(pc.value_U, pc.value_U2)`. **Behaves correctly.** 

<img width="1202" height="938" alt="obraz" src="https://github.com/user-attachments/assets/f0e415a3-5437-414a-905e-9fda0ea16891" />

Both sides should be effectively equivalent for this shader path, the two panels should look identical.
However they differ significantly. With the left side exhibiting the issue from the original report, and the right side serves as an issue-free control.

It should be noted that the artifacts visible on the left side cannot be reproduced on any other GPU vendor.

## Movable version
`repro_movable_vulkan.cpp` allows you to move the noise using arrow keys. You can change the speed of the movement with the `1` and `2` keys on the number row of your keyboard, and zoom in/out with the `-` and `=` keys.
