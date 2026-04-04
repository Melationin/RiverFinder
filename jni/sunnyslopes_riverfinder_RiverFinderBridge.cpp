//
// Created by zhdds on 2026/3/15.
//
#include "sunnyslopes_riverfinder_RiverFinderBridge.h"

#include <cstring>
#include <vector>
#include <set>

#include "../river_finder.cpp"



std::vector<Res> dedup(const std::vector<Res>& v){
    std::set<Point> s;
    std::vector<Res> r;
    for(auto& p:v) if(s.insert(p.point).second) r.push_back(p);
    return r;
}

static Progress progress{};
ThreadSafeResults<Res> globalResults{};
JNIEXPORT jintArray JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_riverSearch
  (JNIEnv *env, jclass clazz, jlong seed, jint startX, jint startZ,
   jint width, jint height, jint y, jint minArea,jfloat opV , jint numThreads) {

    Generator g;
    setupGenerator(&g, MC_1_21_3, FORCE_OCEAN_VARIANTS);

    applySeed(&g, DIM_OVERWORLD, seed);

    globalResults.clear();
    progress.chunkInRunning = 0;
    progress.current = 0;
    progress.total = 1;
    progress.phase1 = 1;
    progress.try_pause = false;
    progress.try_stop = false;
    if (numThreads>0)
        findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height,y, minArea,(Progress*)&progress,numThreads);
    else findBiggestRiverParallelPool(globalResults, &g, startX, startZ, width, height,y, minArea,(Progress*)&progress);
    auto res = globalResults.getAllResults();
    if (progress.try_stop == true)
    {
        return nullptr;
    }
    progress.phase1 = 2;
    progress.total = res.size();
    progress.current = 0;
    progress.chunkInRunning = 0;

    std::ranges::sort(res, [](const Res& a, const Res& b) { return a.area > b.area; });

    int max = 0;
    globalResults.clear();
    for (const auto& it: res)
    {
        while (progress.try_pause)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        auto temp = findBiggestRiver<1>(&g,it.point.x - 128 -32,it.point.y - 128 - 32,256+64,256+64, y, 1,1);
        if (!temp.empty() && temp[0].area > max * opV)
        {
            globalResults.addResult(temp[0]);
            if (temp[0].area > max )
            {
                max = temp[0].area;
            }
            progress.current++;
        }else
        {
            progress.total = (int)progress.current;
            break;
        }
    }

    auto temp  = dedup(globalResults.getAllResults());
    globalResults.clear();
    globalResults.addResults(temp);

    if (globalResults.empty()) {
        return nullptr;  // 返回 null
    }

    auto finallyResults = globalResults.getAllResults();
    progress.phase1 = -1;
    std::vector<jint> results;
    results.reserve(finallyResults.size()*3);

    for (auto & finallyResult : finallyResults)
    {
        results.push_back(finallyResult.point.x);
        results.push_back(finallyResult.point.y);
        results.push_back(finallyResult.area);
    }

    jsize len = results.size();
    jintArray jResults = env->NewIntArray(len);
    if (jResults == nullptr) {
        return nullptr;  // 内存不足
    }

    // 将 C++ 数据复制到 Java 数组
    env->SetIntArrayRegion(jResults, 0, len, results.data());

    return jResults;

}

JNIEXPORT jintArray JNICALL Java_sunnyslopes_riverfinder_RiverFinderBridge_getSearchProgress
  (JNIEnv *env, jclass clazz) {

    if (progress.total == 0)
    {
        return nullptr;
    }
    int status = 0;

    //std::cout<<"pp1"<< &progress <<std::endl;
    if (progress.phase1 == 1)
    {
        if (progress.chunkInRunning == 0)
        {
            status = 1;
        }else
        {
            status = 0;
        }
    }else if (progress.phase1 == 2)
    {
        if (progress.current >= progress.total)
        {
            status = 2;
        }else
        {
            status = 1;
        }
    }


    jint data[4] = {progress.current, progress.total,progress.phase1,status};
    jintArray arr = env->NewIntArray(4);
    if (arr == nullptr) return nullptr;
    env->SetIntArrayRegion(arr, 0, 4, data);
    return arr;
}


inline jboolean Java_sunnyslopes_riverfinder_RiverFinderBridge_pause(JNIEnv *, jclass)
{
    progress.try_pause = true;
    return true;
}

inline jboolean Java_sunnyslopes_riverfinder_RiverFinderBridge_resume(JNIEnv *, jclass)
{
    progress.try_pause = false;
    return true;
}

inline jintArray Java_sunnyslopes_riverfinder_RiverFinderBridge_getNowResult(JNIEnv * env, jclass)
{
    if (!globalResults.empty())
    {
        auto res = globalResults.getAllResults();
        std::ranges::sort(res, [](const Res& a, const Res& b) { return a.area > b.area; });
        std::vector<jint> results;
        results.reserve(res.size()*3);
        for (auto & re : res)
        {
            results.push_back(re.point.x);
            results.push_back(re.point.y);
            results.push_back(re.area);
        }
        jsize len = results.size();
        jintArray jResults = env->NewIntArray(len);
        if (jResults == nullptr) {
            return nullptr;
        }
        env->SetIntArrayRegion(jResults, 0, len, results.data());
        return jResults;
    }
}

inline jboolean Java_sunnyslopes_riverfinder_RiverFinderBridge_stop(JNIEnv *, jclass)
{
    progress.try_stop = true;
}