#include <gtest/gtest.h>
#include "queue/RingBuffer.h"

// latest() on an empty buffer should return nullopt
TEST(RingBuffer, EmptyLatestReturnsNullopt) {
    RingBuffer<int, 8> rb;
    EXPECT_FALSE(rb.latest().has_value());
    EXPECT_EQ(rb.size(), 0u);
}

TEST(RingBuffer, PushAndLatest) {
    RingBuffer<int, 8> rb;
    rb.push(10);
    auto v = rb.latest();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 10);
    EXPECT_EQ(rb.size(), 1u);
}

TEST(RingBuffer, LatestReturnsNewest) {
    RingBuffer<int, 8> rb;
    rb.push(1); rb.push(2); rb.push(3);
    EXPECT_EQ(*rb.latest(), 3);
}

// 0 is most recent, 1 is one before that etc
TEST(RingBuffer, GetByIndex) {
    RingBuffer<int, 8> rb;
    rb.push(10); rb.push(20); rb.push(30);
    EXPECT_EQ(rb.get(0), 30);
    EXPECT_EQ(rb.get(1), 20);
    EXPECT_EQ(rb.get(2), 10);
}

// when the buffer fills up, new pushes overwrite the oldest entry
TEST(RingBuffer, OverwriteWhenFull) {
    RingBuffer<int, 4> rb;
    rb.push(1); rb.push(2); rb.push(3); rb.push(4);
    rb.push(5);
    EXPECT_EQ(rb.size(), 4u);
    EXPECT_EQ(*rb.latest(), 5);
}

// size() never goes above Capacity no matter how many pushes we do
TEST(RingBuffer, SizeCapsByCapacity) {
    RingBuffer<int, 4> rb;
    for (int i = 0; i < 10; i++) rb.push(i);
    EXPECT_EQ(rb.size(), 4u);
}