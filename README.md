Framebuffer::GFX, FastLED's CRGB Backed Framebuffer
===================================================

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


Color Management
----------------
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
- setPassThruColor also takes all 3 color formats and allows forcing a 24bit color when
  using GFX functions that only take RGB565. Make sure to call setPassThruColor() when done.

You can learn more about how to use the GFX API by going to https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library as well as https://learn.adafruit.com/adafruit-gfx-graphics-library/graphics-primitives but keep in mind that this library offers 


Driver backends that use this library base class
------------------------------------------------
This is a base class, offering support for these drivers:
- https://github.com/marcmerlin/FastLED_NeoMatrix/
- https://github.com/marcmerlin/SmartMatrix_GFX/
- https://github.com/marcmerlin/FastLED_SPITFT_GFX (SSD1331 and ILI3941 TFTs)

See the above libraries for example code, and more specifically this repository of example code that works on all these backends:  
https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos
