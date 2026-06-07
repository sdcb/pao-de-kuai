#include "resources/CardAtlasData.h"

namespace pdk::resources {
namespace {

int RankColumn(rules::Rank rank) {
    switch (rank) {
    case rules::Rank::Ace: return 0;
    case rules::Rank::Two: return 1;
    case rules::Rank::Three: return 2;
    case rules::Rank::Four: return 3;
    case rules::Rank::Five: return 4;
    case rules::Rank::Six: return 5;
    case rules::Rank::Seven: return 6;
    case rules::Rank::Eight: return 7;
    case rules::Rank::Nine: return 8;
    case rules::Rank::Ten: return 9;
    case rules::Rank::Jack: return 10;
    case rules::Rank::Queen: return 11;
    case rules::Rank::King: return 12;
    }
    return 0;
}

int SuitRow(rules::Suit suit) {
    switch (suit) {
    case rules::Suit::Spades: return 0;
    case rules::Suit::Hearts: return 1;
    case rules::Suit::Diamonds: return 2;
    case rules::Suit::Clubs: return 3;
    }
    return 0;
}

D2D1_RECT_U RectAt(int column, int row) {
    const CardAtlasInfo& info = GetCardAtlasInfo();
    const UINT32 left = static_cast<UINT32>(column * (info.cardWidth + info.gap));
    const UINT32 top = static_cast<UINT32>(row * (info.cardHeight + info.gap));
    return D2D1::RectU(left, top, left + info.cardWidth, top + info.cardHeight);
}

} // namespace

const CardAtlasInfo& GetCardAtlasInfo() {
    static const CardAtlasInfo info;
    return info;
}

D2D1_RECT_U CardSourceRect(rules::Card card) {
    return RectAt(RankColumn(card.rank), SuitRow(card.suit));
}

D2D1_RECT_U CardBackSourceRect() {
    return RectAt(2, 4);
}

} // namespace pdk::resources
