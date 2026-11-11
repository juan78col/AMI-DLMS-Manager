// dlms_profile_service.h
#pragma once
#include "dlms_profile.h"
#include "AsyncDatabase.h"
#include "spdlog/spdlog.h"
#include <boost/asio/awaitable.hpp>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <string>
using ConnProvider =
std::function<boost::asio::awaitable<std::shared_ptr<AsyncDatabase>>()>;

class DlmsProfileService {
public:
    // Singleton: una sola instancia por proceso
    static DlmsProfileService& Instance();
    static void Init(ConnProvider provider);

    DlmsProfileService(const DlmsProfileService&) = delete;
    DlmsProfileService& operator=(const DlmsProfileService&) = delete;

    // Carga / recarga (async). Solo lo llama main y los triggers de reload.
    boost::asio::awaitable<bool> LoadAllFromDb();
    boost::asio::awaitable<bool> Reload();

    // Lectura de caché (sync, thread-safe, barata)
    bool GetByPk(int pk, DlmsClientProfile& out) const;
    bool GetByTypeclcosem(int typeclcosem, DlmsClientProfile& out) const;

    // Resolución de medidores y DCU: perfil cacheado + secretos cifrados por entidad.
    boost::asio::awaitable<bool> ResolveForMeter(const std::string& meterSerial,
        DlmsClientProfile& out);
    boost::asio::awaitable<bool> ResolveForDcu(const std::string& dcuSerial,
        DlmsClientProfile& out);

    uint64_t Generation() const;
    size_t   CountProfiles() const;

private:
    DlmsProfileService(ConnProvider provider);
    ConnProvider m_provider;
    //std::shared_ptr<AsyncDatabase> m_conn;
    
    mutable std::shared_mutex                  m_mutex;
    std::unordered_map<int, DlmsClientProfile> m_byPk;
    std::unordered_map<int, int>               m_byType;
    uint64_t                                   m_generation = 0;
};