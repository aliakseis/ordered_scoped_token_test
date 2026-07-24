#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "ordered_scoped_token.h"

using namespace std::chrono_literals;

#define TEST(name) \
    std::cout << #name "... "; \
    name(); \
    std::cout << "OK\n";

#define CHECK(x) assert(x)

//////////////////////////////////////////////////////////////////////////////

static void test_basic_order()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();
    auto t1 = gen.generate();
    auto t2 = gen.generate();

    std::vector<int> order;

    {
        auto l0 = t0.lock();
        order.push_back(0);
    }

    {
        auto l1 = t1.lock();
        order.push_back(1);
    }

    {
        auto l2 = t2.lock();
        order.push_back(2);
    }

    CHECK(order.size() == 3);
    CHECK(order[0] == 0);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
}

//////////////////////////////////////////////////////////////////////////////

static void test_parallel_order()
{
    OrderedScopedTokenGenerator gen;

    constexpr int N = 32;

    std::vector<OrderedScopedTokenGenerator::Token> tokens;
    tokens.reserve(N);

    for (int i = 0; i < N; ++i)
        tokens.push_back(gen.generate());

    std::mutex m;
    std::vector<int> order;

    std::vector<std::thread> threads;

    for (int i = N - 1; i >= 0; --i)
    {
        threads.emplace_back([&, i]
            {
                auto scope = tokens[i].lock();

                std::lock_guard<std::mutex> lk(m);
                order.push_back(i);
            });
    }

    for (auto& t : threads)
        t.join();

    CHECK(order.size() == N);

    for (int i = 0; i < N; ++i)
        CHECK(order[i] == i);
}

//////////////////////////////////////////////////////////////////////////////

static void test_destroy_skips()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();
    auto t1 = gen.generate();
    auto t2 = gen.generate();

    t1 = {};      // destroy before use

    {
        auto s0 = t0.lock();
    }

    {
        auto s2 = t2.lock();
    }
}

//////////////////////////////////////////////////////////////////////////////

static void test_multiple_destroy_skips()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();
    auto t1 = gen.generate();
    auto t2 = gen.generate();
    auto t3 = gen.generate();
    auto t4 = gen.generate();

    t1 = {};
    t3 = {};

    {
        auto s = t0.lock();
    }

    {
        auto s = t2.lock();
    }

    {
        auto s = t4.lock();
    }
}

//////////////////////////////////////////////////////////////////////////////

static void test_move()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();

    auto moved = std::move(t0);

    CHECK(!t0);

    {
        auto s = moved.lock();
    }
}

//////////////////////////////////////////////////////////////////////////////

static void test_double_lock()
{
    OrderedScopedTokenGenerator gen;

    auto t = gen.generate();

    auto s = t.lock();

    bool threw = false;

    try
    {
        t.lock();
    }
    catch (...)
    {
        threw = true;
    }

    CHECK(threw);
}

//////////////////////////////////////////////////////////////////////////////

static void test_invalid()
{
    OrderedScopedTokenGenerator::Token t;

    bool threw = false;

    try
    {
        t.lock();
    }
    catch (...)
    {
        threw = true;
    }

    CHECK(threw);
}

//////////////////////////////////////////////////////////////////////////////

static void test_try_lock()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();
    auto t1 = gen.generate();

    auto s0 = t0.try_lock();

    CHECK(s0.valid());

    auto s1 = t1.try_lock();

    CHECK(!s1.valid());

    s0.release();

    // claimed
    auto s2 = t1.try_lock();

    CHECK(s2.valid());
}

//////////////////////////////////////////////////////////////////////////////

static void test_release()
{
    OrderedScopedTokenGenerator gen;

    auto t0 = gen.generate();
    auto t1 = gen.generate();

    auto s0 = t0.lock();

    auto future = std::async(std::launch::async, [&]
        {
            auto s1 = t1.lock();
            return 123;
        });

    std::this_thread::sleep_for(100ms);

    CHECK(future.wait_for(0ms) != std::future_status::ready);

    s0.release();

    CHECK(future.get() == 123);
}

//////////////////////////////////////////////////////////////////////////////

static void test_stress()
{
    OrderedScopedTokenGenerator gen;

    constexpr int N = 500;

    std::vector<OrderedScopedTokenGenerator::Token> tokens;
    tokens.reserve(N);

    for (int i = 0; i < N; ++i)
        tokens.push_back(gen.generate());

    std::mutex m;
    std::vector<int> order;

    std::vector<std::thread> threads;

    for (int i = 0; i < N; ++i)
    {
        threads.emplace_back([&, i]
            {
                auto s = tokens[i].lock();

                std::lock_guard<std::mutex> lk(m);
                order.push_back(i);
            });
    }

    for (auto& t : threads)
        t.join();

    CHECK(order.size() == N);

    for (int i = 0; i < N; ++i)
        CHECK(order[i] == i);
}

//////////////////////////////////////////////////////////////////////////////

int main()
{
    TEST(test_basic_order);
    TEST(test_parallel_order);
    TEST(test_destroy_skips);
    TEST(test_multiple_destroy_skips);
    TEST(test_move);
    TEST(test_double_lock);
    TEST(test_invalid);
    TEST(test_try_lock);
    TEST(test_release);
    TEST(test_stress);

    std::cout << "\nAll tests passed.\n";
}