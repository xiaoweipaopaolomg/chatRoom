#include <gtest/gtest.h>
#include "load_client/histogram.h"
using chatroom::load::Histogram;
TEST(HistogramTest, ComputesNearestRankPercentiles){Histogram histogram;for(int value=1;value<=100;++value)histogram.Record(static_cast<std::uint64_t>(value));EXPECT_EQ(histogram.Percentile(.50),50U);EXPECT_EQ(histogram.Percentile(.95),95U);EXPECT_EQ(histogram.Percentile(.99),99U);}
