#include <gtest/gtest.h>
#include "profiling/profiler.hpp"
#include <rapidjson/document.h>
#include <fstream>
#include <thread>
#include <limits>

TEST(Profiler, StatisticsUseNearestRankAndRejectInvalidValues) {
    std::vector<Bubble::profile_frame> frames(100);
    for (std::size_t i=0; i<100; ++i) frames[i].ms=i+1;
    frames.push_back({}); frames.back().ms=std::numeric_limits<double>::quiet_NaN();
    const auto s=Bubble::summarize_frames(frames);
    EXPECT_DOUBLE_EQ(s.mean,50.5); EXPECT_DOUBLE_EQ(s.median,50.5);
    EXPECT_DOUBLE_EQ(s.p95,95); EXPECT_DOUBLE_EQ(s.p99,99);
    EXPECT_DOUBLE_EQ(s.minimum,1); EXPECT_DOUBLE_EQ(s.maximum,100);
}
#if BENGINE_PROFILING
TEST(Profiler, NestedScopesThreadsBoundedHistoryAndExport) {
    auto& p=Bubble::profiler::instance(); p.enabled=true; p.next_frame(.016);
    {
        BUBBLE_PROFILE("outer", "test");
        { BUBBLE_PROFILE("quoted\"child", "test"); }
        std::thread worker([] { BUBBLE_PROFILE("worker", "task"); }); worker.join();
    }
    p.draw(12,4); p.next_frame(.02);
    auto frames=p.snapshot(); const auto& f=frames.back();
    ASSERT_EQ(f.events.size(),3u); EXPECT_EQ(f.draw_calls,1u); EXPECT_EQ(f.triangles,4u);
    const auto& child=f.events[0]; const auto& outer=f.events.back();
    EXPECT_EQ(child.parent,outer.id); EXPECT_GE(outer.total_us,outer.self_us);
    EXPECT_NE(f.events[1].thread,outer.thread); EXPECT_EQ(f.events[1].parent,0u);
    const auto path=std::filesystem::temp_directory_path()/("bubble-profile-test-"+std::to_string(f.id)+".json");
    std::filesystem::remove(path); std::string error;
    ASSERT_TRUE(p.export_capture(path,true,error)) << error;
    std::ifstream in(path); std::string json((std::istreambuf_iterator<char>(in)),{});
    rapidjson::Document doc; doc.Parse(json.c_str()); ASSERT_FALSE(doc.HasParseError());
    ASSERT_TRUE(doc["traceEvents"].IsArray()); EXPECT_GE(doc["traceEvents"].Size(),3u);
    EXPECT_FALSE(p.export_capture(path,false,error)); std::filesystem::remove(path);
    for(int i=0;i<300;++i) p.next_frame(.01);
    EXPECT_EQ(p.snapshot().size(),240u);
}
TEST(Profiler, ScopeOverflowIsVisibleAndPauseDoesNotRecord) {
    auto& p=Bubble::profiler::instance(); p.enabled=true; p.next_frame(.01);
    for(int i=0;i<4100;++i) { BUBBLE_PROFILE("scope", "test"); }
    p.next_frame(.01); auto frames=p.snapshot();
    EXPECT_EQ(frames.back().events.size(),4096u); EXPECT_EQ(frames.back().dropped,4u);
    const auto count=frames.size(); p.enabled=false;
    { BUBBLE_PROFILE("disabled", "test"); } p.next_frame(.01);
    EXPECT_EQ(p.snapshot().size(),count); p.enabled=true;
}
#endif
#if BENGINE_PROFILING
TEST(Profiler, WorkerSpanningFrameBoundaryKeepsOriginalFrame) {
    auto& p=Bubble::profiler::instance(); p.enabled=true; p.next_frame(.01);
    const auto id=p.frame_id.load();
    {
        BUBBLE_PROFILE("long task", "task");
        p.next_frame(.01);
    }
    const auto frames=p.snapshot();
    ASSERT_EQ(frames.back().id,id); ASSERT_EQ(frames.back().events.size(),1u);
    EXPECT_EQ(frames.back().events.front().name,"long task");
}
#else
TEST(Profiler, DisabledBuildDoesNotEvaluateScopeArgumentsOrRecordDraws) {
    int calls = 0;
    BUBBLE_PROFILE((++calls, "disabled"), "test");
    EXPECT_EQ(calls, 0);
    auto& p = Bubble::profiler::instance();
    p.next_frame(.01); p.draw(3, 1); p.next_frame(.01);
    EXPECT_TRUE(p.snapshot().empty());
}
#endif
TEST(Profiler, JsonUsesNullForUnavailableGpuAndPreservesExistingFiles) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()/("bubble-profile-json-"+std::to_string(stamp)+".json");
    std::vector<Bubble::profile_frame> frames(1); frames.front().id = 7;
    frames.front().gpu_passes.emplace_back("SSAO/HBAO", 2.5);
    std::string error;
    ASSERT_TRUE(Bubble::profiler::export_frames(frames, path, false, error)) << error;
    std::ifstream in(path); std::string json((std::istreambuf_iterator<char>(in)), {});
    rapidjson::Document doc; doc.Parse(json.c_str()); ASSERT_FALSE(doc.HasParseError());
    ASSERT_EQ(doc["frames"].Size(), 1u); EXPECT_TRUE(doc["frames"][0]["gpu_ms"].IsNull());
    EXPECT_DOUBLE_EQ(doc["frames"][0]["gpu_passes_ms"]["SSAO/HBAO"].GetDouble(), 2.5);
    EXPECT_FALSE(Bubble::profiler::export_frames({}, path, true, error));
    std::ifstream unchanged(path); std::string contents((std::istreambuf_iterator<char>(unchanged)), {});
    EXPECT_EQ(contents, json); std::filesystem::remove(path);
}
