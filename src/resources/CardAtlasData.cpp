#include "resources/CardAtlasData.h"

namespace pdk::resources {
namespace {

int RankColumn(rules::Rank rank) {
    switch (rank) {
    case RANK_ACE: return 0;
    case RANK_TWO: return 1;
    case RANK_THREE: return 2;
    case RANK_FOUR: return 3;
    case RANK_FIVE: return 4;
    case RANK_SIX: return 5;
    case RANK_SEVEN: return 6;
    case RANK_EIGHT: return 7;
    case RANK_NINE: return 8;
    case RANK_TEN: return 9;
    case RANK_JACK: return 10;
    case RANK_QUEEN: return 11;
    case RANK_KING: return 12;
    }
    return 0;
}

int SuitRow(rules::Suit suit) {
    switch (suit) {
    case SUIT_SPADES: return 0;
    case SUIT_HEARTS: return 1;
    case SUIT_DIAMONDS: return 2;
    case SUIT_CLUBS: return 3;
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
