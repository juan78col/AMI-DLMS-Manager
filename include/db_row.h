#pragma once
// db_row.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>

/**
 * Vista "por nombre" sobre un ResultSet posicional.
 * El orden de `columnNames` DEBE coincidir con el orden del SELECT.
 */
class DbRow {
public:
    DbRow(const ResultSet& rs, size_t rowIndex,
        const std::vector<std::string>& columnNames)
        : _rs(rs), _row(rowIndex)
    {
        _idx.reserve(columnNames.size());
        for (size_t i = 0; i < columnNames.size(); ++i)
            _idx.emplace(columnNames[i], i);
    }

    std::string s(const std::string& col) const { return _rs.get(_row, At(col)); }
    bool        b(const std::string& col) const {
        auto v = s(col);
        return v == "t" || v == "true" || v == "1";
    }
    int         i(const std::string& col) const {
        auto v = s(col); return v.empty() ? 0 : std::stoi(v);
    }
    uint64_t    u64(const std::string& col) const {
        auto v = s(col); return v.empty() ? 0ULL : std::stoull(v);
    }
    bool        isNull(const std::string& col) const { return s(col).empty(); }

private:
    size_t At(const std::string& col) const {
        auto it = _idx.find(col);
        if (it == _idx.end())
            throw std::runtime_error("DbRow: column '" + col + "' not in result set");
        return it->second;
    }

    const ResultSet& _rs;
    size_t _row;
    std::unordered_map<std::string, size_t> _idx;
};