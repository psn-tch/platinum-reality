#include "sliding_puzzle.hpp"
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>

SlidingPuzzle::SlidingPuzzle()
{
    std::iota(m_tiles.begin(), m_tiles.end(), 1);
    m_tiles.back() = 0;
}
bool SlidingPuzzle::solved() const
{
    for (int i = 0; i < 15; ++i)
        if (m_tiles[i] != i + 1)
            return false;
    return m_tiles.back() == 0;
}
bool SlidingPuzzle::move(int index)
{
    if (index < 0 || index >= 16 ||
        std::abs(index / 4 - m_blank / 4) + std::abs(index % 4 - m_blank % 4) != 1)
        return false;
    std::swap(m_tiles[index], m_tiles[m_blank]);
    m_blank = index;
    ++m_moves;
    return true;
}
void SlidingPuzzle::shuffle(std::uint32_t seed)
{
    *this = SlidingPuzzle();
    std::mt19937 random(seed);
    int previous = -1;
    // Start from the goal and make legal moves, so every deal is solvable.
    for (int step = 0; step < 256; ++step)
    {
        std::vector<int> choices;
        for (int i = 0; i < 16; ++i)
            if (i != previous && std::abs(i / 4 - m_blank / 4) + std::abs(i % 4 - m_blank % 4) == 1)
                choices.push_back(i);
        const auto oldBlank = m_blank;
        move(choices[std::uniform_int_distribution<int>(0, int(choices.size()) - 1)(random)]);
        previous = oldBlank;
    }
    if (solved())
        move(14);
    m_moves = 0;
}
