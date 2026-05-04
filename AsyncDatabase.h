#pragma once
#include <boost/asio.hpp>
#include <string>
#include <vector>

namespace asio = boost::asio;

// ResultSet común para todos los motores
class ResultSet {
public:
    ResultSet() : _current(0) {}

    void addRow(const std::vector<std::string>& row) {
        _resultSet.push_back(row);
    }

    bool fetch(size_t field, std::string& fieldValue) {
        size_t sz = _resultSet.size();
        if (sz && sz > _current) {
            fieldValue = _resultSet[_current++][field];
            return true;
        }
        _current = 0;
        return false;
    }

    bool fetch(std::vector<std::string>& rowValue) {
        size_t sz = _resultSet.size();
        if (sz && sz > _current) {
            rowValue = _resultSet[_current++];
            return true;
        }
        _current = 0;
        return false;
    }

    std::string get(size_t row, size_t field) const {
        return _resultSet[row][field];
    }

    std::vector<std::string> get(size_t row) const {
        return _resultSet[row];
    }

    size_t countRows() const {
        return _resultSet.size();
    }

    size_t countFields() const {
        return _resultSet.empty() ? 0 : _resultSet[0].size();
    }

    void clear() {
        _resultSet.clear();
        _current = 0;
    }

private:
    std::vector<std::vector<std::string>> _resultSet;
    size_t _current;
};

// Interfaz común para bases de datos asíncronas
class AsyncDatabase {
public:
    virtual ~AsyncDatabase() = default;

    // Conectar: firmas concretas pueden variar por motor; usar overloads en implementaciones
    virtual asio::awaitable<void> connect(const std::string& conninfo) = 0;

    virtual bool connected() const = 0;

    virtual asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params = {}) = 0;

    virtual asio::awaitable<bool> execute_non_query(const std::string& sql, const std::vector<std::string>& params = {}) = 0;

    virtual void close() = 0;
};
