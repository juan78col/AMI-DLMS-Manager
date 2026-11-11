#pragma once
#include <boost/asio/awaitable.hpp>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// ResultSet común para todos los motores
class ResultSet {
public:
    ResultSet() : _current(0) {}

    void addRow(const std::vector<std::string>& row) {
        _resultSet.push_back(row);
    }

    bool fetch(size_t field, std::string& fieldValue) {
		if (_current >= _resultSet.size()) {
			return false;
		}
		fieldValue = _resultSet[_current].at(field);
		++_current;
        return true;
    }

    bool fetch(std::vector<std::string>& rowValue) {
		if (_current >= _resultSet.size()) {
			return false;
		}
		rowValue = _resultSet[_current];
		++_current;
		return true;
    }

    // Reinicia el cursor de lectura explícitamente (llamar antes de re-iterar).
    void resetCursor() { _current = 0; }

    std::string get(size_t row, size_t field) const {
		return _resultSet.at(row).at(field);
    }

    std::vector<std::string> get(size_t row) const {
        return _resultSet.at(row);
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
    virtual boost::asio::awaitable<void> connect(const std::string& conninfo) = 0;

    virtual bool connected() const = 0;

    virtual boost::asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params) = 0;

    virtual boost::asio::awaitable<std::size_t> execute_non_query(const std::string& sql, const std::vector<std::string>& params) = 0;

    virtual void close() = 0;
};
