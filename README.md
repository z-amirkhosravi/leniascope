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

The simplest way to play with the program is to press the **Randomize** button, which erases the buffer and re-initializes it randomly, wait to see if an interesting pattern emerges, then either
randomize again or alter the parameters.

The mathematical parameters are in the upper right corner:
- μ and σ are the mean and standard deviation of the normal distribution that determines the averaging kernel
- ε is the step-size in the difference equation

The values are given as integer numerators of fractions, so "10³σ=17" means σ=0.017.

The **Save** button saves the current buffer, as well as the parameters, into a ".len" file, which **Load** can later recover. This way if you find a nice pattern you can record it and share with others.

The **Export** button saves the current buffer as a bitmap image. 

The **Pause/Play** is self-explanatory. It can be useful if you want to pick the right moment to export an image.

In the bottom right corner you can choose the color map that assigns a color to the real values. The choices are copied from Python's matplotlib, so they might be familiar.

There is also an experimental "3D Lenia" mode, which adds a third "z-axis" dimension to the grid. It displays one 2D "z-slice" at a time however, but you can change which slice that is by scrolling the middle mouse button.


# Implementation

Leniascope is coded in Visual Studio C++ using the Win32 API. This was partly an exercise to learn Windows programming, so the interface was coded entirely by hand rather than generated with Visual Studio. I learned quite a bit, including the fact that it's best to use the automated tools as much as possible to keep the repetitive parts of the code organized in a uniform way. On the other hand, some very basic features require altering the window message pipeline using tricks that are standard but not straight-forward or automatic.

The main computation  amounts to calculating a 2D convolution of the state grid with a fixed kernel, so the fast Fourier Transform comes in handy. Initially I used my own implementation of <a href=https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm>Cooley-Tukey</a>'s radix-2 algorithm, including optimizations accounting for the input and output data being real numbers. You can find that implementation in `fft.cpp`, and the old code that uses it in the `customfft` branch. A Python version of the same implementation was comparable in speed to `numpy.fft.rfft2`. The current `master` branch however uses the <a href=https://www.fftw.org/>fftw</a> library, which has many fine-tuned optimizations, is several orders of magnitude faster, and is free. Using that the speed bottleneck is no longer the mathematical computation -- it's evenly divided between the step assigning RGB colors to real values at each pixel, and the step copying the color data into visual memory. Both of these are almost irreducible operations with not much room for improvement, but perhaps using multi-threading to do them in parallel can be one way to improve.

# General Thoughts

In Conway's Game of Life, random initial states usually lead to periodic patterns after a few turns. You get die-offs often, but you rarely see the entire grid fill out with a static repeating pattern. In Lenia on the other hand, most initial data of enough mass evolves into the same type of almost plant-like pseudo-periodic organism:

<img src=images\plants.jpg height=450 width=450>

One would think that the rules somehow do not force enough self-interactions, were it not for the existence of the "Orbium" (pictured before). It is really quite astonishing: it moves, has bilateral 
symmetry like actual living animals, and even looks vaguely like a jellyfish. It's also relatively stable under parameter change -- for instance in Leniascope you can first contruct an orbium with a small kernel radius of around 15, then slowly increase that radius as the program runs. The orbium will adapt and grow larger, and become more detailed. 

But in 3D Lenia, every random initial pattern that I've seen eventually ends up in either everything dying, or a 3D version of the same plant-like static pattern. It seems that having a third degree of spatial freedom is just not restrictive enough to force complex interactions. 

Consider the the following typical pattern in 2D Lenia:

<img src=images\plantorb.bmp height=450 width=450>

It is the same usual pattern in the process of spreading out to take over the grid. On the edges of its "wavefront" it is typically undulating and uneven, and often includes half-formed orbiums attempting to break away from the main mass. 

But in 3D Lenia you don't see this at all. The edges of the analogous spreading pattern are much more uniform, as if the mass is spreading without needing to compete with itself for space:

<img src=images\3dplants.bmp height=400 width=400>

Is it possible to find a more restrictive 3D generalization of Lenia that forces more space competition on spreading matter? Perhaps that is the way to find the 3D orbium.
