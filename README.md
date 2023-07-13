# Leniascope

<img src=images/orbium.bmp height=300 width=300>

This is an implementation of <a href=https://chakazul.github.io/>Bert Chan</a>'s <a href=https://en.wikipedia.org/wiki/Lenia>Lenia</a> in C++ and Win32 API.

Lenia is a continuous generalization of Conway's <a href=https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life>Game of Life</a>, where:

- The on/off state of each pixel is replaced by a value in the real interval [0,1].
- The "neighbour count" of each pixel is replaced by a weighted average of an area around the pixel.
- The discrete change in state is replaced by growth via a difference equation modeling an ODE.

# Implementation

The version here is coded in Visual Studio C++ using the Win32 API. This was partly an exercise to learn Windows programming, so the interface was coded entirely by hand rather than generated with Visual Studio's tools. Probably the best practice is to use the automated tools as much as possible to keep the repetitive parts of the coded organized in a uniform way. On the other hand, even very basic features will require altering the window message pipeline using tricks that are standard but not straight-forward.

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



  
