#pragma once
#include <array>
#include <cstdint>

class SlidingPuzzle
{
  public:
    SlidingPuzzle();
    const std::array<int, 16> &tiles() const
    {
        return m_tiles;
    }
    int blank() const
    {
        return m_blank;
    }
    int moves() const
    {
        return m_moves;
    }
    bool solved() const;
    bool move(int index);
    void shuffle(std::uint32_t seed);

  private:
    std::array<int, 16> m_tiles;
    int m_blank = 15, m_moves = 0;
};
