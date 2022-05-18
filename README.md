Framebuffer::GFX: FastLED CRGB Backed Framebuffer
=================================================
Below is a summary of how things work, but you'll find more details on this post: http://marc.merlins.org/perso/arduino/post_2020-03-16_Framebuffer_GFX_-Choosing-between-its-3-2D-APIs_-FastLED-XY_-NeoMatrix_-and-LEDMatrix_-and-detail-of-its-many-supported-hardware-backends.html


How FrameBuffer::GFX fits and the glue drivers I wrote
------------------------------------------------------
I used all the low level drivers on the left, wrote all the glue drivers in the middle, and FrameBuffer::GFX
```
Low Level Drv|Glue Driver for FrameBuffer::GFX
FastLED     - FastLED_NeoMatrix  -------------\     FastLED CRGB Array 
SmartMatrix - SmartMatrix_GFX -----------------\    24bit FB storage        API Support
                                                \   CRGB methods like
AF:ILI9341-\                                     \  scale8/fadeToBlackBy
AF:SSD1331  |--- FastLED_ArduinoGFX_TFT ----------|        |               ,FastLED API
AF:ST7735  /     FastLED_SPITFT_GFX (for AF)      |        |              / (XY 2D to 1D mapping)
ArduinoGFX/  AF:Adafruit (ArduinoGFX is all in 1) |        |             /
                                                  |        |            /
ArduinoOnPc-FastLED-GFX-LEDMatrix arduino         - FrameBuffer::GFX------ Adafruit::NeoMatrix +
emulation for linux / Raspberry Pi:               |        |             \ Adafruit::GFX APIs
----------------------------------               /    Adafruit::GFX       \ 
rpi-rgb-led-matrix - FastLED_RPIRGBPanel_GFX ---/   LEDMatrix (optional)   `LEDMatrix API
ArduinoOnPC X11/linux - FastLED_TFTWrapper_GFX /
FastLED_SDL (linux)   -  FastLED_NeoMatrix   -/                    
```

Color Management and Adafruit::GFX
----------------------------------
This code was originally based on Adafruit::NeoMatrix although it evolved a fair
amount before becoming what it is now. Namely it tries to work with 24bit colors
and accepts them in uint32_t format, FastLED::CRGB struct format, and 
Adafruit::GFX RGB565 format which is upconverted to its native RGB888 format.

There are conversion functions between these color formats:
- Color(r, g, b) creates an RGB565 (used for Adafruit::GFX draw functions)
- Color24to16 does the same but takes an RGB888 in uint32_t
- CRGBtoint32 turns a FastLED::CRGB struct into a RGB888 uint32_t
- drawPixel can take either RGB65, RGB888, or CRGB. Make sure you give it "(uint32_t) 0"
  instead of "0" so that it knows which version to use.
- To send an RGB888 from 3 variables, you can send (uint32_t) (R<<16 + G<<8 + B)
- setPassThruColor also takes a CRGB or uint32 (0xRRGGBB) and allows forcing a 24bit color when
  using GFX functions that only take RGB565. Make sure to call setPassThruColor() to undo that
  override when done. If you are happy with 16bit color, RGB565 built with matrix->Color(r,g,b)
  then you do not need setPassThruColor.

You can learn more about how to use the GFX API by going to https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library as well as https://learn.adafruit.com/adafruit-gfx-graphics-library/graphics-primitives but keep in mind that this library offers 

How to use this library
-----------------------
Framebuffer::GFX does not drive any hardware directly, but it is used by other
drivers for its shared framebuffer functionality and functions.

It is like Adafruit::GFX, but better, as it provides 3 different 2D APIs:
- Adafruit::GFX (or Adafruit::NeoMatrix)
- FastLED / FastLED::NeoMatrix  https://github.com/marcmerlin/FastLED_NeoMatrix
- LEDMatrix  https://github.com/marcmerlin/LEDMatrix

Why do you want 3 APIs at the same time?  
This is so that you can run existing code that was written for those different APIs and re-run that code on backends it wasn't originally meant to also run on (like FastLED code on an SSD1331 TFT screen or a SmartMatrix backend). See https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos for lots of demos which show how to use those APIs.

It depends on these base libraries:
- https://github.com/adafruit/Adafruit-GFX-Library
- https://github.com/FastLED/FastLED (required as the virtual framebuffer is based on FastLED's CRGB RGB888 struct)
- https://github.com/marcmerlin/LEDMatrix (this one is optional)


Driver backends that use this library base class
------------------------------------------------
This is a base class, offering support for these drivers:
- https://github.com/marcmerlin/FastLED_NeoMatrix/
- https://github.com/marcmerlin/SmartMatrix_GFX/
- https://github.com/marcmerlin/FastLED_SPITFT_GFX (SSD1331, ILI9341, and ST7735 TFTs)
- https://github.com/marcmerlin/FastLED_RPIRGBPanel_GFX (Glue driver for https://github.com/marcmerlin/ArduinoOnPc-FastLED-GFX-LEDMatrix/ that allows displaying a matrix on RGBPanels using https://github.com/hzeller/rpi-rgb-led-matrix )
- https://github.com/marcmerlin/FastLED_TFTWrapper_GFX (Emulate a TFT screen on linux for https://github.com/marcmerlin/ArduinoOnPc-FastLED-GFX-LEDMatrix/ )
- https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos/blob/022257656e2f1beabe327e88bb96747c0fc955f9/neomatrix_config.h#L792 (FastLED emulation on linux/SDL)

See the above libraries for example code, and more specifically this repository of example code that works on all these backends:  
https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos

Here is an example of code ultimately running on top of Framebuffer::GFX via FastLED::NeoMatrix on ESP8266 (24x32 and 32x32) and SmartMatrix::GFX on ESP32 (64x96):
![image](https://user-images.githubusercontent.com/1369412/58442553-03999e80-80a1-11e9-9b79-3b0d438a977e.png)

Below is the same code again now running on top of FastLED_SPITFT::GFX on an SSD1331 96x64 TFT screen:
![image](https://user-images.githubusercontent.com/1369412/58442556-072d2580-80a1-11e9-9cc6-56c5126be20d.png)

And another example of still the same code running on top of FastLED_RPIRGBPanel_GFX + hzeller/rpi-rgb-led-matrix on a 384x192 RGBPanel display run by a Raspberry Pi with ArduinoOnPc-FastLED-GFX-LEDMatrix:
![dsc05123](https://user-images.githubusercontent.com/1369412/76477144-a49fde80-63c1-11ea-82c8-86e5f61dfecd.jpg)

Custom X/Y Mapping for Irregular Arrays
---------------------------------------
This library reuses the neomatrix mapping from adafruit:
![image](https://user-images.githubusercontent.com/1369412/103467198-74834c80-4d01-11eb-8eaa-19b798a6599e.png)
See https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library for how this works when you have tiled matrices.

Now if you have an irregular mapping, you can write your own myRemapFn see the fancy example in https://github.com/marcmerlin/FastLED_NeoMatrix/issues/6  
This allows you to make mapping like this one (note that the count is off by one, the first pixel is 0, not 1).
```
//    .0 .1 .2 .3 .4 .5 .6 .7 .8 .9 10 11.12 13 14 15 16 17 18 19 20 21 22 23 24
// 0  .. .1 .2 .3 .. .. .. 22 23 24 .. .. .. .. .. 43 44 45 .. .. .. 64 65 66 ..
// 1  18 .. .. .. .4 .. 39 .. .. .. 25 .. .. .. 60 .. .. .. 46 .. 81 .. .. .. 67
// 2  17 .. .. .. .5 .. 38 .. .. .. 26 .. 85 .. 59 .. .. .. 47 .. 80 .. .. .. 68
// 3  16 .. .. .. .6 .. 37 .. .. .. 27 .. .. .. 58 .. .. .. 48 .. 79 .. .. .. 69
// 4  .. 19 20 21 .. .. .. 40 41 42 .. .. .. .. .. 61 62 63 .. .. .. 82 83 84 ..
// 5  15 .. .. .. .7 .. 36 .. .. .. 28 .. .. .. 57 .. .. .. 49 .. 78 .. .. .. 70
// 6  14 .. .. .. .8 .. 35 .. .. .. 29 .. 86 .. 56 .. .. .. 50 .. 77 .. .. .. 71
// 7  13 .. .. .. .9 .. 34 .. .. .. 30 .. .. .. 55 .. .. .. 51 .. 76 .. .. .. 72
// 8  .. 12 11 10 .. .. .. 33 32 31 .. .. .. .. .. 54 53 52 .. .. .. 75 74 73 ..
```


Adafruit::GFX vs FastLED vs LEDMatrix APIs
------------------------------------------
Years ago, back when we only had 8x8 displays, Adafruit wrote a nice API, https://github.com/adafruit/Adafruit-GFX-Library .  
It is in no means perfect or exhaustive, but it does what most people need,
including font support. Its huge strength is how it works many different
hardware backends, all the way back to 8x8 matrices. See this example of code
working on 3 different hardware backends all using the same GFX demo code:
http://marc.merlins.org/perso/arduino/post_2017-04-24_Adafruit-GFX-on-NeoMatrix-and-RGB-Matrix-Panel-Demo.html 
https://www.youtube.com/watch?tv=9yGZLtewmfI

Here is an example of my same code on multiple GFX backends:
- https://github.com/marcmerlin/LED-Matrix/blob/master/examples/directmatrix8x8_tricolor_direct_sr/directmatrix8x8_tricolor_direct_sr.ino
- https://github.com/adafruit/Adafruit_NeoMatrix/tree/master/examples/MatrixGFXDemo
- https://github.com/adafruit/RGB-matrix-Panel/blob/master/examples/PanelGFXDemo_16x32/PanelGFXDemo_16x32.ino 
- https://github.com/marcmerlin/FastLED_NeoMatrix/tree/master/examples/MatrixGFXDemo
- https://github.com/mrfaptastic/ESP32-RGB64x32MatrixPanel-I2S-DMA/blob/master/examples/PanelGFXDemo/PanelGFXDemo.ino
- https://github.com/marcmerlin/SmartMatrix_GFX/tree/master/examples/MatrixGFXDemo
- https://github.com/marcmerlin/Adafruit-SSD1331-OLED-Driver-Library-for-Arduino/tree/master/examples/LCDGFXDemo

Now Adafruit::GFX has a drawback nowadays which is how it only supports color in 16 bits (RGB 565).  
Honestly it is good enough for most displays that aren't good enough to show 16,777,216 colors, and 
if you care, there is a bypass to drawPixel that lets you draw in 24bit color (see above)

FastLED supports 24bit color natively. It is not a 2D API per se, but used along with
https://github.com/marcmerlin/FastLED_NeoMatrix you can get GFX API support
while adding FastLED primitives like nscale and fade.  
There is also FastLED 2D demo code written with just FastLED primitives and an XY() function to turn 2D coordinates into FastLED 1D array indexes, see https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos/tree/master/FastLED for examples

LEDMatrix is a library that predates FastLED::NeoMatrix . It offers a GFX like API with a few extras, as well as sprites and font libraries. See https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos/tree/master/LEDMatrix/LEDSprites-Pacman for a sprites example.  One of its drawbacks is that its layout for multiple tiled matrices, layout is complicated and FastLED::NeoMatrix is much simpler to use.
