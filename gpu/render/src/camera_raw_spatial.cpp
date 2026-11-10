// Ported from PhotoCraft crates/algo/src/camera_raw.rs, camera_raw/local_tone.rs,
// camera_raw/whites_blacks.rs, fxutil.rs and photo_util.rs (bilinear)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten in C++17; rows (or columns) run on a worker pool in bands, each sample
// computed in the same order as on one thread, so the result is the same bits; sRGB encoding only.
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <limits>
#include <numeric>

#include "whites_blacks_tables.inc"

namespace pixcake::gpu::camera_raw {
namespace {

constexpr float kTau = 6.28318530717958647692F;

float luma3(float r, float g, float b) { return 0.2126F * r + 0.7152F * g + 0.0722F * b; }
std::int64_t clamp_index(std::int64_t k, std::int64_t n) { return std::clamp<std::int64_t>(k, 0, n - 1); }
std::size_t div_ceil(std::size_t a, std::size_t b) { return (a + b - 1) / b; }

std::atomic<std::size_t> parallel_minimum{std::size_t{1} << 15};

// A fixed pool of worker threads that runs one banded loop at a time.  A loop started while the
// pool is busy (another render on another thread) runs on its caller's thread instead.
class Pool {
 public:
  static Pool& get() {
    static Pool pool;
    return pool;
  }
  // body(first, end) over 0..n in bands, one per thread.
  void run(std::size_t n, const std::function<void(std::size_t, std::size_t)>& body) {
    const std::size_t bands = std::min(n, workers_.size() + 1);
    std::unique_lock<std::mutex> busy(busy_, std::try_to_lock);
    if (bands <= 1 || !busy.owns_lock()) {
      body(0, n);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_ = Job{&body, n, bands, div_ceil(n, bands)};
      next_ = 0;
      finished_ = 0;
      ++generation_;
    }
    wake_.notify_all();
    work(job_);
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return finished_ == bands && active_ == 0; });
    job_.body = nullptr;
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
  }

 private:
  struct Job {
    const std::function<void(std::size_t, std::size_t)>* body = nullptr;
    std::size_t n = 0, bands = 0, band = 0;
  };
  Pool() {
    const unsigned cores = std::max(1U, std::thread::hardware_concurrency());
    for (unsigned i = 1; i < cores; ++i) workers_.emplace_back([this] { loop(); });
  }
  void loop() {
    std::uint64_t seen = 0;
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
        if (job_.body == nullptr) continue;
        job = job_;
        ++active_;
      }
      work(job);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        --active_;
      }
      done_.notify_all();
    }
  }
  void work(const Job& job) {
    for (std::size_t b = next_.fetch_add(1); b < job.bands; b = next_.fetch_add(1)) {
      const std::size_t first = b * job.band;
      (*job.body)(std::min(first, job.n), std::min(first + job.band, job.n));
      if (finished_.fetch_add(1) + 1 == job.bands) {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.notify_all();
      }
    }
  }
  std::vector<std::thread> workers_;
  std::mutex busy_, mutex_;
  std::condition_variable wake_, done_;
  Job job_;
  std::uint64_t generation_ = 0;
  std::atomic<std::size_t> next_{0}, finished_{0};
  std::size_t active_ = 0;
  bool stop_ = false;
};

// body(first, end) over 0..n, in parallel bands when the plane has at least parallel_minimum
// samples.
template <class Body>
void par(std::size_t n, std::size_t samples, const Body& body) {
  if (samples < parallel_minimum.load() || n < 2) {
    body(std::size_t{0}, n);
    return;
  }
  Pool::get().run(n, body);
}

// The end of stage 3 on one pixel (camera_raw.rs tone()).
void tone(std::array<float, 4>& q, float l0, float lc, float lv, float k_con) {
  if (k_con != 0.0F) {
    const float xx = std::clamp(lv, 0.0F, 1.0F);
    lv += -k_con * std::sin(kTau * xx) / kTau;
  }
  lv = std::max(lv, 0.0F) + (l0 - lc);
  const float ratio = l0 > 1e-5F ? lv / l0 : 1.0F;
  for (int c = 0; c < 3; ++c) q[c] = l0 > 1e-5F ? q[c] * ratio : q[c] + (lv - l0);
}

}  // namespace

std::size_t setParallelMinimum(std::size_t samples) { return parallel_minimum.exchange(std::max<std::size_t>(samples, 1)); }

std::array<std::size_t, 3> gaussBoxRadii(float sigma) {
  const float n = 3.0F;
  const float w_ideal = std::sqrt(12.0F * sigma * sigma / n + 1.0F);
  int wl = static_cast<int>(std::floor(w_ideal));
  if (wl % 2 == 0) wl -= 1;
  wl = std::max(wl, 1);
  const int wu = wl + 2;
  const float m_raw = std::round((12.0F * sigma * sigma - n * static_cast<float>(wl * wl) - 4.0F * n * static_cast<float>(wl) - 3.0F * n) /
                                 (-4.0F * static_cast<float>(wl) - 4.0F));
  const auto m = static_cast<std::size_t>(std::clamp(m_raw, 0.0F, n));
  std::array<std::size_t, 3> radii{};
  for (std::size_t i = 0; i < 3; ++i) radii[i] = i < m ? static_cast<std::size_t>(wl - 1) / 2 : static_cast<std::size_t>(wu - 1) / 2;
  if (radii[0] == 0 && radii[1] == 0 && radii[2] == 0) radii[0] = 1;
  return radii;
}

std::vector<float> boxMean(const std::vector<float>& v, std::size_t w, std::size_t h, std::size_t r) {
  if (r == 0 || w == 0 || h == 0) return v;
  const double n = static_cast<double>(2 * r + 1);
  const auto ri = static_cast<std::int64_t>(r);
  std::vector<float> tmp(w * h);
  par(h, w * h, [&](std::size_t y0, std::size_t y1) {
    for (std::size_t y = y0; y < y1; ++y) {
      const float* src = &v[y * w];
      const auto at = [&](std::int64_t k) { return static_cast<double>(src[clamp_index(k, static_cast<std::int64_t>(w))]); };
      double acc = 0.0;
      for (std::int64_t k = -ri; k <= ri; ++k) acc += at(k);
      for (std::size_t x = 0; x < w; ++x) {
        tmp[y * w + x] = static_cast<float>(acc / n);
        const auto xi = static_cast<std::int64_t>(x);
        acc += at(xi + ri + 1) - at(xi - ri);
      }
    }
  });
  // Columns, with running sums whose increments are taken in f32 (as PhotoCraft does); the
  // columns are independent, so they run in bands of columns.
  std::vector<float> out(w * h);
  const auto row = [&](std::int64_t y) { return &tmp[static_cast<std::size_t>(clamp_index(y, static_cast<std::int64_t>(h))) * w]; };
  par(w, w * h, [&](std::size_t x0, std::size_t x1) {
    std::vector<double> acc(x1 - x0, 0.0);
    for (std::int64_t y = -ri; y <= ri; ++y) {
      const float* p = row(y);
      for (std::size_t x = x0; x < x1; ++x) acc[x - x0] += static_cast<double>(p[x]);
    }
    for (std::size_t y = 0; y < h; ++y) {
      for (std::size_t x = x0; x < x1; ++x) out[y * w + x] = static_cast<float>(acc[x - x0] / n);
      const auto yi = static_cast<std::int64_t>(y);
      const float* add = row(yi + ri + 1);
      const float* sub = row(yi - ri);
      for (std::size_t x = x0; x < x1; ++x) acc[x - x0] += static_cast<double>(add[x] - sub[x]);
    }
  });
  return out;
}

std::vector<float> gauss(const std::vector<float>& v, std::size_t w, std::size_t h, float sigma) {
  if (sigma < 0.3F) return v;
  std::vector<float> out = v;
  for (const std::size_t r : gaussBoxRadii(sigma)) out = boxMean(out, w, h, r);
  return out;
}

float bilinear(const std::vector<float>& buf, std::size_t w, std::size_t h, float x, float y) {
  x = std::clamp(x, 0.0F, static_cast<float>(w - 1));
  y = std::clamp(y, 0.0F, static_cast<float>(h - 1));
  const auto x0 = static_cast<std::size_t>(std::floor(x)), y0 = static_cast<std::size_t>(std::floor(y));
  const std::size_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
  const float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
  const auto p = [&](std::size_t xx, std::size_t yy) { return buf[yy * w + xx]; };
  const float top = p(x0, y0) + (p(x1, y0) - p(x0, y0)) * tx;
  const float bot = p(x0, y1) + (p(x1, y1) - p(x0, y1)) * tx;
  return top + (bot - top) * ty;
}

std::vector<float> guided(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                          std::size_t r, float eps) {
  const std::size_t n = w * h;
  const auto mi = boxMean(i, w, h, r), mp = boxMean(p, w, h, r);
  std::vector<float> ip(n), ii(n);
  for (std::size_t k = 0; k < n; ++k) {
    ip[k] = i[k] * p[k];
    ii[k] = i[k] * i[k];
  }
  const auto mip = boxMean(ip, w, h, r), mii = boxMean(ii, w, h, r);
  std::vector<float> a(n), b(n);
  for (std::size_t k = 0; k < n; ++k) {
    a[k] = (mip[k] - mi[k] * mp[k]) / (mii[k] - mi[k] * mi[k] + eps);
    b[k] = mp[k] - a[k] * mi[k];
  }
  const auto ma = boxMean(a, w, h, r), mb = boxMean(b, w, h, r);
  std::vector<float> out(n);
  for (std::size_t k = 0; k < n; ++k) out[k] = ma[k] * i[k] + mb[k];
  return out;
}

std::vector<float> guidedFast(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                              std::size_t r, float eps) {
  const std::size_t s = std::clamp<std::size_t>(r / 8, 1, 8);
  if (s == 1) return guided(i, p, w, h, r, eps);
  const std::size_t sw = div_ceil(w, s), sh = div_ceil(h, s);
  const auto down = [&](const std::vector<float>& v) {
    std::vector<float> o(sw * sh, 0.0F), c(sw * sh, 0.0F);
    for (std::size_t y = 0; y < h; ++y) {
      for (std::size_t x = 0; x < w; ++x) {
        const std::size_t k = (y / s) * sw + x / s;
        o[k] += v[y * w + x];
        c[k] += 1.0F;
      }
    }
    for (std::size_t k = 0; k < o.size(); ++k) o[k] = o[k] / std::max(c[k], 1.0F);
    return o;
  };
  const auto si = down(i), sp = down(p);
  const std::size_t rs = std::max<std::size_t>(r / s, 1), n = sw * sh;
  const auto mi = boxMean(si, sw, sh, rs), mp = boxMean(sp, sw, sh, rs);
  std::vector<float> ip(n), ii(n);
  for (std::size_t k = 0; k < n; ++k) {
    ip[k] = si[k] * sp[k];
    ii[k] = si[k] * si[k];
  }
  const auto mip = boxMean(ip, sw, sh, rs), mii = boxMean(ii, sw, sh, rs);
  std::vector<float> a(n), b(n);
  for (std::size_t k = 0; k < n; ++k) {
    a[k] = (mip[k] - mi[k] * mp[k]) / (mii[k] - mi[k] * mi[k] + eps);
    b[k] = mp[k] - a[k] * mi[k];
  }
  const auto ma = boxMean(a, sw, sh, rs), mb = boxMean(b, sw, sh, rs);
  std::vector<float> out(w * h);
  const auto sf = static_cast<float>(s);
  par(h, w * h, [&](std::size_t y0, std::size_t y1) {
    for (std::size_t y = y0; y < y1; ++y) {
      const float fy = (static_cast<float>(y) + 0.5F) / sf - 0.5F;
      for (std::size_t x = 0; x < w; ++x) {
        const float fx = (static_cast<float>(x) + 0.5F) / sf - 0.5F;
        const float av = bilinear(ma, sw, sh, fx, fy);
        const float bv = bilinear(mb, sw, sh, fx, fy);
        out[y * w + x] = av * i[y * w + x] + bv;
      }
    }
  });
  return out;
}

void dehaze(Frame& frame, float amount) {
  const std::size_t w = frame.width, h = frame.height;
  if (w == 0 || h == 0) return;
  // Transmission on a <= 768 px proxy, upsampled.
  const std::size_t k = std::max<std::size_t>(div_ceil(std::max(w, h), 768), 1);
  const std::size_t sw = div_ceil(w, k), sh = div_ceil(h, k), n = sw * sh;
  std::vector<std::array<float, 3>> small(n, {0.0F, 0.0F, 0.0F});
  std::vector<float> cnt(n, 0.0F);
  for (std::size_t y = 0; y < h; ++y) {
    for (std::size_t x = 0; x < w; ++x) {
      const std::size_t i = (y / k) * sw + x / k;
      for (int c = 0; c < 3; ++c) small[i][c] += std::clamp(frame.px[y * w + x][c], 0.0F, 1.0F);
      cnt[i] += 1.0F;
    }
  }
  for (std::size_t i = 0; i < n; ++i)
    for (int c = 0; c < 3; ++c) small[i][c] = small[i][c] / std::max(cnt[i], 1.0F);
  const std::size_t r = std::max<std::size_t>(std::max(sw, sh) / 60, 2);
  // The minimum (or maximum) over each (2r + 1)^2 window.
  const auto rank_filter = [&](const std::vector<float>& v, bool take_max) {
    const float start = take_max ? std::numeric_limits<float>::lowest() : std::numeric_limits<float>::max();
    const auto pick = [take_max](float a, float b) { return take_max ? std::fmax(a, b) : std::fmin(a, b); };
    std::vector<float> a(n), b(n);
    par(sh, n, [&](std::size_t first, std::size_t end) {
      for (std::size_t y = first; y < end; ++y) {
        for (std::size_t x = 0; x < sw; ++x) {
          const std::size_t x0 = x >= r ? x - r : 0, x1 = std::min(x + r, sw - 1);
          float acc = start;
          for (std::size_t xx = x0; xx <= x1; ++xx) acc = pick(acc, v[y * sw + xx]);
          a[y * sw + x] = acc;
        }
      }
    });
    par(sh, n, [&](std::size_t first, std::size_t end) {
      for (std::size_t y = first; y < end; ++y) {
        for (std::size_t x = 0; x < sw; ++x) {
          const std::size_t y0 = y >= r ? y - r : 0, y1 = std::min(y + r, sh - 1);
          float acc = start;
          for (std::size_t yy = y0; yy <= y1; ++yy) acc = pick(acc, a[yy * sw + x]);
          b[y * sw + x] = acc;
        }
      }
    });
    return b;
  };
  std::vector<float> channel_min(n);
  for (std::size_t i = 0; i < n; ++i) channel_min[i] = std::fmin(std::fmin(small[i][0], small[i][1]), small[i][2]);
  const auto dark = rank_filter(channel_min, false);
  // Atmospheric light: mean colour of the brightest 0.1 % of the dark channel.
  std::vector<std::size_t> idx(n);
  std::iota(idx.begin(), idx.end(), std::size_t{0});
  std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return dark[b] < dark[a]; });
  const std::size_t top = std::max<std::size_t>(n / 1000, 1);
  std::array<float, 3> air{0.0F, 0.0F, 0.0F};
  for (std::size_t t = 0; t < top; ++t)
    for (int c = 0; c < 3; ++c) air[c] += small[idx[t]][c] / static_cast<float>(top);
  for (float& a : air) a = std::fmax(a, 0.2F);
  std::vector<float> normalised(n);
  for (std::size_t i = 0; i < n; ++i)
    normalised[i] = std::fmin(std::fmin(small[i][0] / air[0], small[i][1] / air[1]), small[i][2] / air[2]);
  const auto norm_dark = rank_filter(normalised, false);
  const auto opened = rank_filter(norm_dark, true);
  const auto mean = [](const std::vector<float>& v) {
    float sum = -0.0F;
    for (const float x : v) sum += x;
    return sum / static_cast<float>(std::max<std::size_t>(v.size(), 1));
  };
  const float weight = 0.95F * mean(norm_dark) / std::fmax(mean(opened), 1e-6F);
  std::vector<float> t_raw(n), guide(n);
  for (std::size_t i = 0; i < n; ++i) {
    t_raw[i] = 1.0F - weight * opened[i];
    guide[i] = luma3(small[i][0], small[i][1], small[i][2]);
  }
  const auto t = guided(guide, t_raw, sw, sh, r * 2, 1e-3F);
  const auto ks = static_cast<float>(k);
  par(h, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first; y < end; ++y) {
      for (std::size_t x = 0; x < w; ++x) {
        auto& q = frame.px[y * w + x];
        const float tv = std::clamp(
            bilinear(t, sw, sh, (static_cast<float>(x) + 0.5F) / ks - 0.5F, (static_cast<float>(y) + 0.5F) / ks - 0.5F), 0.1F, 1.0F);
        for (int c = 0; c < 3; ++c) {
          const float i = q[c];
          const float j = (i - air[c]) / tv + air[c];
          q[c] = amount >= 0.0F ? i + (j - i) * amount : i + (air[c] - i) * (-amount) * 0.5F * (1.0F - tv * 0.5F);
        }
      }
    }
  });
}

void toneDetail(Frame& frame, float contrast, float texture, float clarity, float pixel_scale) {
  if (contrast == 0.0F && texture == 0.0F && clarity == 0.0F) return;
  const std::size_t w = frame.width, h = frame.height, n = w * h;
  const float ps = std::clamp(pixel_scale, 0.01F, 1.0F);
  const auto longest = static_cast<float>(std::max(w, h));
  std::vector<float> l(n), lc(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto& q = frame.px[i];
    l[i] = std::fmax(luma3(q[0], q[1], q[2]), 0.0F);
    lc[i] = std::fmin(l[i], 1.0F);
  }
  std::vector<float> clar, tex;
  if (clarity != 0.0F) clar = guidedFast(lc, lc, w, h, std::max<std::size_t>(static_cast<std::size_t>(longest * 0.02F), 3), 0.005F);
  if (texture != 0.0F) tex = gauss(lc, w, h, std::fmax(longest * 0.002F, 1.5F * ps));
  const float k_con = contrast / 100.0F * 0.6F;
  par(h, n, [&](std::size_t first, std::size_t end) {
    for (std::size_t i = first * w; i < end * w; ++i) {
      float lv = lc[i];
      if (!clar.empty()) {
        const float m = 2.0F * std::clamp(lv, 0.0F, 1.0F) - 1.0F;
        const float mw = 1.0F - m * m;
        lv += clarity / 100.0F * 0.8F * (lc[i] - clar[i]) * mw;
      }
      if (!tex.empty()) lv += texture / 100.0F * 0.9F * std::clamp(lc[i] - tex[i], -0.1F, 0.1F);
      tone(frame.px[i], l[i], lc[i], lv, k_con);
    }
  });
}

// ---- Highlights and Shadows (camera_raw/local_tone.rs)
namespace {

constexpr float kRefStep = 0.125F;
constexpr std::size_t kWorkSide = 1600;
constexpr std::size_t kKnots = 5;
struct Lift {
  float own[kKnots], around[kKnots], sigma, down;
};
struct Side {
  float pull[kKnots], push[kKnots], sigma;
};
constexpr Lift kShadows{{-0.017F, 0.466F, 0.886F, 1.221F, -0.081F}, {4.566F, 4.067F, 1.088F, 0.149F, 0.878F}, 0.571F, 0.808F};
constexpr Side kHighlights{{0.179F, 0.141F, 0.196F, 0.629F, 2.07F}, {0.156F, 0.08F, 0.175F, 0.049F, 1.261F}, 0.05F};
constexpr float kTaps[5] = {1.0F / 16.0F, 4.0F / 16.0F, 6.0F / 16.0F, 4.0F / 16.0F, 1.0F / 16.0F};

struct Plane {
  std::size_t w = 0, h = 0;
  std::vector<float> v;
};

// Rust's f32::total_cmp as a strict order.
std::int32_t total_key(float f) {
  std::int32_t bits;
  std::memcpy(&bits, &f, sizeof bits);
  return bits ^ static_cast<std::int32_t>(static_cast<std::uint32_t>(bits >> 31) >> 1);
}
bool total_less(float a, float b) { return total_key(a) < total_key(b); }

// Mirrors i into 0..n (the edge sample is not repeated).
std::size_t mirror(std::ptrdiff_t i, std::size_t n) {
  const auto ni = static_cast<std::ptrdiff_t>(n);
  if (ni == 1) return 0;
  const std::ptrdiff_t p = 2 * (ni - 1);
  std::ptrdiff_t m = i % p;
  if (m < 0) m += p;
  return static_cast<std::size_t>(m >= ni ? p - m : m);
}

// The binomial 5-tap blur, sampled every `step` pixels from 0.
Plane blur_step(const Plane& p, std::size_t step) {
  const std::size_t w2 = div_ceil(p.w, step), h2 = div_ceil(p.h, step);
  std::vector<float> tmp(p.w * h2);
  par(h2, p.w * h2, [&](std::size_t first, std::size_t end) {
    for (std::size_t y2 = first; y2 < end; ++y2) {
      const auto y = static_cast<std::ptrdiff_t>(y2 * step);
      const float* src[5];
      for (int k = 0; k < 5; ++k) src[k] = &p.v[mirror(y + k - 2, p.h) * p.w];
      for (std::size_t x = 0; x < p.w; ++x) {
        float sum = -0.0F;
        for (int k = 0; k < 5; ++k) sum += kTaps[k] * src[k][x];
        tmp[y2 * p.w + x] = sum;
      }
    }
  });
  Plane out{w2, h2, std::vector<float>(w2 * h2)};
  par(h2, w2 * h2, [&](std::size_t first, std::size_t end) {
    for (std::size_t y2 = first; y2 < end; ++y2) {
      const float* src = &tmp[y2 * p.w];
      for (std::size_t x2 = 0; x2 < w2; ++x2) {
        const std::size_t x = x2 * step;
        float sum = -0.0F;
        for (int k = 0; k < 5; ++k) sum += kTaps[k] * src[mirror(static_cast<std::ptrdiff_t>(x) + k - 2, p.w)];
        out.v[y2 * w2 + x2] = sum;
      }
    }
  });
  return out;
}
Plane down(const Plane& p) { return blur_step(p, 2); }

// The source sample behind index i of a zero-stuffed axis of length n holding lim samples.
bool stuffed(std::ptrdiff_t i, std::size_t n, std::size_t lim, std::size_t& at) {
  const std::size_t m = mirror(i, n);
  if (m % 2 != 0 || m / 2 >= lim) return false;
  at = m / 2;
  return true;
}

// p brought up to w x h: zeros between samples, then the blur times four.
Plane up(const Plane& p, std::size_t w, std::size_t h) {
  const std::size_t lh = std::min(p.h, div_ceil(h, 2)), lw = std::min(p.w, div_ceil(w, 2));
  std::vector<float> tmp(lw * h);
  par(h, lw * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first; y < end; ++y) {
      std::size_t rows[5];
      bool has[5];
      for (int k = 0; k < 5; ++k) has[k] = stuffed(static_cast<std::ptrdiff_t>(y) + k - 2, h, lh, rows[k]);
      for (std::size_t j = 0; j < lw; ++j) {
        float sum = -0.0F;
        for (int k = 0; k < 5; ++k)
          if (has[k]) sum += kTaps[k] * (p.v[rows[k] * p.w + j] * 4.0F);
        tmp[y * lw + j] = sum;
      }
    }
  });
  Plane out{w, h, std::vector<float>(w * h)};
  par(h, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first; y < end; ++y) {
      const float* t = &tmp[y * lw];
      for (std::size_t x = 0; x < w; ++x) {
        float sum = -0.0F;
        for (int k = 0; k < 5; ++k) {
          std::size_t j;
          if (stuffed(static_cast<std::ptrdiff_t>(x) + k - 2, w, lw, j)) sum += kTaps[k] * t[j];
        }
        out.v[y * w + x] = sum;
      }
    }
  });
  return out;
}

// One sample of up(p, w, h) at output row y (taps from up_rows) and column x, same terms in the
// same order.
float up_at(const Plane& p, std::size_t w, const std::size_t rows[5], const bool has[5], std::size_t x) {
  const std::size_t lw = std::min(p.w, div_ceil(w, 2));
  const auto col = [&](std::size_t j) {
    float sum = -0.0F;
    for (int k = 0; k < 5; ++k)
      if (has[k]) sum += kTaps[k] * (p.v[rows[k] * p.w + j] * 4.0F);
    return sum;
  };
  float sum = -0.0F;
  for (int k = 0; k < 5; ++k) {
    std::size_t j;
    if (stuffed(static_cast<std::ptrdiff_t>(x) + k - 2, w, lw, j)) sum += kTaps[k] * col(j);
  }
  return sum;
}

std::size_t depth(std::size_t w, std::size_t h) {
  std::size_t s = std::min(w, h), n = 1;
  while (s > 2) {
    s = div_ceil(s, 2);
    ++n;
  }
  return n;
}

std::vector<Plane> gaussian(Plane p, std::size_t n) {
  std::vector<Plane> g;
  g.push_back(std::move(p));
  for (std::size_t i = 1; i < n; ++i) g.push_back(down(g.back()));
  return g;
}

float quantile(const std::vector<float>& v, float q) {
  std::vector<float> s = v;
  const auto i = static_cast<std::size_t>(std::round(static_cast<float>(s.size() - 1) * q));
  std::nth_element(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(i), s.end(), total_less);
  return s[i];
}

// The local Laplacian filter of u with remap(value, reference); the coarsest level is kept.
template <class Remap>
Plane llf(const Plane& u, const Remap& remap) {
  const std::size_t n = depth(u.w, u.h);
  const auto g = gaussian(u, n);
  float hi = -std::numeric_limits<float>::infinity();
  for (const float v : u.v) hi = std::fmax(hi, v);
  const float lo = std::fmin(quantile(u.v, 0.001F), hi);
  if (hi - lo <= 1e-6F || std::isnan(hi - lo)) return u;
  const std::size_t refs = std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil((hi - lo) / kRefStep)) + 1, 2, 96);
  const float step = (hi - lo) / static_cast<float>(refs - 1);
  std::vector<std::vector<float>> lap;
  for (std::size_t l = 0; l + 1 < n; ++l) lap.emplace_back(g[l].v.size(), 0.0F);
  for (std::size_t k = 0; k < refs; ++k) {
    const float r = lo + step * static_cast<float>(k);
    Plane m = u;
    par(u.h, m.v.size(), [&](std::size_t first, std::size_t end) {
      for (std::size_t i = first * u.w; i < end * u.w; ++i) m.v[i] = remap(m.v[i], r);
    });
    const auto gr = gaussian(std::move(m), n);
    for (std::size_t l = 0; l + 1 < n; ++l) {
      const auto& gl = g[l].v;
      const auto& fine = gr[l].v;
      const Plane& coarse = gr[l + 1];
      const std::size_t lw = g[l].w, lh = g[l].h;
      const std::size_t clh = std::min(coarse.h, div_ceil(lh, 2));
      auto& out = lap[l];
      par(lh, lw * lh, [&](std::size_t first, std::size_t end) {
        for (std::size_t y = first; y < end; ++y) {
          std::size_t rows[5];
          bool has[5];
          for (int t = 0; t < 5; ++t) has[t] = stuffed(static_cast<std::ptrdiff_t>(y) + t - 2, lh, clh, rows[t]);
          for (std::size_t x = 0; x < lw; ++x) {
            const std::size_t i = y * lw + x;
            const float wgt = std::fmax(1.0F - std::fabs(std::clamp(gl[i], lo, hi) - r) / step, 0.0F);
            if (wgt > 0.0F) out[i] += wgt * (fine[i] - up_at(coarse, lw, rows, has, x));
          }
        }
      });
    }
  }
  Plane o = g[n - 1];
  for (std::size_t l = n - 1; l-- > 0;) {
    Plane next = up(o, g[l].w, g[l].h);
    for (std::size_t i = 0; i < next.v.size(); ++i) next.v[i] += lap[l][i];
    o = std::move(next);
  }
  return o;
}

// 1st and 99th percentiles of the log luminance on a copy at most 256 pixels across.
std::pair<float, float> range_of(const Plane& u) {
  const std::size_t f = std::max<std::size_t>(div_ceil(std::max(u.w, u.h), 256), 1);
  const std::size_t sw = div_ceil(u.w, f), sh = div_ceil(u.h, f);
  std::vector<std::pair<double, std::uint32_t>> sum(sw * sh, {0.0, 0});
  for (std::size_t y = 0; y < u.h; ++y) {
    for (std::size_t x = 0; x < u.w; ++x) {
      auto& c = sum[(y / f) * sw + x / f];
      c.first += static_cast<double>(u.v[y * u.w + x]);
      c.second += 1;
    }
  }
  std::vector<float> cells;
  for (const auto& c : sum)
    if (c.second > 0) cells.push_back(static_cast<float>(c.first / static_cast<double>(c.second)));
  return {quantile(cells, 0.01F), quantile(cells, 0.99F)};
}

float excess(float d, float sigma) { return d - sigma * std::tanh(d / sigma); }
float knot(float t, const float k[kKnots]) {
  const float x = std::clamp(t, 0.0F, 1.0F) * static_cast<float>(kKnots - 1);
  const std::size_t i = std::min(static_cast<std::size_t>(x), kKnots - 2);
  return k[i] + (k[i + 1] - k[i]) * (x - static_cast<float>(i));
}

Plane highlights_pass(const Plane& u, float amount) {
  const auto bounds = range_of(u);
  const float lo = bounds.first, hi = bounds.second;
  const float span = std::fmax(hi - lo, 1.0F);
  const float* strengths = amount < 0.0F ? kHighlights.pull : kHighlights.push;
  Plane o = llf(u, [&](float x, float g) {
    return x + knot((g - lo) / span, strengths) * amount * excess(std::fmax(x - g, 0.0F), kHighlights.sigma);
  });
  const float shift = quantile(u.v, 0.01F) - quantile(o.v, 0.01F);
  for (float& v : o.v) v = v + shift;
  return o;
}

Plane shadows_pass(const Plane& u, float amount) {
  const auto bounds = range_of(u);
  const float lo = bounds.first, hi = bounds.second;
  const float span = std::fmax(hi - lo, 1.0F);
  const float k = amount > 0.0F ? amount : amount * kShadows.down;
  Plane o = llf(u, [&](float x, float g) {
    const float below = std::clamp((g - x) / kShadows.sigma, 0.0F, 1.0F);
    if (below == 0.0F) return x;
    const float w = below * below * (3.0F - 2.0F * below);
    return x + k * knot((x - lo) / span, kShadows.own) * knot((g - lo) / span, kShadows.around) * w;
  });
  // Keep the brightest content.
  const float shift = quantile(u.v, 0.99F) - quantile(o.v, 0.99F);
  for (float& v : o.v) v = v + shift;
  return o;
}

Plane shrink(const Plane& u, std::size_t f) {
  const std::size_t sw = div_ceil(u.w, f), sh = div_ceil(u.h, f);
  Plane out{sw, sh, std::vector<float>(sw * sh)};
  par(sh, u.v.size(), [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first; y < end; ++y) {
      for (std::size_t x = 0; x < sw; ++x) {
        const std::size_t x0 = x * f, y0 = y * f, x1 = std::min(x0 + f, u.w), y1 = std::min(y0 + f, u.h);
        double s = 0.0;
        for (std::size_t yy = y0; yy < y1; ++yy)
          for (std::size_t xx = x0; xx < x1; ++xx) s += static_cast<double>(u.v[yy * u.w + xx]);
        out.v[y * sw + x] = static_cast<float>(s / static_cast<double>((x1 - x0) * (y1 - y0)));
      }
    }
  });
  return out;
}

std::vector<float> guided_upsample(const Plane& small, const std::vector<float>& d, const Plane& full) {
  const std::size_t sw = small.w, sh = small.h, r = 2, n = sw * sh;
  const float eps = 0.01F;
  const auto mean = [&](const std::vector<float>& v) { return boxMean(v, sw, sh, r); };
  std::vector<float> ud(n), uu(n);
  for (std::size_t i = 0; i < n; ++i) {
    ud[i] = small.v[i] * d[i];
    uu[i] = small.v[i] * small.v[i];
  }
  const auto mu = mean(small.v), md = mean(d), mud = mean(ud), muu = mean(uu);
  std::vector<float> a(n), b(n);
  for (std::size_t i = 0; i < n; ++i) {
    a[i] = (mud[i] - mu[i] * md[i]) / (muu[i] - mu[i] * mu[i] + eps);
    b[i] = md[i] - a[i] * mu[i];
  }
  const auto ma = mean(a), mb = mean(b);
  const float fx = static_cast<float>(sw) / static_cast<float>(full.w), fy = static_cast<float>(sh) / static_cast<float>(full.h);
  std::vector<float> out(full.w * full.h);
  par(full.h, out.size(), [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first; y < end; ++y) {
      const float sy = (static_cast<float>(y) + 0.5F) * fy - 0.5F;
      for (std::size_t x = 0; x < full.w; ++x) {
        const float sx = (static_cast<float>(x) + 0.5F) * fx - 0.5F;
        out[y * full.w + x] = bilinear(ma, sw, sh, sx, sy) * full.v[y * full.w + x] + bilinear(mb, sw, sh, sx, sy);
      }
    }
  });
  return out;
}

}  // namespace

void localTone(Frame& frame, float shadows, float highlights) {
  const std::size_t w = frame.width, h = frame.height;
  if ((shadows == 0.0F && highlights == 0.0F) || w < 2 || h < 2) return;
  constexpr float kFloor = 1.0F / 65536.0F;
  Plane u{w, h, std::vector<float>(w * h)};
  par(h, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t i = first * w; i < end * w; ++i) {
      const auto& q = frame.px[i];
      u.v[i] = std::log2(std::fmax(luma3(srgbToLinear(q[0]), srgbToLinear(q[1]), srgbToLinear(q[2])), 0.0F) + kFloor);
    }
  });
  const std::size_t f = std::max<std::size_t>(div_ceil(std::max(w, h), kWorkSide), 1);
  const Plane small = f == 1 ? u : shrink(u, f);
  Plane o = small;
  if (highlights != 0.0F) o = highlights_pass(o, std::clamp(highlights, -100.0F, 100.0F) / 100.0F);
  if (shadows != 0.0F) o = shadows_pass(o, std::clamp(shadows, -100.0F, 100.0F) / 100.0F);
  std::vector<float> change(w * h);
  if (f == 1) {
    for (std::size_t i = 0; i < w * h; ++i) change[i] = o.v[i] - u.v[i];
  } else {
    std::vector<float> d(o.v.size());
    for (std::size_t i = 0; i < d.size(); ++i) d[i] = o.v[i] - small.v[i];
    change = guided_upsample(small, d, u);
  }
  par(h, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t i = first * w; i < end * w; ++i) {
      const float gain = std::exp2(change[i]);
      auto& q = frame.px[i];
      for (int c = 0; c < 3; ++c) q[c] = linearToSrgb(srgbToLinear(q[c]) * gain);
    }
  });
}

// ---- Whites and Blacks (camera_raw/whites_blacks.rs, camera_raw.rs luminance_points)
namespace {
namespace wb = whites_blacks_tables;
constexpr std::size_t kSteps = 33;
constexpr float kWhitesFrom = -100.0F, kBlacksFrom = -100.0F, kPhotoBlacks = 1.2F;

template <std::size_t Rows>
float curve(const float (&rows)[Rows][kSteps], float from, float x, float v) {
  if (v >= 1.0F) return 1.0F;
  const float r = std::clamp((x - from) / 10.0F, 0.0F, static_cast<float>(Rows - 1));
  const std::size_t i = std::min(static_cast<std::size_t>(r), Rows - 2);
  const float t = r - static_cast<float>(i);
  const auto at = [&](const float (&row)[kSteps]) {
    const float p = std::fmax(v, 0.0F) * static_cast<float>(kSteps - 1);
    const std::size_t j = std::min(static_cast<std::size_t>(p), kSteps - 2);
    const float s = p - static_cast<float>(j);
    return row[j] + (row[j + 1] - row[j]) * s;
  };
  return at(rows[i]) * (1.0F - t) + at(rows[i + 1]) * t;
}

template <std::size_t N>
std::pair<std::size_t, float> grid_index(const float (&grid)[N], float v) {
  v = std::clamp(v, grid[0], grid[N - 1]);
  std::size_t i = N - 2;
  for (std::size_t k = 0; k + 1 < N; ++k) {
    if (v <= grid[k + 1]) {
      i = k;
      break;
    }
  }
  return {i, (v - grid[i]) / (grid[i + 1] - grid[i])};
}

float adapt(const float (&table)[9][8], float bp, float wp) {
  const auto [i, s] = grid_index(wb::kBps, bp);
  const auto [j, t] = grid_index(wb::kWps, wp);
  const std::size_t jj = j;
  const float tt = t;
  const auto row = [&](std::size_t r) { return table[r][jj] * (1.0F - tt) + table[r][jj + 1] * tt; };
  return row(i) * (1.0F - s) + row(i + 1) * s;
}

std::pair<float, float> amounts(float whites, float blacks, float bp, float wp) {
  const float w = whites > 0.0F ? whites * adapt(wb::kAW, bp, wp) : whites;
  const float b = blacks < 0.0F ? blacks * std::fmax(adapt(wb::kAB, bp, wp), kPhotoBlacks) : blacks;
  return {std::clamp(w, -100.0F, 270.0F), std::clamp(b, -100.0F, 100.0F)};
}

float percentile(std::vector<float> v, float q) {
  std::stable_sort(v.begin(), v.end(), total_less);
  return v[static_cast<std::size_t>(std::round(static_cast<float>(v.size() - 1) * q))];
}

}  // namespace

std::array<float, 2> luminancePoints(const Frame& frame) {
  const std::size_t w = frame.width, h = frame.height;
  const std::size_t f = std::max<std::size_t>(div_ceil(std::max(w, h), 256), 1);
  const std::size_t sw = div_ceil(w, f), sh = div_ceil(h, f);
  std::vector<std::array<double, 4>> sum(sw * sh, {0.0, 0.0, 0.0, 0.0});
  // In bands of cell rows: each cell still adds its pixels row by row, left to right.
  par(sh, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t y = first * f; y < std::min(end * f, h); ++y) {
      for (std::size_t x = 0; x < w; ++x) {
        const auto& q = frame.px[y * w + x];
        auto& s = sum[(y / f) * sw + x / f];
        for (int c = 0; c < 3; ++c) s[c] += static_cast<double>(std::clamp(srgbToLinear(q[c]), 0.0F, 1.0F));
        s[3] += 1.0;
      }
    }
  });
  std::vector<float> lum, brightest;
  for (const auto& s : sum) {
    if (!(s[3] > 0.0)) continue;
    const float c0 = static_cast<float>(s[0] / s[3]), c1 = static_cast<float>(s[1] / s[3]), c2 = static_cast<float>(s[2] / s[3]);
    lum.push_back(luma3(c0, c1, c2));
    brightest.push_back(std::fmax(std::fmax(c0, c1), c2));
  }
  if (lum.empty()) return {0.0F, 1.0F};
  return {linearToSrgb(percentile(lum, 0.03F)), linearToSrgb(percentile(brightest, 0.995F))};
}

void whitesBlacks(Frame& frame, float whites, float blacks) {
  if (whites == 0.0F && blacks == 0.0F) return;
  const auto points = luminancePoints(frame);
  const auto [w, b] = amounts(std::clamp(whites, -100.0F, 100.0F), std::clamp(blacks, -100.0F, 100.0F), points[0], points[1]);
  if (w == 0.0F && b == 0.0F) return;
  const float wv = w, bv = b;
  const auto grey = [wv, bv](float v) {
    v = wv != 0.0F ? curve(wb::kWhites, kWhitesFrom, wv, v) : v;
    return bv != 0.0F ? curve(wb::kBlacks, kBlacksFrom, bv, v) : v;
  };
  const bool per_channel = w > 0.0F || b < 0.0F;
  par(frame.height, frame.px.size(), [&](std::size_t first, std::size_t end) {
    for (std::size_t p = first * frame.width; p < end * frame.width; ++p) {
      auto& q = frame.px[p];
      const float lin[3] = {srgbToLinear(q[0]), srgbToLinear(q[1]), srgbToLinear(q[2])};
      if (per_channel) {
        for (int c = 0; c < 3; ++c) {
          const float over = std::fmax(lin[c] - 1.0F, 0.0F);
          q[c] = linearToSrgb(srgbToLinear(grey(linearToSrgb(std::fmin(lin[c], 1.0F)))) + over);
        }
      } else {
        const float y = std::fmax(luma3(lin[0], lin[1], lin[2]), 0.0F);
        if (y <= 1e-7F) continue;
        const float y2 = srgbToLinear(grey(linearToSrgb(std::fmin(y, 1.0F)))) + std::fmax(y - 1.0F, 0.0F);
        const float k = y2 / y;
        for (int c = 0; c < 3; ++c) q[c] = linearToSrgb(lin[c] * k);
      }
    }
  });
}

}  // namespace pixcake::gpu::camera_raw
