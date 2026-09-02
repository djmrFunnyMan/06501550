# Investigation: Using the precise qualifier.

Clearly this issue is caused by some driver optimization. Logically the best solution should be using the `precise` qualifier to force a specific order of operation, instead of using the uniform workaround.

However this seems to have some side effects on its own, and thus I cannot use this method of fixing the bug.

Why? Well I'm bad at explaining things so let's just go through this step by step.

Starting point:
```
float rand(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(23.53, 44.0))) * 42350.45);
}
```
Produces this result:
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 205654" src="https://github.com/user-attachments/assets/1a77e347-41a8-4222-a9f8-200400b2f16c" />



Now let's try using the `precise` qualifier like this

```
precise float rand(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(23.53, 44.0))) * 42350.45);
}
```

Which results in:
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 205536" src="https://github.com/user-attachments/assets/9405ccdf-d0cb-41ef-b0c6-0fae73aee477" />

So the issue seems fixed right?

But if I apply an offset...

```
vec2 inCoord = gridUV * 64.0 - 10000;
```
I get this result:
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 210530" src="https://github.com/user-attachments/assets/d5c44d4d-4190-45a3-bdb5-784cd3dc71b3" />

This is a severe loss of precision. Thus I cannot use the `precise` qualifier to fix the original issue as the usecase requires applying some offset.



Comparatively if I do

```
uniform float value1 = 23.53; 
uniform float value2 = 44.0;

float rand(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(value1, value2))) * 42350.45);
}
```

I can apply a much higher offset of 100000, without the noise losing its shape.
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 210658" src="https://github.com/user-attachments/assets/2e1db96a-04db-424e-b60e-36d96ac6735a" />


# Vulkan

Everything above concerned the OpenGL API. So what about Vulkan?

Well on Vulkan I can use the `precise` qualifier and it does simply fix the original issue without changing the shape of the noise. The output looks the same whether I use the `precise` method or the push constant method.

<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 213244" src="https://github.com/user-attachments/assets/f51db0b0-227b-4a20-9cf3-49f605f4f441" />
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 213317" src="https://github.com/user-attachments/assets/218237ed-7e62-4575-9dcd-66853729c87e" />
<img width="1202" height="938" alt="Zrzut ekranu 2026-09-02 213426" src="https://github.com/user-attachments/assets/88a38642-88b0-403f-b21d-ecdad1045623" />


So once Minecraft switches from OpenGL to the Vulkan API, using `precise` instead of push constants will be a viable option.







