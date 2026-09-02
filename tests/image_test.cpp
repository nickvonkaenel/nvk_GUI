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

#include "../src/error.hpp"
#include "../src/image.hpp"

#include <gtest/gtest.h>
#include <imgui/imgui.h>
#include <memory>
#include <reaper_plugin.h>

// 2x3 RGBA8 PNG, pixel (x, y) = {y*40, x*90, 7, 255}
static const unsigned char PNG_2x3[] {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
  0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03,
  0x08, 0x06, 0x00, 0x00, 0x00, 0xb9, 0xea, 0xde, 0x81, 0x00, 0x00, 0x00,
  0x1b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x60, 0xff,
  0xcf, 0x10, 0x05, 0xc4, 0x1a, 0x40, 0x86, 0x06, 0x88, 0x11, 0x00, 0x64,
  0x04, 0x00, 0x19, 0x00, 0x5f, 0x0d, 0x08, 0x23, 0x78, 0x5d, 0xf9, 0xdf,
  0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const char *pngData()
{
  return reinterpret_cast<const char *>(PNG_2x3);
}

TEST(ImageTest, SyncFromMemory) {
  std::unique_ptr<Image> image {Image::fromMemory(pngData(), sizeof(PNG_2x3))};
  EXPECT_EQ(image->state(), Image::Ready);
  EXPECT_TRUE(image->ready());
  EXPECT_EQ(image->width(),  2u);
  EXPECT_EQ(image->height(), 3u);
  EXPECT_TRUE(image->error().empty());
}

TEST(ImageTest, SyncFailureThrows) {
  EXPECT_THROW(Image::fromMemory("not an image", 12), reascript_error);
}

TEST(ImageTest, AsyncFromMemory) {
  std::unique_ptr<Image> image
    {Image::fromMemory(pngData(), sizeof(PNG_2x3), true)};
  // may or may not be done already, but never failed
  EXPECT_NE(image->state(), Image::Failed);
  EXPECT_EQ(image->wait(10.0), Image::Ready);
  EXPECT_TRUE(image->ready());
  EXPECT_EQ(image->width(),  2u);
  EXPECT_EQ(image->height(), 3u);
}

TEST(ImageTest, AsyncFailureIsReported) {
  std::unique_ptr<Image> image {Image::fromMemory("not an image", 12, true)};
  EXPECT_EQ(image->wait(10.0), Image::Failed);
  EXPECT_FALSE(image->ready());
  EXPECT_FALSE(image->error().empty());
  EXPECT_EQ(image->width(), 0u);
}

TEST(ImageTest, AsyncMissingFile) {
  std::unique_ptr<Image> image
    {Image::fromFile("/nonexistent/reaimgui-image-test.png", true)};
  EXPECT_EQ(image->wait(10.0), Image::Failed);
  EXPECT_FALSE(image->error().empty());
}

TEST(ImageTest, WaitTimeoutOnPendingReturnsState) {
  std::unique_ptr<Image> image
    {Image::fromMemory(pngData(), sizeof(PNG_2x3), true)};
  const Image::State state {image->wait(0.0)};
  EXPECT_TRUE(state == Image::Pending || state == Image::Ready);
  EXPECT_EQ(image->wait(10.0), Image::Ready);
}

TEST(ImageTest, ManyAsyncImages) {
  std::vector<std::unique_ptr<Image>> images;
  for(int i {}; i < 64; ++i)
    images.emplace_back(Image::fromMemory(pngData(), sizeof(PNG_2x3), true));
  for(auto &image : images) {
    ASSERT_EQ(image->wait(10.0), Image::Ready);
    EXPECT_EQ(image->width(), 2u);
  }
}

TEST(ImageTest, ImagesBypassGCGuard) {
  // Resource's short-lived guard throws after MAX_GC_FRAMES consecutive frames
  // that collected something; collected images do not count toward it.
  std::unique_ptr<Image> keeper {Image::fromMemory(pngData(), sizeof(PNG_2x3))};
  for(int i {}; i < 130; ++i) {
    Image *garbage {Image::fromMemory(pngData(), sizeof(PNG_2x3))};
    EXPECT_TRUE(Resource::isValid(garbage));
    for(int frame {}; frame < 4; ++frame) {
      keeper->keepAlive();
      Resource::testHeartbeat();
    }
    EXPECT_FALSE(Resource::isValid(garbage)); // collected by the timer
  }
  std::unique_ptr<Image> image {Image::fromMemory(pngData(), sizeof(PNG_2x3))};
  EXPECT_TRUE(image->ready());
}

// Bitmap::queueTextureUpdates runs once per frame from Context::endFrame. The
// backend consumes tex->Updates only while rendering a viewport, so a frame
// whose viewports were all hidden (a docked tab covered by another tab) leaves
// them pending; the next frame's writes must accumulate, not assert.
TEST(ImageTest, TextureUpdatesSurviveSkippedRender) {
  auto bitmap {std::make_unique<Bitmap>(2, 2, 4)};
  ImTextureData tex;
  tex.Status = ImTextureStatus_OK; // already uploaded by the backend
  unsigned int version {};

  struct { unsigned int size, alloc; double data[1]; } pixel {1, 1, {0xff0000ffp0}};
  auto pixels {reinterpret_cast<const reaper_array *>(&pixel)};

  bitmap->copyPixels<true>(0, 0, 1, 1, pixels);
  bitmap->queueTextureUpdates(&tex, version);
  ASSERT_EQ(tex.Updates.Size, 1);
  EXPECT_EQ(tex.Status, ImTextureStatus_WantUpdates);

  // no viewport rendered this frame: the rectangle was not consumed
  bitmap->copyPixels<true>(1, 1, 1, 1, pixels);
  ASSERT_NO_THROW(bitmap->queueTextureUpdates(&tex, version));
  ASSERT_EQ(tex.Updates.Size, 2);
  EXPECT_EQ(tex.Updates[0].x, 0);
  EXPECT_EQ(tex.Updates[1].x, 1);
  EXPECT_EQ(tex.Status, ImTextureStatus_WantUpdates);

  // a visible frame: the backend uploaded both and cleanupTextures cleared them
  tex.SetStatus(ImTextureStatus_OK);
  tex.Updates.resize(0);
  bitmap->queueTextureUpdates(&tex, version);
  EXPECT_EQ(tex.Updates.Size, 0);
  EXPECT_EQ(tex.Status, ImTextureStatus_OK);
}
