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

#include "image_loader.hpp"

#include "error.hpp"
#include "image.hpp"
#include "win32_unicode.hpp"

#include <algorithm>
#include <boost/iostreams/stream.hpp>
#include <chrono>
#include <cstring> // strerror
#include <deque>
#include <fstream>
#include <thread>

void PixelBuffer::resize(const int width, const int height, const int format)
try
{
  constexpr int MAX_SIZE {0x2000}; // Direct3D10 Texture2D limit

  if(format != 4)
    throw reascript_error {"BUG: unexpected pixel format, missing transform?"};
  if(height > MAX_SIZE || width > MAX_SIZE)
    throw reascript_error {"image is too big"};
  m_width = width, m_height = height;
  m_pixels.resize(m_width * m_height * format);
}
catch(const std::bad_alloc &)
{
  throw reascript_error {"cannot allocate memory"};
}

std::vector<unsigned char *> PixelBuffer::makeScanlines()
{
  std::vector<unsigned char *> scanlines;
  scanlines.reserve(m_height);
  const auto rowStride {m_width * 4};
  for(auto it {m_pixels.begin()}; it < m_pixels.end(); it += rowStride)
    scanlines.push_back(&*it);
  return scanlines;
}

bool ImageLoader::Job::ready()
{
  std::lock_guard lock {mutex};
  return done;
}

bool ImageLoader::Job::wait(const double timeoutSeconds)
{
  std::unique_lock lock {mutex};
  if(done)
    return true;
  const auto timeout {std::chrono::duration<double>(std::max(0.0, timeoutSeconds))};
  return cv.wait_for(lock, timeout, [this] { return done; });
}

namespace {
struct Pool {
  ~Pool() { ImageLoader::teardown(); } // a joinable thread would terminate()

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::shared_ptr<ImageLoader::Job>> queue;
  std::vector<std::thread> workers;
  bool quit {false};
};

Pool &pool()
{
  static Pool instance;
  return instance;
}

unsigned int workerCount()
{
  constexpr unsigned int MAX_WORKERS {6};
  const unsigned int cores {std::thread::hardware_concurrency()};
  return std::clamp(cores > 1 ? cores - 1 : 1u, 1u, MAX_WORKERS);
}
}

void ImageLoader::decode(const Job::Source source, const std::string &input,
  PixelBuffer &pixels)
{
  if(source == Job::File) {
    std::ifstream stream;
    stream.open(WIDEN(input.c_str()), std::ios_base::binary);
    if(!stream.good())
      throw reascript_error {strerror(errno)};
    Image::decode(stream, pixels);
  }
  else {
    using boost::iostreams::array_source;
    boost::iostreams::stream<array_source> stream
      {input.data(), static_cast<std::streamsize>(input.size())};
    Image::decode(stream, pixels);
  }
}

std::shared_ptr<ImageLoader::Job> ImageLoader::enqueue(
  const Job::Source source, std::string input)
{
  auto job {std::make_shared<Job>(source, std::move(input))};
  Pool &p {pool()};
  {
    std::lock_guard lock {p.mutex};
    if(p.quit)
      throw reascript_error {"image loader is shutting down"};
    if(p.workers.empty()) {
      const unsigned int count {workerCount()};
      p.workers.reserve(count);
      for(unsigned int i {}; i < count; ++i)
        p.workers.emplace_back(&ImageLoader::worker);
    }
    p.queue.push_back(job);
  }
  p.cv.notify_one();
  return job;
}

void ImageLoader::worker()
{
  Pool &p {pool()};
  while(true) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock {p.mutex};
      p.cv.wait(lock, [&p] { return p.quit || !p.queue.empty(); });
      if(p.quit)
        return;
      job = std::move(p.queue.front());
      p.queue.pop_front();
    }

    std::string error;
    PixelBuffer pixels;
    if(job->cancelled)
      error = "cancelled";
    else {
      try {
        decode(job->source, job->input, pixels);
      }
      catch(const std::exception &e) {
        error = e.what();
        if(error.empty())
          error = "unknown decoding error";
      }
      catch(...) {
        error = "unknown decoding error";
      }
    }

    {
      std::lock_guard lock {job->mutex};
      job->error = std::move(error);
      job->pixels = std::move(pixels);
      job->done = true;
    }
    job->cv.notify_all();
  }
}

void ImageLoader::teardown()
{
  Pool &p {pool()};
  std::vector<std::thread> workers;
  {
    std::lock_guard lock {p.mutex};
    p.quit = true;
    p.queue.clear();
    workers.swap(p.workers);
  }
  p.cv.notify_all();
  for(std::thread &worker : workers)
    worker.join();
  // allow a later reload of the plugin in the same process to start over
  std::lock_guard lock {p.mutex};
  p.quit = false;
}
