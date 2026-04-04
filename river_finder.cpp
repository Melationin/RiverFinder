#include "cubiomes/generator.h"
#include "cubiomes/biomes.h"
#include <vector>
#include <algorithm>
#include  <queue>
#include <iostream>
#include <chrono>
#include "Thread.h"
#include <map>
#include <functional>
#include <ranges>
#define AREA_SIZE 256// 搜索步长（不重叠）

/* ================== 河流判定 ================== */



static inline int value(int id)
{
    return (id == river);
}
struct Point {
    int x = 0;
    int y = 0;
    std::strong_ordering operator<=>(const Point &) const = default;
};



struct Res {
    Point point;
    int area = 0;

    std::strong_ordering operator<=>(const Res &other) const noexcept
    {
        if (auto cmp = area <=>other.area; cmp != 0) return cmp;
        return point <=> other.point;
    }

    Res() = default;

    Res(Point point, int area) : point(point), area(area)
    {
    };
};

struct Progress {
    std::atomic_int  current = 0;
    std::atomic_int  total = 0;
    std::atomic_int  chunkInRunning;
    std::atomic_int  phase1 = 0;
    std::atomic_bool try_pause = false;
    std::atomic_bool try_stop = false;
};


template<int scale>
std::vector<Res> findBiggestRiver(
    Generator *g,
    int startX, int startZ,
    int sx, int sz,
    int y,
    int min,
    double f = 0.99) noexcept
{



    std::vector<Res> result;

    const int W = sx / scale;
    const int H = sz / scale;

    const int stride = W + 1;

    std::vector<int> raw(W * H);
    std::vector<int> prefix((W + 1) * (H + 1), 0);


#define RAW(x,z) raw[(x) + (z)*W]
#define ARR(x,z) prefix[(x) + (z)*stride]

    if constexpr (scale >1)
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z * scale + scale / 2;

            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x * scale + scale / 2;

                RAW(x, z) = value(sampleBiomeNoiseOnRiver(
                    &g->bn, nullptr,
                    worldX / 4,
                    y /4 + 1,
                    worldZ / 4,
                    nullptr,
                    SAMPLE_NO_SHIFT));
            }
        }
    }else
    {
        for (int z = 0; z < H; z++)
        {
            int worldZ = startZ + z;

            for (int x = 0; x < W; x++)
            {
                int worldX = startX + x;

                RAW(x, z) = value(getBiomeAt(g,1,worldX, y, worldZ));
            }
        }
    }


    for (int z = 1; z <= H; z++)
    {
        for (int x = 1; x <= W; x++)
        {
            ARR(x, z) =
                    RAW(x - 1, z - 1)
                    + ARR(x - 1, z)
                    + ARR(x, z - 1)
                    - ARR(x - 1, z - 1);
        }
    }

    struct RiverArea {
        int area;
        int startX;
        int startZ;

        bool operator<(const RiverArea &other) const noexcept
        {
            if (area != other.area)
                return area < other.area;
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
        dxOut[dz + R_out] =
                (int) floor(sqrt(R_out * R_out - dz * dz));

    for (int dz = -R_in; dz <= R_in; dz++)
        dxIn[dz + R_in] =
                (int) floor(sqrt(R_in * R_in - dz * dz));

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

    RiverArea max = {0, 0, 0};

    for (int cz = R_out; cz < H - R_out; cz++)
    {
        for (int cx = R_out; cx < W - R_out; cx++)
        {
            int area = 0;

            for (const Mask &m: mask)
            {
                int row = cz + m.dz;

                int L = cx - m.out;
                int R = cx + m.out;

                if (m.in == -1)
                {
                    area +=
                            ARR(R + 1, row + 1)
                            - ARR(L, row + 1)
                            - ARR(R + 1, row)
                            + ARR(L, row);
                } else
                {
                    int Lin = cx - m.in;
                    int Rin = cx + m.in;

                    area +=
                            ARR(Lin, row + 1)
                            - ARR(L, row + 1)
                            - ARR(Lin, row)
                            + ARR(L, row);

                    area +=
                            ARR(R + 1, row + 1)
                            - ARR(Rin + 1, row + 1)
                            - ARR(R + 1, row)
                            + ARR(Rin + 1, row);
                }
            }

            int worldArea = area * scale * scale;

            if (worldArea >= max.area * f && worldArea >= min)
            {
                int worldX = startX + cx * scale;
                int worldZ = startZ + cz * scale;

                pq.push({worldArea, worldX, worldZ});

                if (worldArea > max.area)
                    max.area = worldArea;
            }
        }
    }

    while (!pq.empty())
    {
        if (const auto &ra = pq.top(); ra.area >= max.area * f)
        {
            result.emplace_back( Point{ra.startX,ra.startZ}, ra.area);
            if (f == 1)
                break;
        }
        pq.pop();
    }

    return result;
}

/* ================== 主程序 ================== */


void findBiggestRiverParallelPool(
    ThreadSafeResults<Res> &globalResults,
    Generator *g,
    int startX,
    int startZ,
    int sx,
    int sz,
    int y,
    int minArea,
    Progress * progress,
    int numThreads = std::thread::hardware_concurrency()
)
{
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
                if (progress) progress -> total++;

                pool.enqueue([&, x, z, currentSx, currentSz]() {

                    if (progress)
                    {
                        if (progress->try_stop)
                        {
                            return;
                        }

                        while (progress->try_pause)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            if (progress->try_stop)
                            {
                                return;
                            }
                        }
                        progress->chunkInRunning+=1;
                    }

                    Generator localG = *g;
                    auto blockResultsX16 = findBiggestRiver<16>(
                        &localG,
                        startX + x,
                        startZ + z,
                        currentSx,
                        currentSz,
                        y,
                        minArea,
                        0.85
                    );
                    int bx = currentSx / 256 + 2;
                    int bz = currentSz / 256 + 2;
                    std::vector<Res> flags(bx * bz);
                    for (const auto &it: blockResultsX16)
                    {
                        int x2 = (it.point.x - startX - x) / 256;
                        int z2 = (it.point.y - startZ - z) / 256;


                        auto itf = flags[ x2+ bx*z2];

                        if (itf.area < it.area)
                        {
                            flags[ x2+ bx*z2] = it;
                        }
                    }
                    std::vector<Res> pqX16;
                    pqX16.reserve(flags.size());
                    for (auto &kv: flags)
                        if (kv.area > 0) pqX16.push_back(kv);
                    std::ranges::sort(pqX16, [](const Res& a, const Res& b) { return a.area > b.area; });
                    std::unordered_map<uint64_t, Res> blockResultsX4;
                    blockResultsX4.reserve(16);
                    int max = 0;
                    for (auto &res: pqX16)
                    {
                        auto subResults = findBiggestRiver<4>(
                            &localG,
                            res.point.x - 256,
                            res.point.y - 256,
                            512,
                            512,
                            y,
                            minArea,
                            1
                        );

                        if (subResults.empty())
                            break;

                        if (subResults[0].area < max * 0.9)
                            break;

                        auto &r = subResults[0];
                        uint64_t key = (uint64_t(uint32_t(r.point.x))<<32) | uint32_t(r.point.y);
                        auto it = blockResultsX4.find(key);

                        if (it == blockResultsX4.end())
                        {
                            blockResultsX4.emplace( key, r);
                        } else if (it->second.area < r.area)
                        {
                            it->second = r;
                        }

                        if (subResults[0].area > max)
                            max = subResults[0].area;
                    }

                    std::vector<Res> filteredResults;
                    filteredResults.reserve(blockResultsX4.size());
                    for (const auto &result: blockResultsX4 | std::views::values)
                    {
                        int relX = result.point.x - (startX + x);
                        int relZ = result.point.y - (startZ + z);

                        if (relX > overlap / 2 && relX < currentSx - overlap / 2 &&
                            relZ > overlap / 2 && relZ < currentSz - overlap / 2
                            && result.area > 0)
                        {
                            filteredResults.push_back(result);
                        }
                    }
                    std::ranges::sort(filteredResults, [](const Res& a, const Res& b) { return a.area > b.area; });
                    if (!filteredResults.empty())
                    {

                        globalResults.addResults(filteredResults);
                    }
                    int completed = completedChunks.fetch_add(1) + 1;
                    if (progress) progress->current = completed;
                    progress->chunkInRunning-=1;
                });
            }
        }
    }

    if (progress)
    {
        progress -> total = totalChunks;
    }

}


int main2(int argc, char **argv)
{
    int d;
    int64_t seed = -8180004378910677489;
    int startX;
    int startZ ;
    int px = 0, pz = 0,y = -62;
    std::cout <<"seed: ";
    std::cin >> seed;
    std::cout <<"center_x: ";
    std::cin >> px;
    std::cout <<"center_z: ";
    std::cin >> pz;
    std::cout <<"y: ";
    std::cin >> y;
    std::cout <<"r: ";
    std::cin >> d;
    startX = px - d;
    startZ = pz - d;
    int xRange = 2*d;
    int zRange = 2*d;


    int minArea = 30000;

    int outLimit = 1000;


    Generator g;
    setupGenerator(&g, MC_1_21_3, FORCE_OCEAN_VARIANTS);

    applySeed(&g, DIM_OVERWORLD, seed);

    ThreadSafeResults<Res> globalResults;

    findBiggestRiverParallelPool(globalResults, &g, startX, startZ, xRange, zRange,y, minArea,nullptr);

    auto res = globalResults.getAllResults();

    int max = 0;

    std::vector<Res> finallyResults;

    for (const auto& it: res)
    {
        auto temp = findBiggestRiver<1>(&g,it.point.x - 128 -32,it.point.y - 128 - 32,256+64,256+64, y, 1,1);
        if (!temp.empty() && temp[0].area > max * 0.85)
        {
            finallyResults.push_back(temp[0]);
            if (temp[0].area > max)
            {
                max = temp[0].area;
            }
        }else
        {
            break;
        }
    }

    std::ranges::sort(finallyResults, [](const Res& a, const Res& b) { return a.area > b.area; });


    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    std::ostringstream oss;
    oss << "river_finder_result_"
        << std::put_time(now, "%Y%m%d_%H%M%S")
        << ".txt";

    std::string outFile = oss.str();

    FILE *fp = fopen(outFile.c_str(), "w");
    if (fp)
    {
        fprintf(fp, "River Analysis Results (Seed: %lld)\n", (long long) seed);
        fprintf(fp, "Search Area: X=%d to %d, Z=%d to %d\n",
                startX, startX + xRange, startZ, startZ + zRange);
        fprintf(fp, "========================================\n");
    }

    int count = 0;
    for (const auto &it: finallyResults)
    {
        std::cout << "x:" << it.point.x << " y:" << it.point.y
                << "  Area:" << it.area << std::endl;

        if (fp)
        {
            fprintf(fp, "x:%d y:%d  Area:%d\n",
                    it.point.x, it.point.y, it.area);
        }
        count++;
        if (count > outLimit)
        {
            break;
        }
    }
    if (fp)
    {
        fprintf(fp, "\nTotal rivers found: %d\n", count);
        fclose(fp);
        std::cout << "\nResults saved to: " << outFile << std::endl;
    }

    std::cin.get();
    return 0;
}