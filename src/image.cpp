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

#include "image.hpp"

#include "color.hpp"
#include "context.hpp"
#include "error.hpp"

#include <algorithm>
#include <cmath> // abs
#include <imgui/imgui.h>
#include <reaper_plugin_functions.h>

static const Image::RegisterType *&typeHead()
{
  static const Image::RegisterType *head;
  return head;
}

Image::RegisterType::RegisterType(const TestFunc test, const DecodeFunc decode)
  : m_test {test}, m_decode {decode}, m_next {typeHead()}
{
  typeHead() = this;
}

void Image::decode(std::istream &stream, PixelBuffer &pixels)
{
  for(const Image::RegisterType *type {typeHead()}; type; type = type->m_next) {
    if(type->m_test(stream)) {
      type->m_decode(stream, pixels);
      return;
    }
    else
      stream.seekg(0);
  }

  throw reascript_error {"unsupported format"};
}

const std::string &Image::error() const
{
  static const std::string none;
  return none;
}

Image *Image::fromFile(const char *file, const bool async)
{
  // Images are exempt from the short-lived resource guard: a scrolling image
  // grid legitimately creates and evicts them for hundreds of frames in a row.
  Resource::bypassGCCheckOnce();
  std::unique_ptr<Bitmap> bitmap {new Bitmap};
  bitmap->load(ImageLoader::Job::File, file, async);
  return bitmap.release();
}

Image *Image::fromMemory(const char *data, const int size, const bool async)
{
  Resource::bypassGCCheckOnce();
  std::unique_ptr<Bitmap> bitmap {new Bitmap};
  bitmap->load(ImageLoader::Job::Memory,
    {data, static_cast<size_t>(std::max(0, size))}, async);
  return bitmap.release();
}

struct Bitmap::Update {
  Update(unsigned short x, unsigned short y, unsigned short w, unsigned short h,
      unsigned int v)
    : rect {x, y, w, h}, version {v}, age{}
  {}

  ImTextureRect rect;
  unsigned int version;
  unsigned char age;
};

// Shared between a Bitmap and the textures installed from it, so an uninstall
// that outlives the Bitmap (Subresource eviction of a collected resource) can
// still unregister its texture.
struct Bitmap::Installs {
  std::vector<ImTextureData *> textures;
};

struct ImageTextureData {
  ImTextureData *tex;
  std::shared_ptr<Bitmap::Installs> installs;
  unsigned int version;
};

Bitmap::Bitmap()
  : m_installs {std::make_shared<Installs>()},
    m_sourceKind {ImageLoader::Job::File}, m_version {}, m_state {Ready},
    m_hasSource {false}, m_keepPixels {false}
{}

Bitmap::~Bitmap()
{
  if(m_job)
    m_job->cancelled = true;
}

Bitmap::Bitmap(const int width, const int height, const int format)
  : Bitmap {}
{
  m_pixels.resize(width, height, format);
}

LICEBitmap::LICEBitmap(LICE_IBitmap *bitmap)
{
  PixelBuffer &buffer {pixels()};
  buffer.resize(LICE__GetWidth(bitmap), LICE__GetHeight(bitmap), 4);

  const auto stride {LICE__GetRowSpan(bitmap)};
  auto pixels {static_cast<const LICE_pixel *>(LICE__GetBits(bitmap))};

  for(unsigned char *row : buffer.makeScanlines()) {
    for(size_t i {}; i < buffer.width(); ++i) {
      reinterpret_cast<uint32_t *>(row)[i] = (pixels[i] & 0xFF00FF00) |
        (pixels[i] >> 16 & 0x0000FF) | (pixels[i] << 16 & 0xFF0000);
    }
    pixels += stride;
  }
}

void Bitmap::load(const ImageLoader::Job::Source source, std::string input,
  const bool async)
{
  m_sourceKind = source;
  m_hasSource = true;
  if(async) {
    m_state = Pending;
    m_job = ImageLoader::enqueue(source, input);
  }
  else
    ImageLoader::decode(source, input, m_pixels);
  m_source = std::move(input);
}

void Bitmap::poll()
{
  if(!m_job || !m_job->ready())
    return;

  std::shared_ptr<ImageLoader::Job> job {std::move(m_job)};
  std::lock_guard lock {job->mutex};
  if(job->error.empty()) {
    m_pixels = std::move(job->pixels);
    m_state = Ready;
  }
  else {
    m_error = std::move(job->error);
    m_state = Failed;
  }
}

Image::State Bitmap::state()
{
  poll();
  return m_state;
}

Image::State Bitmap::wait(const double timeoutSeconds)
{
  if(m_job)
    m_job->wait(timeoutSeconds);
  return state();
}

bool Bitmap::heartbeat()
{
  if(!Resource::heartbeat())
    return false;

  poll();

  auto it {m_updates.begin()};
  while(it != m_updates.end()) {
    // must leave 1 grace frame because Context's heartbeat may be called after ours
    constexpr unsigned char UPDATE_TTL {Context::SUBRESOURCE_TTL + 1};
    if(++it->age >= UPDATE_TTL)
      it = m_updates.erase(it);
    else
      ++it;
  }

  return true;
}

void Bitmap::ensurePixels()
{
  if(!m_pixels.empty())
    return;
  if(!m_hasSource || m_state != Ready)
    throw reascript_error {"image pixels are not available"};
  ImageLoader::decode(m_sourceKind, m_source, m_pixels);
}

// The CPU copy is only needed until every context's texture has been
// uploaded. Releasing it halves the memory of a loaded image; a later install
// on another context (or a backend re-creating the texture) decodes the
// source again.
void Bitmap::releasePixels()
{
  if(m_keepPixels || !m_hasSource || m_pixels.empty() || !m_updates.empty())
    return;
  for(ImTextureData *tex : m_installs->textures) {
    if(tex->Status != ImTextureStatus_OK)
      return;
  }
  for(ImTextureData *tex : m_installs->textures)
    tex->Pixels = nullptr;
  m_pixels.release();
}

template void Bitmap::copyPixels<false>(int, int, unsigned int, unsigned int,
  reaper_array *, unsigned int, unsigned int);
template void Bitmap::copyPixels<true>(int, int, unsigned int, unsigned int,
  const reaper_array *, unsigned int, unsigned int);

template<bool Write>
void Bitmap::copyPixels(int x, int y, unsigned int w, unsigned int h,
  std::conditional_t<Write, const reaper_array*, reaper_array*> pixels,
  unsigned int offset, unsigned int pitch)
{
  if(!ready())
    throw reascript_error {"image is not ready"};

  const int width {m_pixels.width()}, height {m_pixels.height()};
  if(x >= width || y >= height)
    return;

  if(!pitch)
    pitch = w;
  else if(pitch < w)
    throw reascript_error {"pitch is smaller than width"};

  if(offset >= pixels->size || pitch * h > pixels->size - offset)
    throw reascript_error
      {"pixel array size must be at least {}", offset + (pitch * h)};

  if(x < 0)
    w += x, offset -= x, x = 0;
  if(y < 0)
    h += y, offset -= y * pitch, y = 0;

  w = std::min<unsigned int>(w, width  - x);
  h = std::min<unsigned int>(h, height - y);
  if(w < 1 || h < 1)
    return;

  // scripts that read or write pixels keep them resident from now on
  ensurePixels();
  m_keepPixels = true;
  for(ImTextureData *tex : m_installs->textures) {
    if(!tex->Pixels)
      tex->Pixels = m_pixels.data();
  }

  const auto outPitch {width * 4};
  auto *out {m_pixels.data() + (x + (y * width)) * 4};
  auto *in {pixels->data + offset};

  for(unsigned short iy {}; iy < h; ++iy) {
    for(unsigned short ix {}; ix < w; ++ix) {
      auto pixel {reinterpret_cast<uint32_t *>(&out[ix * 4])};
      if constexpr(Write) // double to int overflow is undefined behavior!
        *pixel = Color::fromBigEndian(static_cast<int64_t>(in[ix]));
      else
        in[ix] = Color::toBigEndian(*pixel);
    }
    out += outPitch, in += pitch;
  }

  if constexpr(Write)
    m_updates.emplace_back(x, y, w, h, ++m_version);
}

ImTextureRef Bitmap::texture(Context *ctx)
{
  if(!ready())
    throw reascript_error {"image is not ready"};
  return ctx->touch<ImageTextureData>(this)->tex->GetTexRef();
}

static void uninstall(Context *, ImageTextureData *data)
{
  auto &textures {data->installs->textures};
  textures.erase(std::find(textures.begin(), textures.end(), data->tex));
  data->tex->Status = ImTextureStatus_WantDestroy;
  data->tex->Pixels = nullptr; // ensure it can't accidentally be used after free
  delete data;
}

SubresourceData Bitmap::install(Context *ctx)
{
  ensurePixels();

  ImTextureData *tex {ctx->createTexture()};
  tex->UniqueID = uniqId();
  tex->Status = ImTextureStatus_WantCreate;
  tex->Format = ImTextureFormat_RGBA32;
  tex->Width = m_pixels.width();
  tex->Height = m_pixels.height();
  tex->BytesPerPixel = 4;
  tex->Pixels = m_pixels.data();
  tex->RefCount = 1;
  m_installs->textures.push_back(tex);

  return {new ImageTextureData {tex, m_installs, m_version}, &uninstall};
}

void Bitmap::update(Context *, void *user)
{
  auto data {static_cast<ImageTextureData *>(user)};
  ImTextureData *tex {data->tex};

  // a backend re-creating the texture needs the pixels back
  if(tex->Status == ImTextureStatus_WantCreate && !tex->Pixels) {
    ensurePixels();
    tex->Pixels = m_pixels.data();
  }

  queueTextureUpdates(tex, data->version);
  releasePixels();
}

void Bitmap::queueTextureUpdates(ImTextureData *tex, unsigned int &version)
{
  // The backend consumes tex->Updates only while rendering a viewport. A
  // frame in which none of the context's viewports is visible (its docked tab
  // hidden behind another tab, a minimized window) renders nothing, so the
  // rectangles queued last frame are still pending: keep them and append.
  const auto prev {std::find_if(m_updates.rbegin(), m_updates.rend(),
    [version](const Update &u) { return u.version == version; })};
  for(auto it = prev.base(); it != m_updates.end(); ++it)
    tex->Updates.push_back(it->rect);
  version = m_version;
  if(tex->Status == ImTextureStatus_OK && tex->Updates.Size > 0)
    tex->SetStatus(ImTextureStatus_WantUpdates);
}

void ImageSet::add(const float scale, Image *img)
{
  // don't allow infinite recursion
  if(dynamic_cast<ImageSet *>(img))
    throw reascript_error {"image cannot be a set"};

  const auto it {std::lower_bound(m_images.begin(), m_images.end(), scale)};
  if(it != m_images.end() && it->scale == scale)
    throw reascript_error {"scale is already in the set"};

  m_images.emplace(it, scale, img);
}

const ImageSet::Item &ImageSet::select() const
{
  if(m_images.empty())
    throw reascript_error {"image set is empty"};

  const float scale {ImGui::GetWindowDpiScale()};
  const auto it {std::lower_bound(m_images.begin(), m_images.end(), scale)};
  if(it == m_images.begin())
    return *it;
  else if(it == m_images.end())
    return m_images.back();

  const auto prev {std::prev(it)};
  if(std::abs(prev->scale - scale) < std::abs(it->scale - scale))
    return *prev;
  else
    return *it;
}

size_t ImageSet::width() const
{
  const Item &item {select()};
  return item.image->width() / item.scale;
}

size_t ImageSet::height() const
{
  const Item &item {select()};
  return item.image->height() / item.scale;
}

ImTextureRef ImageSet::texture(Context *ctx)
{
  return select().image->texture(ctx);
}

Image::State ImageSet::state()
{
  return select().image->state();
}

Image::State ImageSet::wait(const double timeoutSeconds)
{
  return select().image->wait(timeoutSeconds);
}

const std::string &ImageSet::error() const
{
  return select().image->error();
}

bool ImageSet::heartbeat()
{
  if(!Resource::heartbeat())
    return false;

  for(const auto &item : m_images)
    item.image->keepAlive();

  return true;
}
