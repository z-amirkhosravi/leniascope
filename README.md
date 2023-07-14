# Leniascope

<img src=images/orbium.bmp height=300 width=300>

This is an implementation of <a href=https://chakazul.github.io/>Bert Chan</a>'s <a href=https://en.wikipedia.org/wiki/Lenia>Lenia</a> in C++ and Win32 API.

Lenia is a continuous generalization of Conway's <a href=https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life>Game of Life</a>, where:

- The on/off state of each pixel is replaced by a value in the real interval [0,1].
- The "neighbour count" of each pixel is replaced by a weighted average of an area around the pixel.
- The discrete change in state is replaced by growth via a difference equation modeling an ODE.

# Features

Here's an annotated screenshot of the application:

<fig>
<img src=images/screenshot.jpg>
</fig>

The simplest way to play with the program is to press the **Randomize** button, which erases the buffer and re-initializes it randomly, then wait to see if an interesting pattern emerges, and either
randomize again or alter the parameters a little bit.

The mathematical parameters are in the upper right corner:
- μ and σ are the mean and standard deviation of the normal distribution that determines the averaging kernel
- ε is the step-size in the difference equation

The values are given as integer numerators of fractions, so "10³σ=17" means σ=0.017, for example.

Ther **Save** button saves the current buffer, as well as the parameters, into a ".len" file, which **Load** can later recover. This way if you find a nice pattern you can record it and share with others.

The **Export** button saves the current buffer as a bitmap image. 

The **Pause/Play** is self-explanatory. It can be useful if you want to pick the right moment to export an image.

In the bottom right corner you can choose the colour map that assigns a color to the real value of a point. The choices are copied from Python's matplotlib.

There is also a "3D Lenia" mode, which adds a third "z-axis" dimension to the grid. It displays one 2D "z-slice" at a time, which you can change by scrolling the middle mouse button.


# Implementation

Leniascope is coded in Visual Studio C++ using the Win32 API. This was partly an exercise to learn Windows programming, so the interface was coded entirely by hand rather than generated with Visual Studio's tools. Probably the best practice is to use the automated tools as much as possible to keep the repetitive parts of the code organized in a uniform way. On the other hand, some very basic features require altering the window message pipeline using tricks that are standard but not straight-forward.

The computational part amounts to calculating a 2D convolution of the current state with a fixed kernel, so the fast Fourier Transform comes in handy. Initially I used my own implementation of <a href=https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm>Cooley-Tukey</a>'s radix-2 algorithm, including optimizations accounting for the input and output data being real rather than complex. You can find that implementation in the `fft.cpp` file, and the old code that uses it in the `customfft` branch. A Python version of the same implementation was comparable in speed to `numpy.fft.rfft2`. The current `master` branch however uses the <a href=https://www.fftw.org/>fftw</a> library, which has many fine-tuned optimizations, is several orders of magnitude faster, and is free. Using this, along with the fast floating point compiler option, makes it so the speed bottleneck is no longer the mathematical computation! It's now evenly divided between the step assigning RGB colors to real numbers for each pixel, and the step copying the color data into visual memory. Both of these are almost irreducible computations with not much room for improvement, but perhaps using multi-threading to do them in parallel can be one way to improve.

# General Thoughts

In Conway's Game of Life, when you start with random data, quite often it will result in periodic patterns after a few turns. In Lenia on the other hand, most initial data evolves into the same type of static pattern:

<img src=images\plants.jpg height=300 width=300>

One might think that the rules somehow do not force enough self-interactions, were it not for the existence of the "Orbium" (first picture above). I find the existence of this pattern quite astonishing. It even looks like a jellyfish! 

But in 3D Lenia, all random initial patterns that I tried eventually end up either in a blank grid (everything dying), or the same plant-link static pattern as in the 2D version. It seems that the third degree of freedom is just not restrictive enough. 

Consider the e
