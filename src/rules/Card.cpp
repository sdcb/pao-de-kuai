#include "rules/Card.h"

#include <algorithm>
#include <sstream>

namespace pdk::rules {

int RankValue(Rank rank) {
    return static_cast<int>(rank);
}

int SortValue(Card card) {
    return RankValue(card.rank) * 10 + static_cast<int>(card.suit);
}

bool IsSpadeThree(Card card) {
    return card.rank == Rank::Three && card.suit == Suit::Spades;
}

std::string RankName(Rank rank) {
    switch (rank) {
    case Rank::Three: return "3";
    case Rank::Four: return "4";
    case Rank::Five: return "5";
    case Rank::Six: return "6";
    case Rank::Seven: return "7";
    case Rank::Eight: return "8";
    case Rank::Nine: return "9";
    case Rank::Ten: return "10";
    case Rank::Jack: return "J";
    case Rank::Queen: return "Q";
    case Rank::King: return "K";
    case Rank::Ace: return "A";
    case Rank::Two: return "2";
    }
    return "?";
}

std::string SuitName(Suit suit) {
    switch (suit) {
    case Suit::Spades: return "S";
    case Suit::Hearts: return "H";
    case Suit::Diamonds: return "D";
    case Suit::Clubs: return "C";
    }
    return "?";
}

std::string ToString(Card card) {
    return RankName(card.rank) + SuitName(card.suit);
}

std::string ToString(const Cards& cards) {
    std::ostringstream out;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << ToString(cards[i]);
    }
    return out.str();
}

void SortByGameOrder(Cards& cards) {
    std::sort(cards.begin(), cards.end(), [](Card lhs, Card rhs) {
        return SortValue(lhs) < SortValue(rhs);
    });
}

} // namespace pdk::rules
