#include "plugin_loader.h"
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#  include <windows.h>
#  define DL_OPEN(p)  LoadLibraryA(p)
#  define DL_SYM(h,n) reinterpret_cast<void*>(GetProcAddress((HMODULE)(h), n))
#  define DL_CLOSE(h) FreeLibrary((HMODULE)(h))
#else
#  include <dlfcn.h>
#  define DL_OPEN(p)  dlopen(p, RTLD_NOW | RTLD_LOCAL)
#  define DL_SYM(h,n) dlsym(h, n)
#  define DL_CLOSE(h) dlclose(h)
#endif

// Implementación del Singleton (Meyers Singleton)
PluginLoader& PluginLoader::instance() {
    static PluginLoader inst;
    return inst;
}

PluginLoader::~PluginLoader() {
    // Orden correcto: primero destruir el plugin, luego cerrar la .so.
    for (auto& e : plugins_) {
        if (e.plugin && e.destroy) e.destroy(e.plugin);
        if (e.handle) DL_CLOSE(e.handle);
    }
}

void PluginLoader::load(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        std::cerr << "[plugin] directorio no existe: " << dir << "\n";
        return;
    }

    for (auto& f : std::filesystem::directory_iterator(dir)) {
        if (!f.is_regular_file()) continue;

#ifdef _WIN32
        if (f.path().extension() != ".dll") continue;
#else
        if (f.path().extension() != ".so") continue;
#endif

        void* h = DL_OPEN(f.path().string().c_str());
        if (!h) {
            std::cerr << "[plugin] no se pudo abrir: " << f.path() << "\n";
            continue;
        }

        auto create = reinterpret_cast<IPlugin * (*)()>          (DL_SYM(h, "create_plugin"));
        auto destroy = reinterpret_cast<void(*)(IPlugin*)>      (DL_SYM(h, "destroy_plugin"));

        if (!create || !destroy) {
            std::cerr << "[plugin] simbolos faltantes: " << f.path() << "\n";
            DL_CLOSE(h);
            continue;
        }

        IPlugin* p = create();
        if (!p) { DL_CLOSE(h); continue; }

        plugins_.push_back({ h, p, destroy });

        size_t longest = 0;
        for (auto& mb : p->magic_bytes()) {
            if (mb.empty()) continue;
            index_[mb[0]].push_back(p);
            longest = std::max(longest, mb.size());
            max_magic_ = std::max(max_magic_, mb.size());
        }
        plugin_max_len_[p] = longest;

        std::cout << "[plugin] cargado: " << p->name()
            << " (" << f.path().filename().string() << ")\n";
    }

    // Magic más específica gana dentro de cada cubo.
    for (auto& [b, vec] : index_) {
        std::sort(vec.begin(), vec.end(),
            [this](const IPlugin* a, const IPlugin* b) {
                return plugin_max_len_.at(a) > plugin_max_len_.at(b);
            });
    }
}

const IPlugin* PluginLoader::match(std::span<const uint8_t> data, int device_type) const {
    if (data.empty()) return nullptr;

    auto it = index_.find(data[0]);
    if (it == index_.end()) return nullptr;

    for (const IPlugin* p : it->second) {
        if (!p->supports_device_type(device_type)) continue;

        for (const auto& mb : p->magic_bytes()) {
            if (!mb.empty() && data.size() >= mb.size() &&
                std::equal(mb.begin(), mb.end(), data.begin())) {
                return p;
            }
        }
    }
    return nullptr;
}