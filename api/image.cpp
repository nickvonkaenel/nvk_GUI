/* ReaImGui: ReaScript binding for Dear ImGui
 * Copyright (C) 2021-2025  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "helper.hpp"

#include "../src/color.hpp"
#include "../src/image.hpp"

#include <utility> // forward

#include <reaper_plugin_functions.h>

enum ImageFlags {
  ReaImGuiImageFlags_None     = 0,
  ReaImGuiImageFlags_NoErrors = 1<<0,
  ReaImGuiImageFlags_Async    = 1<<1,
};

template<typename T, typename... Args>
static T *createImage(Args &&...args)
{
  // see Image::fromFile for why images bypass the short-lived resource guard
  Resource::bypassGCCheckOnce();
  return new T {std::forward<Args>(args)...};
}

API_SECTION("Image",
R"(ReaImGui currently supports loading PNG and JPEG bitmap images.
Flat vector images may be loaded as fonts, see CreateFont.

UV parameters are texture coordinates in a scale of 0.0 (top/left) to 1.0
(bottom/right). Use values below 0.0 or above 1.0 to tile the image.

Width/height are limited to 8192 pixels.

There are also image functions in the DrawList API such as
DrawList_AddImageQuad and DrawList_AddImageRounded.

Caching of image objects may be implemented like this:

    local images = {}
    local function imageFromCache(fn)
      local img = images[fn]
      if not img then
        img = {}
        images[fn] = img
      end

      if not ImGui.ValidatePtr(img.inst, 'ImGui_Image*') then
        if img.inst then images[img.inst] = nil end
        img.inst = ImGui.CreateImage(fn)
        local prev = images[img.inst]
        if prev and prev ~= img then prev.inst = nil end
        images[img.inst] = img
      end

      return img.inst
    end)");

API_FUNC(0_9, Image*, CreateImage,
(const char*,file) (RO<int*>,flags,ReaImGuiImageFlags_None),
R"(The returned object is valid as long as it is used in each defer cycle
unless attached to a context (see Attach).

With ImageFlags_Async the file is decoded on a background thread and the
function returns at once. Until Image_GetState reports the image as ready,
Image_GetSize returns 0x0 and the image draws nothing; decoding errors are
reported through Image_GetState and Image_GetError instead of being raised.
Image_Wait blocks until the image is ready or a time budget runs out.

Once uploaded to the GPU, the decoded pixels of a file or memory image are
released to save memory and decoded again only if needed (a new context, or
after Image_GetPixels_Array/Image_SetPixels_Array, which keep them resident).)")
try {
  return Image::fromFile(file, API_GET(flags) & ReaImGuiImageFlags_Async);
}
catch(const reascript_error &) {
  if(API_GET(flags) & ReaImGuiImageFlags_NoErrors)
    return nullptr;
  throw;
}

API_FUNC(0_9, Image*, CreateImageFromMem,
(const char*,data) (int,data_sz) (RO<int*>,flags,ReaImGuiImageFlags_None),
R"(Requires REAPER v6.44 or newer for EEL and Lua. Load from a file using
CreateImage or explicitely specify data_sz to support older versions.)")
try {
  // data_sz is inaccurate before REAPER 6.44
  return Image::fromMemory(data, data_sz,
    API_GET(flags) & ReaImGuiImageFlags_Async);
}
catch(const reascript_error &) {
  if(API_GET(flags) & ReaImGuiImageFlags_NoErrors)
    return nullptr;
  throw;
}

API_REGISTER_BASIC_TYPE(LICE_IBitmap*);

template<>
void assertValid(LICE_IBitmap *ptr)
{
  static int (*JS_LICE_GetWidth)(LICE_IBitmap *);
  if(!JS_LICE_GetWidth)
    JS_LICE_GetWidth = reinterpret_cast<decltype(JS_LICE_GetWidth)>
      (plugin_getapi("JS_LICE_GetWidth"));
  if(!JS_LICE_GetWidth)
    throw reascript_error {"cannot load JS_LICE_GetWidth"};
  if(!JS_LICE_GetWidth(ptr))
    Error::invalidObject(ptr);
}

API_FUNC(0_9_2, Image*, CreateImageFromLICE,
(LICE_IBitmap*,bitmap) (RO<int*>,flags,ReaImGuiImageFlags_None),
"Copies pixel data from a LICE bitmap created using JS_LICE_CreateBitmap.")
try {
  assertValid(bitmap);
  return createImage<LICEBitmap>(bitmap);
}
catch(const reascript_error &) {
  if(API_GET(flags) & ReaImGuiImageFlags_NoErrors)
    return nullptr;
  throw;
}

API_FUNC(0_10, Image*, CreateImageFromSize,
(int,width) (int,height) (RO<int*>,flags,ReaImGuiImageFlags_None),
"Create a blank image of the specified dimensions. See Image_SetPixels_Array.")
try {
  return createImage<Bitmap>(width, height, 4);
}
catch(const reascript_error &) {
  if(API_GET(flags) & ReaImGuiImageFlags_NoErrors)
    return nullptr;
  throw;
}

API_FUNC(0_8, void, Image_GetSize, (class Image*,image)
(W<double*>,w) (W<double*>,h),
"Returns 0x0 while an asynchronous image is still decoding.")
{
  assertValid(image);
  if(w) *w = image->width();
  if(h) *h = image->height();
}

API_FUNC(0_10, int, Image_GetState, (class Image*,image),
R"(1 when the image is decoded and drawable, 0 while it is still decoding
(see ImageFlags_Async), -1 when decoding failed (see Image_GetError).
Synchronously loaded images are always ready.)")
{
  assertValid(image);
  return image->state();
}

API_FUNC(0_10, int, Image_Wait, (class Image*,image)
(RO<double*>,timeout_sec,0.0),
R"(Blocks until an asynchronous image finishes decoding or timeout_sec elapses,
then returns its state like Image_GetState. Intended for the first frame after
a list of images changes: spend one small time budget across the visible set
so that frame is complete.)")
{
  assertValid(image);
  return image->wait(API_GET(timeout_sec));
}

API_FUNC(0_10, const char*, Image_GetError, (class Image*,image),
"The decoding error of a failed asynchronous image, or an empty string.")
{
  assertValid(image);
  return image->error().c_str();
}

API_FUNC(0_10, void, Image_GetPixels_Array, (Bitmap*,image)
(int,x) (int,y) (int,w) (int,h) (reaper_array*,pixels)
(RO<int*>,offset,0) (RO<int*>,pitch,0),
"Read the pixel data of the given rectangle. Pixel format is 0xRRGGBBAAp+0.")
{
  assertValid(image);
  assertValid(pixels);
  image->copyPixels<false>(x, y, w, h, pixels, API_GET(offset), API_GET(pitch));
}

API_FUNC(0_10, void, Image_SetPixels_Array, (Bitmap*,image)
(int,x) (int,y) (int,w) (int,h) (reaper_array*,pixels)
(RO<int*>,offset,0) (RO<int*>,pitch,0),
"Write the pixel data of the given rectangle. Pixel format is 0xRRGGBBAAp+0.")
{
  assertValid(image);
  assertValid(pixels);
  image->copyPixels<true>(x, y, w, h, pixels, API_GET(offset), API_GET(pitch));
}

API_FUNC(0_10, void, Image, (Context*,ctx)
(class Image*,image) (double,image_size_w) (double,image_size_h)
(RO<double*>,uv0_x,0.0) (RO<double*>,uv0_y,0.0)
(RO<double*>,uv1_x,1.0) (RO<double*>,uv1_y,1.0),
"Adds StyleVar_ImageBorderSize on each side.")
{
  FRAME_GUARD;
  assertValid(image);
  if(!image->ready()) {
    const float border {ImGui::GetStyle().ImageBorderSize * 2};
    ImGui::Dummy(ImVec2(image_size_w + border, image_size_h + border));
    return;
  }

  ImGui::Image(image->texture(ctx),
    ImVec2(image_size_w, image_size_h),
    ImVec2(API_GET(uv0_x), API_GET(uv0_y)),
    ImVec2(API_GET(uv1_x), API_GET(uv1_y)));
}

API_FUNC(0_10, void, ImageWithBg, (Context*,ctx)
(class Image*,image) (double,image_size_w) (double,image_size_h)
(RO<double*>,uv0_x,0.0) (RO<double*>,uv0_y,0.0)
(RO<double*>,uv1_x,1.0) (RO<double*>,uv1_y,1.0)
(RO<int*>,bg_col_rgba,0x00000000) (RO<int*>,tint_col_rgba,0xFFFFFFFF),
R"(Draws a background based on regular Button color + optionally an inner
background if specified. Adds StyleVar_FramePadding to provided size.)")
{
  FRAME_GUARD;
  assertValid(image);
  if(!image->ready()) {
    const ImVec2 padding {ImGui::GetStyle().FramePadding};
    ImGui::Dummy(ImVec2(image_size_w + padding.x * 2,
                        image_size_h + padding.y * 2));
    return;
  }

  ImGui::ImageWithBg(image->texture(ctx),
    ImVec2(image_size_w, image_size_h),
    ImVec2(API_GET(uv0_x), API_GET(uv0_y)),
    ImVec2(API_GET(uv1_x), API_GET(uv1_y)),
    Color(API_GET(bg_col_rgba)), Color(API_GET(tint_col_rgba)));
}

API_FUNC(0_8, bool, ImageButton, (Context*,ctx) (const char*,str_id)
(class Image*,image) (double,image_size_w) (double,image_size_h)
(RO<double*>,uv0_x,0.0) (RO<double*>,uv0_y,0.0)
(RO<double*>,uv1_x,1.0) (RO<double*>,uv1_y,1.0)
(RO<int*>,bg_col_rgba,0x00000000) (RO<int*>,tint_col_rgba,0xFFFFFFFF),
R"(Draws a background based on regular Button color + optionally an inner
background if specified. Adds StyleVar_FramePadding to provided size.)")
{
  FRAME_GUARD;
  assertValid(image);
  if(!image->ready()) {
    const ImVec2 padding {ImGui::GetStyle().FramePadding};
    return ImGui::InvisibleButton(str_id,
      ImVec2(image_size_w + padding.x * 2, image_size_h + padding.y * 2));
  }

  return ImGui::ImageButton(str_id, image->texture(ctx),
    ImVec2(image_size_w, image_size_h),
    ImVec2(API_GET(uv0_x), API_GET(uv0_y)),
    ImVec2(API_GET(uv1_x), API_GET(uv1_y)),
    Color(API_GET(bg_col_rgba)), Color(API_GET(tint_col_rgba)));
}

API_SUBSECTION("Image Set",
R"(Helper to automatically select and scale an image to the DPI scale of
the current window upon usage.

ImageSet objects may be used in any function that expect an image as parameter.

Usage:

    local set = ImGui.CreateImageSet()
    ImGui.ImageSet_Add(set, 1.0, ImGui.CreateImage('32x32.png'))
    ImGui.ImageSet_Add(set, 2.0, ImGui.CreateImage('64x64.png'))

    local function frame()
      ImGui.Image(ctx, set, ImGui.Image_GetSize(set))
      -- ...
    end)");

API_FUNC(0_9, ImageSet*, CreateImageSet, API_NO_ARGS,
"")
{
  return createImage<ImageSet>();
}

API_FUNC(0_8, void, ImageSet_Add, (ImageSet*,set)
(double,scale) (class Image*,image),
"'img' cannot be another ImageSet.")
{
  assertValid(set);
  assertValid(image);
  set->add(scale, image);
}

API_ENUM_NS(0_10, ReaImGui, ImageFlags_None, "");
API_ENUM_NS(0_10, ReaImGui, ImageFlags_NoErrors,
  "Return nil instead of returning an error.");
API_ENUM_NS(0_10, ReaImGui, ImageFlags_Async,
  "Decode on a background thread. See CreateImage and Image_GetState.");
