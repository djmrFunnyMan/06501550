# Investigation: Effect of decimal precision on the issue at hand.

In the following code

```glsl
float randA(vec2 inCoord) {return fract(sinM(dot(inCoord, vec2(current_float, 44.0))) * 42350.45);}
```

Check the effect of changing `current_float` to the following values: `(23) | (23.5) | (23.75) | (23.625) | (23.03125) | (23.000488) | (23.53)`

Check the effect of running the program on Intel vs Nvidia/AMD GPUs

## Instructions:
- You must change the value of `current_float` manually before compiling
- You can offset the noise by using left/right arrow keys
- You can change the rate at which the offset is applied by using up/down arrow keys

## Issue Description:
The when ran on an Intel GPU (but not Nvidia or AMD) the noise will occasionally exhibit abrupt vertial cuts in the noise pattern.
<img width="951" height="715" alt="obraz" src="https://github.com/user-attachments/assets/a0d24fcd-593a-4750-b194-271319616a2d" />


## Results
The issue gets more frequent, proportionally to the number of decimal places used by `current_float`

Out of the example values provided, `23` produces erroneous results very infrequently. It will require holding the left arrow key for nearly a minute to reach an area exhibiting the issue.

The reason `23.53` errors more than `23.75` is because what matters is the value actually stored in float. So `23.53` doesn't have just 2 decimal places in reality it has 19

<img width="855" height="247" alt="obraz" src="https://github.com/user-attachments/assets/5dab52ae-02fb-4acd-9b7b-1b26bc7af4a9" />

Contary to my initial assessment the value `23.5` is not issue free. Instead it simply triggers the error much more infrequently than `23.53`





