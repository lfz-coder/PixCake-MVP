// Ported from PhotoCraft crates/algo/src/camera_raw.rs, camera_raw/local_tone.rs,
// camera_raw/whites_blacks.rs, fxutil.rs and photo_util.rs (bilinear)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten in C++17, single-threaded (PhotoCraft's parallel rows are independent, so
// the order of every sum is the same); sRGB encoding only.
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pixcake::gpu::camera_raw {
namespace {

constexpr float kTau = 6.28318530717958647692F;

float luma3(float r, float g, float b) { return 0.2126F * r + 0.7152F * g + 0.0722F * b; }
std::int64_t clamp_index(std::int64_t k, std::int64_t n) { return std::clamp<std::int64_t>(k, 0, n - 1); }
std::size_t div_ceil(std::size_t a, std::size_t b) { return (a + b - 1) / b; }

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
  for (std::size_t y = 0; y < h; ++y) {
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
  // Columns, with running sums whose increments are taken in f32 (as PhotoCraft does).
  std::vector<float> out(w * h);
  const auto row = [&](std::int64_t y) { return &tmp[static_cast<std::size_t>(clamp_index(y, static_cast<std::int64_t>(h))) * w]; };
  std::vector<double> acc(w, 0.0);
  for (std::int64_t y = -ri; y <= ri; ++y) {
    const float* p = row(y);
    for (std::size_t x = 0; x < w; ++x) acc[x] += static_cast<double>(p[x]);
  }
  for (std::size_t y = 0; y < h; ++y) {
    for (std::size_t x = 0; x < w; ++x) out[y * w + x] = static_cast<float>(acc[x] / n);
    const auto yi = static_cast<std::int64_t>(y);
    const float* add = row(yi + ri + 1);
    const float* sub = row(yi - ri);
    for (std::size_t x = 0; x < w; ++x) acc[x] += static_cast<double>(add[x] - sub[x]);
  }
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
  for (std::size_t y = 0; y < h; ++y) {
    const float fy = (static_cast<float>(y) + 0.5F) / sf - 0.5F;
    for (std::size_t x = 0; x < w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5F) / sf - 0.5F;
      const float av = bilinear(ma, sw, sh, fx, fy);
      const float bv = bilinear(mb, sw, sh, fx, fy);
      out[y * w + x] = av * i[y * w + x] + bv;
    }
  }
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
    for (std::size_t y = 0; y < sh; ++y) {
      for (std::size_t x = 0; x < sw; ++x) {
        const std::size_t x0 = x >= r ? x - r : 0, x1 = std::min(x + r, sw - 1);
        float acc = start;
        for (std::size_t xx = x0; xx <= x1; ++xx) acc = pick(acc, v[y * sw + xx]);
        a[y * sw + x] = acc;
      }
    }
    for (std::size_t y = 0; y < sh; ++y) {
      for (std::size_t x = 0; x < sw; ++x) {
        const std::size_t y0 = y >= r ? y - r : 0, y1 = std::min(y + r, sh - 1);
        float acc = start;
        for (std::size_t yy = y0; yy <= y1; ++yy) acc = pick(acc, a[yy * sw + x]);
        b[y * sw + x] = acc;
      }
    }
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
  for (std::size_t y = 0; y < h; ++y) {
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
  for (std::size_t i = 0; i < n; ++i) {
    float lv = lc[i];
    if (!clar.empty()) {
      const float m = 2.0F * std::clamp(lv, 0.0F, 1.0F) - 1.0F;
      const float mw = 1.0F - m * m;
      lv += clarity / 100.0F * 0.8F * (lc[i] - clar[i]) * mw;
    }
    if (!tex.empty()) lv += texture / 100.0F * 0.9F * std::clamp(lc[i] - tex[i], -0.1F, 0.1F);
    tone(frame.px[i], l[i], lc[i], lv, k_con);
  }
}

}  // namespace pixcake::gpu::camera_raw
