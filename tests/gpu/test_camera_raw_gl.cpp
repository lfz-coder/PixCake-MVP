// The OpenGL Camera Raw whole-frame stages (camera_raw_gl.cpp) against the C++ reference
// (camera_raw_spatial.cpp), which matches PhotoCraft bit for bit.  Tolerance 1/4096 (porting
// policy section 8, after PhotoCraft's Camera Raw GPU tests).  Skips without a native context.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/camera_raw_gl.hpp>
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include "spatial_frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(x)                                                         \
  do {                                                                   \
    if (!(x)) {                                                          \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);  \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

namespace {

namespace cr = pixcake::gpu::camera_raw;
namespace crgl = pixcake::gpu::camera_raw_gl;
constexpr float kTolerance = 1.0F / 4096.0F;

std::vector<float> noise(std::uint32_t w, std::uint32_t h, std::uint32_t seed) {
  std::uint32_t s = seed * 2654435761U | 1U;
  std::vector<float> v(std::size_t{w} * h);
  for (float& x : v) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    x = static_cast<float>(s % 40000U) / 10000.0F - 1.0F;  // -1 .. 3
  }
  return v;
}

float worst_of(const std::vector<float>& a, const std::vector<float>& b) {
  CHECK(a.size() == b.size());
  float worst = 0.0F;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const float d = std::fabs(a[i] - b[i]);
    worst = std::isnan(d) ? 1e30F : std::max(worst, d);
  }
  return worst;
}
void expect_close(const char* name, const std::vector<float>& want, const std::vector<float>& got, float tolerance = kTolerance) {
  const float worst = worst_of(want, got);
  std::printf("%-40s worst %.3g\n", name, worst);
  CHECK(worst <= tolerance);
}

crgl::Plane up(crgl::Gl& g, const std::vector<float>& v, std::uint32_t w, std::uint32_t h) {
  crgl::Plane p;
  CHECK(crgl::upload(g, v, w, h, p));
  return p;
}
std::vector<float> down(crgl::Gl& g, const crgl::Plane& p) {
  std::vector<float> v;
  CHECK(crgl::download(g, p, v));
  return v;
}

struct Size {
  std::uint32_t w, h;
};
const Size kSizes[] = {{37, 23}, {1, 9}, {9, 1}};

void test_box_mean_gl(crgl::Gl& g) {
  for (const auto s : kSizes) {
    const auto v = noise(s.w, s.h, s.w * 7 + s.h);
    const auto in = up(g, v, s.w, s.h);
    for (const std::size_t r : {1, 3, 12}) {
      crgl::Plane out;
      CHECK(crgl::boxMean(g, in, r, out));
      char name[64];
      std::snprintf(name, sizeof name, "boxMean %ux%u r=%zu", s.w, s.h, r);
      expect_close(name, cr::boxMean(v, s.w, s.h, r), down(g, out));
    }
  }
}

void test_gauss_gl(crgl::Gl& g) {
  const auto v = noise(37, 23, 3);
  const auto in = up(g, v, 37, 23);
  for (const float sigma : {0.2F, 1.5F, 4.8F}) {
    crgl::Plane out;
    CHECK(crgl::gauss(g, in, sigma, out));
    char name[64];
    std::snprintf(name, sizeof name, "gauss sigma=%.1f", sigma);
    expect_close(name, cr::gauss(v, 37, 23, sigma), down(g, out));
  }
}

void test_guided_gl(crgl::Gl& g, bool fast) {
  const std::uint32_t w = 37, h = 23;
  const auto i = noise(w, h, 11), p = noise(w, h, 12), grey = std::vector<float>(std::size_t{w} * h, 0.5F);
  const auto gi = up(g, i, w, h), gp = up(g, p, w, h), gg = up(g, grey, w, h);
  for (const std::size_t r : {3, 24, 81}) {
    for (const float eps : {0.005F, 1e-3F}) {
      crgl::Plane out, flat;
      CHECK(fast ? crgl::guidedFast(g, gi, gp, r, eps, out) : crgl::guided(g, gi, gp, r, eps, out));
      CHECK(fast ? crgl::guidedFast(g, gg, gg, r, eps, flat) : crgl::guided(g, gg, gg, r, eps, flat));
      char name[64];
      std::snprintf(name, sizeof name, "%s r=%zu eps=%g", fast ? "guidedFast" : "guided", r, eps);
      expect_close(name, fast ? cr::guidedFast(i, p, w, h, r, eps) : cr::guided(i, p, w, h, r, eps), down(g, out));
      std::snprintf(name, sizeof name, "%s grey r=%zu eps=%g", fast ? "guidedFast" : "guided", r, eps);
      expect_close(name, fast ? cr::guidedFast(grey, grey, w, h, r, eps) : cr::guided(grey, grey, w, h, r, eps), down(g, flat));
    }
  }
}

void test_rank_filter_gl(crgl::Gl& g) {
  for (const auto s : kSizes) {
    const auto v = noise(s.w, s.h, 5);
    const auto in = up(g, v, s.w, s.h);
    for (const std::size_t r : {2, 12}) {
      for (const bool take_max : {false, true}) {
        crgl::Plane out;
        CHECK(crgl::rankFilter(g, in, r, take_max, out));
        char name[64];
        std::snprintf(name, sizeof name, "rankFilter %ux%u r=%zu %s", s.w, s.h, r, take_max ? "max" : "min");
        expect_close(name, cr::rankFilter(v, s.w, s.h, r, take_max), down(g, out), 0.0F);
      }
    }
  }
}

// ---- Whole frames

const char* const kFrames[] = {"detail", "grey", "block", "strip", "scene", "scaled"};
// The 1700 x 12 frame (wider than local_tone's 1600 px work side) is for Highlights/Shadows.
cr::Frame frame_named(const std::string& name) {
  return name == "scene" ? spatial_frames::frame_of_image(spatial_frames::scene(400, 300)) : spatial_frames::frame_of(name);
}

// Runs `on_gpu` on the uploaded frame and `on_cpu` on a copy; compares every colour channel.
template <class Gpu, class Cpu>
void expect_frame(crgl::Gl& g, const char* what, const std::string& frame, const Gpu& on_gpu, const Cpu& on_cpu) {
  cr::Frame cpu = frame_named(frame), got;
  crgl::Plane rgba;
  CHECK(crgl::uploadFrame(g, cpu, rgba));
  CHECK(on_gpu(rgba));
  CHECK(crgl::downloadFrame(g, rgba, got));
  on_cpu(cpu);
  CHECK(got.width == cpu.width && got.height == cpu.height && got.px.size() == cpu.px.size());
  float worst = 0.0F;
  std::size_t at = 0;
  for (std::size_t i = 0; i < cpu.px.size(); ++i) {
    for (int c = 0; c < 4; ++c) {
      const float d = std::fabs(got.px[i][c] - cpu.px[i][c]);
      if (std::isnan(d) || d > worst) {
        worst = std::isnan(d) ? 1e30F : d;
        at = i;
      }
    }
  }
  std::printf("%-28s %-7s worst %.3g (pixel %zu)\n", what, frame.c_str(), worst, at);
  CHECK(worst <= kTolerance);
}

void test_dehaze_gl(crgl::Gl& g) {
  for (const char* frame : kFrames) {
    for (const float amount : {0.3F, -0.3F, 1.0F, -1.0F}) {
      char name[48];
      std::snprintf(name, sizeof name, "dehaze %+.0f", amount * 100.0F);
      expect_frame(g, name, frame, [&](crgl::Plane& p) { return crgl::dehaze(g, p, amount); },
                   [&](cr::Frame& f) { cr::dehaze(f, amount); });
    }
  }
}

void test_tone_detail_gl(crgl::Gl& g) {
  struct Case {
    const char* name;
    float contrast, texture, clarity, pixel_scale;
  };
  const Case cases[] = {{"texture 100", 0, 100, 0, 1}, {"clarity 100", 0, 0, 100, 1}, {"texture -50 ps 0.5", 0, -50, 0, 0.5F},
                        {"contrast 40", 40, 0, 0, 1}, {"clarity -50 contrast 40 texture -30", 40, -30, -50, 1}};
  for (const char* frame : kFrames) {
    for (const auto& c : cases) {
      expect_frame(g, c.name, frame, [&](crgl::Plane& p) { return crgl::toneDetail(g, p, c.contrast, c.texture, c.clarity, c.pixel_scale); },
                   [&](cr::Frame& f) { cr::toneDetail(f, c.contrast, c.texture, c.clarity, c.pixel_scale); });
    }
  }
}

void test_local_tone_gl(crgl::Gl& g) {
  struct Case {
    const char* name;
    float shadows, highlights;
  };
  const Case cases[] = {{"shadows 100", 100, 0}, {"highlights -100", 0, -100}, {"shadows -50 highlights 50", -50, 50}};
  for (const char* frame : {"detail", "grey", "block", "strip", "scene", "scaled", "long"}) {
    for (const auto& c : cases) {
      expect_frame(g, c.name, frame, [&](crgl::Plane& p) { return crgl::localTone(g, p, c.shadows, c.highlights); },
                   [&](cr::Frame& f) { cr::localTone(f, c.shadows, c.highlights); });
    }
  }
}

// Frames below 2 px on a side return untouched (the same bits); 2 px frames have a one-level
// pyramid, whose change is 0 but whose sRGB round trip runs on the GPU's pow.
void test_local_tone_gl_tiny(crgl::Gl& g) {
  struct Size {
    std::uint32_t w, h;
    float tolerance;
  };
  for (const Size s : {Size{1, 1, 0.0F}, Size{1, 5, 0.0F}, Size{7, 1, 0.0F}, Size{2, 2, kTolerance}, Size{2, 7, kTolerance}}) {
    cr::Frame cpu;
    cpu.width = s.w;
    cpu.height = s.h;
    for (std::size_t i = 0; i < std::size_t{s.w} * s.h; ++i) cpu.px.push_back(spatial_frames::detail(i % s.w, i / s.w, s.w + 1, s.h + 1));
    cr::Frame got;
    crgl::Plane rgba;
    CHECK(crgl::uploadFrame(g, cpu, rgba) && crgl::localTone(g, rgba, 60, -60) && crgl::downloadFrame(g, rgba, got));
    cr::localTone(cpu, 60, -60);
    float worst = 0.0F;
    for (std::size_t i = 0; i < cpu.px.size(); ++i)
      for (int c = 0; c < 4; ++c) worst = std::max(worst, std::fabs(got.px[i][c] - cpu.px[i][c]));
    std::printf("localTone tiny %ux%u worst %.3g\n", s.w, s.h, worst);
    CHECK(worst <= s.tolerance && g.ok());
  }
}

void test_whites_blacks_gl(crgl::Gl& g) {
  struct Case {
    const char* name;
    float whites, blacks;
  };
  const Case cases[] = {{"whites 100", 100, 0}, {"whites -100", -100, 0}, {"blacks 100", 0, 100}, {"blacks -100", 0, -100},
                        {"whites 50 blacks -50", 50, -50}, {"whites -50 blacks 50", -50, 50}};
  for (const char* frame : kFrames) {
    for (const auto& c : cases) {
      expect_frame(g, c.name, frame, [&](crgl::Plane& p) { return crgl::whitesBlacks(g, p, c.whites, c.blacks); },
                   [&](cr::Frame& f) { cr::whitesBlacks(f, c.whites, c.blacks); });
    }
  }
}

// The GPU interpolates one row (row_at) per amount; that is the same curve within 1e-6.
void test_whites_blacks_rows() {
  float worst = 0.0F;
  std::uint32_t s = 12345U;
  for (const float amount : {-100.0F, -73.5F, -10.0F, 0.0F, 25.0F, 100.0F, 187.25F, 270.0F}) {
    const auto wr = cr::whitesRow(amount), br = cr::blacksRow(std::clamp(amount, -100.0F, 100.0F));
    const auto at = [](const std::array<float, 33>& row, float v) {
      if (v >= 1.0F) return 1.0F;
      const float p = std::fmax(v, 0.0F) * 32.0F;
      const auto j = std::min<std::size_t>(static_cast<std::size_t>(p), 31);
      return row[j] + (row[j + 1] - row[j]) * (p - static_cast<float>(j));
    };
    for (int k = 0; k < 1033; ++k) {
      float v;
      if (k < 33) {
        v = static_cast<float>(k) / 32.0F;
      } else {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        v = static_cast<float>(s % 100000U) / 100000.0F;
      }
      worst = std::max(worst, std::fabs(at(wr, v) - cr::whitesCurve(v, amount)));
      worst = std::max(worst, std::fabs(at(br, v) - cr::blacksCurve(v, std::clamp(amount, -100.0F, 100.0F))));
    }
  }
  std::printf("whites/blacks rows vs curve worst %.3g\n", worst);
  CHECK(worst <= 1e-6F);
}

}  // namespace

int main() {
  test_whites_blacks_rows();
  if (!pixcake::gl::native::available()) {
    std::printf("test_camera_raw_gl: skipped (no native GL)\n");
    return 0;
  }
  auto context = pixcake::gl::native::create_context();
  if (!context.valid() || !context.makeCurrent()) {
    std::printf("test_camera_raw_gl: skipped (no context)\n");
    return 0;
  }
  {
    crgl::Gl g(context);
    CHECK(g.ok());
    test_box_mean_gl(g);
    test_gauss_gl(g);
    test_guided_gl(g, false);
    test_guided_gl(g, true);
    test_rank_filter_gl(g);
    test_dehaze_gl(g);
    test_tone_detail_gl(g);
    test_local_tone_gl(g);
    test_local_tone_gl_tiny(g);
    test_whites_blacks_gl(g);
    if (!g.ok()) std::fprintf(stderr, "GL error: %s\n", g.error().c_str());
    CHECK(g.ok());
  }
  context.doneCurrent();
  std::printf("test_camera_raw_gl: ok\n");
  return 0;
}
