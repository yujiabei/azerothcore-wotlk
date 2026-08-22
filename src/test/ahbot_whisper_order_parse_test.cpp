#include "gtest/gtest.h"
#include "../../modules/mod-ah-bot/src/AHBotWhisperOrderParse.h"

#include <cstdint>
#include <string>

TEST(AHBotWhisperOrderParse, ParsesItemLinkAndQuantity)
{
    uint32_t itemId = 0;
    uint32_t qty = 0;
    std::string const msg = "|cffffffff|Hitem:41599:0:0:0:0:0:0:0:80|h[Frostweave Bag]|h|r 5";
    ASSERT_TRUE(AHBotParseWhisperOrder(msg, itemId, qty));
    EXPECT_EQ(itemId, 41599u);
    EXPECT_EQ(qty, 5u);
}

TEST(AHBotWhisperOrderParse, RejectsMissingQuantity)
{
    uint32_t itemId = 0;
    uint32_t qty = 0;
    EXPECT_FALSE(AHBotParseWhisperOrder("|Hitem:41599:0:0:0:0:0:0:0:80|h[Bag]|h", itemId, qty));
}

TEST(AHBotWhisperOrderParse, RejectsBareId)
{
    uint32_t itemId = 0;
    uint32_t qty = 0;
    EXPECT_FALSE(AHBotParseWhisperOrder("41599 5", itemId, qty));
}

TEST(AHBotWhisperOrderParse, FormatsCopper)
{
    EXPECT_EQ(AHBotFormatCopper(0), "0g 0s 0c");
    EXPECT_EQ(AHBotFormatCopper(12345), "1g 23s 45c");
}
