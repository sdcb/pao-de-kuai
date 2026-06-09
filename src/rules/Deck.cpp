#include "rules/Deck.h"

#include <cstdlib>
#include <utility>

namespace pdk::rules {

Cards CreatePaoDeKuaiDeck() {
    Cards deck;
    deck.reserve(48);

    constexpr Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};
    constexpr Rank ranks[] = {
        Rank::Three, Rank::Four, Rank::Five, Rank::Six, Rank::Seven, Rank::Eight,
        Rank::Nine, Rank::Ten, Rank::Jack, Rank::Queen, Rank::King, Rank::Ace, Rank::Two
    };

    for (Rank rank : ranks) {
        for (Suit suit : suits) {
            if (rank == Rank::Two && suit != Suit::Spades) {
                continue;
            }
            if (rank == Rank::Ace && suit == Suit::Clubs) {
                continue;
            }
            deck.push_back(Card{rank, suit});
        }
    }
    return deck;
}

void Shuffle(Cards& deck, unsigned seed) {
    std::srand(seed);
    for (std::size_t i = deck.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(std::rand()) % i;
        std::swap(deck[i - 1], deck[j]);
    }
}

int FindFirstPlayerBySpadeThree(const std::vector<Cards>& hands) {
    for (std::size_t player = 0; player < hands.size(); ++player) {
        for (Card card : hands[player]) {
            if (IsSpadeThree(card)) {
                return static_cast<int>(player);
            }
        }
    }
    return 0;
}

} // namespace pdk::rules
