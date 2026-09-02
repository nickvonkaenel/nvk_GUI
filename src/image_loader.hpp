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

#ifndef REAIMGUI_IMAGE_LOADER_HPP
#define REAIMGUI_IMAGE_LOADER_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Decoded RGBA8 pixels with no Resource identity. Decoders fill one of these
// on whichever thread runs them; a Bitmap adopts it on the main thread.
class PixelBuffer {
public:
  PixelBuffer() : m_width {}, m_height {} {}

  void resize(int width, int height, int format);
  std::vector<unsigned char *> makeScanlines();

  unsigned short width()  const { return m_width;  }
  unsigned short height() const { return m_height; }
  bool empty() const { return m_pixels.empty(); }
  unsigned char *data() { return m_pixels.data(); }
  const unsigned char *data() const { return m_pixels.data(); }
  void release() { std::vector<unsigned char>().swap(m_pixels); }

private:
  std::vector<unsigned char> m_pixels;
  unsigned short m_width, m_height;
};

// Background image decoding. Jobs are decoded FIFO by a small pool of worker
// threads started on first use and joined at plugin unload. The main thread
// polls a job (or waits on it with a timeout) and adopts the result.
class ImageLoader {
public:
  struct Job {
    enum Source { File, Memory };

    Job(Source source, std::string input)
      : source {source}, input {std::move(input)}, done {false} {}

    bool ready(); // done, whether successfully or not
    bool wait(double timeoutSeconds);

    const Source source;
    const std::string input; // path or encoded bytes
    std::atomic<bool> cancelled {false};

    std::mutex mutex;
    std::condition_variable cv;
    bool done;
    std::string error; // set when done and decoding failed
    PixelBuffer pixels;
  };

  static std::shared_ptr<Job> enqueue(Job::Source, std::string input);
  static void teardown(); // joins the workers (blocking)

  // decodes synchronously on the calling thread (throws reascript_error)
  static void decode(Job::Source, const std::string &input, PixelBuffer &);

private:
  static void worker();
};

#endif
