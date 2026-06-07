#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace pdk::rules {

enum class Suit : std::uint8_t {
    Spades,
    Hearts,
    Diamonds,
    Clubs
};

enum class Rank : std::uint8_t {
    Three = 3,
    Four = 4,
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8,
    Nine = 9,
    Ten = 10,
    Jack = 11,
    Queen = 12,
    King = 13,
    Ace = 14,
    Two = 15
};

struct Card {
    Rank rank{};
    Suit suit{};

    auto operator<=>(const Card&) const = default;
};

using Cards = std::vector<Card>;

int RankValue(Rank rank);
int SortValue(Card card);
bool IsSpadeThree(Card card);
std::string RankName(Rank rank);
std::string SuitName(Suit suit);
std::string ToString(Card card);
std::string ToString(const Cards& cards);
void SortByGameOrder(Cards& cards);

} // namespace pdk::rules
