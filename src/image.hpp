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

#ifndef REAIMGUI_IMAGE_HPP
#define REAIMGUI_IMAGE_HPP

#include "image_loader.hpp"
#include "resource.hpp"

#include <istream>
#include <memory>
#include <string>
#include <vector>

class Context;
class LICE_IBitmap;
struct ImTextureData;
struct ImTextureRef;
struct reaper_array;

class Image : public Resource {
public:
  enum State { Failed = -1, Pending = 0, Ready = 1 };

  struct RegisterType {
    using TestFunc   = bool (*)(std::istream &);
    using DecodeFunc = void (*)(std::istream &, PixelBuffer &);

    RegisterType(TestFunc, DecodeFunc);

    const TestFunc   m_test;
    const DecodeFunc m_decode;
    const RegisterType * const m_next;
  };

  // Async images return immediately and decode on a worker thread; poll
  // state() or block with wait(). Synchronous loading throws on failure.
  static Image *fromFile(const char *, bool async = false);
  static Image *fromMemory(const char *, int size, bool async = false);
  static void decode(std::istream &, PixelBuffer &); // any registered format

  virtual size_t width()  const = 0;
  virtual size_t height() const = 0;
  virtual ImTextureRef texture(Context *) = 0;
  virtual State state() { return Ready; }
  virtual State wait(double) { return state(); }
  virtual const std::string &error() const;
  bool ready() { return state() == Ready; }

  bool attachable(const Context *) const override { return true; }
};

API_REGISTER_OBJECT_TYPE(Image);

class Bitmap : public Image {
public:
  Bitmap(int width, int height, int format);
  ~Bitmap();

  size_t width()  const override { return m_pixels.width();  }
  size_t height() const override { return m_pixels.height(); }
  ImTextureRef texture(Context *) override;
  State state() override;
  State wait(double timeoutSeconds) override;
  const std::string &error() const override { return m_error; }

  SubresourceData install(Context *) override;
  void update(Context *, void *) override;

  template<bool Write>
  void copyPixels(int x, int y,
    unsigned int w, unsigned int h,
    std::conditional_t<Write, const reaper_array *, reaper_array *>,
    unsigned int offset = 0, unsigned int pitch = 0);

  // decodes from a path or encoded bytes, now or on a worker thread
  void load(ImageLoader::Job::Source, std::string input, bool async);

  struct Installs;

protected:
  Bitmap();

  bool heartbeat() override;
  PixelBuffer &pixels() { return m_pixels; }

private:
  struct Update;
  friend Image *Image::fromFile(const char *, bool);
  friend Image *Image::fromMemory(const char *, int, bool);

  void poll();         // adopts a finished decoding job
  void ensurePixels(); // decodes again after releasePixels
  void releasePixels();

  PixelBuffer m_pixels;
  std::vector<Update> m_updates;
  std::shared_ptr<Installs> m_installs;
  std::shared_ptr<ImageLoader::Job> m_job;
  std::string m_source, m_error;
  ImageLoader::Job::Source m_sourceKind;
  unsigned int m_version;
  State m_state;
  bool m_hasSource, m_keepPixels;
};

API_REGISTER_OBJECT_TYPE(Bitmap);

class LICEBitmap : public Bitmap {
public:
  LICEBitmap(LICE_IBitmap *bitmap);
};

class ImageSet final : public Image {
public:
  void add(float scale, Image *);

  size_t width() const override;
  size_t height() const override;
  ImTextureRef texture(Context *) override;
  State state() override;
  State wait(double) override;
  const std::string &error() const override;

protected:
  bool heartbeat() override;

private:
  struct Item {
    Item(float scale, Image *image) : image {image}, scale {scale} {}
    Image *image;
    float scale;
    bool operator<(float targetScale) const { return scale < targetScale; }
  };

  const Item &select() const;

  std::vector<Item> m_images;
};

API_REGISTER_OBJECT_TYPE(ImageSet);

#endif
