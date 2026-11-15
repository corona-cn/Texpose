#include "../../../include/texpose/expose/ThreadExposee.hpp"
#include "../../../include/texpose/expose/ThreadExposer.hpp"
#include "../../util/Logger.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <fstream>
#include <string>
#include <nanobench.h>

namespace Texpose::test::expose {
    namespace {
        using namespace Texpose::include::expose;

        ankerl::nanobench::Bench globalBench;

        std::atomic<int> globalDestroyCallbackCount = 0;
        std::atomic<int> globalThreadExitCallbackCount = 0;

        auto dummyThreadExitCallback(void* inExposee, void* inExternal) -> void* {
            (void) inExposee;
            (void) inExternal;
            globalThreadExitCallbackCount.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        auto dummyDestroyCallback(void* inExposee, void* inExternal) -> void {
            (void) inExposee;
            (void) inExternal;
            globalDestroyCallbackCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    namespace {
        auto testBasicLifecycle() -> void {
            LOG("--- [正确性] 基础生命周期 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();
                LOG("current().isValid() = ", exposer.isValid() ? "PASS" : "FAIL");

                auto begin = exposer.begin();
                auto end = exposer.end();
                LOG("空 Exposer 的 begin == end: ", (begin == end) ? "PASS" : "FAIL");
            }
        }

        auto testExposeAndFind() -> void {
            LOG("--- [正确性] Expose 与 Find 语义 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();

                int val1 = 42;
                int* val1Ptr = &val1;
                usize val1Size = sizeof(int);

                int identity1 = 1;
                int identity2 = 2;

                auto exposee1 = exposer.expose((void**) &val1Ptr, &val1Size, &identity1);
                LOG("Expose 返回有效句柄: ", exposee1.isValid() ? "PASS" : "FAIL");

                LOG("peekExposee 读取正确: ", (*(int*) exposee1.peekExposee() == 42) ? "PASS" : "FAIL");
                LOG("peekExposeeSize 读取正确: ", (exposee1.peekExposeeSize() == sizeof(int)) ? "PASS" : "FAIL");

                auto found1 = exposer.find(&identity1);
                LOG("Find(identity1) 命中: ", found1.isValid() ? "PASS" : "FAIL");
                LOG("Find(identity1) 身份匹配: ", (found1.rawHandle() == exposee1.rawHandle()) ? "PASS" : "FAIL");

                auto found2 = exposer.find(&identity2);
                LOG("Find(identity2) 未命中: ", !found2.isValid() ? "PASS" : "FAIL");

                int val2 = 100;
                int* val2Ptr = &val2;
                usize val2Size = sizeof(int);
                auto exposee2 = exposer.expose((void**) &val2Ptr, &val2Size, &identity2);

                auto found2Retry = exposer.find(&identity2);
                LOG("Expose(identity2) 后 Find 命中: ", found2Retry.isValid() ? "PASS" : "FAIL");

                exposer.unexpose(exposee1);
                exposer.unexpose(exposee2);
            }
        }

        auto testIteration() -> void {
            LOG("--- [正确性] 迭代器遍历 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();

                constexpr int kCount = 10;
                std::vector<int> values(kCount, 0);
                std::vector<int> identities(kCount, 0);
                std::vector<int*> ptrs(kCount, nullptr); // 持久化存储指针
                std::vector<usize> sizes(kCount, sizeof(int));
                std::vector<ThreadExposee> exposees;

                int expectedSum = 0;
                for (int i = 0; i < kCount; ++i) {
                    values[i] = i * 10;
                    identities[i] = i + 100;
                    ptrs[i] = &values[i]; // 指向持久化的内存
                    expectedSum += values[i];

                    exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
                }

                int count = 0;
                int sum = 0;
                for (auto it = exposer.begin(); it != exposer.end(); ++it) {
                    auto exposee = *it;
                    sum += *(int*) exposee.peekExposee();
                    count++;
                }

                LOG("迭代器遍历 count = ", count, "（应为 ", kCount, "）");
                LOG("迭代器遍历 sum = ", sum, "（应为 ", expectedSum, "）");

                for (auto& exp : exposees) {
                    exposer.unexpose(exp);
                }
            }
        }

        auto testDestroyCallback() -> void {
            LOG("--- [正确性] 销毁回调 ---");
            LOGGER_INDENT_BLOCK(true) {
                globalDestroyCallbackCount.store(0);
                auto exposer = ThreadExposer::current();
                int val = 1;
                int* ptr = &val;
                usize size = sizeof(int);
                int identity = 777;

                auto exposee = exposer.expose((void**) &ptr, &size, &identity, nullptr, dummyDestroyCallback, nullptr);
                LOG("Expose 成功\n");
                LOG("手动 Unexpose...\n");
                exposer.unexpose(exposee);
                LOG("回调触发次数 = ", globalDestroyCallbackCount.load(), "（应为 1）");
            }
        }

        auto testThreadExitCallback() -> void {
            LOG("--- [正确性] 线程退出回调 ---");
            LOGGER_INDENT_BLOCK(true) {
                globalThreadExitCallbackCount.store(0);

                std::thread t([]() {
                    auto exposer = ThreadExposer::current();
                    int val = 999;
                    int* ptr = &val;
                    usize size = sizeof(int);
                    int identity = 888;

                    exposer.expose((void**) &ptr, &size, &identity, dummyThreadExitCallback, nullptr, nullptr);
                });
                t.join();

                LOG("线程退出后回调触发次数 = ", globalThreadExitCallbackCount.load(), "（应为 1）");
            }
        }

        auto testThreadIsolation() -> void {
            LOG("--- [正确性] 多线程隔离 ---");
            LOGGER_INDENT_BLOCK(true) {
                constexpr int kThreadCount = 4;
                constexpr int kExposePerThread = 5;
                std::atomic<bool> isolationOk = true;
                std::vector<std::thread> threads;

                for (int t = 0; t < kThreadCount; ++t) {
                    threads.emplace_back([t, &isolationOk, kExposePerThread, kThreadCount]() {
                        auto exposer = ThreadExposer::current();
                        std::vector<int> values(kExposePerThread, t);
                        std::vector<int> identities(kExposePerThread, t * 100);
                        std::vector<ThreadExposee> exposees;

                        for (int i = 0; i < kExposePerThread; ++i) {
                            identities[i] = t * 100 + i;
                            int* ptr = &values[i];
                            usize size = sizeof(int);
                            exposees.push_back(exposer.expose((void**) &ptr, &size, &identities[i]));
                        }

                        int foundCount = 0;
                        for (int i = 0; i < kExposePerThread; ++i) {
                            auto found = exposer.find(&identities[i]);
                            if (found.isValid() && *(int*) found.peekExposee() == t) {
                                foundCount++;
                            }
                        }

                        if (foundCount != kExposePerThread) {
                            isolationOk.store(false);
                        }

                        int otherFoundCount = 0;
                        for (int i = 0; i < kExposePerThread; ++i) {
                            int otherIdentity = ((t + 1) % kThreadCount) * 100 + i;
                            auto found = exposer.find(&otherIdentity);
                            if (found.isValid()) {
                                otherFoundCount++;
                            }
                        }

                        if (otherFoundCount > 0) {
                            isolationOk.store(false);
                        }

                        for (auto& exp : exposees) {
                            exposer.unexpose(exp);
                        }
                    });
                }

                for (auto& t : threads) t.join();

                LOG("多线程数据隔离严格正确: ", isolationOk.load() ? "PASS" : "FAIL");
            }
        }

        auto testCacheMechanism() -> void {
            LOG("--- [正确性] Find 缓存机制 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();

                int val1 = 1, val2 = 2;
                int* ptr1 = &val1;
                int* ptr2 = &val2;
                usize size = sizeof(int);
                int id1 = 10, id2 = 20;

                auto exp1 = exposer.expose((void**) &ptr1, &size, &id1);
                auto exp2 = exposer.expose((void**) &ptr2, &size, &id2);

                auto f1 = exposer.find(&id1);
                auto f2 = exposer.find(&id2);
                auto f3 = exposer.find(&id1);

                LOG("缓存命中结果正确: ", (f1.rawHandle() == exp1.rawHandle() && f3.rawHandle() == exp1.rawHandle()) ? "PASS" : "FAIL");

                exposer.unexpose(exp1);
                exposer.unexpose(exp2);

                auto f4 = exposer.find(&id1);
                LOG("Unexpose 后缓存失效: ", !f4.isValid() ? "PASS" : "FAIL");
            }
        }

        auto testRehashExpansion() -> void {
            LOG("--- [正确性] 哈希桶动态扩容 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();

                constexpr int kCount = 1000;
                std::vector<int> values(kCount, 0);
                std::vector<int> identities(kCount, 0);
                std::vector<int*> ptrs(kCount, nullptr);
                std::vector<usize> sizes(kCount, sizeof(int));
                std::vector<ThreadExposee> exposees;

                for (int i = 0; i < kCount; ++i) {
                    values[i] = i;
                    identities[i] = i + 1;
                    ptrs[i] = &values[i];
                    exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
                }

                int hitCount = 0;
                int valueSum = 0;
                for (int i = 0; i < kCount; ++i) {
                    auto found = exposer.find(&identities[i]);
                    if (found.isValid()) {
                        hitCount++;
                        valueSum += *(int*) found.peekExposee();
                    }
                }
                constexpr int expectedSum = (kCount - 1) * kCount / 2;
                LOG("扩容后 ", kCount, " 个 find 全部命中: ", (hitCount == kCount) ? "PASS" : "FAIL");
                LOG("扩容后 find 读取值总和 = ", valueSum, "（应为 ", expectedSum, "）");

                int iterateCount = 0;
                for (auto it = exposer.begin(); it != exposer.end(); ++it) {
                    auto exp = *it;
                    ankerl::nanobench::doNotOptimizeAway(exp);
                    iterateCount++;
                }
                LOG("扩容后迭代器遍历 count = ", iterateCount, "（应为 ", kCount, "）");

                for (auto& exp : exposees) {
                    exposer.unexpose(exp);
                }

                int afterCount = 0;
                for (int i = 0; i < kCount; ++i) {
                    auto found = exposer.find(&identities[i]);
                    if (found.isValid()) {
                        afterCount++;
                    }
                }
                LOG("全部 unexpose 后 find 全部失效: ", (afterCount == 0) ? "PASS" : "FAIL");
            }
        }
    }

    namespace {
        auto testBenchExposeLatency() -> void {
            LOG("--- [性能] Expose 延迟 ---");
            auto exposer = ThreadExposer::current();
            int val = 1;
            int* ptr = &val;
            usize size = sizeof(int);
            int identity = 1;

            globalBench.batch(1000).run("[性能] Expose 注册", [&] {
                for (int i = 0; i < 1000; ++i) {
                    auto exp = exposer.expose((void**) &ptr, &size, &identity);
                    exposer.unexpose(exp);
                }
            });
        }

        auto testBenchFindHitLatency() -> void {
            LOG("--- [性能] Find 缓存命中延迟 ---");
            auto exposer = ThreadExposer::current();
            int val = 1;
            int* ptr = &val;
            usize size = sizeof(int);
            int identity = 1;
            auto exp = exposer.expose((void**) &ptr, &size, &identity);

            globalBench.batch(1000).run("[性能] Find 缓存命中", [&] {
                for (int i = 0; i < 1000; ++i) {
                    auto found = exposer.find(&identity);
                    ankerl::nanobench::doNotOptimizeAway(found);
                }
            });
            exposer.unexpose(exp);
        }

        auto testBenchFindMissLatency() -> void {
            LOG("--- [性能] Find 缓存未命中延迟 ---");
            auto exposer = ThreadExposer::current();
            constexpr int kCount = 100;
            std::vector<int> values(kCount, 0);
            std::vector<int> identities(kCount, 0);
            std::vector<ThreadExposee> exposees;

            for (int i = 0; i < kCount; ++i) {
                identities[i] = i;
                int* ptr = &values[i];
                usize size = sizeof(int);
                exposees.push_back(exposer.expose((void**) &ptr, &size, &identities[i]));
            }

            globalBench.batch(1000).run("[性能] Find 频繁换目标 (未命中)", [&] {
                for (int i = 0; i < 1000; ++i) {
                    auto found = exposer.find(&identities[i % kCount]);
                    ankerl::nanobench::doNotOptimizeAway(found);
                }
            });

            for (auto& exp : exposees) {
                exposer.unexpose(exp);
            }
        }

        auto testBenchFindMissLargeScale() -> void {
            LOG("--- [性能] Find 大规模未命中延迟（扩容验证） ---");
            auto exposer = ThreadExposer::current();
            constexpr int kCount = 1000;
            std::vector<int> values(kCount, 0);
            std::vector<int> identities(kCount, 0);
            std::vector<int*> ptrs(kCount, nullptr);
            std::vector<usize> sizes(kCount, sizeof(int));
            std::vector<ThreadExposee> exposees;

            for (int i = 0; i < kCount; ++i) {
                identities[i] = i + 1;
                ptrs[i] = &values[i];
                exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
            }

            globalBench.batch(1000).run("[性能] 1000 个 identity 频繁换目标 (未命中)", [&] {
                for (int i = 0; i < 1000; ++i) {
                    auto found = exposer.find(&identities[i % kCount]);
                    ankerl::nanobench::doNotOptimizeAway(found);
                }
            });

            for (auto& exp : exposees) {
                exposer.unexpose(exp);
            }
        }

        auto testBenchIteration() -> void {
            LOG("--- [性能] 迭代器遍历开销 ---");
            auto exposer = ThreadExposer::current();
            constexpr int kCount = 1000;
            std::vector<int> values(kCount, 0);
            std::vector<int> identities(kCount, 0);
            std::vector<int*> ptrs(kCount, nullptr);
            std::vector<usize> sizes(kCount, sizeof(int));
            std::vector<ThreadExposee> exposees;

            for (int i = 0; i < kCount; ++i) {
                identities[i] = i;
                ptrs[i] = &values[i];
                exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
            }

            // 增大 batch 到 10000 解决 Unstable
            globalBench.batch(10000).run("[性能] 遍历 1000 个 Exposee", [&] {
                int count = 0;
                for (auto it = exposer.begin(); it != exposer.end(); ++it) {
                    auto exp = *it;
                    ankerl::nanobench::doNotOptimizeAway(exp);
                    count++;
                }
                ankerl::nanobench::doNotOptimizeAway(count);
            });

            for (auto& exp : exposees) {
                exposer.unexpose(exp);
            }
        }
    }

    auto runTests() -> void {
        LOG("===== ThreadExposer 深度测试开始 =====");

        globalBench.title("ThreadExposer Benchmarks").unit("op");

        LOGGER_INDENT_BLOCK(true) {
            testBasicLifecycle();
            testExposeAndFind();
            testIteration();
            testDestroyCallback();
            testThreadExitCallback();
            testThreadIsolation();
            testCacheMechanism();
            testRehashExpansion();

            testBenchExposeLatency();
            testBenchFindHitLatency();
            testBenchFindMissLatency();
            testBenchFindMissLargeScale();
            testBenchIteration();
        }

        LOG("--- [导出] 导出 Nanobench 性能数据 ---");
        std::ofstream out("nanobench_texpose_results.json");
        if (out.is_open()) {
            ankerl::nanobench::render(ankerl::nanobench::templates::json(), globalBench, out);
            LOG("性能数据已保存至 nanobench_texpose_results.json");
        } else {
            LOG("无法打开 nanobench_texpose_results.json 进行写入");
        }

        LOG("===== ThreadExposer 深度测试结束 =====");
    }
}