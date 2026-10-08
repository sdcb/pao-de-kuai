#include "resources/CardAtlasData.h"

/*
 * Column order is A 2 3 .. K and row order is spades hearts diamonds clubs, which
 * is what the packed atlas uses; the card back sits at column 2, row 4.
 */

static const CardAtlasInfo kCardAtlasInfo = {
    13,   /* columns */
    5,    /* rows */
    240,  /* cardWidth */
    336,  /* cardHeight */
    1,    /* gap */
    80    /* mainX */
};

static int RankColumn(Rank rank)
{
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

static int SuitRow(Suit suit)
{
    switch (suit) {
    case SUIT_SPADES: return 0;
    case SUIT_HEARTS: return 1;
    case SUIT_DIAMONDS: return 2;
    case SUIT_CLUBS: return 3;
    }
    return 0;
}

static D2D1_RECT_U RectAt(int column, int row)
{
    const CardAtlasInfo *info = GetCardAtlasInfo();
    D2D1_RECT_U rect;
    const UINT32 left = (UINT32)(column * (info->cardWidth + info->gap));
    const UINT32 top = (UINT32)(row * (info->cardHeight + info->gap));

    rect.left = left;
    rect.top = top;
    rect.right = left + (UINT32)info->cardWidth;
    rect.bottom = top + (UINT32)info->cardHeight;
    return rect;
}

const CardAtlasInfo *GetCardAtlasInfo(void)
{
    return &kCardAtlasInfo;
}

D2D1_RECT_U CardSourceRect(Card card)
{
    return RectAt(RankColumn(card.rank), SuitRow(card.suit));
}

D2D1_RECT_U CardBackSourceRect(void)
{
    return RectAt(2, 4);
}
