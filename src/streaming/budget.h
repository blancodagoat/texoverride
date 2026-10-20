#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

constexpr uint64_t budgetFor(uint64_t cap, uint64_t current)
{
    if (!cap) return 0;
    uint64_t reserve = (cap / 8 > (2ull << 30)) ? cap / 8 : (2ull << 30);
    if (cap <= reserve) return 0;
    uint64_t want = (cap - reserve) & ~((256ull << 20) - 1);             // 256 MB steps
    return (want > current) ? want : 0;
}

static_assert(budgetFor(24ull << 30, 3145728000ull) == 21ull << 30, "24 GB card holds an eighth back");
static_assert(budgetFor( 8ull << 30, 3145728000ull) ==  6ull << 30, "8 GB card holds the 2 GB floor back");
static_assert(budgetFor( 6ull << 30, 3145728000ull) ==  4ull << 30, "6 GB card holds the 2 GB floor back");
static_assert(budgetFor( 4ull << 30, 3145728000ull) == 0, "no gain over the default, leave it alone");
static_assert(budgetFor(          0, 3145728000ull) == 0, "card unreadable, leave it alone");
static_assert(budgetFor( 8ull << 30, 8388608000ull) == 0, "8 GB card, slider maxed, leave it alone");
static_assert(budgetFor(11ull << 30, 8388608000ull) ==  9ull << 30, "the real 11.7 GB machine, 7.8 -> 9.0");
static_assert(budgetFor(24ull << 30, 8388608000ull) == 21ull << 30, "24 GB card, slider maxed, plenty to gain");

// Beat thread at startup, in this order (see backgroundStartup): find the table, read the
// setting, decide. DXGI only works off the loader lock, which is why none of it is in Setup().
void locateBudgetTable();
void readBudgetFile();
void decideBudget();
void budgetBeat();   // beat thread, every second: re-assert the raised budget
void poolBeat();     // game main thread, once a second: how full the texture pool is
void costReport(uint64_t costVirt, uint64_t costPhys, std::vector<std::pair<uint64_t, std::string>>& big);
