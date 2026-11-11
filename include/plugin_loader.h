#pragma once
#include "plugin_api.h"
#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>

class PluginLoader {
public:
    // Punto de acceso global al Singleton
    static PluginLoader& instance();

    ~PluginLoader();

    // Evitar copias
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    void load(const std::filesystem::path& dir);
    const IPlugin* match(std::span<const uint8_t> data, int device_type) const;
    size_t max_magic() const { return max_magic_; }

private:
    // Constructor privado para que nadie pueda instanciarlo con "new" o en el stack
    PluginLoader() = default;

    struct Entry {
        void* handle = nullptr;
        IPlugin* plugin = nullptr;
        void (*destroy)(IPlugin*) = nullptr;
    };

    std::vector<Entry> plugins_;
    std::unordered_map<uint8_t, std::vector<const IPlugin*>> index_;
    std::unordered_map<const IPlugin*, size_t> plugin_max_len_;
    size_t max_magic_ = 0;
};