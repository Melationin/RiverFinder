//
// Created by zhdds on 2026/3/15.
//
#include "sunnyslopes_riverfinder_RiverFinderBridge.h"

#include <cstring>
#include <vector>
#include <set>
#include <thread>
#include <chrono>
#include <ranges>

#include "../river_finder.cpp"

std::vector<Res> dedup(const std::vector<Res> &v)
{
    std::set<Point> s;
    std::vector<Res> r;
    for (auto &p: v)
        if (s.insert(p.point).second)
            r.push_back(p);
    return r;
}

static Progress progress{};
ThreadSafeResults<Res> globalResults{};

static jintArray toJavaArray(JNIEnv *env, const std::vector<Res> &finallyResults)
{
    std::vector<jint> results;
    results.reserve(finallyResults.size() * 4);
    for (auto &it: finallyResults)
    {
        results.push_back(it.point.x);
        results.push_back(it.point.y);
        results.push_back(it.riverArea);
        results.push_back(it.caveArea);
    }
    jsize len = static_cast<jsize>(results.size());
    jintArray jResults = env->NewIntArray(len);
    if (jResults == nullptr)
        return nullptr;
    if (len > 0)
        env->SetIntArrayRegion(jResults, 0, len, results.data());
    return jResults;
}

JNIEXPORT jintArray JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_riverSearch(
    JNIEnv *env, jclass clazz, jlong seed, jint startX, jint startZ,
    jint width, jint height, jint y, jint minArea, jfloat opV, jint numThreads, jfloat caveFactor)
{
    (void) clazz;
    Generator g;
    setupGenerator(&g, MC_1_21_3, FORCE_OCEAN_VARIANTS);
    applySeed(&g, DIM_OVERWORLD, seed);

    const double caveF = static_cast<double>(caveFactor);

    globalResults.clear();
    progress.chunkInRunning = 0;
    progress.current = 0;
    progress.total = 1;
    progress.phase1 = 1;
    progress.try_pause = false;
    progress.try_stop = false;

    if (numThreads > 0)
        findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height, y, minArea, caveF,
                                     (Progress *) &progress, numThreads);
    else
        findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height, y, minArea, caveF,
                                     (Progress *) &progress);

    auto res = globalResults.getAllResults();
    if (progress.try_stop == true)
        return nullptr;

    progress.phase1 = 2;
    progress.total = static_cast<int>(res.size());
    progress.current = 0;
    progress.chunkInRunning = 0;

    std::ranges::sort(res, [](const Res &a, const Res &b) { return a.score > b.score; });

    int maxScore = 0;
    globalResults.clear();
    for (const auto &it: res)
    {
        while (progress.try_pause)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (progress.try_stop)
                break;
        }
        if (progress.try_stop)
            break;

        auto temp = findBiggestRiver<1>(&g, it.point.x - 128 - 32, it.point.y - 128 - 32, 256 + 64, 256 + 64, y, 1,
                                        1.0, caveF);
        if (!temp.empty() && temp[0].score > maxScore * opV)
        {
            globalResults.addResult(temp[0]);
            if (temp[0].score > maxScore)
                maxScore = temp[0].score;
            progress.current++;
        }
        else
        {
            progress.total = (int) progress.current;
            break;
        }
    }

    auto temp = dedup(globalResults.getAllResults());
    globalResults.clear();
    globalResults.addResults(temp);

    if (globalResults.empty())
        return nullptr;

    auto finallyResults = globalResults.getAllResults();
    progress.phase1 = -1;
    std::ranges::sort(finallyResults, [](const Res &a, const Res &b) { return a.score > b.score; });
    return toJavaArray(env, finallyResults);
}

JNIEXPORT jintArray JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_getSearchProgress(
    JNIEnv *env, jclass clazz)
{
    (void) clazz;
    if (progress.total == 0)
        return nullptr;

    int status = 0;
    if (progress.phase1 == 1)
        status = (progress.chunkInRunning == 0) ? 1 : 0;
    else if (progress.phase1 == 2)
        status = (progress.current >= progress.total) ? 2 : 1;

    jint data[6] = {
        progress.current,
        progress.total,
        progress.phase1,
        status,
        progress.try_pause ? 1 : 0,
        progress.try_stop ? 1 : 0
    };
    jintArray arr = env->NewIntArray(6);
    if (arr == nullptr)
        return nullptr;
    env->SetIntArrayRegion(arr, 0, 6, data);
    return arr;
}

JNIEXPORT jboolean JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_pause(JNIEnv *, jclass)
{
    progress.try_pause = true;
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_resume(JNIEnv *, jclass)
{
    progress.try_pause = false;
    return JNI_TRUE;
}

JNIEXPORT jintArray JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_getNowResult(JNIEnv *env, jclass)
{
    if (globalResults.empty())
        return nullptr;
    auto res = globalResults.getAllResults();
    std::ranges::sort(res, [](const Res &a, const Res &b) { return a.score > b.score; });
    return toJavaArray(env, res);
}

JNIEXPORT jboolean JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_stop(JNIEnv *, jclass)
{
    progress.try_stop = true;
    return JNI_TRUE;
}
