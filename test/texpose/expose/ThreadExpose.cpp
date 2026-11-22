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

        auto dummyThreadExitCallback(void* inExposee, void* inExternal) -> void {
            (void) inExposee;
            (void) inExternal;
            globalThreadExitCallbackCount.fetch_add(1, std::memory_order_relaxed);
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

        auto testCurrentFastPath() -> void {
            LOG("--- [正确性] current() 原子快速路径 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto first = ThreadExposer::current();
                LOG("首次 current().isValid() = ", first.isValid() ? "PASS" : "FAIL");

                int val = 123;
                int* ptr = &val;
                usize size = sizeof(int);
                int identity = 4242;
                auto exp = first.expose((void**) &ptr, &size, &identity);

                bool stable = true;
                for (int i = 0; i < 1000; ++i) {
                    auto next = ThreadExposer::current();
                    auto found = next.find(&identity);
                    if (!found.isValid() || found.rawHandle() != exp.rawHandle()) {
                        stable = false;
                        break;
                    }
                }
                LOG("连续 1000 次 current() 指向同一 exposer: ", stable ? "PASS" : "FAIL");

                first.unexpose(exp);

                std::atomic<int> validCount = 0;
                constexpr int kThreadCount = 8;
                constexpr int kCallPerThread = 100;
                std::vector<std::thread> threads;

                for (int t = 0; t < kThreadCount; ++t) {
                    threads.emplace_back([&validCount, kCallPerThread]() {
                        for (int i = 0; i < kCallPerThread; ++i) {
                            auto exposer = ThreadExposer::current();
                            if (exposer.isValid()) {
                                validCount.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                    });
                }
                for (auto& t : threads) t.join();

                LOG("多线程并发 ", kThreadCount * kCallPerThread, " 次 current() 全部有效: ", (validCount.load() == kThreadCount * kCallPerThread) ? "PASS" : "FAIL");
            }
        }

        auto testUnexposePositionConsistency() -> void {
            LOG("--- [正确性] Unexpose 相对位置一致性 ---");
            LOGGER_INDENT_BLOCK(true) {
                auto exposer = ThreadExposer::current();

                constexpr int kCount = 100;
                std::vector<int> values(kCount, 0);
                std::vector<int> identities(kCount, 0);
                std::vector<int*> ptrs(kCount, nullptr);
                std::vector<usize> sizes(kCount, sizeof(int));
                std::vector<ThreadExposee> exposees;
                exposees.reserve(kCount);

                for (int i = 0; i < kCount; ++i) {
                    values[i] = i;
                    identities[i] = i + 5000;
                    ptrs[i] = &values[i];
                    exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
                }

                // 乱序 unexpose：中间 / 首 / 尾交替，触发大量 swap-with-last
                const int removeOrder[] = {
                    50, 0, 99, 1, 98, 2, 97, 25, 75, 30,
                    70, 20, 80, 10, 90, 40, 60, 5, 95, 45,
                };
                constexpr int kRemoveCount = sizeof(removeOrder) / sizeof(removeOrder[0]);
                bool allConsistent = true;
                int removedSoFar = 0;

                for (int removeIdx : removeOrder) {
                    exposer.unexpose(exposees[removeIdx]);
                    exposees[removeIdx].rawHandle() = nullptr;
                    removedSoFar++;

                    for (int i = 0; i < kCount; ++i) {
                        auto expHandle = exposees[i].rawHandle();
                        if (!expHandle) {
                            continue;
                        }

                        auto found = exposer.find(&identities[i]);
                        if (!found.isValid() || found.rawHandle() != expHandle) {
                            allConsistent = false;
                            break;
                        }
                    }

                    if (!allConsistent) {
                        LOG("第 ", removedSoFar, " 次乱序 unexpose 后出现不一致: FAIL");
                        break;
                    }
                }
                LOG("逐次乱序 unexpose 后剩余节点 find 全部一致: ", allConsistent ? "PASS" : "FAIL");

                int iterateCount = 0;
                for (auto it = exposer.begin(); it != exposer.end(); ++it) {
                    auto exp = *it;
                    ankerl::nanobench::doNotOptimizeAway(exp);
                    iterateCount++;
                }
                LOG("乱序 unexpose 后迭代器 count = ", iterateCount, "（应为 ", kCount - kRemoveCount, "）");

                for (auto& exp : exposees) {
                    exposer.unexpose(exp);
                }

                int afterCount = 0;
                for (auto it = exposer.begin(); it != exposer.end(); ++it) {
                    ankerl::nanobench::doNotOptimizeAway(*it);
                    afterCount++;
                }
                LOG("全部 unexpose 后迭代器 count = ", afterCount, "（应为 0）");
            }
        }
    }

    namespace {
        auto testBenchCurrentLatency() -> void {
            LOG("--- [性能] current() 快速路径延迟 ---");

            globalBench.batch(1000).run("[性能] current() 快速路径", [&] {
                for (int i = 0; i < 1000; ++i) {
                    auto exposer = ThreadExposer::current();
                    ankerl::nanobench::doNotOptimizeAway(exposer);
                }
            });
        }

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

        auto testBenchUnexposeLargeScale() -> void {
            LOG("--- [性能] 大规模逆序 Unexpose 延迟 ---");
            auto exposer = ThreadExposer::current();
            constexpr int kCount = 1000;
            std::vector<int> values(kCount, 0);
            std::vector<int> identities(kCount, 0);
            std::vector<int*> ptrs(kCount, nullptr);
            std::vector<usize> sizes(kCount, sizeof(int));

            // batch 与 lambda 内操作单元数匹配：1000 expose + 1000 unexpose = 2000 对
            globalBench.batch(2000).minEpochIterations(100).run("[性能] 逆序 Unexpose 1000 节点", [&] {
                std::vector<ThreadExposee> exposees;
                exposees.reserve(kCount);

                for (int i = 0; i < kCount; ++i) {
                    identities[i] = i;
                    ptrs[i] = &values[i];
                    exposees.push_back(exposer.expose((void**) &ptrs[i], &sizes[i], &identities[i]));
                }

                for (int i = kCount - 1; i >= 0; --i) {
                    exposer.unexpose(exposees[i]);
                }
            });
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
            testCurrentFastPath();
            testUnexposePositionConsistency();

            testBenchCurrentLatency();
            testBenchExposeLatency();
            testBenchFindHitLatency();
            testBenchFindMissLatency();
            testBenchFindMissLargeScale();
            testBenchIteration();
            testBenchUnexposeLargeScale();
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