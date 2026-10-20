#include "cubiomes/generator.h"
#include "cubiomes/biomes.h"
#include "Thread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <functional>
#include <iostream>
#include <queue>
#include <ranges>
#include <unordered_map>
#include <vector>
#define AREA_SIZE 256

/* ================== 河流 / 溶洞判定 ================== */

static inline int isRiverBiomeId(int id)
{
    return id == river;
}

static inline int isCaveBiomeId(int id)
{
    return id == dripstone_caves;
}

static inline int scoreFromAreas(int riverArea, int caveArea, double caveFactor)
{
    return riverArea + static_cast<int>(caveArea * caveFactor + (caveFactor >= 0 ? 0.5 : -0.5));
}

struct Point {
    int x = 0;
    int y = 0;
    std::strong_ordering operator<=>(const Point &) const = default;
};

struct Res {
    Point point;
    int riverArea = 0;
    int caveArea = 0;
    /** river + cave * caveFactor */
    int score = 0;

    bool operator<(const Res &other) const noexcept
    {
        if (score != other.score)
            return score < other.score;
        if (riverArea != other.riverArea)
            return riverArea < other.riverArea;
        if (caveArea != other.caveArea)
            return caveArea < other.caveArea;
        return point < other.point;
    }

    bool operator>(const Res &other) const noexcept
    {
        return other < *this;
    }

    Res() = default;

    Res(Point point, int riverArea, int caveArea, int score)
        : point(point), riverArea(riverArea), caveArea(caveArea), score(score)
    {
    }
};

struct Progress {
    std::atomic_int current = 0;
    std::atomic_int total = 0;
    std::atomic_int chunkInRunning{0};
    std::atomic_int phase1 = 0;
    std::atomic_bool try_pause = false;
    std::atomic_bool try_stop = false;
};

template<int scale>
std::vector<Res> findBiggestRiver(
    Generator *g,
    int startX, int startZ,
    int sx, int sz,
    int y,
    int minScore,
    double preserveRange = 0.99,
    double caveFactor = 0.0) noexcept
{
    std::vector<Res> result;

    const int W = sx / scale;
    const int H = sz / scale;
    if (W <= 0 || H <= 0)
        return result;

    const int stride = W + 1;
    const bool countCave = caveFactor != 0.0;

    std::vector<int> rawRiver(W * H);
    std::vector<int> rawCave(countCave ? W * H : 0);
    std::vector<int> prefixRiver((W + 1) * (H + 1), 0);
    std::vector<int> prefixCave(countCave ? (W + 1) * (H + 1) : 0, 0);

#define RAW_R(x, z) rawRiver[(x) + (z) * W]
#define RAW_C(x, z) rawCave[(x) + (z) * W]
#define ARR_R(x, z) prefixRiver[(x) + (z) * stride]
#define ARR_C(x, z) prefixCave[(x) + (z) * stride]

    if constexpr (scale > 1)
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z * scale + scale / 2;
            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x * scale + scale / 2;
                int id = sampleBiomeNoiseOnRiver(
                    &g->bn, nullptr,
                    worldX / 4,
                    y / 4 + 1,
                    worldZ / 4,
                    nullptr,
                    SAMPLE_NO_SHIFT);
                int river = isRiverBiomeId(id);
                RAW_R(x, z) = river;
                if (countCave)
                    RAW_C(x, z) = river ? 0 : isCaveBiomeId(id);
            }
        }
    }
    else
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z;
            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x;
                int id = getBiomeAt(g, 1, worldX, y, worldZ);
                int river = isRiverBiomeId(id);
                RAW_R(x, z) = river;
                if (countCave)
                    RAW_C(x, z) = river ? 0 : isCaveBiomeId(id);
            }
        }
    }

    for (int z = 1; z <= H; z++)
    {
        for (int x = 1; x <= W; x++)
        {
            ARR_R(x, z) =
                    RAW_R(x - 1, z - 1)
                    + ARR_R(x - 1, z)
                    + ARR_R(x, z - 1)
                    - ARR_R(x - 1, z - 1);
            if (countCave)
            {
                ARR_C(x, z) =
                        RAW_C(x - 1, z - 1)
                        + ARR_C(x - 1, z)
                        + ARR_C(x, z - 1)
                        - ARR_C(x - 1, z - 1);
            }
        }
    }

    struct RiverArea {
        int score;
        int riverArea;
        int caveArea;
        int startX;
        int startZ;

        bool operator<(const RiverArea &other) const noexcept
        {
            if (score != other.score)
                return score < other.score;
            if (startX != other.startX)
                return startX > other.startX;
            return startZ > other.startZ;
        }
    };

    std::priority_queue<RiverArea> pq;

    const int R_out = 128 / scale;
    const int R_in = 24 / scale;

    std::vector<int> dxOut(2 * R_out + 1);
    std::vector<int> dxIn(2 * R_in + 1);

    for (int dz = -R_out; dz <= R_out; dz++)
        dxOut[dz + R_out] = (int) floor(sqrt((double) R_out * R_out - dz * dz));

    for (int dz = -R_in; dz <= R_in; dz++)
        dxIn[dz + R_in] = (int) floor(sqrt((double) R_in * R_in - dz * dz));

    struct Mask {
        int dz;
        int out;
        int in;
    };

    std::vector<Mask> mask;
    mask.reserve(2 * R_out + 1);

    for (int dz = -R_out; dz <= R_out; dz++)
    {
        Mask m{};
        m.dz = dz;
        m.out = dxOut[dz + R_out];
        m.in = (abs(dz) <= R_in) ? dxIn[dz + R_in] : -1;
        mask.push_back(m);
    }

    auto ringSum = [&](const std::vector<int> &prefix, int cx, int cz) -> int {
        int area = 0;
#define ARR(x, z) prefix[(x) + (z) * stride]
        for (const Mask &m: mask)
        {
            int row = cz + m.dz;
            int L = cx - m.out;
            int R = cx + m.out;
            if (m.in == -1)
            {
                area += ARR(R + 1, row + 1) - ARR(L, row + 1) - ARR(R + 1, row) + ARR(L, row);
            }
            else
            {
                int Lin = cx - m.in;
                int Rin = cx + m.in;
                area += ARR(Lin, row + 1) - ARR(L, row + 1) - ARR(Lin, row) + ARR(L, row);
                area += ARR(R + 1, row + 1) - ARR(Rin + 1, row + 1) - ARR(R + 1, row) + ARR(Rin + 1, row);
            }
        }
#undef ARR
        return area;
    };

    RiverArea maxArea = {0, 0, 0, 0, 0};

    for (int cz = R_out; cz < H - R_out; cz++)
    {
        for (int cx = R_out; cx < W - R_out; cx++)
        {
            int riverCells = ringSum(prefixRiver, cx, cz);
            int caveCells = countCave ? ringSum(prefixCave, cx, cz) : 0;
            int worldRiver = riverCells * scale * scale;
            int worldCave = caveCells * scale * scale;
            int worldScore = scoreFromAreas(worldRiver, worldCave, caveFactor);

            if (worldScore >= maxArea.score * preserveRange && worldScore >= minScore)
            {
                int worldX = startX + cx * scale;
                int worldZ = startZ + cz * scale;
                pq.push({worldScore, worldRiver, worldCave, worldX, worldZ});
                if (worldScore > maxArea.score)
                    maxArea = {worldScore, worldRiver, worldCave, worldX, worldZ};
            }
        }
    }

    while (!pq.empty())
    {
        const auto &ra = pq.top();
        if (ra.score >= maxArea.score * preserveRange)
        {
            result.emplace_back(Point{ra.startX, ra.startZ}, ra.riverArea, ra.caveArea, ra.score);
            if (preserveRange == 1.0)
                break;
        }
        pq.pop();
    }

#undef RAW_R
#undef RAW_C
#undef ARR_R
#undef ARR_C
    return result;
}

void findBiggestRiverParallelPool(
    ThreadSafeResults<Res> &globalResults,
    Generator *g,
    int startX,
    int startZ,
    int sx,
    int sz,
    int y,
    int minScore,
    double caveFactor,
    Progress *progress,
    int numThreads = 0)
{
    if (numThreads <= 0)
        numThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (numThreads <= 0)
        numThreads = 1;

    ThreadPool pool(numThreads);
    const int chunkSize = 4096 * 2;
    const int overlap = 256;
    std::atomic<int> completedChunks{0};
    int totalChunks = 0;

    for (int x = 0; x < sx; x += chunkSize - overlap)
    {
        for (int z = 0; z < sz; z += chunkSize - overlap)
        {
            int currentSx = std::min(chunkSize, sx - x);
            int currentSz = std::min(chunkSize, sz - z);

            if (currentSx >= 256 && currentSz >= 256)
            {
                totalChunks++;
                if (progress)
                    progress->total++;

                pool.enqueue([&, x, z, currentSx, currentSz, caveFactor]() {
                    if (progress)
                    {
                        if (progress->try_stop)
                            return;
                        while (progress->try_pause)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            if (progress->try_stop)
                                return;
                        }
                        progress->chunkInRunning += 1;
                    }

                    Generator localG = *g;
                    auto blockResultsX16 = findBiggestRiver<16>(
                        &localG,
                        startX + x,
                        startZ + z,
                        currentSx,
                        currentSz,
                        y,
                        minScore,
                        0.85,
                        caveFactor);

                    int bx = currentSx / 256 + 2;
                    int bz = currentSz / 256 + 2;
                    std::vector<Res> flags(bx * bz);
                    for (const auto &it: blockResultsX16)
                    {
                        int x2 = (it.point.x - startX - x) / 256;
                        int z2 = (it.point.y - startZ - z) / 256;
                        if (x2 < 0 || z2 < 0 || x2 >= bx || z2 >= bz)
                            continue;
                        auto &slot = flags[x2 + bx * z2];
                        if (slot.score < it.score)
                            slot = it;
                    }

                    std::vector<Res> pqX16;
                    pqX16.reserve(flags.size());
                    for (auto &kv: flags)
                        if (kv.score > 0)
                            pqX16.push_back(kv);
                    std::ranges::sort(pqX16, [](const Res &a, const Res &b) { return a.score > b.score; });

                    std::unordered_map<uint64_t, Res> blockResultsX4;
                    blockResultsX4.reserve(16);
                    int maxScore = 0;

                    for (auto &res: pqX16)
                    {
                        if (progress && progress->try_stop)
                            break;
                        while (progress && progress->try_pause)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            if (progress->try_stop)
                                break;
                        }

                        auto subResults = findBiggestRiver<4>(
                            &localG,
                            res.point.x - 256,
                            res.point.y - 256,
                            512,
                            512,
                            y,
                            minScore,
                            1.0,
                            caveFactor);

                        if (subResults.empty())
                            break;
                        if (subResults[0].score < maxScore * 0.9)
                            break;

                        auto &r = subResults[0];
                        uint64_t key = (uint64_t(uint32_t(r.point.x)) << 32) | uint32_t(r.point.y);
                        auto it = blockResultsX4.find(key);
                        if (it == blockResultsX4.end())
                            blockResultsX4.emplace(key, r);
                        else if (it->second.score < r.score)
                            it->second = r;

                        if (r.score > maxScore)
                            maxScore = r.score;
                    }

                    std::vector<Res> filteredResults;
                    filteredResults.reserve(blockResultsX4.size());
                    for (const auto &result: blockResultsX4 | std::views::values)
                    {
                        int relX = result.point.x - (startX + x);
                        int relZ = result.point.y - (startZ + z);
                        if (relX > overlap / 2 && relX < currentSx - overlap / 2 &&
                            relZ > overlap / 2 && relZ < currentSz - overlap / 2 &&
                            result.score > 0)
                        {
                            filteredResults.push_back(result);
                        }
                    }
                    std::ranges::sort(filteredResults, [](const Res &a, const Res &b) { return a.score > b.score; });
                    if (!filteredResults.empty())
                        globalResults.addResults(filteredResults);

                    int completed = completedChunks.fetch_add(1) + 1;
                    if (progress)
                    {
                        progress->current = completed;
                        progress->chunkInRunning -= 1;
                    }
                });
            }
        }
    }

    if (progress)
        progress->total = totalChunks;
}
