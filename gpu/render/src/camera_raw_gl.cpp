// Ported from PhotoCraft crates/algo/src/camera_raw.rs, camera_raw/local_tone.rs,
// camera_raw/whites_blacks.rs, fxutil.rs and photo_util.rs (bilinear)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten as OpenGL 3.3 fragment passes over float textures; the C++ reference
// (camera_raw_spatial.cpp) is the oracle.  Image statistics (quantiles, sorting) stay on the CPU.
#include <pixcake/gpu/camera_raw_gl.hpp>

#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>

#include "camera_raw_gl_shaders.hpp"

namespace pixcake::gpu::camera_raw_gl {

struct Slot {
  gl::Texture2D tex{nullptr};
  gl::Framebuffer fb{nullptr};
  std::uint32_t w = 0, h = 0;
  gl::PixelFormat format = gl::PixelFormat::R32F;
};

namespace {

// Free slots by size and format; shared with the planes' deleters so a plane dropped after its
// Gl only frees its texture.
struct Pool {
  std::multimap<std::tuple<std::uint32_t, std::uint32_t, int>, std::unique_ptr<Slot>> free;
};

}  // namespace

struct Gl::Impl {
  explicit Impl(gl::Context& c) : context(c) {}
  gl::Context& context;
  std::string error;
  bool failed = false;
  gl::Shader vertex{nullptr};
  std::map<const char*, gl::Program> programs;
  std::shared_ptr<Pool> pool = std::make_shared<Pool>();

  bool fail(const std::string& what) {
    if (!failed) error = what;
    failed = true;
    return false;
  }

  bool acquire(std::uint32_t w, std::uint32_t h, gl::PixelFormat format, Plane& out) {
    if (failed) return false;
    if (w == 0 || h == 0) return fail("plane size is zero");
    const auto key = std::make_tuple(w, h, static_cast<int>(format));
    std::unique_ptr<Slot> slot;
    const auto it = pool->free.find(key);
    if (it != pool->free.end()) {
      slot = std::move(it->second);
      pool->free.erase(it);
    } else {
      slot = std::make_unique<Slot>();
      slot->tex = context.createTexture2D();
      slot->fb = context.createFramebuffer();
      slot->w = w;
      slot->h = h;
      slot->format = format;
      std::string e;
      if (!slot->tex.valid() || !slot->fb.valid() || !slot->tex.allocate(w, h, format, e) || !slot->fb.attachColor(slot->tex, e) ||
          !slot->fb.complete(e)) {
        return fail("float plane: " + e);
      }
    }
    std::weak_ptr<Pool> weak = pool;
    out.slot = std::shared_ptr<Slot>(slot.release(), [weak, key](Slot* s) {
      if (auto p = weak.lock()) {
        p->free.emplace(key, std::unique_ptr<Slot>(s));
      } else {
        delete s;
      }
    });
    out.w = w;
    out.h = h;
    return true;
  }

  gl::Program* program(const char* body) {
    auto it = programs.find(body);
    if (it != programs.end()) return &it->second;
    std::string e;
    if (!vertex.valid()) {
      vertex = context.createShader();
      if (!vertex.valid() || !vertex.compile(gl::ShaderStage::Vertex, shaders::kVertex, e)) {
        fail("vertex shader: " + e);
        return nullptr;
      }
    }
    gl::Shader fragment = context.createShader();
    gl::Program linked = context.createProgram();
    if (!fragment.valid() || !linked.valid() ||
        !fragment.compile(gl::ShaderStage::Fragment, std::string(shaders::kHeader) + body, e) || !linked.link(vertex, fragment, e)) {
      fail("fragment shader: " + e);
      return nullptr;
    }
    return &programs.emplace(body, std::move(linked)).first->second;
  }

  // Draws `body` into `target` with inputs bound to t0, t1, ...; `uniforms` sets the rest.
  bool draw(const char* body, const Plane& target, std::initializer_list<const Plane*> inputs,
            const std::function<bool(gl::Program&, std::string&)>& uniforms = {}) {
    if (failed) return false;
    gl::Program* p = program(body);
    if (p == nullptr) return false;
    std::string e;
    bool ok = p->use(e) && target.slot->fb.bind(e) && context.setViewport(target.w, target.h, e);
    std::uint32_t unit = 0;
    for (const Plane* in : inputs) {
      static const char* const kNames[] = {"t0", "t1", "t2", "t3"};
      ok = ok && in->valid() && in->slot->tex.bind(unit, e) && p->setTextureUnit(kNames[unit], unit, e);
      ++unit;
    }
    ok = ok && (!uniforms || uniforms(*p, e)) && context.drawTriangles(3, e);
    return ok || fail(std::string("draw: ") + e);
  }

  bool read(const Plane& p, gl::PixelFormat format, std::vector<std::byte>& bytes) {
    if (failed) return false;
    std::string e;
    if (!p.valid() || !p.slot->fb.bind(e) || !context.readPixels(p.w, p.h, format, bytes, e)) return fail("readback: " + e);
    return true;
  }
};

Gl::Gl(gl::Context& context) : impl_(std::make_unique<Impl>(context)) {}
Gl::~Gl() = default;
bool Gl::ok() const { return !impl_->failed; }
const std::string& Gl::error() const { return impl_->error; }

namespace {

using gl::PixelFormat;

bool same_size_plane(Gl::Impl& g, const Plane& like, PixelFormat format, Plane& out) { return g.acquire(like.w, like.h, format, out); }

PixelFormat format_of(const Plane& p) { return p.slot->format; }

// Separable box mean of every channel, edge-clamped.
bool box(Gl::Impl& g, const Plane& in, std::size_t r, Plane& out) {
  if (r == 0) {
    Plane copy;
    if (!same_size_plane(g, in, format_of(in), copy) || !g.draw(shaders::kCopy, copy, {&in})) return false;
    out = copy;
    return true;
  }
  Plane rows, cols;
  const auto ri = static_cast<std::int32_t>(r);
  const auto pass = [ri](int axis) {
    return [ri, axis](gl::Program& p, std::string& e) { return p.setInt("radius", ri, e) && p.setInt("axis", axis, e); };
  };
  if (!same_size_plane(g, in, format_of(in), rows) || !g.draw(shaders::kBox, rows, {&in}, pass(0)) ||
      !same_size_plane(g, in, format_of(in), cols) || !g.draw(shaders::kBox, cols, {&rows}, pass(1))) {
    return false;
  }
  out = cols;
  return true;
}

// The guided filter's last steps on (I, p, Ip, II) packed in `packed`, whose means are taken
// with radius r; `guide` is I at full size and `scale` the subsampling (1: none).
bool guided_from_packed(Gl::Impl& g, const Plane& packed, const Plane& guide, std::size_t r, float eps, float scale, Plane& out) {
  Plane means, ab, mab, result;
  if (!box(g, packed, r, means) || !same_size_plane(g, packed, PixelFormat::Rgba32F, ab) ||
      !g.draw(shaders::kGuidedAb, ab, {&means}, [eps](gl::Program& p, std::string& e) { return p.setFloat("eps", eps, e); }) ||
      !box(g, ab, r, mab) || !same_size_plane(g, guide, PixelFormat::R32F, result) ||
      !g.draw(shaders::kGuidedOut, result, {&mab, &guide}, [scale](gl::Program& p, std::string& e) { return p.setFloat("scale", scale, e); })) {
    return false;
  }
  out = result;
  return true;
}

bool block_mean(Gl::Impl& g, const Plane& in, std::uint32_t block, PixelFormat format, Plane& out) {
  Plane small;
  const std::uint32_t w = (in.w + block - 1) / block, h = (in.h + block - 1) / block;
  if (!g.acquire(w, h, format, small) ||
      !g.draw(shaders::kBlockMean, small, {&in}, [block](gl::Program& p, std::string& e) {
        return p.setInt("block", static_cast<std::int32_t>(block), e);
      })) {
    return false;
  }
  out = small;
  return true;
}

}  // namespace

bool upload(Gl& gl_, const std::vector<float>& v, std::uint32_t w, std::uint32_t h, Plane& out) {
  auto& g = gl_.impl();
  if (v.size() != std::size_t{w} * h) return g.fail("upload: size mismatch");
  Plane p;
  if (!g.acquire(w, h, PixelFormat::R32F, p)) return false;
  const gl::ImageView view{w, h, w * 4, PixelFormat::R32F, reinterpret_cast<const std::byte*>(v.data()), v.size() * sizeof(float)};
  std::string e;
  if (!p.slot->tex.upload(view, e)) return g.fail("upload: " + e);
  out = p;
  return true;
}

bool download(Gl& gl_, const Plane& p, std::vector<float>& out) {
  auto& g = gl_.impl();
  std::vector<std::byte> bytes;
  if (!g.read(p, PixelFormat::R32F, bytes)) return false;
  out.resize(std::size_t{p.w} * p.h);
  std::memcpy(out.data(), bytes.data(), out.size() * sizeof(float));
  return true;
}

bool boxMean(Gl& gl_, const Plane& in, std::size_t r, Plane& out) { return box(gl_.impl(), in, r, out); }

bool gauss(Gl& gl_, const Plane& in, float sigma, Plane& out) {
  auto& g = gl_.impl();
  Plane cur = in;
  if (sigma >= 0.3F) {
    for (const std::size_t r : camera_raw::gaussBoxRadii(sigma)) {
      Plane next;
      if (!box(g, cur, r, next)) return false;
      cur = next;
    }
  } else if (!box(g, in, 0, cur)) {
    return false;
  }
  out = cur;
  return true;
}

bool guided(Gl& gl_, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out) {
  auto& g = gl_.impl();
  Plane packed;
  if (!same_size_plane(g, i, PixelFormat::Rgba32F, packed) || !g.draw(shaders::kGuidedPack, packed, {&i, &p})) return false;
  return guided_from_packed(g, packed, i, r, eps, 1.0F, out);
}

bool guidedFast(Gl& gl_, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out) {
  auto& g = gl_.impl();
  const std::size_t s = std::clamp<std::size_t>(r / 8, 1, 8);
  if (s == 1) return guided(gl_, i, p, r, eps, out);
  // Means of I and p over s x s cells first, then the products on the small copy (as the CPU).
  Plane si, sp, packed;
  if (!block_mean(g, i, static_cast<std::uint32_t>(s), PixelFormat::R32F, si) ||
      !block_mean(g, p, static_cast<std::uint32_t>(s), PixelFormat::R32F, sp) ||
      !same_size_plane(g, si, PixelFormat::Rgba32F, packed) || !g.draw(shaders::kGuidedPack, packed, {&si, &sp})) {
    return false;
  }
  return guided_from_packed(g, packed, i, std::max<std::size_t>(r / s, 1), eps, static_cast<float>(s), out);
}

bool rankFilter(Gl& gl_, const Plane& in, std::size_t r, bool take_max, Plane& out) {
  auto& g = gl_.impl();
  Plane rows, cols;
  const auto pass = [r, take_max](int axis) {
    return [r, take_max, axis](gl::Program& p, std::string& e) {
      return p.setInt("radius", static_cast<std::int32_t>(r), e) && p.setInt("axis", axis, e) && p.setInt("take_max", take_max ? 1 : 0, e);
    };
  };
  if (!same_size_plane(g, in, PixelFormat::R32F, rows) || !g.draw(shaders::kRank, rows, {&in}, pass(0)) ||
      !same_size_plane(g, in, PixelFormat::R32F, cols) || !g.draw(shaders::kRank, cols, {&rows}, pass(1))) {
    return false;
  }
  out = cols;
  return true;
}

}  // namespace pixcake::gpu::camera_raw_gl
