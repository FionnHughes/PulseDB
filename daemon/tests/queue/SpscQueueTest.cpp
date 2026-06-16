#include <gtest/gtest.h>
#include "queue/SpscQueue.h"

// fresh queue should be empty and return nullopt on pop
TEST(SpscQueue, EmptyOnConstruct) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscQueue, PushAndPop) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.try_push(42));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_TRUE(q.empty());
}

// capacity 4 holds 3 items max as we have one slot "wasted" to distinguish empty from full
TEST(SpscQueue, FullAtCapacityMinusOne) {
    SpscQueue<int, 4> q;
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4));
    EXPECT_EQ(q.size(), 3u);
}

// push and pop enough times to wrap the indices around the boundary
TEST(SpscQueue, WrapAround) {
    SpscQueue<int, 4> q;
    q.try_push(1); q.try_push(2); q.try_push(3);
    q.try_pop(); q.try_pop();
    EXPECT_TRUE(q.try_push(4));
    EXPECT_TRUE(q.try_push(5));
    auto a = q.try_pop(); ASSERT_TRUE(a.has_value()); EXPECT_EQ(*a, 3);
    auto b = q.try_pop(); ASSERT_TRUE(b.has_value()); EXPECT_EQ(*b, 4);
    auto c = q.try_pop(); ASSERT_TRUE(c.has_value()); EXPECT_EQ(*c, 5);
    EXPECT_TRUE(q.empty());
}

// items should come out in the same order they went in
TEST(SpscQueue, FifoOrdering) {
    SpscQueue<int, 16> q;
    for (int i = 0; i < 10; i++) q.try_push(i);
    for (int i = 0; i < 10; i++) {
        auto v = q.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
}